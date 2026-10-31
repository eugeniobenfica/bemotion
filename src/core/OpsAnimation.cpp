// Operations on how layers look and move: masks, paint, pins, keys, expressions, effects, audio,
// transitions and arranging (core/OpsInternal.h).

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

Mask& Applier::mask(Layer& l, const json& op) {
    const double i = number(field(op, "index"), "index");
    if (i < 0 || i >= static_cast<double>(l.masks.size()) || i != std::floor(i))
        fail("layer \"" + l.name + "\" has " + std::to_string(l.masks.size()) + " mask(s): index must be 0.." +
             std::to_string(static_cast<int>(l.masks.size()) - 1));
    return l.masks[static_cast<size_t>(i)];
}

void Applier::setMask(Mask& m, const json& op) {
    if (has(op, "name")) {
        m.name = text(op["name"], "name");
        if (m.name.empty()) fail("\"name\" cannot be empty");
    }
    if (has(op, "mode")) m.mode = static_cast<MaskMode>(idOf(kMaskModeIds, op, "mode"));
    if (has(op, "inverted")) m.inverted = boolean(op["inverted"], "inverted");
    if (has(op, "enabled")) m.enabled = boolean(op["enabled"], "enabled");
    if (has(op, "path")) {
        // With "time" (or on a mask already animated): a Mask Path keyframe there.
        if (has(op, "time") || !m.pathKeys.empty())
            m.setPathKey(time(op), pathOf(op["path"]));
        else
            m.path = pathOf(op["path"]);
    }
    if (has(op, "animated") && !boolean(op["animated"], "animated")) { // back to one outline: the one at "time"
        m.path = m.pathAt(time(op));
        m.pathKeys.clear();
    }
}

// A mask: "shape" rectangle / ellipse over the layer's bounds (or "box": [x0, y0, x1, y1] in
// the layer's space), or a "path"; the animatable values are properties ("mask0.feather").
json Applier::maskAdd(const json& op) {
    Layer& l = layer(op);
    if (l.type == LayerType::Camera || (l.type == LayerType::Media && !l.media.hasVideo))
        fail("\"" + l.name + "\" has no picture to mask");
    Mask m;
    m.name = "Mask " + std::to_string(l.masks.size() + 1);
    if (has(op, "path")) {
        m.path = pathOf(op["path"]);
    } else {
        Vec2 mn{-200, -200}, mx{200, 200};
        if (has(op, "box")) {
            const json& b = op["box"];
            if (!b.is_array() || b.size() != 4) fail("\"box\" is [x0, y0, x1, y1] in the layer's own space");
            mn = {static_cast<float>(number(b[0], "box")), static_cast<float>(number(b[1], "box"))};
            mx = {static_cast<float>(number(b[2], "box")), static_cast<float>(number(b[3], "box"))};
        } else if (!env_.layerBounds || !env_.layerBounds(c_, l, time(op), mn, mx)) {
            // Without the renderer: what the core knows of the layer's size.
            const Vec2 size = l.type == LayerType::Media    ? l.media.size
                              : l.type == LayerType::Plugin ? l.plugin.size.valueAt(time(op))
                              : l.type == LayerType::Shape && l.shape.kind != ShapeKind::Path
                                  ? l.shape.size.valueAt(time(op))
                                  : Vec2{400, 400};
            mn = size * -0.5f, mx = size * 0.5f;
            if (l.type == LayerType::Shape && l.shape.kind == ShapeKind::Path) bezierBounds(l.shape.path, mn, mx);
        }
        const std::string shape = has(op, "shape") ? text(op["shape"], "shape") : "rectangle";
        if (shape != "rectangle" && shape != "ellipse")
            fail("\"shape\" must be \"rectangle\" or \"ellipse\" (or give a \"path\")");
        m.path = shape == "ellipse" ? ellipsePath(mn, mx) : rectanglePath(mn, mx);
    }
    setMask(m, op);
    l.masks.push_back(std::move(m));
    const int index = static_cast<int>(l.masks.size()) - 1;
    if (has(op, "feather"))
        propertySet({{"layer", l.id}, {"prop", "mask" + std::to_string(index) + ".feather"}, {"value", op["feather"]}});
    return {{"index", index}};
}

json Applier::maskSet(const json& op) {
    Layer& l = layer(op);
    setMask(mask(l, op), op);
    return json::object();
}

// Paint, Puppet and Warp: on layers with a picture.
Layer& Applier::pictureLayer(const json& op, const char* what) {
    Layer& l = layer(op);
    if (l.type == LayerType::Camera || (l.type == LayerType::Media && !l.media.hasVideo))
        fail("\"" + l.name + "\" has no picture for " + what);
    return l;
}

Vec2 Applier::pointOf(const json& v, const char* name) {
    if (!v.is_array() || v.size() != 2) fail(std::string("\"") + name + "\" is [x, y] in the layer's own space");
    return {static_cast<float>(number(v[0], name)), static_cast<float>(number(v[1], name))};
}

json Applier::paintStroke(const json& op) {
    Layer& l = pictureLayer(op, "paint");
    const json& pts = field(op, "points");
    if (!pts.is_array() || pts.empty()) fail("\"points\" is a list of [x, y] in the layer's own space");
    if (pts.size() > 100000) fail("\"points\" has too many points (100000 at most)");
    PaintStroke s;
    for (const json& p : pts)
        s.points.push_back(pointOf(p, "points"));
    if (has(op, "color")) s.color = colorOf(op["color"], "color");
    s.width = static_cast<float>(number(op, "width", s.width));
    if (s.width < 0.5 || s.width > 2000) fail("\"width\" must be 0.5 to 2000 px");
    if (has(op, "erase")) s.erase = boolean(op["erase"], "erase");
    s.softness = static_cast<float>(number(op, "softness", 0.0));
    if (s.softness < 0 || s.softness > 500) fail("\"softness\" must be 0 to 500 px");
    l.paint.push_back(std::move(s));
    return {{"index", l.paint.size() - 1}};
}

json Applier::paintClear(const json& op) {
    Layer& l = layer(op);
    l.paint.clear();
    l.paintReveal = AnimFloat{100.0f};
    return json::object();
}

json Applier::pinAdd(const json& op) {
    Layer& l = pictureLayer(op, "puppet pins");
    if (l.pins.size() >= 64) fail("a layer takes 64 puppet pins at most");
    PuppetPin pin;
    pin.rest = pointOf(field(op, "point"), "point");
    pin.position = AnimVec2{pin.rest};
    if (has(op, "starch")) pin.starch = boolean(op["starch"], "starch");
    l.pins.push_back(std::move(pin));
    return {{"index", l.pins.size() - 1}, {"property", "pin" + std::to_string(l.pins.size() - 1)}};
}

json Applier::pinRemove(const json& op) {
    Layer& l = layer(op);
    const double i = number(field(op, "index"), "index");
    if (i < 0 || i >= static_cast<double>(l.pins.size()) || i != std::floor(i))
        fail("layer \"" + l.name + "\" has " + std::to_string(l.pins.size()) + " pin(s): index must be 0.." +
             std::to_string(static_cast<int>(l.pins.size()) - 1));
    l.pins.erase(l.pins.begin() + static_cast<std::ptrdiff_t>(i));
    return json::object();
}

json Applier::maskDuplicate(const json& op) {
    Layer& l = layer(op);
    Mask copy = mask(l, op);
    const auto at = &mask(l, op) - l.masks.data() + 1;
    copy.name += " " + std::string("copy");
    l.masks.insert(l.masks.begin() + at, std::move(copy));
    return {{"index", at}};
}

json Applier::maskRemove(const json& op) {
    Layer& l = layer(op);
    Mask& m = mask(l, op);
    l.masks.erase(l.masks.begin() + (&m - l.masks.data()));
    return json::object();
}

json Applier::propertySet(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    setPropertyValue(p, time(op), valueOf(p, field(op, "value")));
    return json::object();
}

json Applier::keyAdd(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    const double t = time(op, "time", -1.0);
    if (!has(op, "time")) fail("missing field \"time\"");
    const std::vector<float> v = has(op, "value") ? valueOf(p, op["value"]) : propertyValue(p, t);
    const Easing e = has(op, "easing") ? easingOf(op["easing"]) : Easing::EaseInOut;
    switch (p.kind) {
    case PropertyKind::Float: addKey<float>(p, t, v, e); break;
    case PropertyKind::Vec2: addKey<Vec2>(p, t, v, e); break;
    case PropertyKind::Color: addKey<Color>(p, t, v, e); break;
    }
    return json::object();
}

// A formula for the property, every frame (core/Expressions.h); "" removes it.
json Applier::expressionSet(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    const std::string e = text(field(op, "expression"), "expression");
    if (const std::string problem = e.empty() ? std::string() : checkExpression(e); !problem.empty())
        fail("the expression does not read: " + problem);
    p.anim->expression = e;
    return json::object();
}

// The expression turned into keyframes, one a frame (After Effects' Convert Expression to
// Keyframes): `from` / `to` default to the clip, `step` (frames, default 1).
json Applier::expressionBake(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    if (!p.anim->hasExpression()) fail("\"" + std::string(p.id) + "\" of \"" + l.name + "\" has no expression");
    if (const std::string problem = expressionError(*p.anim); !problem.empty())
        fail("the expression does not work: " + problem);
    const double from = time(op, "from", l.inPoint), to = has(op, "to") ? time(op, "to") : l.outPoint;
    if (to <= from) fail("\"to\" must be after \"from\"");
    const int step = static_cast<int>(number(op, "step", 1.0));
    if (step < 1 || step > 1000) fail("\"step\" is a number of frames, from 1 to 1000");
    const double dt = step / std::max(1.0, c_.fps);
    std::vector<std::pair<double, std::vector<float>>> samples;
    for (int i = 0;; ++i) {
        const double at = std::min(to, from + i * dt);
        std::vector<float> v;
        if (const std::string problem = evaluateExpressionFor(p.anim->expression, l, p, at, v); !problem.empty())
            fail("the expression does not work at " + std::to_string(at) + " s: " + problem);
        samples.emplace_back(at, std::move(v));
        if (at >= to || samples.size() > 100000) break;
    }
    p.anim->expression.clear();
    p.anim->clearKeys(from);
    for (const auto& [at, v] : samples)
        switch (p.kind) {
        case PropertyKind::Float: addKey<float>(p, at, v, Easing::Linear); break;
        case PropertyKind::Vec2: addKey<Vec2>(p, at, v, Easing::Linear); break;
        case PropertyKind::Color: addKey<Color>(p, at, v, Easing::Linear); break;
        }
    return {{"keys", samples.size()}};
}

json Applier::keyRemove(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    p.anim->removeKey(keyAt(p, time(op)));
    return json::object();
}

json Applier::keyClear(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    const double t = time(op);
    p.anim->clearKeys(t); // keeps the value it had at `t`
    if (has(op, "value")) setPropertyValue(p, t, valueOf(p, op["value"]));
    return json::object();
}

json Applier::keyEasing(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    const Easing e = easingOf(field(op, "easing"));
    if (has(op, "all") && boolean(op["all"], "all")) {
        for (int i = 0; i < p.anim->keyCount(); ++i)
            p.anim->setKeyEasing(i, e);
    } else {
        p.anim->setKeyEasing(keyAt(p, time(op)), e);
    }
    return json::object();
}

json Applier::effectAdd(const json& op) {
    Layer& l = layer(op);
    if (l.type == LayerType::Camera) fail("cameras have no effects");
    const std::string id = text(field(op, "effect"), "effect");
    const EffectType type = effectFromId(id);
    if (type == EffectType::Count) {
        std::vector<std::string> ids;
        for (int i = 0; i < static_cast<int>(EffectType::Count); ++i)
            ids.push_back(effectId(static_cast<EffectType>(i)));
        fail("unknown effect \"" + id + "\"" + suggest(id, ids));
    }
    l.effects.push_back(makeEffect(type));
    const int index = static_cast<int>(l.effects.size()) - 1;
    json props = json::array();
    const std::string prefix = "fx" + std::to_string(index) + ".";
    for (const PropertyRef& p : l.properties())
        if (p.effectIndex == index) props.push_back(std::string(p.id).substr(prefix.size()));
    if (has(op, "params")) {
        if (!op["params"].is_object()) fail("\"params\" must be an object like {\"size\": 20}");
        for (const auto& [slot, value] : op["params"].items())
            propertySet({{"layer", l.id}, {"prop", prefix + slot}, {"value", value}});
    }
    return {{"index", index}, {"params", props}};
}

json Applier::effectRemove(const json& op) {
    Layer& l = layer(op);
    Effect& e = effect(l, op);
    l.effects.erase(l.effects.begin() + (&e - l.effects.data()));
    return json::object();
}

json Applier::effectSet(const json& op) {
    Layer& l = layer(op);
    Effect& e = effect(l, op);
    if (has(op, "enabled")) e.enabled = boolean(op["enabled"], "enabled");
    if (has(op, "samples")) {
        const double s = number(op["samples"], "samples");
        if (s < 2 || s > 64) fail("\"samples\" must be 2 to 64");
        e.samples = static_cast<int>(s);
    }
    if (has(op, "file")) {
        if (e.type != EffectType::Lut) fail("\"file\" is for lut effects (a .cube file)");
        e.file = text(op["file"], "file");
        e.relative.clear();
    }
    if (has(op, "curves")) {
        if (e.type != EffectType::Curves) fail("\"curves\" is for curves effects");
        const json& curves = op["curves"];
        if (!curves.is_object()) fail("\"curves\" must be an object like {\"rgb\": [[0, 0], [0.5, 0.6], [1, 1]]}");
        for (const auto& [name, points] : curves.items()) {
            int channel = -1;
            for (int c = 0; c < 4; ++c)
                if (name == curveId(c)) channel = c;
            if (channel < 0)
                fail("unknown curve \"" + name + "\" (rgb, red, green or blue)" +
                     suggest(name, {"rgb", "red", "green", "blue"}));
            std::vector<Vec2> pts;
            if (!points.is_null()) {
                if (!points.is_array())
                    fail("curve \"" + name + "\" must be a list of [x, y] points (0 to 1), or null to reset it");
                for (const json& pt : points) {
                    if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number())
                        fail("curve \"" + name + "\": each point is [x, y] (0 to 1)");
                    const float x = pt[0].get<float>(), y = pt[1].get<float>();
                    if (x < 0 || x > 1 || y < 0 || y > 1) fail("curve \"" + name + "\": points are 0 to 1");
                    pts.push_back({x, y});
                }
                if (pts.size() > static_cast<size_t>(kMaxCurvePoints))
                    fail("curve \"" + name + "\": at most " + std::to_string(kMaxCurvePoints) + " points");
            }
            cleanCurve(pts);
            e.curves[channel] = std::move(pts);
        }
    }
    return json::object();
}

// Sound-only and sound-carrying clips take audio effects.
Layer& Applier::audioLayer(const json& op) {
    Layer& l = layer(op);
    if (l.type != LayerType::Media || !l.media.hasAudio)
        fail("layer \"" + l.name + "\" has no sound: audio effects are for video and audio clips with sound");
    return l;
}

AudioEffect& Applier::audioEffect(Layer& l, const json& op) {
    const double i = number(field(op, "index"), "index");
    if (i < 0 || i >= static_cast<double>(l.audioEffects.size()) || i != std::floor(i))
        fail("layer \"" + l.name + "\" has " + std::to_string(l.audioEffects.size()) +
             " audio effect(s): index must be 0.." + std::to_string(static_cast<int>(l.audioEffects.size()) - 1));
    return l.audioEffects[static_cast<size_t>(i)];
}

// An audio effect's values by name ("low", "amount"...), each inside its range.
void Applier::audioValues(AudioEffect& e, const json& op) {
    if (!has(op, "values")) return;
    if (!op["values"].is_object()) fail("\"values\" must be an object like {\"low\": 3}");
    const auto& params = audioEffectParams(e.type);
    for (const auto& [name, value] : op["values"].items()) {
        const auto it =
            std::find_if(params.begin(), params.end(), [&](const AudioEffectParam& p) { return name == p.id; });
        if (it == params.end()) {
            std::vector<std::string> ids;
            for (const AudioEffectParam& p : params)
                ids.push_back(p.id);
            std::string list;
            for (const std::string& id : ids)
                list += (list.empty() ? "" : ", ") + id;
            fail(std::string(audioEffectId(e.type)) + " has no value \"" + name + "\" (it has " + list + ")" +
                 suggest(name, ids));
        }
        const double v = number(value, name.c_str());
        if (v < it->min || v > it->max) {
            char range[96];
            std::snprintf(range, sizeof range, "\"%s\" must be %g to %g", it->id, it->min, it->max);
            fail(range);
        }
        audioEffectValue(e, it->index) = static_cast<float>(v);
    }
}

// Noise Reduction's sample of only the noise: [from, to] seconds of the clip's source, or null.
void Applier::audioSample(AudioEffect& e, const json& op) {
    if (!op.contains("sample")) return;
    if (e.type != AudioEffectType::NoiseReduction) fail("only noise_reduction takes a \"sample\"");
    const json& s = op["sample"];
    if (s.is_null()) {
        e.sampleFrom = e.sampleTo = -1.0;
        return;
    }
    if (!s.is_array() || s.size() != 2 || !s[0].is_number() || !s[1].is_number() || s[0].get<double>() < 0.0 ||
        s[1].get<double>() <= s[0].get<double>())
        fail("\"sample\" must be [from, to]: seconds of the clip's source with only the noise, from < to (null: none)");
    e.sampleFrom = s[0].get<double>();
    e.sampleTo = s[1].get<double>();
}

json Applier::audioEffectAdd(const json& op) {
    Layer& l = audioLayer(op);
    const std::string id = text(field(op, "effect"), "effect");
    const AudioEffectType type = audioEffectFromId(id);
    if (type == AudioEffectType::Count) {
        std::vector<std::string> ids;
        for (int i = 0; i < static_cast<int>(AudioEffectType::Count); ++i)
            ids.push_back(audioEffectId(static_cast<AudioEffectType>(i)));
        fail("unknown audio effect \"" + id + "\"" + suggest(id, ids));
    }
    AudioEffect e = makeAudioEffect(type);
    audioValues(e, op);
    audioSample(e, op);
    l.audioEffects.push_back(e);
    json values = json::object();
    for (const AudioEffectParam& p : audioEffectParams(type))
        values[p.id] = audioEffectValue(e, p.index);
    return {{"index", l.audioEffects.size() - 1}, {"values", values}};
}

json Applier::audioEffectRemove(const json& op) {
    Layer& l = audioLayer(op);
    AudioEffect& e = audioEffect(l, op);
    l.audioEffects.erase(l.audioEffects.begin() + (&e - l.audioEffects.data()));
    return json::object();
}

json Applier::audioEffectSet(const json& op) {
    Layer& l = audioLayer(op);
    AudioEffect& e = audioEffect(l, op);
    if (has(op, "enabled")) e.enabled = boolean(op["enabled"], "enabled");
    audioValues(e, op);
    audioSample(e, op);
    return json::object();
}

// Automatic levels for one or more clips: each gets an Auto Level effect at the same target
// (the one it has is set), so they all sound as loud.
json Applier::audioLevel(const json& op) {
    std::vector<uint64_t> ids;
    if (has(op, "layer"))
        ids.push_back(audioLayer(op).id);
    else
        for (uint64_t id : layerList(op))
            if (const Layer* l = c_.findLayer(id); l && l->type == LayerType::Media && l->media.hasAudio)
                ids.push_back(id);
    if (ids.empty()) fail("no clips with sound to level");
    AudioEffect level = makeAudioEffect(AudioEffectType::AutoLevel);
    audioValues(
        level, {{"values",
                 {{"target", number(op, "target", level.value)}, {"even_out", number(op, "even_out", level.value2)}}}});
    for (uint64_t id : ids) {
        Layer& l = *c_.findLayer(id);
        const auto it = std::find_if(l.audioEffects.begin(), l.audioEffects.end(),
                                     [](const AudioEffect& e) { return e.type == AudioEffectType::AutoLevel; });
        if (it != l.audioEffects.end())
            it->value = level.value, it->value2 = level.value2, it->enabled = true;
        else
            l.audioEffects.push_back(level);
    }
    return {{"layers", ids}, {"target", level.value}};
}

// Removes the quiet stretches of a clip with sound: below `threshold` dB (or learned from a
// `sample` of the silence: [from, to] composition seconds) for at least `min` seconds.
json Applier::audioRemoveSilence(const json& op) {
    Layer& l = audioLayer(op);
    if (l.media.reversed || l.media.timeRemap)
        fail("layer \"" + l.name + "\" plays reversed or with Time Remap: turn that off first");
    const MediaItem* item = c_.findMedia(l.media.mediaId);
    if (!item) fail("layer \"" + l.name + "\" has no media");
    if (!env_.mediaLoudness) fail("reading sound is not available here");
    std::vector<float> loud;
    double block = 0.0;
    std::string error;
    if (!env_.mediaLoudness(*item, loud, block, error))
        fail("could not read the sound of \"" + item->name + "\": " + error);
    const double minimum = number(op, "min", 0.5), padding = number(op, "padding", 0.1);
    if (minimum < 0.05 || minimum > 60) fail("\"min\" must be 0.05 to 60 seconds");
    if (padding < 0 || padding > 5) fail("\"padding\" must be 0 to 5 seconds");
    double threshold = number(op, "threshold", -40.0);
    if (has(op, "sample")) {
        const json& s = op["sample"];
        if (!s.is_array() || s.size() != 2 || !s[0].is_number() || !s[1].is_number())
            fail("\"sample\" must be [from, to]: composition seconds of a moment that is only silence or noise");
        const double speed = std::max(1e-3, l.media.speed);
        const double from = (s[0].get<double>() - l.media.start) * speed,
                     to = (s[1].get<double>() - l.media.start) * speed;
        threshold = dsp::thresholdFromSample(loud, block, from, to);
        if (threshold <= -99.0) fail("the sample is outside the clip's sound");
    }
    if (threshold < -100 || threshold > 0) fail("\"threshold\" must be -100 to 0 dB");
    const auto quiet = dsp::quietStretches(loud, block, static_cast<float>(threshold), minimum, padding);
    const double removed = removeQuietParts(c_, l.id, quiet, !has(op, "ripple") || boolean(op["ripple"], "ripple"));
    return {{"removed_seconds", removed}, {"threshold", threshold}};
}

// A "layers" list (ids or names); "all" or missing: every layer.
std::vector<uint64_t> Applier::layerList(const json& op) {
    std::vector<uint64_t> ids;
    if (!has(op, "layers") || (op["layers"].is_string() && op["layers"].get<std::string>() == "all")) {
        for (const Layer& l : c_.layers)
            ids.push_back(l.id);
        return ids;
    }
    if (!op["layers"].is_array()) fail("\"layers\" must be a list of layers (ids or names) or \"all\"");
    for (const json& ref : op["layers"])
        ids.push_back(layer(json{{"layer", ref}}).id);
    return ids;
}

json Applier::transitionAdd(const json& op) {
    Layer& l = layer(op);
    const std::string id = text(field(op, "transition"), "transition");
    if (!findTransition(id)) {
        std::vector<std::string> ids;
        for (const TransitionInfo& t : transitions())
            ids.push_back(t.id);
        fail("unknown transition \"" + id + "\"" + suggest(id, ids));
    }
    const std::string edgeName = has(op, "edge") ? text(op["edge"], "edge") : "in";
    if (edgeName == "cut") { // across the cut after this clip (with the next one on its track)
        const double duration = number(op, "duration", 1.0);
        if (duration <= 0) fail("\"duration\" must be positive");
        if (!nextOnTrack(c_, l))
            fail("layer \"" + l.name + "\" has no clip right after it on its track (\"cut\" needs one)");
        if (!applyCutTransition(c_, l, id, duration)) fail("these clips cannot take a transition");
        return json::object();
    }
    TransitionEdge edge = TransitionEdge::In;
    if (edgeName == "out")
        edge = TransitionEdge::Out;
    else if (edgeName == "both")
        edge = TransitionEdge::Both;
    else if (edgeName != "in")
        fail("\"edge\" must be \"in\", \"out\", \"both\" or \"cut\"");
    const double duration = number(op, "duration", 0.5);
    if (duration <= 0) fail("\"duration\" must be positive");
    if (!applyTransition(l, id, edge, duration))
        fail("layer \"" + l.name + "\" cannot take a transition (" + typeName(l) + ")");
    return json::object();
}

json Applier::arrangeDistribute(const json& op) {
    const std::string axis = has(op, "axis") ? text(op["axis"], "axis") : "x";
    if (axis != "x" && axis != "y") fail("\"axis\" must be \"x\" or \"y\"");
    std::string error;
    if (!distributeLayers(c_, layerList(op), axis == "y", time(op), &error)) fail(error);
    return json::object();
}

json Applier::arrangeStagger(const json& op) {
    const double step = number(op, "step", 0.1);
    std::string error;
    if (!staggerLayers(c_, layerList(op), step, &error)) fail(error);
    return json::object();
}

} // namespace bemotion::opsimpl
