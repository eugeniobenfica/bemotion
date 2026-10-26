#include "media/MediaManager.h"

#include "core/AudioDsp.h"
#include "media/MediaIO.h"
#include "render/SceneRenderer.h"

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
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

} // namespace

// Scrubbing: a video's far frames decoded on a thread of its own, the newest wish only (the ones
// passed over are skipped), so dragging the playhead over a long-GOP video never waits for a
// keyframe and the frames after it; the editor shows the last frame meanwhile.
class Seeker {
public:
    struct Frame {
        double time = -1.0, start = -1.0; // asked for; where the frame starts
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
    };
    Seeker(std::string file, std::atomic<uint64_t>* arrivals) : file_(std::move(file)), arrivals_(arrivals), thread_([this] { run(); }) {}
    ~Seeker() {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    void want(double t) {
        {
            std::lock_guard lock(mutex_);
            if (pending_ && wanted_ == t) return;
            wanted_ = t;
            pending_ = true;
        }
        wake_.notify_one();
    }
    bool take(Frame& out) {
        std::lock_guard lock(mutex_);
        if (!ready_) return false;
        out = std::move(done_);
        ready_ = false;
        return true;
    }

private:
    void run() {
        std::unique_ptr<VideoReader> reader = VideoReader::open(pathOf(file_));
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return quit_ || pending_; });
            if (quit_) return;
            Frame f;
            f.time = wanted_;
            pending_ = false;
            lock.unlock();
            const bool ok = reader && reader->frameAt(f.time, f.rgba);
            if (ok) {
                f.w = reader->width();
                f.h = reader->height();
                f.start = reader->frameTime();
            }
            lock.lock();
            if (ok) {
                done_ = std::move(f);
                ready_ = true;
                arrivals_->fetch_add(1); // the views draw again
            }
        }
    }

    std::string file_;
    std::atomic<uint64_t>* arrivals_;
    std::mutex mutex_;
    std::condition_variable wake_;
    double wanted_ = 0.0;
    bool pending_ = false, quit_ = false, ready_ = false;
    Frame done_;
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
        std::unique_ptr<Seeker> seeker; // the editor's far frames (scrubbing)
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
        std::future<std::shared_ptr<const Filmstrip>> job;
        std::shared_ptr<const Filmstrip> done;
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    };
    std::map<std::string, FilmstripJob> filmstrips; // by file
    bool staleServed = false;          // a picture shown while its frame was on its way

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
    const std::string file = showsProxy(comp, *item) ? platform8(proxyPath(*item)) : item->path;
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
    const double source = std::clamp(layer.media.sourceTime(t), 0.0, std::max(0.0, item->duration));
    const double frame = item->fps > 0 ? 1.0 / item->fps : 1.0 / 30.0;
    if (v.image && std::fabs(source - v.shown) < frame * 0.5) return v.image; // this frame is already decoded
    // Decoded before (by this layer or another one showing the same file)?
    auto& cached = m.frames[file];
    if (auto it = cached.upper_bound(source + 1e-6); it != cached.begin()) {
        --it;
        if (source < it->first + it->second.duration - 1e-6) {
            it->second.lastUse = ++m.frameUses;
            v.image = it->second.image;
            v.shown = source;
            return v.image;
        }
    }
    // The editor, scrubbing: a frame far from where this reader is comes from the seeker (the
    // last picture shows until it is there); reading on (playing) stays here, frame by frame.
    if (&comp == m.preview && v.image) {
        Seeker::Frame f;
        if (v.seeker && v.seeker->take(f)) {
            if (sk_sp<SkImage> img = toImage(f.w, f.h, f.rgba, false)) {
                if (f.start >= 0 && m.frameBudget > 0) {
                    Impl::CachedFrame& entry = cached[f.start];
                    if (entry.image) m.frameBytes -= entry.image->imageInfo().computeMinByteSize();
                    entry = {img, frame, ++m.frameUses};
                    m.frameBytes += img->imageInfo().computeMinByteSize();
                    m.trimFrames();
                }
                v.image = img;
                v.shown = f.time;
                if (std::fabs(f.time - source) < frame * 0.5) return v.image;
            }
        }
        const double at = v.reader->frameTime(); // where this reader stands
        const auto near = [&](double from) { return from >= 0 && source >= from - 1e-6 && source <= from + frame * 4.0; };
        // Just after the picture shown (playing on, a slow drag forward): read here, so this reader
        // catches up once and then goes on frame by frame.
        if (!near(at) && !near(v.shown)) {
            if (!v.seeker) v.seeker = std::make_unique<Seeker>(file, &m.arrivals);
            v.seeker->want(source);
            m.staleServed = true;
            return v.image;
        }
    }
    if (!v.reader->frameAt(source, v.rgba)) return v.image;
    v.image = toImage(v.reader->width(), v.reader->height(), v.rgba, false);
    v.shown = source;
    if (const double start = v.reader->frameTime(); v.image && start >= 0 && m.frameBudget > 0) {
        Impl::CachedFrame& entry = cached[start];
        if (entry.image) m.frameBytes -= entry.image->imageInfo().computeMinByteSize();
        entry = {v.image, frame, ++m.frameUses};
        m.frameBytes += v.image->imageInfo().computeMinByteSize();
        m.trimFrames();
    }
    return v.image;
}

namespace {

// Decodes `count` frames along the video, each shrunk to `height` pixels high (box filter).
std::shared_ptr<const Filmstrip> makeFilmstrip(const std::filesystem::path& file, double duration, const std::atomic_bool* cancel) {
    std::unique_ptr<VideoReader> reader = VideoReader::open(file);
    if (!reader || reader->width() <= 0 || reader->height() <= 0 || duration <= 0) return nullptr;
    auto strip = std::make_shared<Filmstrip>();
    const int count = std::clamp(static_cast<int>(std::ceil(duration / 0.5)), 1, 160); // one every half second, at most 160
    strip->step = duration / count;
    strip->height = 54;
    strip->width = std::max(1, static_cast<int>(std::lround(strip->height * static_cast<double>(reader->width()) / reader->height())));
    const int sw = reader->width(), sh = reader->height(), dw = strip->width, dh = strip->height;
    std::vector<uint8_t> rgba;
    for (int i = 0; i < count && !cancel->load(); ++i) {
        if (!reader->frameAt((i + 0.5) * strip->step, rgba) || rgba.size() < static_cast<size_t>(sw) * sh * 4) break;
        std::vector<uint8_t> small(static_cast<size_t>(dw) * dh * 4);
        for (int y = 0; y < dh; ++y) {
            const int y0 = y * sh / dh, y1 = std::max(y0 + 1, (y + 1) * sh / dh);
            for (int x = 0; x < dw; ++x) {
                const int x0 = x * sw / dw, x1 = std::max(x0 + 1, (x + 1) * sw / dw);
                // A few samples of the box are plenty for a picture this small.
                uint32_t sum[4] = {0, 0, 0, 0}, n = 0;
                for (int yy = y0; yy < y1; yy += std::max(1, (y1 - y0) / 3))
                    for (int xx = x0; xx < x1; xx += std::max(1, (x1 - x0) / 3)) {
                        const uint8_t* px = rgba.data() + (static_cast<size_t>(yy) * sw + xx) * 4;
                        for (int c = 0; c < 4; ++c) sum[c] += px[c];
                        ++n;
                    }
                uint8_t* out = small.data() + (static_cast<size_t>(y) * dw + x) * 4;
                for (int c = 0; c < 4; ++c) out[c] = static_cast<uint8_t>(sum[c] / std::max(1u, n));
            }
        }
        strip->frames.push_back(std::move(small));
    }
    return strip->frames.empty() ? nullptr : strip;
}

} // namespace

std::shared_ptr<const Filmstrip> MediaManager::filmstrip(const MediaItem& item) {
    if (item.kind != MediaKind::Video || !item.hasVideo || item.duration <= 0) return nullptr;
    Impl& m = impl();
    const bool proxy = hasProxy(item);
    const std::string file = proxy ? platform8(proxyPath(item)) : item.path;
    Impl::FilmstripJob& f = m.filmstrips[file];
    if (f.done) return f.done;
    if (!f.job.valid()) {
        try {
            f.job = std::async(std::launch::async, [path = pathOf(file), duration = item.duration, cancel = f.cancel, arrivals = &m.arrivals] {
                auto strip = makeFilmstrip(path, duration, cancel.get());
                arrivals->fetch_add(1); // the timeline draws again
                return strip;
            });
        } catch (const std::exception&) {
            return nullptr;
        }
        return nullptr;
    }
    if (f.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    f.done = f.job.get();
    if (!f.done) f.done = std::make_shared<Filmstrip>(); // nothing to show: not tried again
    return f.done->frames.empty() ? nullptr : f.done;
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
