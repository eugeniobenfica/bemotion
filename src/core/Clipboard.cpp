#include "core/Clipboard.h"

#include "core/Serialization.h"

#include <algorithm>
#include <limits>
#include <map>

namespace bemotion {

namespace {

// Marks the text as layers (a regular project file is not pasted by accident).
constexpr const char* kClipName = "bemotion-clipboard";

bool contains(const std::vector<uint64_t>& ids, uint64_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

} // namespace

std::string copyLayers(const Composition& comp, const std::vector<uint64_t>& ids) {
    std::vector<uint64_t> all;
    for (uint64_t id : ids) {
        if (!comp.findLayer(id) || contains(all, id)) continue;
        all.push_back(id);
        for (uint64_t d : comp.descendantsOf(id))
            if (!contains(all, d)) all.push_back(d);
    }
    if (all.empty()) return {};
    Project clip;
    clip.name = kClipName;
    clip.comp.width = comp.width;
    clip.comp.height = comp.height;
    clip.comp.fps = comp.fps;
    for (const Layer& l : comp.layers) // stack order kept
        if (contains(all, l.id)) {
            clip.comp.layers.push_back(l);
            if (l.type == LayerType::Media)
                if (const MediaItem* m = comp.findMedia(l.media.mediaId);
                    m && std::none_of(clip.comp.media.begin(), clip.comp.media.end(), [&](const MediaItem& x) { return x.id == m->id; }))
                    clip.comp.media.push_back(*m);
        }
    clip.comp.nextLayerId = comp.nextLayerId;
    clip.comp.nextTrackId = comp.nextTrackId;
    clip.comp.nextMediaId = comp.nextMediaId;
    clip.comp.fitDuration();
    return serializeProject(clip);
}

bool isCopiedLayers(const std::string& text) {
    if (text.find(kClipName) == std::string::npos) return false;
    Project clip;
    return deserializeProject(text, clip) && clip.name == kClipName;
}

std::vector<uint64_t> pasteLayers(Composition& comp, const std::string& text, double time, int index, uint64_t scope) {
    Project clip;
    if (!deserializeProject(text, clip) || clip.name != kClipName || clip.comp.layers.empty()) return {};
    std::map<uint64_t, uint64_t> media; // copied id -> id here
    for (const MediaItem& m : clip.comp.media) media[m.id] = comp.addMedia(m);

    const auto inClip = [&](uint64_t id) { return id && clip.comp.findLayer(id); };
    double earliest = std::numeric_limits<double>::max();
    for (const Layer& l : clip.comp.layers)
        if (!inClip(l.parent)) earliest = std::min(earliest, l.inPoint);
    const double delta = comp.snapToFrame(std::max(0.0, time)) - earliest;

    // Bottom-up, each above `index`: the copies keep their order.
    std::map<uint64_t, uint64_t> ids; // copied id -> new id
    std::vector<uint64_t> top;
    index = std::clamp(index, 0, static_cast<int>(comp.layers.size()));
    for (auto it = clip.comp.layers.rbegin(); it != clip.comp.layers.rend(); ++it) {
        Layer l = *it;
        const uint64_t old = l.id;
        l.track = 0;
        l.shiftTime(delta);
        if (l.type == LayerType::Media) l.media.mediaId = media.count(l.media.mediaId) ? media[l.media.mediaId] : 0;
        if (!inClip(l.parent)) l.name = comp.uniqueLayerName(l.name);
        const uint64_t fresh = comp.addLayer(std::move(l), index).id;
        ids[old] = fresh;
        if (!inClip(it->parent)) top.insert(top.begin(), fresh);
    }
    for (const Layer& src : clip.comp.layers) {
        Layer* l = comp.findLayer(ids[src.id]);
        if (inClip(src.parent)) l->parent = ids[src.parent];
        else l->parent = comp.findLayer(scope) && l->type != LayerType::Camera ? scope : 0; // no compound copied: where we paste
    }
    // A track matte copied along: its copy. Otherwise the matte stays only beside its layer.
    for (const Layer& src : clip.comp.layers) {
        Layer* l = comp.findLayer(ids[src.id]);
        const Layer* matte = src.matte != MatteMode::None && inClip(src.matteLayer) ? comp.findLayer(ids[src.matteLayer]) : nullptr;
        if (matte && matte->parent == l->parent) l->matteLayer = matte->id;
        else if (src.matte != MatteMode::None) l->matte = MatteMode::None, l->matteLayer = 0;
    }
    comp.normalizeTracks();
    comp.fitDuration();
    return top;
}

} // namespace bemotion
