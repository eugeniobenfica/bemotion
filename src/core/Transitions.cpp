#include "core/Transitions.h"

#include "core/Language.h"
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
    {{"fade", N_("Fade"), N_("Fades from / to transparent"), N_("Fade")},
     R"([{"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"fade_up", N_("Fade Up"), N_("Fades in while rising a little, fades out while lifting"), N_("Fade")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,40]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,-40]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_left", N_("Slide Left"), N_("Comes in from the left, leaves to the left"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[-320,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[-320,0]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_right", N_("Slide Right"), N_("Comes in from the right, leaves to the right"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[320,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[320,0]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_up", N_("Slide Up"), N_("Rises in from below, leaves upwards"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,200]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,-200]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"slide_down", N_("Slide Down"), N_("Drops in from above, leaves downwards"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,-200]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,200]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"zoom", N_("Zoom"), N_("Grows in from smaller, grows out while fading"), N_("Scale")},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":0.7},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":1.3}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"pop", N_("Pop"), N_("Springs in from nothing, shrinks away"), N_("Scale")},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":0},"ease":"back_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":0}}]}])"},
    {{"stretch", N_("Stretch"), N_("Opens out sideways from a line, closes back into one"), N_("Scale")},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":[0,1]},"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":[0,1]}}]}])"},
    {{"swing", N_("Swing"), N_("Swings in from a tilt, swings away"), N_("Rotate")},
     R"([{"property":"rotation","keys":[{"t":0,"v":{"offset":-25},"ease":"back_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.5,"v":"rest"}]}])",
     R"([{"property":"rotation","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":25}}]},
         {"property":"opacity","keys":[{"t":0.5,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"roll", N_("Roll"), N_("Rolls in from the left, rolls away to the right"), N_("Rotate")},
     R"([{"property":"rotation","keys":[{"t":0,"v":{"offset":-360},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"position","keys":[{"t":0,"v":{"offset":[-480,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}])",
     R"([{"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":360}}]},
         {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[480,0]}}]},
         {"property":"opacity","keys":[{"t":0.6,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"spin", N_("Spin"), N_("Turns in while growing, turns away while shrinking"), N_("Rotate")},
     R"([{"property":"rotation","keys":[{"t":0,"v":{"offset":-90},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"scale","keys":[{"t":0,"v":{"mul":0.4},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.6,"v":"rest"}]}])",
     R"([{"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":90}}]},
         {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":0.4}}]},
         {"property":"opacity","keys":[{"t":0.4,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"fade_down", N_("Fade Down"), N_("Fades in while settling down, fades out while sinking"), N_("Fade")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,-40]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,40]}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"whip_left", N_("Whip Left"), N_("Shoots in from far left, shoots away to the left"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[-1400,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[-1400,0]}}]}])"},
    {{"whip_right", N_("Whip Right"), N_("Shoots in from far right, shoots away to the right"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[1400,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[1400,0]}}]}])"},
    {{"drop", N_("Drop"), N_("Falls in and bounces, falls away"), N_("Slide")},
     R"([{"property":"position","keys":[{"t":0,"v":{"offset":[0,-300]},"ease":"bounce"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.3,"v":"rest"}]}])",
     R"([{"property":"position","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":[0,300]}}]},
         {"property":"opacity","keys":[{"t":0.7,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"zoom_out", N_("Zoom Out"), N_("Shrinks in from bigger, shrinks away"), N_("Scale")},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":1.5},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":0.6}}]},
         {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"elastic", N_("Elastic"), N_("Snaps in with a wobble, snaps away"), N_("Scale")},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":0},"ease":"elastic"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":0}}]}])"},
    {{"squash", N_("Squash"), N_("Opens up from a line, flattens away"), N_("Scale")},
     R"([{"property":"scale","keys":[{"t":0,"v":{"mul":[1,0]},"ease":"back_out"},{"t":1,"v":"rest"}]}])",
     R"([{"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":[1,0]}}]}])"},
    {{"turn", N_("Turn"), N_("Turns in like a door, turns away (makes the layer 3D)"), "3D"},
     R"([{"property":"rotation_y","keys":[{"t":0,"v":{"offset":-90},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}])",
     R"([{"property":"rotation_y","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":90}}]},
         {"property":"opacity","keys":[{"t":0.6,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"tilt", N_("Tilt"), N_("Swings up from lying flat, falls back (makes the layer 3D)"), "3D"},
     R"([{"property":"rotation_x","keys":[{"t":0,"v":{"offset":80},"ease":"back_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}])",
     R"([{"property":"rotation_x","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":80}}]},
         {"property":"opacity","keys":[{"t":0.6,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
    {{"fly", N_("Fly"), N_("Flies in from far away, flies past the camera (makes the layer 3D)"), "3D"},
     R"([{"property":"position_z","keys":[{"t":0,"v":{"offset":3000},"ease":"ease_out"},{"t":1,"v":"rest"}]},
         {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.5,"v":"rest"}]}])",
     R"([{"property":"position_z","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":-2400}}]},
         {"property":"opacity","keys":[{"t":0.7,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}])"},
};

bool isThreeD(const char* category) { return std::strcmp(category, "3D") == 0; }

const Definition* definitionOf(const std::string& id) {
    for (const Definition& d : kDefinitions)
        if (id == d.info.id) return &d;
    return nullptr;
}

// On a crossed cut the outgoing clip plays its Out animation too (a push, a zoom through), but
// not a fade: it stays whole under the incoming clip fading in, or the picture would dim.
bool outgoingAnimates(const Definition& d) { return std::strcmp(d.info.category, "Fade") != 0; }

AnimationPreset presetOf(const char* tracks, double duration) {
    AnimationPreset p;
    parsePreset(std::string(R"({"name":"transition","tracks":)") + tracks + "}", p);
    p.duration = duration;
    return p;
}

void removePresetKeys(const AnimationPreset& preset, Layer& layer, double time, bool insideClip = true) {
    const double start = insideClip ? std::clamp(time, layer.inPoint, std::max(layer.inPoint, layer.outPoint - preset.duration)) : time;
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
    static const std::vector<const char*> list = {N_("Fade"), N_("Slide"), N_("Scale"), N_("Rotate"), "3D"};
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
    // A crossed cut's keys span the whole transition, half before the cut and half after.
    auto removeCrossed = [&](ClipTransition& transition, const char* tracks, double cut) {
        removePresetKeys(presetOf(tracks, transition.duration * 2.0), layer, cut - transition.duration, false);
        transition = {};
        removed = true;
    };
    if (edge != TransitionEdge::Out) {
        const Definition* definition = definitionOf(layer.transitionIn.id);
        if (definition && layer.transitionIn.overlap) removeCrossed(layer.transitionIn, definition->in, layer.inPoint);
        else if (definition) remove(layer.transitionIn, definition->in, layer.inPoint);
        else if (layer.transitionIn.active()) layer.transitionIn = {}, removed = true;
    }
    if (edge != TransitionEdge::In) {
        const Definition* definition = definitionOf(layer.transitionOut.id);
        if (definition && layer.transitionOut.overlap) {
            if (outgoingAnimates(*definition)) removeCrossed(layer.transitionOut, definition->out, layer.outPoint);
            else layer.transitionOut = {}, removed = true;
        } else if (definition) remove(layer.transitionOut, definition->out, layer.outPoint - layer.transitionOut.duration);
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
    const Definition* def = definitionOf(id);
    if (!def) return false;
    if (isThreeD(def->info.category) && (first.type == LayerType::Background || first.parent || second->type == LayerType::Background || second->parent))
        return false; // only top-level layers turn in 3D
    // Half on each side of the cut, at most half of either clip.
    const double half = std::clamp(duration * 0.5, 0.05, std::max(0.05, std::min(first.outPoint - first.inPoint, second->outPoint - second->inPoint) * 0.5));
    if (first.transitionOut.active()) removeTransition(comp, first, TransitionEdge::Out);
    if (second->transitionIn.active()) removeTransition(comp, *second, TransitionEdge::In);
    // The clips overlap for the whole transition (a cross dissolve, not a dip between two
    // fades): the incoming one animates in above the outgoing one, which stays (a fade) or
    // animates out at the same time (a push, a zoom through).
    if (isThreeD(def->info.category)) first.threeD = second->threeD = true;
    const double cut = first.outPoint;
    applyPreset(presetOf(def->in, half * 2.0), *second, cut - half, false);
    if (outgoingAnimates(*def)) applyPreset(presetOf(def->out, half * 2.0), first, cut - half, false);
    first.transitionOut = {id, static_cast<float>(half), true, true};
    second->transitionIn = {id, static_cast<float>(half), true, true};
    // Same track: the incoming clip goes right above the outgoing one in the stack.
    auto& layers = comp.layers;
    const auto a = std::find_if(layers.begin(), layers.end(), [&](const Layer& l) { return &l == &first; });
    const auto b = std::find_if(layers.begin(), layers.end(), [&](const Layer& l) { return &l == second; });
    if (b > a) std::rotate(a, b, b + 1); // (references to them change: callers look them up again)
    return true;
}

bool resizeTransition(Composition& comp, Layer& layer, TransitionEdge edge, double duration) {
    Layer* partner = transitionPartner(comp, layer, edge);
    // A crossed cut is made again: both clips' keys span it together.
    if (partner && (edge == TransitionEdge::In ? layer.transitionIn : layer.transitionOut).overlap) {
        Layer* first = edge == TransitionEdge::Out ? &layer : partner;
        const std::string id = first->transitionOut.id;
        const uint64_t firstId = first->id;
        removeTransition(comp, *first, TransitionEdge::Out);
        Layer* again = comp.findLayer(firstId);
        return again && applyCutTransition(comp, *again, id, duration * 2.0);
    }
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
