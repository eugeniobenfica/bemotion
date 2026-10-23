#include "core/Arrange.h"

#include <algorithm>
#include <limits>

namespace bemotion {

namespace {

// The layers to arrange, in stacking order: cameras and layers inside compounds stay out
// (a compound's children are positioned relative to it).
std::vector<Layer*> arrangeable(Composition& comp, const std::vector<uint64_t>& ids) {
    std::vector<Layer*> out;
    for (Layer& l : comp.layers)
        if (std::find(ids.begin(), ids.end(), l.id) != ids.end() && l.type != LayerType::Camera && !comp.findLayer(l.parent))
            out.push_back(&l);
    return out;
}

} // namespace

bool distributeLayers(Composition& comp, const std::vector<uint64_t>& ids, bool vertical, double t, std::string* error) {
    std::vector<Layer*> layers = arrangeable(comp, ids);
    if (layers.size() < 2) {
        if (error) *error = "select at least two layers (cameras and layers inside compounds are not moved)";
        return false;
    }
    auto coord = [&](const Layer* l) {
        const Vec2 p = l->transform.position.valueAt(t);
        return vertical ? p.y : p.x;
    };
    std::stable_sort(layers.begin(), layers.end(), [&](const Layer* a, const Layer* b) { return coord(a) < coord(b); });
    const float extent = static_cast<float>(vertical ? comp.height : comp.width);
    float first = coord(layers.front()), last = coord(layers.back());
    if (layers.size() == 2) { // nothing in between to keep: spread over the frame
        first = extent / 3.0f;
        last = extent * 2.0f / 3.0f;
    }
    const float step = (last - first) / static_cast<float>(layers.size() - 1);
    for (size_t i = 0; i < layers.size(); ++i) {
        Vec2 p = layers[i]->transform.position.valueAt(t);
        (vertical ? p.y : p.x) = first + step * static_cast<float>(i);
        layers[i]->transform.position.set(t, p);
    }
    return true;
}

bool staggerLayers(Composition& comp, const std::vector<uint64_t>& ids, double step, std::string* error) {
    std::vector<Layer*> layers = arrangeable(comp, ids);
    if (layers.size() < 2) {
        if (error) *error = "select at least two layers";
        return false;
    }
    double start = std::numeric_limits<double>::max();
    for (const Layer* l : layers) start = std::min(start, l->inPoint);
    // Moved in time, a clip could run into another one on its track: each gets a track of its own.
    for (Layer* l : layers) {
        const auto shared = std::count_if(comp.layers.begin(), comp.layers.end(), [&](const Layer& o) { return o.track == l->track; });
        if (shared > 1) l->track = comp.nextTrackId++;
    }
    for (size_t i = 0; i < layers.size(); ++i) {
        const double target = comp.snapToFrame(start + step * static_cast<double>(i));
        const double delta = target - layers[i]->inPoint;
        // A compound's contents move with it.
        for (uint64_t child : comp.descendantsOf(layers[i]->id))
            if (Layer* c = comp.findLayer(child)) c->shiftTime(delta);
        layers[i]->shiftTime(delta);
    }
    comp.normalizeTracks(); // clips that now overlap on a track move to tracks of their own
    comp.fitDuration();
    return true;
}

} // namespace bemotion
