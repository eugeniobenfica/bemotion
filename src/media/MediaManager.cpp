#include "media/MediaManager.h"

#include "core/AudioDsp.h"
#include "media/MediaIO.h"
#include "render/SceneRenderer.h"

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace bemotion::media {

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string platform8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Straight RGBA -> an immutable Skia image (copied; mipmaps for still pictures that get scaled down).
sk_sp<SkImage> toImage(int w, int h, const std::vector<uint8_t>& rgba, bool mipmaps) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return nullptr;
    const SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    sk_sp<SkImage> img = SkImages::RasterFromData(info, SkData::MakeWithCopy(rgba.data(), rgba.size()), static_cast<size_t>(w) * 4);
    return img && mipmaps ? img->withDefaultMipmaps() : img;
}

// What a filmstrip being made has so far (the job writes, the editor reads).
struct FilmstripProgress {
    std::mutex mutex;
    std::shared_ptr<const Filmstrip> latest;
};

// ... taking the pixels over instead of copying them (video frames: one a frame, 8 MB at 1080p).
sk_sp<SkImage> toImage(int w, int h, std::vector<uint8_t>&& rgba) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return nullptr;
    auto* pixels = new std::vector<uint8_t>(std::move(rgba));
    sk_sp<SkData> data = SkData::MakeWithProc(
        pixels->data(), pixels->size(), [](const void*, void* owner) { delete static_cast<std::vector<uint8_t>*>(owner); }, pixels);
    const SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    return SkImages::RasterFromData(info, std::move(data), static_cast<size_t>(w) * 4);
}

} // namespace

// The editor's video frames, decoded on a thread of their own (per clip): the frame asked for
// (the newest wish only: frames passed over while scrubbing are skipped, so dragging the
// playhead over a long-GOP video never waits), then the next few, read ahead while playing, so
// the interface never waits for a decoder. The editor shows the last frame until one arrives.
class Seeker {
public:
    struct Frame {
        double start = -1.0, duration = 0.0; // where the frame starts, how long it shows
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
    };
    static constexpr int kAhead = 8; // frames read past the playhead

    Seeker(std::string file, double scale, double frameDuration, std::atomic<uint64_t>* arrivals)
        : file_(std::move(file)), scale_(scale), frameDuration_(frameDuration), arrivals_(arrivals), thread_([this] { run(); }) {}
    ~Seeker() {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    double scale() const { return scale_; }
    // The playhead is at `t` (source seconds): decode it unless it is on its way (read on from
    // where it is), and keep a few frames ahead of it.
    void want(double t) {
        {
            std::lock_guard lock(mutex_);
            playhead_ = t;
            const bool onTheWay = rangeStart_ >= 0 && t >= rangeStart_ - 1e-6 && t < next_ + frameDuration_ * 2.0;
            if (!onTheWay && !(pending_ && wanted_ == t)) {
                wanted_ = t;
                pending_ = true;
            }
        }
        wake_.notify_one();
    }
    // The frames made since the last call.
    std::vector<Frame> take() {
        std::lock_guard lock(mutex_);
        return std::exchange(done_, {});
    }

private:
    void run() {
        std::unique_ptr<VideoReader> reader = VideoReader::open(pathOf(file_));
        if (reader) reader->setOutputScale(scale_);
        std::unique_lock lock(mutex_);
        for (;;) {
            // Something asked, or the playhead not far enough behind what is read.
            wake_.wait(lock, [this] { return quit_ || pending_ || (rangeStart_ >= 0 && !atEnd_ && next_ < playhead_ + kAhead * frameDuration_); });
            if (quit_) return;
            const bool jump = pending_;
            const double t = jump ? wanted_ : next_;
            pending_ = false;
            lock.unlock();
            Frame f;
            const bool ok = reader && reader->frameAt(t, f.rgba);
            if (ok) {
                f.w = reader->width();
                f.h = reader->height();
                f.start = reader->frameTime();
                f.duration = frameDuration_;
            }
            lock.lock();
            if (!ok || f.start < 0) { // past the end (or unreadable): nothing more ahead
                atEnd_ = true;
                if (jump) rangeStart_ = -1.0;
                continue;
            }
            if (pending_) continue; // asked elsewhere meanwhile: this one is not wanted any more
            atEnd_ = false;
            if (jump) rangeStart_ = f.start;
            next_ = std::max(next_, f.start + frameDuration_ * 1.01);
            if (jump) next_ = f.start + frameDuration_ * 1.01;
            const bool asked = jump || std::fabs(f.start - playhead_) < frameDuration_;
            done_.push_back(std::move(f));
            if (done_.size() > static_cast<size_t>(kAhead) * 2) done_.erase(done_.begin()); // not collected: the oldest goes
            if (asked) arrivals_->fetch_add(1); // the views draw again (frames read ahead wait for their time)
        }
    }

    std::string file_;
    double scale_, frameDuration_;
    std::atomic<uint64_t>* arrivals_;
    std::mutex mutex_;
    std::condition_variable wake_;
    double wanted_ = 0.0, playhead_ = 0.0;
    double rangeStart_ = -1.0, next_ = 0.0; // what was read since the last jump: [rangeStart_, next_)
    bool pending_ = false, quit_ = false, atEnd_ = false;
    std::vector<Frame> done_;
    std::thread thread_; // last: everything above exists before it runs
};

struct MediaManager::Impl {
    struct Picture { // an image, or a video's thumbnail
        std::string path;
        sk_sp<SkImage> image;
    };
    struct Video { // per (composition, layer)
        std::string path;
        std::unique_ptr<VideoReader> reader;
        sk_sp<SkImage> image;
        double shown = -1.0; // source time of `image`
        std::vector<uint8_t> rgba;
        int lastUse = 0;
        std::unique_ptr<Seeker> seeker; // the editor's frames (scrubbing, playing)
        std::deque<std::pair<double, sk_sp<SkImage>>> recent; // the seeker's latest frames, by start
    };
    struct Decoded {
        std::vector<float> samples;
        std::vector<float> loudness;
        std::string error;
    };
    struct Audio {
        std::string path;
        AudioData data;
        Loudness loudness;
        std::future<Decoded> job;
        std::string error;
    };
    struct Processed { // a media's audio through some EQ / Noise Reduction (by media id and dsp::processKey)
        AudioData source; // what it was made from (relinked or decoded again: made again)
        AudioData data;
        std::future<std::vector<float>> job;
        int lastUse = 0;
    };
    std::map<std::pair<uint64_t, std::string>, Processed> processed;
    std::map<uint64_t, Picture> pictures; // by media id
    // By composition and layer: an export renders its own snapshot of the composition, so it gets
    // readers of its own instead of seeking the editor's back and forth.
    std::map<std::pair<const Composition*, uint64_t>, Video> videos;
    int tick = 0;
    std::map<uint64_t, Audio> audios; // by media id
    std::map<uint64_t, std::string> errors;
    mutable std::mutex mutex; // audios: read by the audio engine from other threads

    // Decoded frames by source file and the time each starts.
    struct CachedFrame {
        sk_sp<SkImage> image;
        double duration = 0.0;
        uint64_t lastUse = 0;
    };
    std::map<std::string, std::map<double, CachedFrame>> frames;
    size_t frameBytes = 0, frameBudget = size_t(512) << 20;
    uint64_t frameUses = 0;

    // Proxies: made one at a time on a thread of their own.
    const Composition* preview = nullptr;
    bool useProxies = true;
    std::map<std::string, bool> proxyExists; // by original path (checked again now and then)
    struct ProxyJob {
        MediaItem item;
        std::atomic<float> progress{0.0f};
    };
    std::deque<std::shared_ptr<ProxyJob>> proxyQueue;
    std::shared_ptr<ProxyJob> proxyRunning;
    std::future<std::string> proxyThread; // its error ("" when made)
    std::atomic_bool proxyCancel{false};
    std::map<uint64_t, std::string> proxyErrors;
    uint64_t generation = 1;
    std::atomic<uint64_t> arrivals{0}; // frames a seeker finished (and filmstrips made)
    struct FilmstripJob {
        std::future<void> job;
        std::shared_ptr<FilmstripProgress> progress = std::make_shared<FilmstripProgress>();
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    };
    std::map<std::string, FilmstripJob> filmstrips; // by file
    bool staleServed = false;          // a picture shown while its frame was on its way
    // How many screen pixels a composition pixel takes in each view showing the editor's
    // composition (by view, with the tick it was told): videos are decoded no bigger than shown.
    std::map<const void*, std::pair<double, int>> previewScales;
    std::map<const Composition*, double> outputScales; // setOutputScale
    bool scaledPreview = true;
    double previewScale() const {
        double s = 0.0;
        for (const auto& [view, entry] : previewScales)
            if (tick - entry.second < 60) s = std::max(s, entry.first);
        return s > 0.0 ? s : 1.0;
    }

    void trimFrames() {
        while (frameBytes > frameBudget && !frames.empty()) {
            // The least recently shown frame goes.
            std::map<double, CachedFrame>* oldestSource = nullptr;
            std::map<double, CachedFrame>::iterator oldest;
            for (auto& [source, byTime] : frames)
                for (auto it = byTime.begin(); it != byTime.end(); ++it)
                    if (!oldestSource || it->second.lastUse < oldest->second.lastUse) oldestSource = &byTime, oldest = it;
            if (!oldestSource) break;
            frameBytes -= oldest->second.image ? oldest->second.image->imageInfo().computeMinByteSize() : 0;
            oldestSource->erase(oldest);
            std::erase_if(frames, [](const auto& entry) { return entry.second.empty(); });
        }
    }

    void startNextProxy() {
        if (proxyRunning || proxyQueue.empty()) return;
        proxyRunning = proxyQueue.front();
        proxyQueue.pop_front();
        proxyCancel = false;
        std::shared_ptr<ProxyJob> job = proxyRunning;
        proxyThread = std::async(std::launch::async, [this, job] {
            std::string error;
            makeProxy(job->item, &job->progress, &proxyCancel, &error);
            return error;
        });
    }
};

MediaManager& MediaManager::get() {
    static MediaManager m;
    return m;
}

MediaManager::Impl& MediaManager::impl() const {
    static Impl i;
    return i;
}

void MediaManager::install() {
    render::setMediaProvider([](const Composition& comp, const Layer& layer, double t) {
        return MediaManager::get().frameFor(comp, layer, t);
    });
}

void MediaManager::release() {
    Impl& m = impl();
    m.proxyQueue.clear();
    m.proxyCancel = true; // a proxy being made stops (its half-made file goes)
    if (m.proxyThread.valid()) m.proxyThread.wait();
    m.proxyRunning = nullptr;
    m.pictures.clear();
    m.videos.clear();
    m.frames.clear();
    m.frameBytes = 0;
    for (auto& [file, f] : m.filmstrips) f.cancel->store(true); // filmstrips being made stop
    for (auto& [file, f] : m.filmstrips)
        if (f.job.valid()) f.job.wait();
    m.filmstrips.clear();
    render::setMediaProvider({});
}

void MediaManager::update(const Composition& comp) {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    for (const MediaItem& item : comp.media) {
        if (!item.hasAudio) continue;
        Impl::Audio& a = m.audios[item.id];
        if (a.path != item.path) { // new (or relinked): decode in the background
            a = {};
            a.path = item.path;
            const std::filesystem::path file = pathOf(item.path);
            a.job = std::async(std::launch::async, [file] {
                Impl::Decoded d;
                decodeAudio(file, d.samples, &d.error);
                d.loudness = dsp::loudness(d.samples, kLoudnessBlock);
                return d;
            });
        }
        if (a.job.valid() && a.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            Impl::Decoded d = a.job.get();
            a.error = d.error;
            if (d.error.empty()) {
                a.data = std::make_shared<const std::vector<float>>(std::move(d.samples));
                a.loudness = std::make_shared<const std::vector<float>>(std::move(d.loudness));
            }
        }
    }
    // Layers' EQ and Noise Reduction: their audio made once in the background, kept while used.
    for (const Layer& l : comp.layers) {
        if (l.type != LayerType::Media || !l.media.hasAudio) continue;
        const std::string key = dsp::processKey(l.audioEffects);
        if (key.empty()) continue;
        const auto source = m.audios.find(l.media.mediaId);
        if (source == m.audios.end() || !source->second.data) continue;
        Impl::Processed& p = m.processed[{l.media.mediaId, key}];
        p.lastUse = m.tick;
        if (p.source != source->second.data && !p.job.valid()) {
            p.source = source->second.data;
            p.data = nullptr;
            const AudioData input = p.source;
            const std::vector<AudioEffect> effects = l.audioEffects;
            p.job = std::async(std::launch::async, [input, effects] {
                std::vector<float> samples = *input;
                dsp::process(samples, kAudioRate, effects);
                return samples;
            });
        }
    }
    for (auto it = m.processed.begin(); it != m.processed.end();) {
        Impl::Processed& p = it->second;
        if (p.job.valid() && p.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            p.data = std::make_shared<const std::vector<float>>(p.job.get());
        // Settings no layer has used for a while (a dragged value leaves many behind).
        const bool stale = !p.job.valid() && m.tick - p.lastUse > 300;
        it = stale ? m.processed.erase(it) : std::next(it);
    }
    // Proxies: the one being made, then the next.
    if (m.proxyThread.valid() && m.proxyThread.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const std::string error = m.proxyThread.get();
        const MediaItem& item = m.proxyRunning->item;
        if (error.empty()) m.proxyErrors.erase(item.id);
        else m.proxyErrors[item.id] = error;
        m.proxyExists.erase(item.path);
        ++m.generation;
        m.proxyRunning = nullptr;
    }
    m.startNextProxy();
    if (m.tick % 120 == 0) m.proxyExists.clear(); // files deleted or made outside: noticed in a few seconds
    // Readers of layers that no longer exist, or not used for a while (finished exports, previews).
    ++m.tick;
    for (auto it = m.videos.begin(); it != m.videos.end();) {
        const bool gone = it->first.first == &comp ? !comp.findLayer(it->first.second) : m.tick - it->second.lastUse > 120;
        it = gone ? m.videos.erase(it) : std::next(it);
    }
}

sk_sp<SkImage> MediaManager::thumbnail(const MediaItem& item) {
    Impl& m = impl();
    Impl::Picture& pic = m.pictures[item.id];
    if (pic.path == item.path) return pic.image;
    pic.path = item.path;
    pic.image = nullptr;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string error;
    bool ok = false;
    if (item.kind == MediaKind::Image) {
        ok = loadImage(pathOf(item.path), w, h, rgba, &error);
    } else if (item.kind == MediaKind::Video) {
        if (auto reader = VideoReader::open(pathOf(item.path), &error)) {
            w = reader->width(), h = reader->height();
            ok = reader->frameAt(std::min(0.5, item.duration * 0.25), rgba);
        }
    }
    if (!ok) {
        if (!error.empty()) m.errors[item.id] = error;
        return nullptr;
    }
    pic.image = toImage(w, h, rgba, item.kind == MediaKind::Image);
    return pic.image;
}

sk_sp<SkImage> MediaManager::frameFor(const Composition& comp, const Layer& layer, double t) {
    const MediaItem* item = comp.findMedia(layer.media.mediaId);
    if (!item || !item->hasVideo) return nullptr;
    if (item->kind == MediaKind::Image) return thumbnail(*item); // the full image
    Impl& m = impl();
    const bool preview = &comp == m.preview;
    const std::string file = showsProxy(comp, *item) ? platform8(proxyPath(*item)) : item->path;
    const double frame = item->fps > 0 ? 1.0 / item->fps : 1.0 / 30.0;
    // The editor decodes a video no bigger than it shows it (steps of an eighth, so zooming does
    // not decode again at every step), an export no bigger than it comes out (rounded up: never
    // less sharp); 3D layers and other snapshots, at full size.
    double scale = 1.0;
    const auto output = m.outputScales.find(&comp);
    if (((preview && m.scaledPreview) || output != m.outputScales.end()) && !layer.threeD && item->width > 0) {
        const Affine w = comp.worldMatrix(layer, t);
        const double shown = (preview ? m.previewScale() : output->second) * std::max(std::hypot(w.a, w.b), std::hypot(w.c, w.d)) *
                             (layer.media.size.x > 0 ? layer.media.size.x / item->width : 1.0);
        scale = std::clamp(std::ceil(shown * 8.0) / 8.0, 0.125, 1.0);
    }
    Impl::Video& v = m.videos[{&comp, layer.id}];
    v.lastUse = m.tick;
    if (v.path != file) {
        v = {};
        v.path = file;
        v.lastUse = m.tick;
        std::string error;
        v.reader = VideoReader::open(pathOf(file), &error);
        if (!v.reader) m.errors[item->id] = error;
    }
    if (!v.reader) return nullptr;
    if (v.seeker && v.seeker->scale() != scale) v.seeker = nullptr, v.recent.clear(); // made again at the new size
    const double source = std::clamp(layer.media.sourceTime(t), 0.0, std::max(0.0, item->duration));
    const bool fits = v.image && std::abs(v.image->width() - std::max(2, static_cast<int>(std::lround(item->width * scale / 2.0)) * 2)) <= 2;
    if (v.image && fits && std::fabs(source - v.shown) < frame * 0.5) return v.image; // this frame is already decoded
    // Decoded before (by this layer or another one showing the same file at this size)?
    auto& cached = m.frames[scale < 1.0 ? file + "@" + std::to_string(scale) : file];
    auto keep = [&](double start, const sk_sp<SkImage>& img) {
        if (start < 0 || m.frameBudget == 0 || !img) return;
        Impl::CachedFrame& entry = cached[start];
        if (entry.image) m.frameBytes -= entry.image->imageInfo().computeMinByteSize();
        entry = {img, frame, ++m.frameUses};
        m.frameBytes += img->imageInfo().computeMinByteSize();
        m.trimFrames();
    };
    auto lookUp = [&]() -> sk_sp<SkImage> {
        for (auto it = v.recent.rbegin(); it != v.recent.rend(); ++it)
            if (source >= it->first - 1e-6 && source < it->first + frame - 1e-6) return it->second;
        if (auto it = cached.upper_bound(source + 1e-6); it != cached.begin()) {
            --it;
            if (source < it->first + it->second.duration - 1e-6) {
                it->second.lastUse = ++m.frameUses;
                return it->second.image;
            }
        }
        return nullptr;
    };
    if (preview) {
        // The editor: frames come from the clip's seeker; what it made since is kept first.
        if (!v.seeker) v.seeker = std::make_unique<Seeker>(file, scale, frame, &m.arrivals);
        for (Seeker::Frame& f : v.seeker->take()) {
            sk_sp<SkImage> img = toImage(f.w, f.h, std::move(f.rgba));
            if (!img) continue;
            v.recent.emplace_back(f.start, img);
            if (v.recent.size() > static_cast<size_t>(Seeker::kAhead) * 3) v.recent.pop_front();
            keep(f.start, img);
        }
        v.seeker->want(source); // this one (unless on its way), and the next few
        if (sk_sp<SkImage> hit = lookUp()) {
            v.image = hit;
            v.shown = source;
            return v.image;
        }
        if (v.image) { // still on its way: the last picture meanwhile
            m.staleServed = true;
            return v.image;
        }
        v.reader->setOutputScale(scale); // nothing to show yet: this one now
    } else if (sk_sp<SkImage> hit = lookUp()) {
        v.image = hit;
        v.shown = source;
        return v.image;
    } else {
        v.reader->setOutputScale(scale); // an export's: the size it comes out (setOutputScale)
    }
    if (!v.reader->frameAt(source, v.rgba)) return v.image;
    v.image = toImage(v.reader->width(), v.reader->height(), std::move(v.rgba));
    v.shown = source;
    keep(v.reader->frameTime(), v.image);
    return v.image;
}

void MediaManager::setOutputScale(const Composition* comp, double pixelsPerUnit) {
    Impl& m = impl();
    if (pixelsPerUnit > 0.0) m.outputScales[comp] = pixelsPerUnit;
    else m.outputScales.erase(comp);
}

void MediaManager::setPreviewScale(const void* view, double pixelsPerUnit) {
    Impl& m = impl();
    if (pixelsPerUnit > 0.0) m.previewScales[view] = {pixelsPerUnit, m.tick};
    else m.previewScales.erase(view);
}

void MediaManager::setScaledPreview(bool on) { impl().scaledPreview = on; }

namespace {

// Pictures along the video, 54 pixels high, decoded small (never at full size). First the
// keyframe before each time (quick: all along the clip in a moment), coarse to fine; then the
// exact frames, reading on. What is made so far is published every few pictures.
void makeFilmstrip(const std::filesystem::path& file, double duration, const std::atomic_bool* cancel, FilmstripProgress* progress,
                   std::atomic<uint64_t>* arrivals) {
    std::unique_ptr<VideoReader> reader = VideoReader::open(file);
    if (!reader || reader->width() <= 0 || reader->height() <= 0 || duration <= 0) return;
    Filmstrip strip;
    const int count = std::clamp(static_cast<int>(std::ceil(duration / 0.5)), 1, 160); // one every half second, at most 160
    strip.step = duration / count;
    strip.height = 54;
    strip.width = std::max(2, static_cast<int>(std::lround(strip.height * static_cast<double>(reader->width()) / reader->height())));
    strip.frames.resize(static_cast<size_t>(count));
    reader->setOutputScale(std::min(1.0, (strip.height + 1.0) / reader->height()));
    auto publish = [&] {
        ++strip.version;
        auto copy = std::make_shared<const Filmstrip>(strip);
        std::lock_guard lock(progress->mutex);
        progress->latest = std::move(copy);
        arrivals->fetch_add(1); // the timeline draws again
    };
    // A picture from the reader, fitted to the strip's size (the reader's is within a pixel).
    std::vector<uint8_t> rgba;
    auto grab = [&](int i) {
        if (!reader->frameAt((i + 0.5) * strip.step, rgba)) return false;
        const int sw = reader->width(), sh = reader->height(), dw = strip.width, dh = strip.height;
        if (rgba.size() < static_cast<size_t>(sw) * sh * 4) return false;
        std::vector<uint8_t>& out = strip.frames[static_cast<size_t>(i)];
        out.resize(static_cast<size_t>(dw) * dh * 4);
        for (int y = 0; y < dh; ++y) {
            const uint8_t* row = rgba.data() + static_cast<size_t>(std::min(sh - 1, y * sh / dh)) * sw * 4;
            for (int x = 0; x < dw; ++x) std::memcpy(out.data() + (static_cast<size_t>(y) * dw + x) * 4, row + std::min(sw - 1, x * sw / dw) * 4, 4);
        }
        return true;
    };
    // 1. Keyframes, coarse to fine.
    reader->setQuick(true);
    int made = 0;
    for (int stride = 16; stride >= 1 && !cancel->load(); stride /= 2)
        for (int i = 0; i < count && !cancel->load(); i += stride) {
            if (!strip.frames[static_cast<size_t>(i)].empty()) continue;
            if (grab(i) && ++made % 8 == 0) publish();
        }
    if (cancel->load()) return;
    publish();
    // 2. The exact frames, reading on (no seeking back and forth).
    reader->setQuick(false);
    for (int i = 0; i < count && !cancel->load(); ++i) {
        grab(i);
        if (i % 16 == 15) publish();
    }
    if (cancel->load()) return;
    strip.complete = true;
    publish();
}

} // namespace

std::shared_ptr<const Filmstrip> MediaManager::filmstrip(const MediaItem& item) {
    if (item.kind != MediaKind::Video || !item.hasVideo || item.duration <= 0) return nullptr;
    Impl& m = impl();
    const bool proxy = hasProxy(item);
    const std::string file = proxy ? platform8(proxyPath(item)) : item.path;
    Impl::FilmstripJob& f = m.filmstrips[file];
    if (!f.job.valid()) {
        try {
            f.job = std::async(std::launch::async, [path = pathOf(file), duration = item.duration, cancel = f.cancel, progress = f.progress,
                                                    arrivals = &m.arrivals] { makeFilmstrip(path, duration, cancel.get(), progress.get(), arrivals); });
        } catch (const std::exception&) {
            return nullptr;
        }
        return nullptr;
    }
    std::lock_guard lock(f.progress->mutex);
    return f.progress->latest;
}

void MediaManager::setPreview(const Composition* preview) { impl().preview = preview; }

uint64_t MediaManager::arrivals() const { return impl().arrivals.load(); }

bool MediaManager::takeStale() { return std::exchange(impl().staleServed, false); }

void MediaManager::setUseProxies(bool on) {
    Impl& m = impl();
    if (m.useProxies != on) ++m.generation;
    m.useProxies = on;
}

bool MediaManager::useProxies() const { return impl().useProxies; }

bool MediaManager::hasProxy(const MediaItem& item) const {
    Impl& m = impl();
    if (item.kind != MediaKind::Video) return false;
    if (m.proxyRunning && m.proxyRunning->item.path == item.path) return false; // half made
    const auto it = m.proxyExists.find(item.path);
    if (it != m.proxyExists.end()) return it->second;
    std::error_code ec;
    const std::filesystem::path p = proxyPath(item);
    const bool exists = !p.empty() && std::filesystem::is_regular_file(p, ec);
    m.proxyExists[item.path] = exists;
    return exists;
}

bool MediaManager::showsProxy(const Composition& comp, const MediaItem& item) const {
    const Impl& m = impl();
    return m.useProxies && &comp == m.preview && hasProxy(item);
}

void MediaManager::makeProxies(const std::vector<MediaItem>& items) {
    Impl& m = impl();
    for (const MediaItem& item : items) {
        if (item.kind != MediaKind::Video || hasProxy(item) || proxyProgress(item.id) >= 0) continue;
        auto job = std::make_shared<Impl::ProxyJob>();
        job->item = item;
        m.proxyQueue.push_back(std::move(job));
        m.proxyErrors.erase(item.id);
    }
    m.startNextProxy();
}

float MediaManager::proxyProgress(uint64_t mediaId) const {
    const Impl& m = impl();
    if (m.proxyRunning && m.proxyRunning->item.id == mediaId) return m.proxyRunning->progress.load();
    for (const auto& job : m.proxyQueue)
        if (job->item.id == mediaId) return 0.0f;
    return -1.0f;
}

std::string MediaManager::proxyError(uint64_t mediaId) const {
    const Impl& m = impl();
    const auto it = m.proxyErrors.find(mediaId);
    return it == m.proxyErrors.end() ? std::string() : it->second;
}

void MediaManager::deleteProxy(const MediaItem& item) {
    Impl& m = impl();
    std::erase_if(m.proxyQueue, [&](const auto& job) { return job->item.path == item.path; });
    if (m.proxyRunning && m.proxyRunning->item.path == item.path) m.proxyCancel = true;
    std::error_code ec;
    if (const std::filesystem::path p = proxyPath(item); !p.empty()) std::filesystem::remove(p, ec);
    // Readers still holding the file let go (Windows keeps open files).
    for (auto it = m.videos.begin(); it != m.videos.end();)
        it = it->second.path == platform8(proxyPath(item)) ? m.videos.erase(it) : std::next(it);
    m.proxyExists.erase(item.path);
    ++m.generation;
}

uint64_t MediaManager::proxyBytes() const {
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(proxyFolder(), ec))
        if (entry.path().extension() == ".mkv") total += entry.file_size(ec);
    return total;
}

void MediaManager::deleteAllProxies() {
    Impl& m = impl();
    m.proxyQueue.clear();
    if (m.proxyRunning) m.proxyCancel = true;
    m.videos.clear(); // readers of proxies let their files go
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(proxyFolder(), ec))
        if (entry.path().extension() == ".mkv") files.push_back(entry.path());
    for (const auto& f : files) std::filesystem::remove(f, ec);
    m.proxyExists.clear();
    ++m.generation;
}

void MediaManager::setFrameCacheMegabytes(int megabytes) {
    Impl& m = impl();
    m.frameBudget = static_cast<size_t>(std::max(0, megabytes)) << 20;
    m.trimFrames();
}

uint64_t MediaManager::generation() const { return impl().generation; }

void MediaManager::reopenVideos() {
    Impl& m = impl();
    m.videos.clear();
    ++m.generation;
}

AudioData MediaManager::audio(uint64_t mediaId) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    const auto it = m.audios.find(mediaId);
    return it == m.audios.end() ? nullptr : it->second.data;
}

AudioData MediaManager::audioFor(uint64_t mediaId, const std::vector<AudioEffect>& effects) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    const auto it = m.audios.find(mediaId);
    if (it == m.audios.end()) return nullptr;
    const std::string key = dsp::processKey(effects);
    if (key.empty()) return it->second.data;
    const auto p = m.processed.find({mediaId, key});
    return p != m.processed.end() && p->second.data && p->second.source == it->second.data ? p->second.data : it->second.data;
}

Loudness MediaManager::loudness(uint64_t mediaId) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    const auto it = m.audios.find(mediaId);
    return it == m.audios.end() ? nullptr : it->second.loudness;
}

bool MediaManager::audioPending(uint64_t mediaId) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    const auto it = m.audios.find(mediaId);
    return it != m.audios.end() && it->second.job.valid();
}

bool MediaManager::audioReady(const Composition& comp) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    for (const MediaItem& item : comp.media) {
        if (!item.hasAudio) continue;
        const auto it = m.audios.find(item.id);
        if (it == m.audios.end() || it->second.path != item.path || it->second.job.valid()) return false; // not started or running
    }
    for (const Layer& l : comp.layers) { // and every layer's processed audio is made
        if (l.type != LayerType::Media || !l.media.hasAudio) continue;
        const std::string key = dsp::processKey(l.audioEffects);
        if (key.empty()) continue;
        const auto source = m.audios.find(l.media.mediaId);
        if (source == m.audios.end() || !source->second.data) continue; // no audio after all (an error)
        const auto p = m.processed.find({l.media.mediaId, key});
        if (p == m.processed.end() || !p->second.data || p->second.source != source->second.data) return false;
    }
    return true;
}

bool MediaManager::fileMissing(const MediaItem& item) const {
    std::error_code ec;
    return !std::filesystem::exists(pathOf(item.path), ec);
}

std::string MediaManager::error(uint64_t mediaId) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    if (const auto it = m.audios.find(mediaId); it != m.audios.end() && !it->second.error.empty()) return it->second.error;
    const auto it = m.errors.find(mediaId);
    return it == m.errors.end() ? std::string() : it->second;
}

std::vector<uint64_t> MediaManager::import(Composition& comp, const std::vector<std::filesystem::path>& files,
                                           std::vector<std::string>* errors) {
    std::vector<uint64_t> ids;
    for (const auto& file : files) {
        ProbeResult r = probe(file);
        if (!r.ok) {
            if (errors) {
                const std::u8string name = file.filename().u8string();
                errors->push_back(std::string(name.begin(), name.end()) + ": " + r.error);
            }
            continue;
        }
        ids.push_back(comp.addMedia(std::move(r.item)));
    }
    return ids;
}

} // namespace bemotion::media
