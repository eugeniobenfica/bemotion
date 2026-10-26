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

#include <atomic>
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

// Hardware decoding: videos read by the graphics card (D3D11VA on Windows, VideoToolbox on
// macOS, VAAPI on Linux) when it can and the video is at least HD; software otherwise, and
// whenever the hardware fails. Off by default: with frames copied back for drawing, a modern
// processor is often faster; it helps machines with weak processors. hardwareDecoderName: what
// is in use ("" if none).
void setHardwareDecoding(bool on);
bool hardwareDecoding();
std::string hardwareDecoderName();

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
    virtual bool hardware() const { return false; } // decoding on the graphics card
    virtual double frameTime() const { return -1.0; } // where the last frame starts (seconds), -1 if none
    // Pictures this much smaller than the video (0 < scale <= 1; width() and height() follow):
    // a preview showing the video small never converts and copies its full size.
    virtual void setOutputScale(double scale) { (void)scale; }
    // For small pictures (the timeline's filmstrip): a lighter decode, and with `keyframesOnly`
    // the keyframe before the time asked (much faster to reach than the exact frame).
    virtual void setQuick(bool keyframesOnly) { (void)keyframesOnly; }
};

// Proxies: a small copy of a video, every frame a picture of its own (Motion JPEG), quick to
// scrub and to play; the editor shows it instead of the original (exports never do). Made in
// `folder` (the app's data), named after the original's path, size and date: a changed file
// gets a new one. proxyWanted: a video big enough for a proxy to help (above 1280 x 720).
void setProxyFolder(const std::filesystem::path& folder);
std::filesystem::path proxyFolder();
std::filesystem::path proxyPath(const MediaItem& item); // empty without a folder
bool proxyWanted(const MediaItem& item);
// Makes the proxy (blocking: run it on a thread). `progress` 0..1 while it works; `cancel` stops it.
bool makeProxy(const MediaItem& item, std::atomic<float>* progress, const std::atomic_bool* cancel, std::string* error);

} // namespace bemotion::media
