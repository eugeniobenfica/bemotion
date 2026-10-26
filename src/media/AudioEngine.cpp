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
#include <optional>

namespace bemotion::media {

namespace {

// One audio layer as heard: its samples, where they sit in time and how loud.
struct Clip {
    AudioData data;
    double in = 0, out = 0; // composition times
    float gain = 1.0f;
    const std::vector<AudioEffect>* effects = nullptr;
    Loudness loudness; // of the plain audio, every kLoudnessBlock frames
    std::optional<AudioEffect> ducking; // lowered while the other clips have sound (a copy: the layer may change meanwhile)
    // The source second at a composition time: a + b * time (speed, reverse), or, time-remapped,
    // sampled every kRemapStep from t0 (between the samples, a straight line).
    double a = 0.0, b = 1.0;
    double t0 = 0.0;
    std::vector<double> remap;
    static constexpr double kRemapStep = 0.005;

    double sourceAt(double time) const {
        if (remap.empty()) return a + b * time;
        const double u = std::clamp((time - t0) / kRemapStep, 0.0, static_cast<double>(remap.size() - 1));
        const size_t i = std::min(static_cast<size_t>(u), remap.size() - 1), j = std::min(i + 1, remap.size() - 1);
        return remap[i] + (remap[j] - remap[i]) * (u - static_cast<double>(i));
    }
    // A frozen (or nearly still) remap makes no sound rather than a buzz.
    bool still(double time) const {
        return !remap.empty() && std::fabs(sourceAt(time + kRemapStep) - sourceAt(time)) < kRemapStep * 0.05;
    }
    // How loud it is at a composition time (RMS, its volume counted): what ducking listens to.
    float levelAt(double time) const {
        if (!loudness || time < in || time >= out || still(time)) return 0.0f;
        const double block = sourceAt(time) * kAudioRate / static_cast<double>(kLoudnessBlock);
        if (block < 0 || block >= static_cast<double>(loudness->size())) return 0.0f;
        return (*loudness)[static_cast<size_t>(block)] * gain;
    }
};

constexpr double kMaxDuckFade = 3.0; // seconds (the Ducking effect's longest fade)

std::vector<Clip> clipsAt(const Composition& comp, double t, double until) {
    std::vector<Clip> clips;
    // Ducking listens a fade ahead and behind: clips that start or end that close count too.
    const bool ducking = std::any_of(comp.layers.begin(), comp.layers.end(), [](const Layer& l) {
        return std::any_of(l.audioEffects.begin(), l.audioEffects.end(), [](const AudioEffect& e) { return e.enabled && e.type == AudioEffectType::Ducking; });
    });
    if (ducking) t -= kMaxDuckFade, until += kMaxDuckFade;
    for (const Layer& l : comp.layers) {
        if (l.type != LayerType::Media || !l.media.hasAudio || !l.visible || l.muted) continue;
        if (l.outPoint <= t || l.inPoint >= until) continue;
        // Inside a compound, the compound's clip bounds it too; solo counts at every level.
        double in = l.inPoint, out = l.outPoint;
        bool hidden = comp.soloedOut(l);
        for (const Layer* p = comp.findLayer(l.parent); p; p = comp.findLayer(p->parent)) {
            if (!p->visible || comp.soloedOut(*p)) hidden = true;
            in = std::max(in, p->inPoint);
            out = std::min(out, p->outPoint);
        }
        if (hidden || out <= in) continue;
        AudioData data = MediaManager::get().audioFor(l.media.mediaId, l.audioEffects);
        if (!data) continue;
        Clip c{std::move(data), in, out, std::max(0.0f, l.media.volume.valueAt(t) * 0.01f), &l.audioEffects};
        c.loudness = MediaManager::get().loudness(l.media.mediaId);
        for (const AudioEffect& e : l.audioEffects)
            if (e.enabled && e.type == AudioEffectType::Ducking) c.ducking = e;
        const MediaProps& m = l.media;
        if (m.timeRemap) {
            c.t0 = std::max(in, t) - 0.05;
            const double last = std::min(out, until) + 0.05;
            for (double at = c.t0; at <= last + Clip::kRemapStep; at += Clip::kRemapStep) c.remap.push_back(m.sourceTime(at));
        } else {
            c.b = m.reversed ? -m.speed : m.speed;
            c.a = -m.start * c.b;
        }
        clips.push_back(std::move(c));
    }
    return clips;
}

// Ducking: how far down (0..1 of its amount) a clip is at `time`. Full while another clip (one
// that does not duck) is louder than the threshold, easing in and out over the fade around it.
float duckAt(const Clip& c, const std::vector<Clip>& clips, double time) {
    const float threshold = std::pow(10.0f, c.ducking->value2 / 20.0f);
    const double fade = std::clamp(static_cast<double>(c.ducking->value3), 0.05, kMaxDuckFade);
    constexpr double kStep = static_cast<double>(kLoudnessBlock) / kAudioRate;
    float cover = 0.0f;
    for (const Clip& o : clips) {
        if (&o == &c || o.ducking) continue;
        for (double d = -fade; d <= fade && cover < 1.0f; d += kStep)
            if (o.levelAt(time + d) > threshold) cover = std::max(cover, static_cast<float>(1.0 - std::fabs(d) / fade));
    }
    return cover;
}

// Adds the clips' samples for [t, t + frames / rate) into `out` (interleaved stereo).
void mixInto(const std::vector<Clip>& clips, double t, size_t frames, float* out) {
    for (const Clip& c : clips) {
        const std::vector<float>& s = *c.data;
        const size_t total = s.size() / 2;
        // Ducking moves every 10 ms, in a straight line between.
        float duckFrom = 0.0f, duckTo = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const double time = t + static_cast<double>(i) / kAudioRate;
            if (c.ducking && i % kLoudnessBlock == 0) {
                duckFrom = i == 0 ? duckAt(c, clips, time) : duckTo;
                duckTo = duckAt(c, clips, time + static_cast<double>(kLoudnessBlock) / kAudioRate);
            }
            if (time < c.in || time >= c.out || c.still(time)) continue;
            const double src = c.sourceAt(time) * kAudioRate;
            if (src < 0) continue;
            const size_t k = static_cast<size_t>(src);
            if (k >= total) continue;
            // Between two samples (a clip at another speed falls between them): a straight line.
            const size_t k1 = std::min(k + 1, total - 1);
            const float f = static_cast<float>(src - static_cast<double>(k));
            float gain = c.gain;
            for (const AudioEffect& effect : *c.effects) {
                if (!effect.enabled) continue;
                if (effect.type == AudioEffectType::Gain) gain *= std::pow(10.0f, effect.value / 20.0f);
                else if (effect.type == AudioEffectType::FadeIn) gain *= std::clamp(static_cast<float>((time - c.in) / effect.value), 0.0f, 1.0f);
                else if (effect.type == AudioEffectType::FadeOut) gain *= std::clamp(static_cast<float>((c.out - time) / effect.value), 0.0f, 1.0f);
            }
            if (c.ducking) {
                const float u = static_cast<float>(i % kLoudnessBlock) / kLoudnessBlock;
                gain *= std::pow(10.0f, -c.ducking->value * (duckFrom + (duckTo - duckFrom) * u) / 20.0f);
            }
            out[i * 2] += (s[k * 2] + (s[k1 * 2] - s[k * 2]) * f) * gain;
            out[i * 2 + 1] += (s[k * 2 + 1] + (s[k1 * 2 + 1] - s[k * 2 + 1]) * f) * gain;
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
    float peakL = 0.0f, peakR = 0.0f; // since the meters last looked
    bool clipped = false;
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
    for (ma_uint32 i = 0; i < frames; ++i) {
        const float l = std::fabs(out[i * 2]), r = std::fabs(out[i * 2 + 1]);
        d.peakL = std::max(d.peakL, l);
        d.peakR = std::max(d.peakR, r);
        d.clipped = d.clipped || l > 1.0f || r > 1.0f;
    }
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
    // Small differences are not jumps: the timeline follows this clock (see clock()), and
    // skipping the sound for them would click.
    if (playing && (!d.playing || std::fabs(d.clock - time) > 0.5)) d.clock = time;
    d.playing = playing;
    d.clips = std::move(clips);
}

AudioEngine::Levels AudioEngine::takeLevels() {
    Device& d = device();
    if (!d.open) return {};
    std::lock_guard lock(d.mutex);
    Levels levels{std::min(1.0f, d.peakL), std::min(1.0f, d.peakR), d.clipped};
    d.peakL = d.peakR = 0.0f;
    d.clipped = false;
    return levels;
}

bool AudioEngine::clock(double& time) const {
    Device& d = device();
    if (!d.open) return false;
    std::lock_guard lock(d.mutex);
    if (!d.playing) return false;
    time = d.clock;
    return true;
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
