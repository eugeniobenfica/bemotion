#pragma once

// Audio playback (an output device mixing the composition's audio layers
// while the timeline plays) and offline mixing for exports.

#include "core/Project.h"

#include <vector>

namespace bemotion::media {

class AudioEngine {
public:
    static AudioEngine& get();
    // Opens the default output device (safe to call when there is none: playback stays silent).
    void start();
    void stop();

    // Call once per frame from the main thread: what plays now and from where.
    void update(const Composition& comp, double time, bool playing);
    // While playing: the composition time the sound has reached (false: no device, or stopped).
    // The timeline follows it, so picture and sound never drift apart.
    bool clock(double& time) const;

    // The composition's audio between `start` and `end` (seconds), stereo at kAudioRate.
    // Empty when no audio layer is heard in that range.
    static std::vector<float> mix(const Composition& comp, double start, double end);

private:
    AudioEngine() = default;
};

} // namespace bemotion::media
