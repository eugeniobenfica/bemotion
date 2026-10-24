#include "media/Recorder.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <miniaudio.h> // the implementation is in AudioEngine.cpp

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace bemotion::media {

namespace {

constexpr ma_uint32 kRate = 48000;
constexpr ma_uint32 kChannels = 1; // a voice: mono

// The capture callback runs on the audio thread and must never wait: it only copies into a
// fixed ring (no lock, no allocation); a drain thread moves the samples into the buffer the
// rest of the app reads. (Locking and growing the buffer in the callback dropped samples when
// the vector reallocated or the interface held the lock: audible pops.)
constexpr size_t kRingSize = kRate * 8; // 8 s of slack if the drain is ever slow

struct State {
    std::unique_ptr<ma_device> device;
    std::mutex mutex; // guards `buffer`
    RecordingBuffer buffer;
    std::atomic<float> peak{0.0f};
    std::vector<float> ring = std::vector<float>(kRingSize);
    std::atomic<size_t> written{0}, read{0}; // monotonic counts (single producer, single consumer)
    std::atomic<bool> draining{false};
    std::thread drain;
    std::atomic<size_t> dropped{0};
};

State& state() {
    static State s;
    return s;
}

void onCapture(ma_device*, void*, const void* input, ma_uint32 frames) {
    if (!input) return;
    State& s = state();
    const auto* in = static_cast<const float*>(input);
    float peak = 0.0f;
    for (ma_uint32 i = 0; i < frames * kChannels; ++i) peak = std::max(peak, std::fabs(in[i]));
    s.peak = std::max(peak, s.peak.load() * 0.9f);
    const size_t n = frames * kChannels;
    const size_t w = s.written.load(std::memory_order_relaxed), r = s.read.load(std::memory_order_acquire);
    if (kRingSize - (w - r) < n) { // the drain fell 8 s behind: keep time, lose this block
        s.dropped += n;
        return;
    }
    for (size_t i = 0; i < n; ++i) s.ring[(w + i) % kRingSize] = in[i];
    s.written.store(w + n, std::memory_order_release);
}

// Moves what the callback wrote into the buffer.
void drainRing() {
    State& s = state();
    const size_t w = s.written.load(std::memory_order_acquire), r = s.read.load(std::memory_order_relaxed);
    if (w == r) return;
    std::vector<float> chunk(w - r);
    for (size_t i = 0; i < chunk.size(); ++i) chunk[i] = s.ring[(r + i) % kRingSize];
    s.read.store(w, std::memory_order_release);
    std::lock_guard lock(s.mutex);
    s.buffer.append(chunk);
}

void closeDevice() {
    State& s = state();
    if (!s.device) return;
    ma_device_uninit(s.device.get()); // no more callbacks after this
    s.draining = false;
    if (s.drain.joinable()) s.drain.join();
    drainRing(); // the last samples
    s.device.reset();
}

template <typename T>
void put(std::ofstream& out, T v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof v);
}

} // namespace

Recorder& Recorder::get() {
    static Recorder r;
    return r;
}

bool Recorder::start(std::string* error) {
    State& s = state();
    if (s.device) {
        if (error) *error = "already recording";
        return false;
    }
    {
        std::lock_guard lock(s.mutex);
        s.buffer.clear();
    }
    s.peak = 0.0f;
    s.written = 0;
    s.read = 0;
    s.dropped = 0;
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate = kRate;
    config.dataCallback = onCapture;
    // Recording is not interactive: longer periods ride over the moments the system is busy.
    config.performanceProfile = ma_performance_profile_conservative;
    config.periodSizeInMilliseconds = 30;
    auto device = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &config, device.get()) != MA_SUCCESS) {
        if (error) *error = "no microphone could be opened";
        return false;
    }
    if (ma_device_start(device.get()) != MA_SUCCESS) {
        ma_device_uninit(device.get());
        if (error) *error = "the microphone did not start";
        return false;
    }
    s.device = std::move(device);
    s.draining = true;
    s.drain = std::thread([] {
        State& st = state();
        while (st.draining) {
            drainRing();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
    return true;
}

bool Recorder::stop(const std::filesystem::path& file, std::string* error) {
    State& s = state();
    if (!s.device) {
        if (error) *error = "not recording";
        return false;
    }
    closeDevice();
    std::vector<float> samples;
    {
        std::lock_guard lock(s.mutex);
        samples = s.buffer.takeSamples();
    }
    if (samples.empty()) {
        if (error) *error = "nothing was recorded (is the microphone muted?)";
        return false;
    }
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "could not write the recording";
        return false;
    }
    // 16-bit PCM WAV.
    const auto dataBytes = static_cast<uint32_t>(samples.size() * 2);
    out.write("RIFF", 4);
    put<uint32_t>(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    put<uint32_t>(out, 16);
    put<uint16_t>(out, 1);
    put<uint16_t>(out, static_cast<uint16_t>(kChannels));
    put<uint32_t>(out, kRate);
    put<uint32_t>(out, kRate * kChannels * 2);
    put<uint16_t>(out, static_cast<uint16_t>(kChannels * 2));
    put<uint16_t>(out, 16);
    out.write("data", 4);
    put<uint32_t>(out, dataBytes);
    for (float v : samples) put<int16_t>(out, static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
    return static_cast<bool>(out);
}

void Recorder::cancel() {
    closeDevice();
    std::lock_guard lock(state().mutex);
    state().buffer.clear();
}

bool Recorder::recording() const { return state().device != nullptr; }

float Recorder::level() const { return state().peak.load(); }

double Recorder::seconds() const {
    State& s = state();
    std::lock_guard lock(s.mutex);
    return s.buffer.seconds();
}

RecordingPreview Recorder::preview() const {
    State& s = state();
    std::lock_guard lock(s.mutex);
    return s.buffer.preview();
}

} // namespace bemotion::media
