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

void reduceNoise(std::vector<float>& stereo, double rate, float reductionDb, float sensitivity) {
    (void)rate;
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
        // The noise in each bin: a low percentile of its level over (up to 1500) moments.
        const size_t total = static_cast<size_t>((last - first) / static_cast<long>(hop)) + 1;
        const size_t stride = std::max<size_t>(1, total / 1500);
        std::vector<std::vector<float>> levels(bins);
        for (long at = first; at < last; at += static_cast<long>(hop * stride)) {
            spectrum(x, at);
            for (size_t k = 0; k < bins; ++k) levels[k].push_back(std::abs(buf[k]));
        }
        std::vector<float> noise(bins);
        for (size_t k = 0; k < bins; ++k) {
            auto& v = levels[k];
            const size_t at = v.size() / 5;
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(at), v.end());
            noise[k] = v[at];
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
            std::snprintf(part, sizeof part, "nr:%.2f,%.2f;", e.value, e.value2), key += part;
    }
    return key;
}

bool changesSamples(const std::vector<AudioEffect>& effects) { return !processKey(effects).empty(); }

void process(std::vector<float>& stereo, double rate, const std::vector<AudioEffect>& effects) {
    for (const AudioEffect& e : effects) {
        if (!e.enabled) continue;
        if (e.type == AudioEffectType::Eq) equalize(stereo, rate, e.value, e.value2, e.value3);
        else if (e.type == AudioEffectType::NoiseReduction) reduceNoise(stereo, rate, e.value, e.value2);
    }
    for (float& v : stereo)
        if (!std::isfinite(v)) v = 0.0f;
}

} // namespace bemotion::dsp
