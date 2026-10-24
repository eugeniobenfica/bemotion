#include "media/Recorder.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <miniaudio.h> // the implementation is in AudioEngine.cpp

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

namespace bemotion::media {

namespace {

constexpr ma_uint32 kRate = 48000;
constexpr ma_uint32 kChannels = 1; // a voice: mono

struct State {
    std::unique_ptr<ma_device> device;
    std::mutex mutex;
    RecordingBuffer buffer;
    std::atomic<float> peak{0.0f};
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
    std::lock_guard lock(s.mutex);
    s.buffer.append(std::span<const float>(in, frames * kChannels));
}

void closeDevice() {
    State& s = state();
    if (!s.device) return;
    ma_device_uninit(s.device.get());
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
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate = kRate;
    config.dataCallback = onCapture;
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
