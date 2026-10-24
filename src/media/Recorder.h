#pragma once

// Records the default microphone into a WAV file (plugins with the
// "microphone" permission drive it: bemotion.microphone.*). One recording at
// a time; the samples stay in memory until stop() writes them.

#include "media/RecordingBuffer.h"

#include <filesystem>
#include <string>

namespace bemotion::media {

class Recorder {
public:
    static Recorder& get();

    bool start(std::string* error = nullptr);
    // Ends the recording and writes it (16-bit PCM WAV) to `file`; false (and why) on failure.
    bool stop(const std::filesystem::path& file, std::string* error = nullptr);
    void cancel(); // ends it, keeping nothing
    bool recording() const;
    float level() const;   // recent peak, 0..1
    double seconds() const; // recorded so far
    RecordingPreview preview() const;

private:
    Recorder() = default;
};

} // namespace bemotion::media
