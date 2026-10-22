#include "ui/EditorContext.h"

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

Layer& EditorContext::insertLayer(Layer layer) {
    layer.name = comp().uniqueLayerName(layer.name);
    layer.label = static_cast<int>(comp().nextLayerId % kLabelColorCount);
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

void EditorContext::deleteSelected() {
    if (selection.empty()) return;
    int index = static_cast<int>(comp().layers.size());
    for (uint64_t id : selection) index = std::min(index, comp().indexOf(id));
    for (uint64_t id : selection) comp().removeLayer(id);
    selectedKeys.clear();
    auto& layers = comp().layers;
    select(layers.empty() ? 0 : layers[std::clamp<size_t>(index, 0, layers.size() - 1)].id);
    touch();
}

void EditorContext::duplicateSelected() {
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

} // namespace bemotion
