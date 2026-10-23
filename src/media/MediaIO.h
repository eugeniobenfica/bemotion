#pragma once

// Reading media files: what they are (probe), still images, video frames
// and audio samples.
//
//   images   PNG, JPEG, BMP, TGA, GIF (first frame), PSD (composite), HDR - stb_image
//   audio    WAV, MP3, FLAC, M4A / AAC, OGG / Opus, WMA, AIFF - FFmpeg
//   video    MP4, MOV, M4V, MKV, WebM, AVI, WMV, MPEG-TS (any codec FFmpeg decodes) - FFmpeg
//
// Audio is always delivered as interleaved stereo float at kAudioRate.

#include "core/Media.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bemotion::media {

constexpr int kAudioRate = 48000;

// Kind and properties of a file; `error` is set (and `ok` false) for unsupported or broken files.
struct ProbeResult {
    bool ok = false;
    std::string error;
    MediaItem item; // everything but the id
};
ProbeResult probe(const std::filesystem::path& file);

// Every extension the importer accepts (lower case, without the dot).
const std::vector<std::string>& supportedExtensions();

bool loadImage(const std::filesystem::path& file, int& width, int& height, std::vector<uint8_t>& rgba,
               std::string* error = nullptr);

// The whole audio track, resampled to kAudioRate, stereo.
bool decodeAudio(const std::filesystem::path& file, std::vector<float>& stereo, std::string* error = nullptr);

// Sequential / random access to the frames of a video.
class VideoReader {
public:
    static std::unique_ptr<VideoReader> open(const std::filesystem::path& file, std::string* error = nullptr);
    virtual ~VideoReader() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    // The frame shown at `seconds` into the video, as top-down RGBA (alpha 255).
    // Returns false at / past the end or on error. Reading forward is cheap; jumping back seeks.
    virtual bool frameAt(double seconds, std::vector<uint8_t>& rgba) = 0;
};

} // namespace bemotion::media
