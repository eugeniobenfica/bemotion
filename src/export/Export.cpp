#include "export/Export.h"

#include "app/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace bemotion::exporting {

// Implemented in the encoder translation units.
std::unique_ptr<Encoder> makePngSequenceEncoder();
std::unique_ptr<Encoder> makeFfmpegEncoder();
bool ffmpegCanEncode(Format f);

namespace {

struct FormatInfo {
    const char* label;
    const char* extension;
    bool alpha;
    bool evenSize;
};

constexpr FormatInfo kFormats[] = {
    {"MP4 video (H.264)", ".mp4", false, true},
    {"PNG image sequence", ".png", true, false},
    {"MOV video (ProRes 4444)", ".mov", true, true},
    {"WebM video (VP9)", ".webm", true, true},
    {"Animated GIF", ".gif", true, false},
};
static_assert(std::size(kFormats) == static_cast<size_t>(Format::Count));

const FormatInfo& info(Format f) { return kFormats[static_cast<int>(f)]; }

} // namespace

const char* formatLabel(Format f) { return info(f).label; }
const char* formatExtension(Format f) { return info(f).extension; }
bool formatSupportsAlpha(Format f) { return info(f).alpha; }
bool formatNeedsEvenSize(Format f) { return info(f).evenSize; }
bool formatAvailable(Format f) { return f == Format::PngSequence || ffmpegCanEncode(f); }

void outputSize(const Composition& comp, const Settings& s, int& width, int& height) {
    width = std::max(2, static_cast<int>(std::lround(comp.width * s.scale)));
    height = std::max(2, static_cast<int>(std::lround(comp.height * s.scale)));
    if (formatNeedsEvenSize(s.format)) { // 4:2:0 video needs even dimensions
        width += width % 2;
        height += height % 2;
    }
}

bool formatHasAudio(Format f) { return f != Format::Gif; }

bool writeWav(const std::filesystem::path& file, const std::vector<float>& stereo, int rate, std::string& error) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Could not write " + platform::toUtf8(file);
        return false;
    }
    auto u32 = [&](uint32_t v) { out.put(char(v & 255)).put(char((v >> 8) & 255)).put(char((v >> 16) & 255)).put(char(v >> 24)); };
    auto u16 = [&](uint16_t v) { out.put(char(v & 255)).put(char(v >> 8)); };
    const uint32_t bytes = static_cast<uint32_t>(stereo.size() * 2);
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1); // PCM
    u16(2);
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate) * 4);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    std::vector<int16_t> pcm(stereo.size());
    for (size_t i = 0; i < stereo.size(); ++i) pcm[i] = static_cast<int16_t>(std::lround(std::clamp(stereo[i], -1.0f, 1.0f) * 32767.0f));
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * 2));
    if (!out) {
        error = "Could not write " + platform::toUtf8(file);
        return false;
    }
    return true;
}

std::unique_ptr<Encoder> createEncoder(Format format) {
    return format == Format::PngSequence ? makePngSequenceEncoder() : makeFfmpegEncoder();
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
    width_ = w;
    height_ = h;
    pixels_.clear();
    if (!renderer_.init(w, h, error)) return false;
    if (settings_.decodeScale) settings_.decodeScale(&comp, static_cast<double>(w) / comp.width);

    audio_.clear();
    if (settings_.mixAudio && formatHasAudio(settings_.format))
        audio_ = settings_.mixAudio(comp, comp.timeOfFrame(first_), comp.timeOfFrame(first_ + count_));

    encoder_ = createEncoder(settings_.format);
    EncoderParams params{settings_.format, settings_.output, w, h, comp.fps, settings_.quality,
                         settings_.transparent && formatSupportsAlpha(settings_.format), audio_.empty() ? nullptr : &audio_};
    if (!encoder_->open(params, error)) {
        encoder_.reset();
        renderer_.release();
        if (settings_.decodeScale) settings_.decodeScale(&comp, 0.0);
        return false;
    }
    running_ = true;
    startTime_ = std::chrono::steady_clock::now();
    startWriter();
    return true;
}

ExportJob::~ExportJob() {
    if (running_) cancel();
}

void ExportJob::startWriter() {
    writerStop_ = writerDrop_ = false;
    writeError_.clear();
    queue_.clear();
    writer_ = std::thread([this] { writerLoop(); });
}

void ExportJob::writerLoop() {
    for (;;) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock lock(queueMutex_);
            queueChanged_.wait(lock, [this] { return !queue_.empty() || writerStop_; });
            if (queue_.empty() || writerDrop_ || !writeError_.empty()) return;
            frame = std::move(queue_.front());
            queue_.pop_front();
        }
        queueChanged_.notify_all(); // room for the next one
        std::string err;
        const bool ok = encoder_->write(frame.data(), err);
        std::lock_guard lock(queueMutex_);
        if (!ok) {
            writeError_ = err.empty() ? "Writing the frame failed." : err;
            queueChanged_.notify_all();
            return;
        }
        spare_.push_back(std::move(frame));
    }
}

bool ExportJob::stopWriter(bool drop) {
    if (!writer_.joinable()) return writeError_.empty();
    {
        std::lock_guard lock(queueMutex_);
        writerStop_ = true;
        writerDrop_ = drop;
    }
    queueChanged_.notify_all();
    writer_.join();
    queue_.clear();
    spare_.clear();
    return writeError_.empty();
}

void ExportJob::step(double budgetSeconds) {
    if (!running_) return;
    const auto stepStart = std::chrono::steady_clock::now();
    const Composition& comp = project_.comp;
    const bool background = !(settings_.transparent && formatSupportsAlpha(settings_.format));
    // Output pixels per composition pixel (shapes and text are re-rasterized, so 2x stays sharp).
    const float scale = static_cast<float>(renderer_.width()) / static_cast<float>(comp.width);
    constexpr size_t kQueued = 3; // frames rendered ahead of the encoder (memory: a few frames)

    while (frame_ < count_) {
        renderer_.render(comp, comp.timeOfFrame(first_ + frame_), scale, background, pixels_);
        {
            std::unique_lock lock(queueMutex_);
            queueChanged_.wait(lock, [this] { return queue_.size() < kQueued || !writeError_.empty(); });
            if (!writeError_.empty()) {
                lock.unlock();
                fail(writeError_);
                return;
            }
            std::vector<uint8_t> frame;
            if (!spare_.empty()) frame = std::move(spare_.back()), spare_.pop_back();
            frame.assign(pixels_.begin(), pixels_.end()); // pixels_ stays the last frame (the dialog's preview)
            queue_.push_back(std::move(frame));
        }
        queueChanged_.notify_all();
        ++frame_;
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart).count() > budgetSeconds) return;
    }

    std::string err;
    bool ok = stopWriter(false); // every frame encoded
    if (!ok) {
        err = writeError_;
        std::string ignored;
        encoder_->finish(ignored);
    } else {
        ok = encoder_->finish(err);
    }
    encoder_.reset();
    renderer_.release();
    if (settings_.decodeScale) settings_.decodeScale(&project_.comp, 0.0);
    running_ = false;
    done_ = true;
    endTime_ = std::chrono::steady_clock::now();
    if (!ok) error_ = err.empty() ? "Finishing the file failed." : err;
}

void ExportJob::cancel() {
    if (!running_) return;
    stopWriter(true);
    encoder_->abort(); // at once: no flushing, the partial file removed
    encoder_.reset();
    renderer_.release();
    if (settings_.decodeScale) settings_.decodeScale(&project_.comp, 0.0);
    running_ = false;
    done_ = true;
    endTime_ = std::chrono::steady_clock::now();
    cancelled_ = true;
}

void ExportJob::fail(std::string message) {
    stopWriter(true);
    std::string ignored;
    if (encoder_) encoder_->finish(ignored);
    encoder_.reset();
    renderer_.release();
    if (settings_.decodeScale) settings_.decodeScale(&project_.comp, 0.0);
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
