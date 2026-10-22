#include "export/Export.h"

#include "app/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bemotion::exporting {

// Implemented in the encoder translation units.
std::unique_ptr<Encoder> makePngSequenceEncoder();
std::unique_ptr<Encoder> makeFfmpegEncoder();
#ifdef _WIN32
std::unique_ptr<Encoder> makeMediaFoundationEncoder();
#endif

namespace {

struct FormatInfo {
    const char* label;
    const char* extension;
    bool alpha;
    bool evenSize;
    bool needsFfmpeg;
};

constexpr FormatInfo kFormats[] = {
    {"MP4 video (H.264)", ".mp4", false, true,
#ifdef _WIN32
     false},
#else
     true},
#endif
    {"PNG image sequence", ".png", true, false, false},
    {"MOV video (ProRes 4444)", ".mov", true, true, true},
    {"WebM video (VP9)", ".webm", true, true, true},
    {"Animated GIF", ".gif", true, false, true},
};
static_assert(std::size(kFormats) == static_cast<size_t>(Format::Count));

const FormatInfo& info(Format f) { return kFormats[static_cast<int>(f)]; }

} // namespace

const char* formatLabel(Format f) { return info(f).label; }
const char* formatExtension(Format f) { return info(f).extension; }
bool formatSupportsAlpha(Format f) { return info(f).alpha; }
bool formatNeedsEvenSize(Format f) { return info(f).evenSize; }
bool formatAvailable(Format f) { return !info(f).needsFfmpeg || findFfmpeg().has_value(); }

std::optional<std::filesystem::path> findFfmpeg() {
    namespace fs = std::filesystem;
    static const std::optional<fs::path> cached = []() -> std::optional<fs::path> {
#ifdef _WIN32
        const char* exe = "ffmpeg.exe";
        const char sep = ';';
#else
        const char* exe = "ffmpeg";
        const char sep = ':';
#endif
        std::error_code ec;
        std::vector<fs::path> candidates{platform::executableDir() / exe};
        if (const char* path = std::getenv("PATH")) {
            std::string all = path;
            size_t begin = 0;
            while (begin <= all.size()) {
                const size_t end = std::min(all.find(sep, begin), all.size());
                if (end > begin) candidates.push_back(platform::fromUtf8(all.substr(begin, end - begin)) / exe);
                begin = end + 1;
            }
        }
#ifdef _WIN32
        // winget installs (PATH changes only reach new logins).
        if (const char* local = std::getenv("LOCALAPPDATA")) {
            const fs::path packages = fs::path(local) / "Microsoft" / "WinGet" / "Packages";
            for (const auto& pkg : fs::directory_iterator(packages, ec))
                if (pkg.path().filename().string().rfind("Gyan.FFmpeg", 0) == 0 ||
                    pkg.path().filename().string().rfind("BtbN.FFmpeg", 0) == 0)
                    for (const auto& sub : fs::directory_iterator(pkg.path(), ec))
                        candidates.push_back(sub.path() / "bin" / exe);
        }
#else
        candidates.push_back("/usr/bin/ffmpeg");
        candidates.push_back("/usr/local/bin/ffmpeg");
        candidates.push_back("/opt/homebrew/bin/ffmpeg");
#endif
        for (const auto& c : candidates)
            if (fs::is_regular_file(c, ec)) return c;
        return std::nullopt;
    }();
    return cached;
}

void outputSize(const Composition& comp, const Settings& s, int& width, int& height) {
    width = std::max(2, static_cast<int>(std::lround(comp.width * s.scale)));
    height = std::max(2, static_cast<int>(std::lround(comp.height * s.scale)));
    if (formatNeedsEvenSize(s.format)) { // 4:2:0 video needs even dimensions
        width += width % 2;
        height += height % 2;
    }
}

std::unique_ptr<Encoder> createEncoder(Format format) {
    switch (format) {
    case Format::PngSequence: return makePngSequenceEncoder();
#ifdef _WIN32
    case Format::Mp4: return makeMediaFoundationEncoder();
#endif
    default: return makeFfmpegEncoder();
    }
}

// --- ExportJob -----------------------------------------------------------------------

bool ExportJob::start(const Project& project, const Settings& settings, std::string& error) {
    project_ = project;
    settings_ = settings;
    const Composition& comp = project_.comp;
    first_ = comp.frameAt(std::max(0.0, settings.start));
    const int last = std::clamp(static_cast<int>(std::ceil(settings.end * comp.fps - 1e-6)), first_ + 1, comp.frameCount());
    count_ = last - first_;
    frame_ = 0;
    done_ = cancelled_ = false;
    error_.clear();

    int w = 0, h = 0;
    outputSize(comp, settings_, w, h);
    if (!renderer_.init(w, h, error)) return false;

    encoder_ = createEncoder(settings_.format);
    EncoderParams params{settings_.format, settings_.output, w, h, comp.fps, settings_.quality,
                         settings_.transparent && formatSupportsAlpha(settings_.format)};
    if (!encoder_->open(params, error)) {
        encoder_.reset();
        renderer_.release();
        return false;
    }
    running_ = true;
    startTime_ = std::chrono::steady_clock::now();
    return true;
}

void ExportJob::step(double budgetSeconds) {
    if (!running_) return;
    const auto stepStart = std::chrono::steady_clock::now();
    const Composition& comp = project_.comp;
    const bool background = !(settings_.transparent && formatSupportsAlpha(settings_.format));
    // Output pixels per composition pixel (shapes and text are re-rasterized, so 2x stays sharp).
    const float scale = static_cast<float>(renderer_.width()) / static_cast<float>(comp.width);

    while (frame_ < count_) {
        renderer_.render(comp, comp.timeOfFrame(first_ + frame_), scale, background, pixels_);
        std::string err;
        if (!encoder_->write(pixels_.data(), err)) {
            fail(err.empty() ? "Writing the frame failed." : err);
            return;
        }
        ++frame_;
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart).count() > budgetSeconds) return;
    }

    std::string err;
    const bool ok = encoder_->finish(err);
    encoder_.reset();
    renderer_.release();
    running_ = false;
    done_ = true;
    endTime_ = std::chrono::steady_clock::now();
    if (!ok) error_ = err.empty() ? "Finishing the file failed." : err;
}

void ExportJob::cancel() {
    if (!running_) return;
    std::string ignored;
    encoder_->finish(ignored);
    encoder_.reset();
    renderer_.release();
    running_ = false;
    done_ = true;
    endTime_ = std::chrono::steady_clock::now();
    cancelled_ = true;
}

void ExportJob::fail(std::string message) {
    std::string ignored;
    if (encoder_) encoder_->finish(ignored);
    encoder_.reset();
    renderer_.release();
    running_ = false;
    done_ = true;
    endTime_ = std::chrono::steady_clock::now();
    error_ = std::move(message);
}

double ExportJob::elapsedSeconds() const {
    // Frozen once the job ends, so the result dialog shows the real export time.
    const auto end = done_ ? endTime_ : std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - startTime_).count();
}

} // namespace bemotion::exporting
