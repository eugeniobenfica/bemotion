#pragma once

// Mono capture samples and a compact envelope for the live timeline preview.
// The recorder owns synchronization; this buffer also accepts synthetic audio in tests.

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace bemotion::media {

struct RecordingPreview {
    static constexpr int kPeaksPerSecond = 100;
    double seconds = 0.0;
    std::vector<float> peaks;
};

class RecordingBuffer {
public:
    static constexpr int kRate = 48000;
    void append(std::span<const float> input) {
        for (float value : input) {
            const float sample = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
            if (samples_.size() % kSamplesPerPeak == 0) peaks_.push_back(0.0f);
            peaks_.back() = std::max(peaks_.back(), std::fabs(sample));
            samples_.push_back(sample);
        }
    }
    void clear() {
        samples_.clear();
        peaks_.clear();
    }
    double seconds() const { return static_cast<double>(samples_.size()) / kRate; }
    RecordingPreview preview() const { return {seconds(), peaks_}; }
    const std::vector<float>& samples() const { return samples_; }
    std::vector<float> takeSamples() {
        auto samples = std::move(samples_);
        clear();
        return samples;
    }

private:
    static constexpr int kSamplesPerPeak = kRate / RecordingPreview::kPeaksPerSecond;
    std::vector<float> samples_, peaks_;
};

} // namespace bemotion::media
