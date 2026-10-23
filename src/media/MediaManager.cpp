#include "media/MediaManager.h"

#include "media/MediaIO.h"
#include "render/SceneRenderer.h"

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"

#include <chrono>
#include <cmath>
#include <future>
#include <map>
#include <mutex>

namespace bemotion::media {

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// Straight RGBA -> an immutable Skia image (copied; mipmaps for still pictures that get scaled down).
sk_sp<SkImage> toImage(int w, int h, const std::vector<uint8_t>& rgba, bool mipmaps) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return nullptr;
    const SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    sk_sp<SkImage> img = SkImages::RasterFromData(info, SkData::MakeWithCopy(rgba.data(), rgba.size()), static_cast<size_t>(w) * 4);
    return img && mipmaps ? img->withDefaultMipmaps() : img;
}

} // namespace

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
    };
    struct Audio {
        std::string path;
        AudioData data;
        std::future<std::pair<std::vector<float>, std::string>> job;
        std::string error;
    };
    std::map<uint64_t, Picture> pictures; // by media id
    // By composition and layer: an export renders its own snapshot of the composition, so it gets
    // readers of its own instead of seeking the editor's back and forth.
    std::map<std::pair<const Composition*, uint64_t>, Video> videos;
    int tick = 0;
    std::map<uint64_t, Audio> audios; // by media id
    std::map<uint64_t, std::string> errors;
    mutable std::mutex mutex; // audios: read by the audio engine from other threads
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
    m.pictures.clear();
    m.videos.clear();
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
                std::vector<float> samples;
                std::string error;
                decodeAudio(file, samples, &error);
                return std::make_pair(std::move(samples), error);
            });
        }
        if (a.job.valid() && a.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto [samples, error] = a.job.get();
            a.error = error;
            if (error.empty()) a.data = std::make_shared<const std::vector<float>>(std::move(samples));
        }
    }
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
    Impl::Video& v = m.videos[{&comp, layer.id}];
    v.lastUse = m.tick;
    if (v.path != item->path) {
        v = {};
        v.path = item->path;
        v.lastUse = m.tick;
        std::string error;
        v.reader = VideoReader::open(pathOf(item->path), &error);
        if (!v.reader) m.errors[item->id] = error;
    }
    if (!v.reader) return nullptr;
    const double source = std::clamp(layer.media.sourceTime(t), 0.0, std::max(0.0, item->duration));
    const double frame = item->fps > 0 ? 1.0 / item->fps : 1.0 / 30.0;
    if (v.image && std::fabs(source - v.shown) < frame * 0.5) return v.image; // this frame is already decoded
    if (!v.reader->frameAt(source, v.rgba)) return v.image;
    v.image = toImage(v.reader->width(), v.reader->height(), v.rgba, false);
    v.shown = source;
    return v.image;
}

AudioData MediaManager::audio(uint64_t mediaId) const {
    Impl& m = impl();
    std::lock_guard lock(m.mutex);
    const auto it = m.audios.find(mediaId);
    return it == m.audios.end() ? nullptr : it->second.data;
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
