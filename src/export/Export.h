#pragma once

// Video / image-sequence export.
//
//   ExportJob renders frame by frame with render::OffscreenRenderer and feeds
//   an Encoder. Encoders:
//     - MP4 (H.264): Media Foundation on Windows (no extra software needed),
//       FFmpeg elsewhere
//     - PNG sequence: built in, supports transparency
//     - MOV (ProRes 4444), WebM (VP9), GIF: require FFmpeg on the PATH
//
// A job is stepped from the UI thread (GL context) a few frames at a time so
// the interface stays responsive and the export can be cancelled.

#include "core/Project.h"
#include "render/OffscreenRenderer.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bemotion::exporting {

enum class Format { Mp4, PngSequence, MovProRes, WebM, Gif, Count };

const char* formatLabel(Format f);
const char* formatExtension(Format f); // including the dot
bool formatSupportsAlpha(Format f);
bool formatNeedsEvenSize(Format f);
bool formatAvailable(Format f); // false when it needs FFmpeg and FFmpeg was not found

// Path to an ffmpeg executable, if one can be found.
std::optional<std::filesystem::path> findFfmpeg();

enum class Quality { Low, Medium, High };

struct Settings {
    Format format = Format::Mp4;
    std::filesystem::path output;
    float scale = 1.0f;       // output size relative to the composition
    Quality quality = Quality::High;
    bool transparent = false; // only for formats with alpha
    double start = 0.0;       // seconds
    double end = 0.0;         // seconds, exclusive
};

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
};

class Encoder {
public:
    virtual ~Encoder() = default;
    virtual bool open(const EncoderParams& params, std::string& error) = 0;
    // `rgba` is top-down, straight alpha, width * height * 4 bytes.
    virtual bool write(const uint8_t* rgba, std::string& error) = 0;
    virtual bool finish(std::string& error) = 0;
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

private:
    void fail(std::string message);

    Project project_; // snapshot, so edits made meanwhile don't leak into the file
    Settings settings_;
    render::OffscreenRenderer renderer_;
    std::unique_ptr<Encoder> encoder_;
    std::vector<uint8_t> pixels_;
    int first_ = 0;
    int count_ = 0;
    int frame_ = 0;
    bool running_ = false;
    bool done_ = false;
    bool cancelled_ = false;
    std::string error_;
    std::chrono::steady_clock::time_point startTime_;
};

} // namespace bemotion::exporting
