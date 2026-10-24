#pragma once

// Speech to text: the words spoken in an audio track, as timed captions. It runs
// whisper.cpp through FFmpeg's "whisper" audio filter (FFmpeg 8 built with it, as
// MSYS2's is), offline, on the GPU when there is one (Vulkan) and otherwise on the
// CPU. The model ships with bemotion (assets/models, see tools/fetch-speech-model.sh).

#include "core/Layer.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bemotion::media {

struct TranscribeOptions {
    std::filesystem::path model; // a whisper.cpp ggml model (ggml-*.bin)
    std::filesystem::path vad;   // optional voice-activity model: cuts at pauses, skips silence
    std::string language = "auto"; // "auto", or a code: "en", "pt", "es"...
    bool gpu = true;
};

// False when this FFmpeg has no whisper filter (e.g. older Linux packages).
bool speechToTextAvailable();

// The models in `folders`, first found wins: the speech model (the largest ggml-*.bin that
// is not a voice-activity one) and the voice-activity model (ggml-silero-*.bin).
bool findSpeechModels(const std::vector<std::filesystem::path>& folders, TranscribeOptions& options);

// The speech between `from` and `to` seconds of the file's audio (to <= from: to the end), as
// captions in the file's own time. `progress` (0..1) returns false to stop. False on error / stop.
bool transcribe(const std::filesystem::path& file, double from, double to, const TranscribeOptions& options,
                std::vector<Caption>& out, const std::function<bool(float)>& progress, std::string* error = nullptr);

} // namespace bemotion::media
