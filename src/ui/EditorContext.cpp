#include "ui/EditorContext.h"

#include "app/PluginHost.h"
#include "core/Serialization.h"

#include <algorithm>
#include <cmath>

namespace bemotion {

void EditorContext::open(Project p, std::filesystem::path file) {
    project = std::move(p);
    path = std::move(file);
    history.reset(project);
    selection.clear();
    selectedKeys.clear();
    tool = Tool::Select;
    scope = 0;
    time = 0.0;
    playing = false;
    pendingCommit_ = false;
}

// --- selection -----------------------------------------------------------------

bool EditorContext::isSelected(uint64_t layerId) const {
    return std::find(selection.begin(), selection.end(), layerId) != selection.end();
}

std::vector<Layer*> EditorContext::selectedLayers() {
    std::vector<Layer*> out;
    for (auto& l : comp().layers)
        if (isSelected(l.id)) out.push_back(&l);
    return out;
}

void EditorContext::select(uint64_t layerId) {
    if (primaryId() != layerId) selectedKeys.clear();
    selection.clear();
    if (layerId) selection.push_back(layerId);
}

void EditorContext::toggleSelected(uint64_t layerId) {
    if (isSelected(layerId)) std::erase(selection, layerId);
    else selection.push_back(layerId);
    selectedKeys.clear();
}

void EditorContext::selectRange(uint64_t layerId) {
    const int from = comp().indexOf(primaryId());
    const int to = comp().indexOf(layerId);
    if (from < 0 || to < 0) {
        select(layerId);
        return;
    }
    selection.clear();
    const int step = to >= from ? 1 : -1;
    for (int i = from; i != to + step; i += step) selection.push_back(comp().layers[i].id);
    selectedKeys.clear();
}

void EditorContext::selectAll() {
    selection.clear();
    for (const auto& l : comp().layers) selection.push_back(l.id);
    selectedKeys.clear();
}

bool EditorContext::isKeySelected(const KeySelection& k) const {
    return std::find(selectedKeys.begin(), selectedKeys.end(), k) != selectedKeys.end();
}

void EditorContext::pruneSelection() {
    std::erase_if(selection, [&](uint64_t id) { return comp().findLayer(id) == nullptr; });
}

// --- time ------------------------------------------------------------------------

void EditorContext::setTime(double t) { time = comp().snapToFrame(std::clamp(t, 0.0, comp().duration)); }

void EditorContext::stepFrames(int frames) {
    playing = false;
    setTime(comp().timeOfFrame(comp().frameAt(time) + frames));
}

void EditorContext::tick(double dt) {
    if (!playing) return;
    const Composition& c = comp();
    time += dt;
    const double end = c.timeOfFrame(c.frameCount() - 1);
    if (time > end) {
        if (loop) time = std::fmod(time - end - c.frameDuration(), c.duration);
        else {
            time = end;
            playing = false;
        }
        time = std::max(0.0, time);
    }
}

// --- history / files ---------------------------------------------------------------

void EditorContext::commitIfIdle(bool interacting) {
    if (!pendingCommit_ || interacting) return;
    history.commit(project);
    pendingCommit_ = false;
}

void EditorContext::undo() {
    commitIfIdle(false);
    if (history.undo(project)) {
        selectedKeys.clear();
        pruneSelection();
    }
}

void EditorContext::redo() {
    commitIfIdle(false);
    if (history.redo(project)) {
        selectedKeys.clear();
        pruneSelection();
    }
}

bool EditorContext::save(std::string* error) { return saveTo(path, error); }

bool EditorContext::saveTo(const std::filesystem::path& file, std::string* error) {
    commitIfIdle(false);
    if (!saveProject(project, file, error)) return false;
    path = file;
    history.markSaved();
    return true;
}

// --- document operations -----------------------------------------------------------

void EditorContext::enterScope(uint64_t compoundId) {
    const Layer* l = comp().findLayer(compoundId);
    if (!l || l->type != LayerType::Compound) return;
    scope = compoundId;
    selection.clear();
    selectedKeys.clear();
}

void EditorContext::exitScope() {
    const Layer* l = scope ? comp().findLayer(scope) : nullptr;
    const uint64_t up = l ? l->parent : 0;
    if (l) select(l->id); // back out with the compound selected
    scope = up;
}

Layer& EditorContext::insertLayer(Layer layer) {
    // Inside an open compound, new layers belong to it (their position becomes relative to it).
    if (scope && !comp().findLayer(scope)) scope = 0;
    if (scope && layer.type != LayerType::Camera) {
        const Layer& c = *comp().findLayer(scope);
        const Affine toLocal = comp().worldMatrix(c, time).inverse();
        layer.parent = scope;
        layer.transform.position.base = toLocal.apply(layer.transform.position.base);
    }
    layer.name = comp().uniqueLayerName(layer.name);
    if (layer.type != LayerType::Media) layer.label = static_cast<int>(comp().nextLayerId % kLabelColorCount);
    // Like After Effects: the new layer starts at the playhead and runs to the end of the composition.
    const double start = comp().snapToFrame(std::clamp(time, 0.0, std::max(0.0, comp().duration - comp().frameDuration())));
    if (start > 0.0) {
        layer.shiftTime(start - layer.inPoint); // keyframes (e.g. plugin intros) move along
        // Footage keeps its own length; everything else runs to the end of the composition.
        const MediaItem* item = layer.type == LayerType::Media ? comp().findMedia(layer.media.mediaId) : nullptr;
        const bool footage = item && item->kind != MediaKind::Image;
        if (!footage) layer.outPoint = std::max(comp().duration, layer.inPoint + comp().frameDuration());
    }
    // New layers go right above the primary selection, or on top.
    const int index = std::max(0, comp().indexOf(primaryId()));
    Layer& added = comp().addLayer(std::move(layer), index);
    select(added.id);
    touch();
    return added;
}

Layer& EditorContext::addShape(ShapeKind kind, std::optional<Vec2> position) {
    return insertLayer(makeShapeLayer(kind, position.value_or(comp().center()), comp().duration));
}

Layer& EditorContext::addText(std::optional<Vec2> position) {
    return insertLayer(makeTextLayer("Your text", position.value_or(comp().center()), comp().duration));
}

Layer& EditorContext::addCamera() {
    const Vec2 size{static_cast<float>(comp().width), static_cast<float>(comp().height)};
    Layer& cam = insertLayer(makeCameraLayer(size, comp().duration));
    return cam;
}

Layer& EditorContext::addPluginObject(const PluginObjectType& type, std::optional<Vec2> position) {
    Layer layer = PluginHost::get().createLayer(type, position.value_or(comp().center()), comp().duration);
    return insertLayer(std::move(layer));
}

Layer* EditorContext::addMedia(uint64_t mediaId, std::optional<Vec2> position, std::optional<double> start) {
    const MediaItem* item = comp().findMedia(mediaId);
    if (!item) return nullptr;
    Layer layer = makeMediaLayer(*item, position.value_or(comp().center()), 0.0, comp().duration);
    // Footage larger than the composition is scaled down to fit (like After Effects' "fit" on import).
    if (item->width > 0 && item->height > 0) {
        const float fit = std::min({1.0f, static_cast<float>(comp().width) / item->width,
                                    static_cast<float>(comp().height) / item->height});
        layer.transform.scale.base = {fit * 100.0f, fit * 100.0f};
    }
    const double saved = time;
    if (start) time = std::clamp(*start, 0.0, comp().duration); // new layers start at `time`
    Layer* added = &insertLayer(std::move(layer));
    time = saved;
    return added;
}

Layer* EditorContext::addObject(int kind, Vec2 position, double start) {
    const double saved = time;
    time = std::clamp(start, 0.0, comp().duration); // new layers start at `time`
    Layer* added = nullptr;
    if (kind == -2) added = &addCamera(); // kDropCamera
    else if (kind == -1) added = &addText(position);
    else if (kind >= 0 && kind < static_cast<int>(ShapeKind::Count)) added = &addShape(static_cast<ShapeKind>(kind), position);
    else if (kind <= -100) { // kDropPlugin - i: plugin object type i
        const auto types = PluginHost::get().objectTypes();
        if (const size_t i = static_cast<size_t>(-100 - kind); i < types.size()) added = &addPluginObject(*types[i], position);
    }
    time = saved;
    return added;
}

void EditorContext::deleteSelected() {
    if (selection.empty()) return;
    int index = static_cast<int>(comp().layers.size());
    for (uint64_t id : selection) index = std::min(index, comp().indexOf(id));
    for (uint64_t id : selection) comp().removeLayerTree(id); // a compound takes its contents along
    selectedKeys.clear();
    auto& layers = comp().layers;
    select(layers.empty() ? 0 : layers[std::clamp<size_t>(index, 0, layers.size() - 1)].id);
    touch();
}

void EditorContext::duplicateSelected() {
    // Selected compounds are copied with their contents, re-linked to the copies.
    std::vector<uint64_t> trees;
    for (uint64_t id : selection)
        if (const Layer* l = comp().findLayer(id); l && l->type == LayerType::Compound) trees.push_back(id);
    if (!trees.empty()) {
        std::vector<uint64_t> copies;
        for (uint64_t root : trees) {
            std::vector<uint64_t> ids = comp().descendantsOf(root);
            ids.insert(ids.begin(), root);
            std::vector<Layer> layers;
            for (uint64_t id : ids) layers.push_back(*comp().findLayer(id));
            std::vector<std::pair<uint64_t, uint64_t>> remap; // old id -> new id
            uint64_t rootCopy = 0;
            // Bottom-up, each copy goes above the original compound: the copies keep their order.
            for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
                Layer l = *it;
                const uint64_t old = l.id;
                l.track = 0;
                if (old == root) l.name = comp().uniqueLayerName(l.name);
                const uint64_t fresh = comp().addLayer(std::move(l), comp().indexOf(root)).id;
                remap.emplace_back(old, fresh);
                if (old == root) rootCopy = fresh;
            }
            for (auto [old, fresh] : remap) {
                Layer* l = comp().findLayer(fresh);
                for (auto [o, f] : remap)
                    if (l->parent == o) l->parent = f;
            }
            copies.push_back(rootCopy);
        }
        selection = copies;
        selectedKeys.clear();
        touch();
        return;
    }
    const auto sources = selectedLayers();
    if (sources.empty()) return;
    // Copy first: adding layers invalidates the pointers.
    std::vector<std::pair<Layer, int>> copies;
    for (const Layer* src : sources) {
        Layer copy = *src;
        copy.name = comp().uniqueLayerName(src->name);
        copy.track = 0; // each copy gets its own new track
        copies.emplace_back(std::move(copy), comp().indexOf(src->id));
    }
    selection.clear();
    selectedKeys.clear();
    // Insert bottom-up so earlier indices stay valid; each copy's track sits above its source's.
    for (auto it = copies.rbegin(); it != copies.rend(); ++it)
        selection.push_back(comp().addLayer(std::move(it->first), it->second).id);
    touch();
}

std::vector<uint64_t> EditorContext::selectedTracks() {
    std::vector<uint64_t> tracks;
    for (const Layer* l : selectedLayers())
        if (std::find(tracks.begin(), tracks.end(), l->track) == tracks.end()) tracks.push_back(l->track);
    return tracks;
}

void EditorContext::moveSelectedTracksTo(int targetRow) {
    if (selection.empty()) return;
    comp().moveTracks(selectedTracks(), targetRow);
    touch();
}

void EditorContext::moveSelectedClipsToNewTrack(int gapRow) {
    if (selection.empty()) return;
    comp().moveClipsToNewTracks(selection, gapRow);
    touch();
}

bool EditorContext::moveSelectedClipsToTrack(uint64_t trackId) {
    if (selection.empty() || !comp().moveClipsToTrack(selection, trackId)) return false;
    touch();
    return true;
}

void EditorContext::createCompound() {
    if (selection.empty()) return;
    if (const uint64_t id = comp().createCompound(selection, time)) {
        select(id);
        touch();
    }
}

void EditorContext::releaseSelectedCompounds() {
    std::vector<uint64_t> released;
    for (uint64_t id : selection)
        if (const Layer* l = comp().findLayer(id); l && l->type == LayerType::Compound) {
            for (const Layer& c : comp().layers)
                if (c.parent == id) released.push_back(c.id);
            comp().releaseCompound(id, time);
        }
    if (released.empty() && !selectionHasCompound()) return;
    selection = released;
    selectedKeys.clear();
    touch();
}

bool EditorContext::selectionHasCompound() {
    for (const Layer* l : selectedLayers())
        if (l->type == LayerType::Compound) return true;
    return false;
}

std::vector<uint64_t> EditorContext::selectionWithContents() {
    std::vector<uint64_t> out = selection;
    for (uint64_t id : selection)
        for (uint64_t d : comp().descendantsOf(id))
            if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    return out;
}

void EditorContext::shiftSelected(int delta) {
    if (selection.empty()) return;
    comp().shiftTracks(selectedTracks(), delta);
    touch();
}

void EditorContext::splitLayerAt(uint64_t id, double t) {
    if (const uint64_t tail = comp().splitLayer(id, comp().snapToFrame(t))) {
        select(tail);
        touch();
    }
}

void EditorContext::splitSelectedAtPlayhead() {
    std::vector<uint64_t> targets = selection;
    if (targets.empty())
        for (const auto& l : comp().layers) targets.push_back(l.id);
    std::vector<uint64_t> tails;
    for (uint64_t id : targets)
        if (const uint64_t tail = comp().splitLayer(id, time)) tails.push_back(tail);
    if (tails.empty()) return;
    selection = tails;
    selectedKeys.clear();
    touch();
}

void EditorContext::deleteSelectedKeys() {
    for (const auto& sel : selectedKeys) {
        Layer* layer = comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) p->anim->removeKey(i);
    }
    if (!selectedKeys.empty()) touch();
    selectedKeys.clear();
}

void EditorContext::setSelectedKeysEasing(Easing easing) {
    for (const auto& sel : selectedKeys) {
        Layer* layer = comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) p->anim->setKeyEasing(i, easing);
    }
    touch();
}

template <typename Fn>
void EditorContext::forEachSelectedKey(Fn&& fn) {
    for (const auto& sel : selectedKeys) {
        Layer* layer = comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) fn(*p, i);
    }
}

void EditorContext::setSelectedKeysCurve(const BezierEase& curve) {
    forEachSelectedKey([&](PropertyRef& p, int i) {
        p.anim->setKeyEasing(i, Easing::Custom);
        p.anim->setKeyCurve(i, curve);
    });
    touch();
}

void EditorContext::setSelectedKeysPath(PathMode mode) {
    forEachSelectedKey([&](PropertyRef& p, int i) {
        if (p.kind != PropertyKind::Vec2) return;
        auto& keys = p.asVec2().keys;
        Vec2 in, out;
        pathTangents(keys, static_cast<size_t>(i), in, out); // custom starts from the current shape
        keys[i].path = mode;
        keys[i].inTangent = in;
        keys[i].outTangent = out;
    });
    touch();
}

BezierEase EditorContext::selectedKeysCurve() {
    std::optional<BezierEase> curve;
    forEachSelectedKey([&](PropertyRef& p, int i) {
        if (!curve) curve = p.anim->keyEasing(i) == Easing::Custom ? p.anim->keyCurve(i) : bezierFor(p.anim->keyEasing(i));
    });
    return curve.value_or(BezierEase{});
}

bool EditorContext::selectionHasPathKeys() {
    bool any = false;
    forEachSelectedKey([&](PropertyRef& p, int) { any = any || p.kind == PropertyKind::Vec2; });
    return any;
}

} // namespace bemotion
