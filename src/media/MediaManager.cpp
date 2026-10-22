#include "media/MediaManager.h"

#include "media/MediaIO.h"
#include "render/GL.h"
#include "render/SceneRenderer.h"

#include <chrono>
#include <future>
#include <map>
#include <mutex>

namespace bemotion::media {

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

GLuint makeTexture() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    return tex;
}

void upload(GLuint tex, int w, int h, const std::vector<uint8_t>& rgba, bool sameSize) {
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (sameSize) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}

ImTextureID asId(GLuint tex) { return static_cast<ImTextureID>(static_cast<uintptr_t>(tex)); }

} // namespace

struct MediaManager::Impl {
    struct Image {
        std::string path;
        GLuint tex = 0;
        std::string error;
    };
    struct Video { // per layer
        std::string path;
        std::unique_ptr<VideoReader> reader;
        GLuint tex = 0;
        int w = 0, h = 0;
        double shown = -1.0; // source time of the uploaded frame
        std::vector<uint8_t> rgba;
        int lastUse = 0;
    };
    struct Audio {
        std::string path;
        AudioData data;
        std::future<std::pair<std::vector<float>, std::string>> job;
        std::string error;
    };
    std::map<uint64_t, Image> images;     // by media id (images, video thumbnails)
    // By composition and layer: an export renders its own snapshot of the composition, so it gets
    // readers of its own instead of seeking the editor's back and forth.
    std::map<std::pair<const Composition*, uint64_t>, Video> videos;
    int tick = 0;
    std::map<uint64_t, Audio> audios;     // by media id
    std::map<uint64_t, std::string> errors;
    mutable std::mutex mutex;             // audios: read by the audio engine from other threads
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
    for (auto& [id, img] : m.images)
        if (img.tex) glDeleteTextures(1, &img.tex);
    for (auto& [id, v] : m.videos)
        if (v.tex) glDeleteTextures(1, &v.tex);
    m.images.clear();
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
        if (gone) {
            if (it->second.tex) glDeleteTextures(1, &it->second.tex);
            it = m.videos.erase(it);
        } else {
            ++it;
        }
    }
}

ImTextureID MediaManager::thumbnail(const MediaItem& item) {
    Impl& m = impl();
    Impl::Image& img = m.images[item.id];
    if (img.path == item.path) return asId(img.tex);
    img.path = item.path;
    img.error.clear();
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
        img.error = error;
        if (!error.empty()) m.errors[item.id] = error;
        return 0;
    }
    if (!img.tex) img.tex = makeTexture();
    upload(img.tex, w, h, rgba, false);
    return asId(img.tex);
}

ImTextureID MediaManager::frameFor(const Composition& comp, const Layer& layer, double t) {
    const MediaItem* item = comp.findMedia(layer.media.mediaId);
    if (!item || !item->hasVideo) return 0;
    if (item->kind == MediaKind::Image) return thumbnail(*item); // the full image
    Impl& m = impl();
    Impl::Video& v = m.videos[{&comp, layer.id}];
    v.lastUse = m.tick;
    if (v.path != item->path) {
        if (v.tex) glDeleteTextures(1, &v.tex);
        v = {};
        v.path = item->path;
        std::string error;
        v.reader = VideoReader::open(pathOf(item->path), &error);
        if (!v.reader) {
            m.errors[item->id] = error;
            return 0;
        }
    }
    if (!v.reader) return 0;
    const double source = std::clamp(layer.media.sourceTime(t), 0.0, std::max(0.0, item->duration));
    const double frame = item->fps > 0 ? 1.0 / item->fps : 1.0 / 30.0;
    if (v.tex && std::fabs(source - v.shown) < frame * 0.5) return asId(v.tex); // this frame is already up
    if (!v.reader->frameAt(source, v.rgba)) return v.tex ? asId(v.tex) : 0;
    const int w = v.reader->width(), h = v.reader->height();
    const bool sameSize = v.tex && w == v.w && h == v.h;
    if (!v.tex) v.tex = makeTexture();
    upload(v.tex, w, h, v.rgba, sameSize);
    v.w = w, v.h = h;
    v.shown = source;
    return asId(v.tex);
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
