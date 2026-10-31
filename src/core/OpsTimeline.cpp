// Operations on clips in time: trims, rolls, slips, speed, markers, compounds (core/OpsInternal.h).

#include "core/OpsInternal.h"

#include "core/Arrange.h"
#include "core/AudioDsp.h"
#include "core/Data.h"
#include "core/Editing.h"
#include "core/Expressions.h"
#include "core/Geometry.h"
#include "core/Serialization.h"
#include "core/Suggest.h"
#include "core/Svg.h"
#include "core/Transitions.h"
#include "core/Validate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace bemotion::opsimpl {

ClipEdge Applier::edgeOf(const json& op) {
    const std::string e = text(field(op, "edge"), "edge");
    if (e != "in" && e != "out") fail("\"edge\" must be \"in\" or \"out\"");
    return e == "in" ? ClipEdge::In : ClipEdge::Out;
}

// An edge to `time`; with "ripple": true the later clips on the track follow (Premiere's ripple trim).
json Applier::layerTrim(const json& op) {
    Layer& l = layer(op);
    const uint64_t id = l.id;
    const ClipEdge edge = edgeOf(op);
    const double t = c_.snapToFrame(time(op));
    c_.duration = std::max(c_.duration, t + 1.0); // room to grow (fitDuration after the batch)
    if (has(op, "ripple") && boolean(op["ripple"], "ripple"))
        rippleTrim(c_, id, edge, t);
    else if (edge == ClipEdge::In)
        c_.setInPoint(id, t);
    else
        c_.setOutPoint(id, t);
    const Layer& now = *c_.findLayer(id);
    return {{"in", now.inPoint}, {"out", now.outPoint}};
}

json Applier::layerRoll(const json& op) {
    const Layer& l = layer(op);
    const uint64_t next = clipAfterCut(c_, l.id);
    if (!next)
        fail("\"" + l.name +
             "\" has no clip right after it on its track (a rolling edit moves the cut between two clips)");
    rollEdit(c_, l.id, c_.snapToFrame(time(op)));
    return {{"cut", c_.findLayer(l.id)->outPoint}, {"next", next}};
}

json Applier::layerSlip(const json& op) {
    const Layer& l = layer(op);
    const double delta = number(field(op, "delta"), "delta");
    return {{"delta", slipClip(c_, l.id, delta)}};
}

json Applier::layerRippleDelete(const json& op) {
    const json& list = field(op, "layers");
    if (!list.is_array() || list.empty()) fail("\"layers\" must be a list of layers");
    std::vector<uint64_t> ids;
    for (const json& ref : list)
        ids.push_back(layer(json{{"layer", ref}}).id);
    return {{"removed", rippleDelete(c_, ids)}};
}

// The empty space at `time` on the track of `layer` (any clip of it) closes up.
json Applier::trackCloseGap(const json& op) {
    const Layer& l = layer(op);
    const double t = time(op);
    if (!closeGap(c_, l.track, t))
        fail("there is no gap at " + std::to_string(t) + " s on the track of \"" + l.name + "\" with a clip after it");
    return json::object();
}

Layer& Applier::footage(const json& op) {
    Layer& l = layer(op);
    if (!hasTimeline(c_, l)) fail("\"" + l.name + "\" is not a video or audio clip (" + typeName(l) + ")");
    return l;
}

// "speed": percent (100 = as recorded), "reverse": backwards, "ripple": the later clips follow.
json Applier::layerSpeed(const json& op) {
    Layer& l = footage(op);
    const double speed = has(op, "speed") ? number(op["speed"], "speed") : l.media.speed * 100.0;
    if (speed < 1.0 || speed > 10000.0) fail("\"speed\" is a percent from 1 to 10000");
    const bool reverse = has(op, "reverse") ? boolean(op["reverse"], "reverse") : l.media.reversed;
    const bool ripple = has(op, "ripple") && boolean(op["ripple"], "ripple");
    c_.duration += kMaxOverhang; // a slower clip may grow past the end (fitDuration after the batch)
    setClipSpeed(c_, l.id, speed / 100.0, reverse, ripple);
    c_.duration -= kMaxOverhang;
    const Layer& now = *c_.findLayer(l.id);
    return {{"in", now.inPoint}, {"out", now.outPoint}};
}

json Applier::layerTimeRemap(const json& op) {
    Layer& l = footage(op);
    setTimeRemap(c_, l.id, boolean(field(op, "enabled"), "enabled"));
    return json::object();
}

json Applier::layerFreeze(const json& op) {
    Layer& l = footage(op);
    freezeFrame(c_, l.id, time(op));
    return {{"source_time", round3(l.media.remap.base)}};
}

json Applier::layerSeparateAudio(const json& op) {
    Layer& l = layer(op);
    const std::string name = l.name;
    if (l.type != LayerType::Media || !l.media.hasVideo || !l.media.hasAudio)
        fail("layer \"" + name + "\" is not a video with sound (nothing to separate)");
    const uint64_t audio = separateAudio(c_, l.id);
    return {{"layer", audio}, {"name", name + " (audio)"}};
}

json Applier::svgAdd(const json& op) {
    if (!has(op, "svg")) fail("missing field \"svg\" (the SVG file's text)");
    std::string error;
    const uint64_t id =
        addSvg(c_, text(op["svg"], "svg"), has(op, "name") ? text(op["name"], "name") : "SVG", time(op), &error);
    if (!id) fail(error);
    return {{"layer", id}, {"name", c_.findLayer(id)->name}};
}

json Applier::layerLinkAudio(const json& op) {
    Layer& v = layer(op);
    const std::string name = v.name;
    if (v.type != LayerType::Media || !v.media.hasVideo || v.media.hasAudio)
        fail("layer \"" + name + "\" is not a video without sound (nothing to link)");
    uint64_t audio = 0;
    if (has(op, "audio"))
        audio = layer(json{{"layer", op["audio"]}}).id;
    else
        audio = audioToLink(c_, v.id);
    if (!audio) fail("no sound-only clip of the same media to link with \"" + name + "\"");
    bool inSync = true;
    if (!linkAudio(c_, v.id, audio, &inSync)) fail("\"audio\" must be a sound-only clip of the same media");
    return {{"in_sync", inSync}};
}

int Applier::markerIndex(const json& op) {
    if (has(op, "index")) {
        const double i = number(op["index"], "index");
        if (i < 0 || i >= static_cast<double>(c_.markers.size()) || i != std::floor(i))
            fail("there are " + std::to_string(c_.markers.size()) + " marker(s): index must be 0.." +
                 std::to_string(static_cast<int>(c_.markers.size()) - 1));
        return static_cast<int>(i);
    }
    const double t = time(op);
    const int i = markerAt(c_, t);
    if (i < 0) fail("there is no marker at " + std::to_string(t) + " s (give \"index\" or the marker's \"time\")");
    return i;
}

int Applier::markerColorOf(const json& op) {
    if (!has(op, "color")) return -1;
    const double v = number(op["color"], "color");
    if (v < 0 || v >= kLabelColorCount || v != std::floor(v)) fail("\"color\" is a label color, 0 to 7");
    return static_cast<int>(v);
}

json Applier::markerAdd(const json& op) {
    const int i =
        addMarker(c_, time(op), has(op, "name") ? text(op["name"], "name") : std::string(), markerColorOf(op));
    return {{"index", i}, {"time", c_.markers[i].time}};
}

json Applier::markerSet(const json& op) {
    int i = markerIndex(op);
    if (has(op, "name")) c_.markers[i].name = text(op["name"], "name");
    if (const int color = markerColorOf(op); color >= 0) c_.markers[i].color = color;
    if (has(op, "to")) i = moveMarker(c_, i, time(op, "to"));
    return {{"index", i}, {"time", c_.markers[i].time}};
}

json Applier::markerRemove(const json& op) {
    c_.markers.erase(c_.markers.begin() + markerIndex(op));
    return json::object();
}

json Applier::group(const json& op) {
    const json& list = field(op, "layers");
    if (!list.is_array() || list.empty()) fail("\"layers\" must be a list of layers");
    std::vector<uint64_t> ids;
    uint64_t parent = UINT64_MAX;
    for (const json& ref : list) {
        const Layer& l = layer(json{{"layer", ref}});
        if (parent != UINT64_MAX && l.parent != parent)
            fail("the layers to group must be inside the same compound (or none)");
        parent = l.parent;
        ids.push_back(l.id);
    }
    const uint64_t compound = c_.createCompound(ids, time(op));
    if (!compound) fail("could not group these layers");
    if (has(op, "name")) {
        Layer& g = *c_.findLayer(compound);
        g.name.clear();
        g.name = c_.uniqueLayerName(text(op["name"], "name"));
    }
    return {{"layer", compound}};
}

json Applier::ungroup(const json& op) {
    const Layer& l = layer(op);
    if (l.type != LayerType::Compound) fail("\"" + l.name + "\" is not a compound");
    c_.releaseCompound(l.id, time(op));
    return json::object();
}

} // namespace bemotion::opsimpl
