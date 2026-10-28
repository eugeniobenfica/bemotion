#include "core/AudioDsp.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace bemotion::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

// One RBJ ("Audio EQ Cookbook") biquad, run on both channels of interleaved stereo.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static Biquad shelf(bool high, double rate, double freq, float db) {
        const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * freq / rate, c = std::cos(w);
        const double alpha = std::sin(w) / 2 * std::sqrt(2.0), k = 2 * std::sqrt(A) * alpha;
        double b0, b1, b2, a0, a1, a2;
        if (!high) {
            b0 = A * ((A + 1) - (A - 1) * c + k);
            b1 = 2 * A * ((A - 1) - (A + 1) * c);
            b2 = A * ((A + 1) - (A - 1) * c - k);
            a0 = (A + 1) + (A - 1) * c + k;
            a1 = -2 * ((A - 1) + (A + 1) * c);
            a2 = (A + 1) + (A - 1) * c - k;
        } else {
            b0 = A * ((A + 1) + (A - 1) * c + k);
            b1 = -2 * A * ((A - 1) + (A + 1) * c);
            b2 = A * ((A + 1) + (A - 1) * c - k);
            a0 = (A + 1) - (A - 1) * c + k;
            a1 = 2 * ((A - 1) - (A + 1) * c);
            a2 = (A + 1) - (A - 1) * c - k;
        }
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }

    static Biquad bell(double rate, double freq, double q, float db) {
        const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * freq / rate, c = std::cos(w);
        const double alpha = std::sin(w) / (2 * q);
        const double a0 = 1 + alpha / A;
        return {(1 + alpha * A) / a0, -2 * c / a0, (1 - alpha * A) / a0, -2 * c / a0, (1 - alpha / A) / a0};
    }

    void run(std::vector<float>& stereo) const {
        for (int ch = 0; ch < 2; ++ch) {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (size_t i = ch; i < stereo.size(); i += 2) {
                const double x = stereo[i];
                const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1, x1 = x, y2 = y1, y1 = y;
                stereo[i] = static_cast<float>(y);
            }
        }
    }
};

// In-place radix-2 FFT (n a power of two); inverse without the 1/n.
void fft(std::vector<std::complex<float>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = 2 * kPi / static_cast<double>(len) * (inverse ? 1 : -1);
        const std::complex<float> step(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

} // namespace

void equalize(std::vector<float>& stereo, double rate, float lowDb, float midDb, float highDb) {
    if (std::fabs(lowDb) > 0.01f) Biquad::shelf(false, rate, 100.0, lowDb).run(stereo);
    if (std::fabs(midDb) > 0.01f) Biquad::bell(rate, 1000.0, 0.9, midDb).run(stereo);
    if (std::fabs(highDb) > 0.01f) Biquad::shelf(true, rate, 8000.0, highDb).run(stereo);
}

void reduceNoise(std::vector<float>& stereo, double rate, float reductionDb, float sensitivity, double sampleFrom, double sampleTo) {
    constexpr size_t N = 2048, hop = N / 4, bins = N / 2 + 1;
    const size_t frames = stereo.size() / 2;
    if (reductionDb <= 0.01f || frames < N) return;
    const float floorGain = std::pow(10.0f, -std::clamp(reductionDb, 0.0f, 60.0f) / 20.0f);
    const float factor = 1.5f + 3.0f * std::clamp(sensitivity, 0.0f, 100.0f) / 100.0f;
    std::vector<float> window(N);
    for (size_t n = 0; n < N; ++n) window[n] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(n) / N));
    // Frames start before the first sample and end after the last, so every sample gets full overlap.
    const long first = -static_cast<long>(N - hop), last = static_cast<long>(frames);
    std::vector<std::complex<float>> buf(N);
    auto spectrum = [&](const std::vector<float>& x, long at) {
        for (size_t n = 0; n < N; ++n) {
            const long i = at + static_cast<long>(n);
            buf[n] = {i >= 0 && i < static_cast<long>(x.size()) ? x[static_cast<size_t>(i)] * window[n] : 0.0f, 0.0f};
        }
        fft(buf, false);
    };
    for (int ch = 0; ch < 2; ++ch) {
        std::vector<float> x(frames);
        for (size_t i = 0; i < frames; ++i) x[i] = stereo[i * 2 + ch];
        // The noise in each bin: its average level over the sample of only noise (the user's), or
        // else a low percentile of its level over (up to 1500) moments of the whole.
        std::vector<float> noise(bins, 0.0f);
        const long sFrom = static_cast<long>(std::max(0.0, sampleFrom) * rate), sTo = std::min(static_cast<long>(sampleTo * rate), last);
        if (sampleFrom >= 0.0 && sTo - sFrom >= static_cast<long>(N)) {
            int moments = 0;
            for (long at = sFrom; at + static_cast<long>(N) <= sTo; at += static_cast<long>(hop), ++moments) {
                spectrum(x, at);
                for (size_t k = 0; k < bins; ++k) noise[k] += std::abs(buf[k]);
            }
            for (float& v : noise) v /= static_cast<float>(std::max(1, moments));
        } else {
            const size_t total = static_cast<size_t>((last - first) / static_cast<long>(hop)) + 1;
            const size_t stride = std::max<size_t>(1, total / 1500);
            std::vector<std::vector<float>> levels(bins);
            for (long at = first; at < last; at += static_cast<long>(hop * stride)) {
                spectrum(x, at);
                for (size_t k = 0; k < bins; ++k) levels[k].push_back(std::abs(buf[k]));
            }
            for (size_t k = 0; k < bins; ++k) {
                auto& v = levels[k];
                const size_t at = v.size() / 5;
                std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(at), v.end());
                noise[k] = v[at];
            }
        }
        // Each moment: bins near the noise turned down, smoothed across frequency and time
        // (quick to open, slower to close) so what is left does not warble.
        std::vector<float> out(frames, 0.0f), gain(bins), held(bins, 1.0f);
        for (long at = first; at < last; at += static_cast<long>(hop)) {
            spectrum(x, at);
            for (size_t k = 0; k < bins; ++k) {
                const float t = noise[k] * factor + 1e-9f;
                const float over = std::clamp((std::abs(buf[k]) - t) / t, 0.0f, 1.0f);
                gain[k] = floorGain + (1.0f - floorGain) * over;
            }
            for (size_t k = 0; k < bins; ++k) {
                const float g = (gain[k > 0 ? k - 1 : k] + 2 * gain[k] + gain[k + 1 < bins ? k + 1 : k]) * 0.25f;
                held[k] = g > held[k] ? g : held[k] + (g - held[k]) * 0.5f;
            }
            for (size_t k = 0; k < bins; ++k) {
                buf[k] *= held[k];
                if (k > 0 && k < N / 2) buf[N - k] = std::conj(buf[k]); // a real signal's mirror
            }
            fft(buf, true);
            for (size_t n = 0; n < N; ++n) {
                const long i = at + static_cast<long>(n);
                if (i >= 0 && i < static_cast<long>(frames)) out[static_cast<size_t>(i)] += buf[n].real() / N * window[n];
            }
        }
        // Hann analysis and synthesis at 75% overlap add up to 1.5.
        for (size_t i = 0; i < frames; ++i) stereo[i * 2 + ch] = out[i] / 1.5f;
    }
}

namespace {

// Freeverb's pieces: a comb with a low-pass in its loop, and an all-pass.
struct Comb {
    std::vector<float> buf;
    size_t at = 0;
    float store = 0.0f;
    float process(float in, float feedback, float damp) {
        const float out = buf[at];
        store = out * (1.0f - damp) + store * damp;
        buf[at] = in + store * feedback;
        if (++at >= buf.size()) at = 0;
        return out;
    }
};
struct AllPass {
    std::vector<float> buf;
    size_t at = 0;
    float process(float in) {
        const float held = buf[at];
        const float out = held - in;
        buf[at] = in + held * 0.5f;
        if (++at >= buf.size()) at = 0;
        return out;
    }
};

float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1e-9f)); }

} // namespace

void reverb(std::vector<float>& stereo, double rate, float mix, float size, float damping) {
    const float wet = std::clamp(mix, 0.0f, 100.0f) / 100.0f;
    if (wet <= 0.001f || stereo.size() < 4) return;
    // Freeverb's tunings (for 44.1 kHz), scaled to the rate; the right side a little longer.
    static const int kCombs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int kAllPasses[4] = {556, 441, 341, 225};
    const double scale = rate / 44100.0;
    const float feedback = 0.7f + 0.28f * std::clamp(size, 0.0f, 100.0f) / 100.0f;
    const float damp = 0.4f * std::clamp(damping, 0.0f, 100.0f) / 100.0f;
    // The tail stops where the clip does: effects never make a clip longer.
    for (int ch = 0; ch < 2; ++ch) {
        const int spread = ch * 23;
        Comb combs[8];
        AllPass passes[4];
        for (int i = 0; i < 8; ++i) combs[i].buf.assign(static_cast<size_t>((kCombs[i] + spread) * scale) + 1, 0.0f);
        for (int i = 0; i < 4; ++i) passes[i].buf.assign(static_cast<size_t>((kAllPasses[i] + spread) * scale) + 1, 0.0f);
        for (size_t i = ch; i < stereo.size(); i += 2) {
            const float in = stereo[i] * 0.015f; // Freeverb's fixed input gain
            float out = 0.0f;
            for (Comb& c : combs) out += c.process(in, feedback, damp);
            for (AllPass& a : passes) out = a.process(out);
            stereo[i] = stereo[i] * (1.0f - wet * 0.5f) + out * wet * 3.0f;
        }
    }
}

void echo(std::vector<float>& stereo, double rate, float delaySeconds, float feedback, float mix) {
    const float wet = std::clamp(mix, 0.0f, 100.0f) / 100.0f, fb = std::clamp(feedback, 0.0f, 90.0f) / 100.0f;
    const size_t delay = static_cast<size_t>(std::clamp(delaySeconds, 0.01f, 5.0f) * rate) * 2; // interleaved
    if (wet <= 0.001f || delay >= stereo.size()) return;
    std::vector<float> line(stereo.size(), 0.0f); // the repeats alone
    for (size_t i = delay; i < stereo.size(); ++i) line[i] = stereo[i - delay] + line[i - delay] * fb;
    for (size_t i = 0; i < stereo.size(); ++i) stereo[i] += line[i] * wet;
}

void compress(std::vector<float>& stereo, double rate, float thresholdDb, float ratio, float makeupDb) {
    ratio = std::clamp(ratio, 1.0f, 20.0f);
    if (ratio <= 1.001f && std::fabs(makeupDb) < 0.01f) return;
    const float attack = static_cast<float>(std::exp(-1.0 / (0.005 * rate))), release = static_cast<float>(std::exp(-1.0 / (0.15 * rate)));
    const float makeup = dbToGain(makeupDb);
    float env = 0.0f;
    for (size_t i = 0; i + 1 < stereo.size(); i += 2) {
        const float level = std::max(std::fabs(stereo[i]), std::fabs(stereo[i + 1]));
        env = level > env ? attack * env + (1.0f - attack) * level : release * env + (1.0f - release) * level;
        const float over = gainToDb(env) - thresholdDb;
        const float gain = over > 0.0f ? dbToGain(-over * (1.0f - 1.0f / ratio)) : 1.0f;
        stereo[i] *= gain * makeup;
        stereo[i + 1] *= gain * makeup;
    }
}

void bandLimit(std::vector<float>& stereo, double rate, float lowCutHz, float highCutHz) {
    auto pass = [&](bool high, double freq) { // RBJ high / low pass, Q = 0.707
        const double w = 2 * kPi * freq / rate, c = std::cos(w), alpha = std::sin(w) / (2 * 0.7071);
        const double a0 = 1 + alpha;
        Biquad b;
        if (high) b = {(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - alpha) / a0};
        else b = {(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - alpha) / a0};
        b.run(stereo);
    };
    if (lowCutHz > 20.5f) pass(true, std::min<double>(lowCutHz, rate * 0.45));
    if (highCutHz < 19999.0f) pass(false, std::clamp<double>(highCutHz, 100.0, rate * 0.45));
}

void autoLevel(std::vector<float>& stereo, double rate, float targetDb, float evenOut) {
    const size_t block = std::max<size_t>(1, static_cast<size_t>(rate * 0.05)); // 50 ms
    const std::vector<float> rms = loudness(stereo, block);
    // The clip's level: the RMS of its blocks with sound (more than 50 dB under full scale).
    double sum = 0;
    size_t n = 0;
    for (float r : rms)
        if (r > 0.00316f) sum += static_cast<double>(r) * r, ++n;
    if (n == 0) return; // silence stays silence
    const float level = gainToDb(static_cast<float>(std::sqrt(sum / static_cast<double>(n))));
    const float overall = std::clamp(targetDb - level, -24.0f, 24.0f);
    // The rider: per block, toward the target over a 1.5 s window, at most 12 dB either way,
    // never lifting near-silence (no noise pumped up).
    const float amount = std::clamp(evenOut, 0.0f, 100.0f) / 100.0f;
    std::vector<float> rider(rms.size(), 0.0f);
    if (amount > 0.001f) {
        const size_t half = 15; // blocks each side: a 1.5 s window
        for (size_t b = 0; b < rms.size(); ++b) {
            double s = 0;
            size_t m = 0;
            for (size_t k = b > half ? b - half : 0; k < std::min(rms.size(), b + half + 1); ++k)
                if (rms[k] > 0.00316f) s += static_cast<double>(rms[k]) * rms[k], ++m;
            if (m == 0) continue;
            const float local = gainToDb(static_cast<float>(std::sqrt(s / static_cast<double>(m)))) + overall;
            rider[b] = std::clamp(targetDb - local, -12.0f, 12.0f) * amount;
        }
    }
    const float ceiling = dbToGain(-1.0f);
    float limiter = 1.0f;
    const float recover = static_cast<float>(std::exp(-1.0 / (0.2 * rate)));
    const size_t frames = stereo.size() / 2;
    for (size_t f = 0; f < frames; ++f) {
        // The rider's gain between the centers of two blocks, linearly.
        const double pos = static_cast<double>(f) / static_cast<double>(block) - 0.5;
        const size_t b0 = static_cast<size_t>(std::clamp(pos, 0.0, static_cast<double>(rider.size() - 1)));
        const size_t b1 = std::min(b0 + 1, rider.size() - 1);
        const float frac = static_cast<float>(std::clamp(pos - static_cast<double>(b0), 0.0, 1.0));
        const float g = dbToGain(overall + rider[b0] + (rider[b1] - rider[b0]) * frac);
        const float l = stereo[f * 2] * g, r = stereo[f * 2 + 1] * g;
        const float peak = std::max(std::fabs(l), std::fabs(r)) * limiter;
        if (peak > ceiling) limiter *= ceiling / peak;      // down at once
        else limiter = 1.0f - (1.0f - limiter) * recover; // back up slowly
        stereo[f * 2] = l * limiter;
        stereo[f * 2 + 1] = r * limiter;
    }
}

std::vector<std::pair<double, double>> quietStretches(const std::vector<float>& loud, double blockSeconds, float thresholdDb,
                                                      double minSeconds, double padSeconds) {
    std::vector<std::pair<double, double>> out;
    if (blockSeconds <= 0.0) return out;
    const float threshold = dbToGain(thresholdDb);
    size_t i = 0;
    while (i < loud.size()) {
        if (loud[i] >= threshold) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < loud.size() && loud[j] < threshold) ++j;
        const double start = static_cast<double>(i) * blockSeconds, end = static_cast<double>(j) * blockSeconds;
        if (end - start >= minSeconds) {
            // Silence at the very start or end of the sound goes entirely (no padding there).
            const double a = i == 0 ? start : start + padSeconds, b = j == loud.size() ? end : end - padSeconds;
            if (b - a > blockSeconds * 0.5) out.emplace_back(a, b);
        }
        i = j;
    }
    return out;
}

float thresholdFromSample(const std::vector<float>& loud, double blockSeconds, double from, double to) {
    if (blockSeconds <= 0.0 || to <= from) return -100.0f;
    float peak = -1.0f;
    for (size_t i = static_cast<size_t>(std::max(0.0, from / blockSeconds)); i < loud.size() && static_cast<double>(i) * blockSeconds < to; ++i)
        peak = std::max(peak, loud[i]);
    return peak < 0.0f ? -100.0f : gainToDb(peak) + 4.0f;
}

std::vector<float> loudness(const std::vector<float>& stereo, size_t blockFrames) {
    std::vector<float> out;
    if (blockFrames == 0) return out;
    const size_t frames = stereo.size() / 2;
    out.reserve(frames / blockFrames + 1);
    for (size_t f = 0; f < frames; f += blockFrames) {
        const size_t n = std::min(blockFrames, frames - f);
        double sum = 0;
        for (size_t i = f * 2; i < (f + n) * 2; ++i) sum += static_cast<double>(stereo[i]) * stereo[i];
        out.push_back(static_cast<float>(std::sqrt(sum / static_cast<double>(n * 2))));
    }
    return out;
}

std::string processKey(const std::vector<AudioEffect>& effects) {
    std::string key;
    char part[96];
    for (const AudioEffect& e : effects) {
        if (!e.enabled) continue;
        if (e.type == AudioEffectType::Eq && (std::fabs(e.value) > 0.01f || std::fabs(e.value2) > 0.01f || std::fabs(e.value3) > 0.01f))
            std::snprintf(part, sizeof part, "eq:%.2f,%.2f,%.2f;", e.value, e.value2, e.value3), key += part;
        else if (e.type == AudioEffectType::NoiseReduction && e.value > 0.01f)
            std::snprintf(part, sizeof part, "nr:%.2f,%.2f,%.3f,%.3f;", e.value, e.value2, e.hasSample() ? e.sampleFrom : -1.0,
                          e.hasSample() ? e.sampleTo : -1.0),
                key += part;
        else if (e.type == AudioEffectType::Reverb && e.value > 0.01f)
            std::snprintf(part, sizeof part, "rv:%.2f,%.2f,%.2f;", e.value, e.value2, e.value3), key += part;
        else if (e.type == AudioEffectType::Echo && e.value3 > 0.01f)
            std::snprintf(part, sizeof part, "ec:%.3f,%.2f,%.2f;", e.value, e.value2, e.value3), key += part;
        else if (e.type == AudioEffectType::Compressor)
            std::snprintf(part, sizeof part, "co:%.2f,%.2f,%.2f;", e.value, e.value2, e.value3), key += part;
        else if (e.type == AudioEffectType::Filter && (e.value > 20.5f || e.value2 < 19999.0f))
            std::snprintf(part, sizeof part, "fi:%.1f,%.1f;", e.value, e.value2), key += part;
        else if (e.type == AudioEffectType::AutoLevel)
            std::snprintf(part, sizeof part, "al:%.2f,%.2f;", e.value, e.value2), key += part;
    }
    return key;
}

bool changesSamples(const std::vector<AudioEffect>& effects) { return !processKey(effects).empty(); }

void process(std::vector<float>& stereo, double rate, const std::vector<AudioEffect>& effects) {
    for (const AudioEffect& e : effects) {
        if (!e.enabled) continue;
        if (e.type == AudioEffectType::Eq) equalize(stereo, rate, e.value, e.value2, e.value3);
        else if (e.type == AudioEffectType::NoiseReduction)
            reduceNoise(stereo, rate, e.value, e.value2, e.hasSample() ? e.sampleFrom : -1.0, e.hasSample() ? e.sampleTo : -1.0);
        else if (e.type == AudioEffectType::Reverb) reverb(stereo, rate, e.value, e.value2, e.value3);
        else if (e.type == AudioEffectType::Echo) echo(stereo, rate, e.value, e.value2, e.value3);
        else if (e.type == AudioEffectType::Compressor) compress(stereo, rate, e.value, e.value2, e.value3);
        else if (e.type == AudioEffectType::Filter) bandLimit(stereo, rate, e.value, e.value2);
        else if (e.type == AudioEffectType::AutoLevel) autoLevel(stereo, rate, e.value, e.value2);
    }
    for (float& v : stereo)
        if (!std::isfinite(v)) v = 0.0f;
}

} // namespace bemotion::dsp
