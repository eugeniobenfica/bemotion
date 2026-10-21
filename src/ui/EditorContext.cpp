#include "ui/EditorContext.h"

#include "core/Serialization.h"

#include <algorithm>
#include <cmath>

namespace mirage {

void EditorContext::open(Project p, std::filesystem::path file) {
    project = std::move(p);
    path = std::move(file);
    history.reset(project);
    selectedLayer = 0;
    selectedKeys.clear();
    time = 0.0;
    playing = false;
    pendingCommit_ = false;
}

void EditorContext::select(uint64_t layerId) {
    if (selectedLayer != layerId) selectedKeys.clear();
    selectedLayer = layerId;
}

bool EditorContext::isKeySelected(const KeySelection& k) const {
    return std::find(selectedKeys.begin(), selectedKeys.end(), k) != selectedKeys.end();
}

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

void EditorContext::commitIfIdle(bool interacting) {
    if (!pendingCommit_ || interacting) return;
    history.commit(project);
    pendingCommit_ = false;
}

void EditorContext::undo() {
    commitIfIdle(false);
    if (history.undo(project)) {
        selectedKeys.clear();
        if (!comp().findLayer(selectedLayer)) selectedLayer = 0;
    }
}

void EditorContext::redo() {
    commitIfIdle(false);
    if (history.redo(project)) {
        selectedKeys.clear();
        if (!comp().findLayer(selectedLayer)) selectedLayer = 0;
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

Layer& EditorContext::insertLayer(Layer layer) {
    layer.name = comp().uniqueLayerName(layer.name);
    layer.label = static_cast<int>(comp().nextLayerId % kLabelColorCount);
    // New layers go right above the selection, or on top.
    const int index = std::max(0, comp().indexOf(selectedLayer));
    Layer& added = comp().addLayer(std::move(layer), index);
    select(added.id);
    touch();
    return added;
}

Layer& EditorContext::addShape(ShapeKind kind) {
    return insertLayer(makeShapeLayer(kind, comp().center(), comp().duration));
}

Layer& EditorContext::addText() { return insertLayer(makeTextLayer("Your text", comp().center(), comp().duration)); }

void EditorContext::deleteSelected() {
    if (!selected()) return;
    const int index = comp().indexOf(selectedLayer);
    comp().removeLayer(selectedLayer);
    selectedKeys.clear();
    auto& layers = comp().layers;
    selectedLayer = layers.empty() ? 0 : layers[std::min<size_t>(index, layers.size() - 1)].id;
    touch();
}

void EditorContext::duplicateSelected() {
    const Layer* src = selected();
    if (!src) return;
    Layer copy = *src;
    copy.name = comp().uniqueLayerName(src->name);
    const int index = comp().indexOf(selectedLayer);
    Layer& added = comp().addLayer(std::move(copy), index);
    select(added.id);
    touch();
}

void EditorContext::moveLayer(uint64_t id, int newIndex) {
    auto& layers = comp().layers;
    const int from = comp().indexOf(id);
    if (from < 0) return;
    newIndex = std::clamp(newIndex, 0, static_cast<int>(layers.size()) - 1);
    if (from == newIndex) return;
    Layer moving = std::move(layers[from]);
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + newIndex, std::move(moving));
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

} // namespace mirage
