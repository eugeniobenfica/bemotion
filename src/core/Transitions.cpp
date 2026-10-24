#include "core/Transitions.h"

#include "core/Presets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bemotion {

namespace {

struct Definition {
    TransitionInfo info;
    const char* in;  // preset tracks (core/Presets.h format), playing over the transition's duration
    const char* out;
};

// Movement offsets are in composition pixels.
const Definition kDefinitions[] = {
    {{"fade", "Fade", "Fades from / to transparent", "Fade"},
     R"([{"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"fade_up", "Fade Up", "Fades in while rising a little, fades out while lifting", "Fade"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,40]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,-40]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_left", "Slide Left", "Comes in from the left, leaves to the left", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[-320,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[-320,0]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_right", "Slide Right", "Comes in from the right, leaves to the right", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[320,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[320,0]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_up", "Slide Up", "Rises in from below, leaves upwards", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,200]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,-200]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_down", "Slide Down", "Drops in from above, leaves downwards", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,-200]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,200]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"zoom", "Zoom", "Grows in from smaller, grows out while fading", "Scale"},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":0.7},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":1.3}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"pop", "Pop", "Springs in from nothing, shrinks away", "Scale"},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":0},"ease":"back_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":0}}]}])"},
    {{"stretch", "Stretch", "Opens out sideways from a line, closes back into one", "Scale"},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":[0,1]},"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":[0,1]}}]}])"},
    {{"swing", "Swing", "Swings in from a tilt, swings away", "Rotate"},
     R"([{"property":"rotation","keys":[{"t":0,"v":{"offset":-25},"ease":"back_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.5,"v":"rest"}]}])",
     R"([{"property":"rotation","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":25}}]},
         {"property":"opacity","keys":[{"t":0.5,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"roll", "Roll", "Rolls in from the left, rolls away to the right", "Rotate"},
     R"([{"property":"rotation","keys":[{"t":0,"v":{"offset":-360},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"position","keys":[{"t":0,"v":{"offset":[-480,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}])",
     R"([{"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":360}}]},
         {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[480,0]}}]},
         {"property":"opacity","keys":[{"t":0.6,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"spin", "Spin", "Turns in while growing, turns away while shrinking", "Rotate"},
     R"([{"property":"rotation","keys":[{"t":0,"v":{"offset":-90},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"scale","keys":[{"t":0,"v":{"mul":0.4},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.6,"v":"rest"}]}])",
     R"([{"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":90}}]},
         {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":0.4}}]},
         {"property":"opacity","keys":[{"t":0.4,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"fade_down", "Fade Down", "Fades in while settling down, fades out while sinking", "Fade"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,-40]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,40]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"whip_left", "Whip Left", "Shoots in from far left, shoots away to the left", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[-1400,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[-1400,0]}}]}])"},
    {{"whip_right", "Whip Right", "Shoots in from far right, shoots away to the right", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[1400,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[1400,0]}}]}])"},
    {{"drop", "Drop", "Falls in and bounces, falls away", "Slide"},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,-300]},"ease":"bounce"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.3,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":[0,300]}}]},
         {"property":"opacity","keys":[{"t":0.7,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"zoom_out", "Zoom Out", "Shrinks in from bigger, shrinks away", "Scale"},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":1.5},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":0.6}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"elastic", "Elastic", "Snaps in with a wobble, snaps away", "Scale"},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":0},"ease":"elastic"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":0}}]}])"},
    {{"squash", "Squash", "Opens up from a line, flattens away", "Scale"},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":[1,0]},"ease":"back_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":[1,0]}}]}])"},
    {{"turn", "Turn", "Turns in like a door, turns away (makes the layer 3D)", "3D"},
     R"([{"property":"rotation_y","keys":[{"t":0,"v":{"offset":-90},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}])",
     R"([{"property":"rotation_y","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":90}}]},
         {"property":"opacity","keys":[{"t":0.6,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"tilt", "Tilt", "Swings up from lying flat, falls back (makes the layer 3D)", "3D"},
     R"([{"property":"rotation_x","keys":[{"t":0,"v":{"offset":80},"ease":"back_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}])",
     R"([{"property":"rotation_x","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":80}}]},
         {"property":"opacity","keys":[{"t":0.6,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"fly", "Fly", "Flies in from far away, flies past the camera (makes the layer 3D)", "3D"},
     R"([{"property":"position_z","keys":[{"t":0,"v":{"offset":3000},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.5,"v":"rest"}]}])",
     R"([{"property":"position_z","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":-2400}}]},
         {"property":"opacity","keys":[{"t":0.7,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
};

bool isThreeD(const char* category) { return std::strcmp(category, "3D") == 0; }

AnimationPreset presetOf(const char* tracks, double duration) {
    AnimationPreset p;
    parsePreset(std::string(R"({"name":"transition","tracks":)") + tracks + "}", p);
    p.duration = duration;
    return p;
}

void removePresetKeys(const AnimationPreset& preset, Layer& layer, double time) {
    const double start = std::clamp(time, layer.inPoint, std::max(layer.inPoint, layer.outPoint - preset.duration));
    const double end = start + preset.duration;
    auto properties = layer.properties();
    for (const PresetTrack& track : preset.tracks) {
        const PropertyRef* property = layer.findProperty(properties, track.property.c_str());
        if (!property) continue;
        for (int i = property->anim->keyCount() - 1; i >= 0; --i) {
            const double keyTime = property->anim->keyTime(i);
            if (keyTime >= start - kTimeEpsilon && keyTime <= end + kTimeEpsilon) property->anim->removeKey(i);
        }
    }
}

} // namespace

const std::vector<TransitionInfo>& transitions() {
    static const std::vector<TransitionInfo> list = [] {
        std::vector<TransitionInfo> v;
        for (const Definition& d : kDefinitions) v.push_back(d.info);
        return v;
    }();
    return list;
}

const std::vector<const char*>& transitionCategories() {
    static const std::vector<const char*> list = {"Fade", "Slide", "Scale", "Rotate", "3D"};
    return list;
}

const TransitionInfo* findTransition(const std::string& id) {
    for (const TransitionInfo& t : transitions())
        if (id == t.id) return &t;
    return nullptr;
}

const char* transitionEdgeId(TransitionEdge edge) {
    switch (edge) {
    case TransitionEdge::In: return "in";
    case TransitionEdge::Out: return "out";
    case TransitionEdge::Both: return "both";
    }
    return "in";
}

bool canTakeTransition(const Layer& layer) {
    return layer.type != LayerType::Camera && !(layer.type == LayerType::Media && layer.media.hasAudio && !layer.media.hasVideo);
}

bool canTakeTransition(const Layer& layer, const std::string& id) {
    const TransitionInfo* info = findTransition(id);
    if (!info || !canTakeTransition(layer)) return false;
    return !isThreeD(info->category) || (layer.type != LayerType::Background && !layer.parent);
}

bool applyTransition(Layer& layer, const std::string& id, TransitionEdge edge, double duration) {
    if (!canTakeTransition(layer)) return false;
    const Definition* def = nullptr;
    for (const Definition& d : kDefinitions)
        if (id == d.info.id) def = &d;
    if (!def) return false;
    if (isThreeD(def->info.category)) {
        if (layer.type == LayerType::Background || layer.parent) return false; // only top-level layers turn in 3D
        layer.threeD = true;
    }
    const double length = layer.outPoint - layer.inPoint;
    duration = std::clamp(duration, 0.05, edge == TransitionEdge::Both ? length * 0.5 : length);
    if (edge != TransitionEdge::Out) {
        applyPreset(presetOf(def->in, duration), layer, layer.inPoint);
        layer.transitionIn = {id, static_cast<float>(duration), false};
    }
    if (edge != TransitionEdge::In) {
        applyPreset(presetOf(def->out, duration), layer, layer.outPoint - duration);
        layer.transitionOut = {id, static_cast<float>(duration), false};
    }
    return true;
}

bool resizeTransition(Layer& layer, TransitionEdge edge, double duration) {
    ClipTransition* transition = edge == TransitionEdge::In ? &layer.transitionIn : edge == TransitionEdge::Out ? &layer.transitionOut : nullptr;
    if (!transition || !transition->active()) return false;
    const std::string id = transition->id;
    const bool joined = transition->joined;
    if (!removeTransition(layer, edge)) return false;
    if (!applyTransition(layer, id, edge, duration)) return false;
    transition->joined = joined;
    return true;
}

bool removeTransition(Layer& layer, TransitionEdge edge) {
    bool removed = false;
    auto remove = [&](ClipTransition& transition, const char* tracks, double time) {
        if (!transition.active()) return;
        removePresetKeys(presetOf(tracks, transition.duration), layer, time);
        transition = {};
        removed = true;
    };
    if (edge != TransitionEdge::Out) {
        const Definition* definition = nullptr;
        for (const Definition& candidate : kDefinitions)
            if (candidate.info.id == layer.transitionIn.id) definition = &candidate;
        if (definition) remove(layer.transitionIn, definition->in, layer.inPoint);
        else if (layer.transitionIn.active()) layer.transitionIn = {}, removed = true;
    }
    if (edge != TransitionEdge::In) {
        const Definition* definition = nullptr;
        for (const Definition& candidate : kDefinitions)
            if (candidate.info.id == layer.transitionOut.id) definition = &candidate;
        if (definition) remove(layer.transitionOut, definition->out, layer.outPoint - layer.transitionOut.duration);
        else if (layer.transitionOut.active()) layer.transitionOut = {}, removed = true;
    }
    return removed;
}

Layer* nextOnTrack(Composition& comp, const Layer& layer) {
    const double eps = comp.frameDuration() * 0.5;
    for (Layer& other : comp.layers)
        if (&other != &layer && other.track == layer.track && other.parent == layer.parent && std::fabs(other.inPoint - layer.outPoint) < eps)
            return &other;
    return nullptr;
}

Layer* transitionPartner(Composition& comp, const Layer& layer, TransitionEdge edge) {
    const double eps = comp.frameDuration() * 0.5;
    if (edge == TransitionEdge::Out) {
        if (!layer.transitionOut.joined) return nullptr;
        Layer* next = nextOnTrack(comp, layer);
        return next && next->transitionIn.joined ? next : nullptr;
    }
    if (edge != TransitionEdge::In || !layer.transitionIn.joined) return nullptr;
    for (Layer& other : comp.layers)
        if (&other != &layer && other.track == layer.track && other.parent == layer.parent && std::fabs(other.outPoint - layer.inPoint) < eps &&
            other.transitionOut.joined)
            return &other;
    return nullptr;
}

bool applyCutTransition(Composition& comp, Layer& first, const std::string& id, double duration) {
    Layer* second = nextOnTrack(comp, first);
    if (!second || !canTakeTransition(first, id) || !canTakeTransition(*second, id)) return false;
    const double half = std::max(0.05, duration * 0.5);
    if (first.transitionOut.active()) removeTransition(comp, first, TransitionEdge::Out);
    if (second->transitionIn.active()) removeTransition(comp, *second, TransitionEdge::In);
    if (!applyTransition(first, id, TransitionEdge::Out, half)) return false;
    if (!applyTransition(*second, id, TransitionEdge::In, half)) {
        removeTransition(first, TransitionEdge::Out);
        return false;
    }
    first.transitionOut.joined = second->transitionIn.joined = true;
    return true;
}

bool resizeTransition(Composition& comp, Layer& layer, TransitionEdge edge, double duration) {
    Layer* partner = transitionPartner(comp, layer, edge);
    if (!resizeTransition(layer, edge, duration)) return false;
    if (partner) resizeTransition(*partner, edge == TransitionEdge::In ? TransitionEdge::Out : TransitionEdge::In, duration);
    return true;
}

bool removeTransition(Composition& comp, Layer& layer, TransitionEdge edge) {
    Layer* partner = transitionPartner(comp, layer, edge);
    const bool removed = removeTransition(layer, edge);
    if (partner) removeTransition(*partner, edge == TransitionEdge::In ? TransitionEdge::Out : TransitionEdge::In);
    return removed;
}

} // namespace bemotion
