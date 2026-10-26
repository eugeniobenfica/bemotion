#pragma once

// Video / image-sequence export.
//
//   ExportJob renders frame by frame with render::OffscreenRenderer and feeds
//   an Encoder. Encoders:
//     - MP4 (H.264), MOV (ProRes 4444), WebM (VP9), GIF: FFmpeg's libraries,
//       linked in (FfmpegEncoder.cpp)
//     - PNG sequence: built in, supports transparency
//
// A job is stepped from the UI thread (GL context) a few frames at a time so
// the interface stays responsive and the export can be cancelled.

#include "core/Project.h"
#include "render/OffscreenRenderer.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bemotion::exporting {

enum class Format { Mp4, PngSequence, MovProRes, WebM, Gif, Count };

const char* formatLabel(Format f);
const char* formatExtension(Format f); // including the dot
bool formatSupportsAlpha(Format f);
bool formatNeedsEvenSize(Format f);
bool formatAvailable(Format f); // false when the linked FFmpeg has no encoder for it

enum class Quality { Low, Medium, High };

struct Settings {
    Format format = Format::Mp4;
    std::filesystem::path output;
    float scale = 1.0f;       // output size relative to the composition
    Quality quality = Quality::High;
    bool transparent = false; // only for formats with alpha
    double start = 0.0;       // seconds
    double end = 0.0;         // seconds, exclusive
    // Mixes the composition's audio for [start, end) (interleaved stereo at kExportAudioRate);
    // empty when nothing is heard. Unset: a silent export.
    std::function<std::vector<float>(const Composition&, double start, double end)> mixAudio;
};

inline constexpr int kExportAudioRate = 48000;
bool formatHasAudio(Format f); // GIF has no sound

// Writes 16-bit PCM WAV (used for PNG sequences and to hand audio to FFmpeg).
bool writeWav(const std::filesystem::path& file, const std::vector<float>& stereo, int rate, std::string& error);

// Output pixel size for a composition and settings.
void outputSize(const Composition& comp, const Settings& s, int& width, int& height);

struct EncoderParams {
    Format format;
    std::filesystem::path output;
    int width = 0;
    int height = 0;
    double fps = 30.0;
    Quality quality = Quality::High;
    bool alpha = false;
    const std::vector<float>* audio = nullptr; // interleaved stereo at kExportAudioRate, or none
};

class Encoder {
public:
    virtual ~Encoder() = default;
    virtual bool open(const EncoderParams& params, std::string& error) = 0;
    // `rgba` is top-down, straight alpha, width * height * 4 bytes.
    virtual bool write(const uint8_t* rgba, std::string& error) = 0;
    virtual bool finish(std::string& error) = 0;
    // Stops at once (cancel): nothing more is encoded or written, the unfinished file goes.
    virtual void abort() {
        std::string ignored;
        finish(ignored);
    }
};

std::unique_ptr<Encoder> createEncoder(Format format);

class ExportJob {
public:
    bool start(const Project& project, const Settings& settings, std::string& error);

    // Renders and encodes frames for roughly `budgetSeconds`.
    void step(double budgetSeconds);
    void cancel();

    bool running() const { return running_; }
    bool succeeded() const { return done_ && error_.empty() && !cancelled_; }
    bool cancelled() const { return cancelled_; }
    const std::string& error() const { return error_; }
    int framesDone() const { return frame_; }
    int framesTotal() const { return count_; }
    double elapsedSeconds() const;
    const Settings& settings() const { return settings_; }
    // The last frame rendered (RGBA, straight alpha, top-down), for a preview; empty before one.
    const std::vector<uint8_t>& lastFrame() const { return pixels_; }
    int frameWidth() const { return width_; }
    int frameHeight() const { return height_; }

private:
    void fail(std::string message);

    Project project_; // snapshot, so edits made meanwhile don't leak into the file
    Settings settings_;
    render::OffscreenRenderer renderer_;
    std::unique_ptr<Encoder> encoder_;
    std::vector<uint8_t> pixels_;
    std::vector<float> audio_;
    int first_ = 0;
    int count_ = 0;
    int frame_ = 0;
    int width_ = 0, height_ = 0; // output size
    bool running_ = false;
    bool done_ = false;
    bool cancelled_ = false;
    std::string error_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point endTime_;
};

} // namespace bemotion::exporting
