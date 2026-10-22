#include "media/AudioEngine.h"

#include "media/MediaIO.h"
#include "media/MediaManager.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // miniaudio includes <windows.h>: keep std::min / std::max usable
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace bemotion::media {

namespace {

// One audio layer as heard: its samples, where they sit in time and how loud.
struct Clip {
    AudioData data;
    double start = 0, in = 0, out = 0; // composition times (source starts at `start`)
    float gain = 1.0f;
};

std::vector<Clip> clipsAt(const Composition& comp, double t, double until) {
    std::vector<Clip> clips;
    for (const Layer& l : comp.layers) {
        if (l.type != LayerType::Media || !l.media.hasAudio || !l.visible) continue;
        if (l.outPoint <= t || l.inPoint >= until) continue;
        // Inside a compound, the compound's clip bounds it too.
        double in = l.inPoint, out = l.outPoint;
        bool hidden = false;
        for (const Layer* p = comp.findLayer(l.parent); p; p = comp.findLayer(p->parent)) {
            if (!p->visible) hidden = true;
            in = std::max(in, p->inPoint);
            out = std::min(out, p->outPoint);
        }
        if (hidden || out <= in) continue;
        AudioData data = MediaManager::get().audio(l.media.mediaId);
        if (!data) continue;
        clips.push_back({std::move(data), l.media.start, in, out, std::max(0.0f, l.media.volume.valueAt(t) * 0.01f)});
    }
    return clips;
}

// Adds the clips' samples for [t, t + frames / rate) into `out` (interleaved stereo).
void mixInto(const std::vector<Clip>& clips, double t, size_t frames, float* out) {
    for (const Clip& c : clips) {
        const std::vector<float>& s = *c.data;
        const size_t total = s.size() / 2;
        for (size_t i = 0; i < frames; ++i) {
            const double time = t + static_cast<double>(i) / kAudioRate;
            if (time < c.in || time >= c.out) continue;
            const double src = (time - c.start) * kAudioRate;
            if (src < 0) continue;
            const size_t k = static_cast<size_t>(src);
            if (k >= total) continue;
            out[i * 2] += s[k * 2] * c.gain;
            out[i * 2 + 1] += s[k * 2 + 1] * c.gain;
        }
    }
}

struct Device {
    ma_device device{};
    bool open = false;
    std::mutex mutex;          // guards everything below (the callback runs on the audio thread)
    std::vector<Clip> clips;
    double clock = 0.0;        // composition time of the next sample
    bool playing = false;
};

Device& device() {
    static Device d;
    return d;
}

void callback(ma_device*, void* output, const void*, ma_uint32 frames) {
    float* out = static_cast<float*>(output);
    std::fill(out, out + frames * 2, 0.0f);
    Device& d = device();
    std::lock_guard lock(d.mutex);
    if (!d.playing) return;
    mixInto(d.clips, d.clock, frames, out);
    for (ma_uint32 i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
    d.clock += static_cast<double>(frames) / kAudioRate;
}

} // namespace

AudioEngine& AudioEngine::get() {
    static AudioEngine e;
    return e;
}

void AudioEngine::start() {
    Device& d = device();
    if (d.open) return;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kAudioRate;
    config.dataCallback = callback;
    if (ma_device_init(nullptr, &config, &d.device) != MA_SUCCESS) return; // no audio output: stay silent
    if (ma_device_start(&d.device) != MA_SUCCESS) {
        ma_device_uninit(&d.device);
        return;
    }
    d.open = true;
}

void AudioEngine::stop() {
    Device& d = device();
    if (!d.open) return;
    ma_device_uninit(&d.device);
    d.open = false;
}

void AudioEngine::update(const Composition& comp, double time, bool playing) {
    Device& d = device();
    if (!d.open) return;
    std::vector<Clip> clips = playing ? clipsAt(comp, time, time + 1.0) : std::vector<Clip>{};
    std::lock_guard lock(d.mutex);
    // Follow the timeline: start where the playhead is, and resync when it jumps (loops, scrubs).
    if (playing && (!d.playing || std::fabs(d.clock - time) > 0.12)) d.clock = time;
    d.playing = playing;
    d.clips = std::move(clips);
}

std::vector<float> AudioEngine::mix(const Composition& comp, double start, double end) {
    if (end <= start) return {};
    const size_t frames = static_cast<size_t>(std::ceil((end - start) * kAudioRate));
    std::vector<float> out;
    // Blocks of 10 ms: volume keyframes are followed closely enough.
    constexpr size_t kBlock = kAudioRate / 100;
    bool any = false;
    for (size_t f = 0; f < frames; f += kBlock) {
        const double t = start + static_cast<double>(f) / kAudioRate;
        const size_t n = std::min(kBlock, frames - f);
        const auto clips = clipsAt(comp, t, t + static_cast<double>(n) / kAudioRate);
        if (clips.empty()) continue;
        if (!any) out.assign(frames * 2, 0.0f), any = true;
        mixInto(clips, t, n, out.data() + f * 2);
    }
    for (float& v : out) v = std::clamp(v, -1.0f, 1.0f);
    return out;
}

} // namespace bemotion::media
