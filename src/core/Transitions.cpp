#include "core/Transitions.h"

#include "core/Presets.h"

#include <algorithm>
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
};

AnimationPreset presetOf(const char* tracks, double duration) {
    AnimationPreset p;
    parsePreset(std::string(R"({"name":"transition","tracks":)") + tracks + "}", p);
    p.duration = duration;
    return p;
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
    static const std::vector<const char*> list = {"Fade", "Slide", "Scale", "Rotate"};
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

bool applyTransition(Layer& layer, const std::string& id, TransitionEdge edge, double duration) {
    if (layer.type == LayerType::Camera || (layer.type == LayerType::Media && layer.media.hasAudio && !layer.media.hasVideo)) return false;
    const Definition* def = nullptr;
    for (const Definition& d : kDefinitions)
        if (id == d.info.id) def = &d;
    if (!def) return false;
    const double length = layer.outPoint - layer.inPoint;
    duration = std::clamp(duration, 0.05, edge == TransitionEdge::Both ? length * 0.5 : length);
    if (edge != TransitionEdge::Out) applyPreset(presetOf(def->in, duration), layer, layer.inPoint);
    if (edge != TransitionEdge::In) applyPreset(presetOf(def->out, duration), layer, layer.outPoint - duration);
    return true;
}

} // namespace bemotion
