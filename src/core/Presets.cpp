#include "core/Presets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bemotion {

using json = nlohmann::json;

namespace {

// --- built-in presets (same format as imported files) --------------------------

constexpr const char* kBuiltinPresets[] = {
    R"({"name":"Fade In","category":"Entrance","duration":0.6,"tracks":[
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Pop In","category":"Entrance","duration":0.6,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":{"mul":0},"ease":"back_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Slide Up","category":"Entrance","duration":0.6,"tracks":[
        {"property":"position","keys":[{"t":0,"v":{"offset":[0,160]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Slide In","category":"Entrance","duration":0.6,"tracks":[
        {"property":"position","keys":[{"t":0,"v":{"offset":[-240,0]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Drop In","category":"Entrance","duration":0.9,"tracks":[
        {"property":"position","keys":[{"t":0,"v":{"offset":[0,-220]},"ease":"bounce"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.3,"v":"rest"}]}]})",
    R"({"name":"Typewriter","category":"Entrance","applies_to":"text","duration":0.3,"per_glyph":0.05,"tracks":[
        {"property":"reveal","keys":[{"t":0,"v":0,"ease":"linear"},{"t":1,"v":100}]}]})",
    R"({"name":"Draw On","category":"Entrance","applies_to":"shape","duration":1.2,"enable_stroke":true,"tracks":[
        {"property":"trim_end","keys":[{"t":0,"v":0,"ease":"ease_in_out"},{"t":1,"v":100}]}]})",
    R"({"name":"Pulse","category":"Emphasis","duration":0.5,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":0.5,"v":{"mul":1.15},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Zoom In","category":"Entrance","duration":0.6,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":{"mul":1.6},"ease":"ease_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Rise","category":"Entrance","duration":0.7,"tracks":[
        {"property":"position","keys":[{"t":0,"v":{"offset":[0,90]},"ease":"back_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.5,"v":"rest"}]}]})",
    R"({"name":"Shake","category":"Emphasis","duration":0.5,"tracks":[
        {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":0.15,"v":{"offset":[-14,0]},"ease":"ease_in_out"},
            {"t":0.35,"v":{"offset":[14,0]},"ease":"ease_in_out"},{"t":0.55,"v":{"offset":[-10,0]},"ease":"ease_in_out"},
            {"t":0.75,"v":{"offset":[8,0]},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Heartbeat","category":"Emphasis","duration":0.8,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_out"},{"t":0.15,"v":{"mul":1.15},"ease":"ease_in_out"},
            {"t":0.35,"v":"rest","ease":"ease_out"},{"t":0.5,"v":{"mul":1.1},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Float","category":"Emphasis","duration":2.0,"tracks":[
        {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":0.5,"v":{"offset":[0,-24]},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Bounce","category":"Emphasis","duration":0.8,"tracks":[
        {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_out"},{"t":0.4,"v":{"offset":[0,-70]},"ease":"bounce"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Wobble","category":"Emphasis","duration":0.8,"tracks":[
        {"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":0.2,"v":{"offset":-10},"ease":"ease_in_out"},
            {"t":0.45,"v":{"offset":8},"ease":"ease_in_out"},{"t":0.7,"v":{"offset":-4},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Blink","category":"Emphasis","duration":0.6,"tracks":[
        {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"hold"},{"t":0.25,"v":0,"ease":"hold"},{"t":0.5,"v":"rest","ease":"hold"},
            {"t":0.75,"v":0,"ease":"hold"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Spin","category":"Emphasis","duration":1.2,"tracks":[
        {"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":1,"v":{"offset":360}}]}]})",
    R"({"name":"Grow In","category":"Entrance","duration":0.6,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":{"mul":0.6},"ease":"ease_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Swing In","category":"Entrance","duration":0.8,"tracks":[
        {"property":"rotation","keys":[{"t":0,"v":{"offset":-30},"ease":"back_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":0.4,"v":"rest"}]}]})",
    R"({"name":"Glide Down","category":"Entrance","duration":0.7,"tracks":[
        {"property":"position","keys":[{"t":0,"v":{"offset":[0,-160]},"ease":"ease_out"},{"t":1,"v":"rest"}]},
        {"property":"opacity","keys":[{"t":0,"v":0,"ease":"ease_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Jelly","category":"Emphasis","duration":0.8,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":0.25,"v":{"mul":[1.25,0.8]},"ease":"ease_in_out"},
            {"t":0.5,"v":{"mul":[0.85,1.15]},"ease":"ease_in_out"},{"t":0.75,"v":{"mul":[1.08,0.95]},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Swing","category":"Emphasis","duration":1.0,"tracks":[
        {"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in_out"},{"t":0.25,"v":{"offset":12},"ease":"ease_in_out"},
            {"t":0.75,"v":{"offset":-12},"ease":"ease_in_out"},{"t":1,"v":"rest"}]}]})",
    R"({"name":"Fade Out","category":"Exit","duration":0.6,"tracks":[
        {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}]})",
    R"({"name":"Pop Out","category":"Exit","duration":0.5,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"mul":0}}]}]})",
    R"({"name":"Drop Out","category":"Exit","duration":0.7,"tracks":[
        {"property":"position","keys":[{"t":0,"v":"rest","ease":"back_in"},{"t":1,"v":{"offset":[0,260]}}]},
        {"property":"opacity","keys":[{"t":0.5,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}]})",
    R"({"name":"Spin Out","category":"Exit","duration":0.7,"tracks":[
        {"property":"rotation","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":180}}]},
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":0}}]}]})",
    R"({"name":"Slide Out","category":"Exit","duration":0.6,"tracks":[
        {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[240,0]}}]},
        {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}]})",
    R"({"name":"Zoom Out","category":"Exit","duration":0.6,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":1.6}}]},
        {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}]})",
    R"({"name":"Rise Out","category":"Exit","duration":0.6,"tracks":[
        {"property":"position","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"offset":[0,-140]}}]},
        {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}]})",
    R"({"name":"Shrink Out","category":"Exit","duration":0.5,"tracks":[
        {"property":"scale","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":{"mul":0.5}}]},
        {"property":"opacity","keys":[{"t":0,"v":"rest","ease":"ease_in"},{"t":1,"v":0}]}]})",
    // Letters one by one (text): the layer's Letter Animation, no keyframes.
    R"({"name":"Letters Fade", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"fade","by":"letter","order":"forward","delay":0.04,"duration":0.5}})",
    R"({"name":"Letters Rise", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"rise","by":"letter","order":"forward","delay":0.04,"duration":0.5}})",
    R"({"name":"Letters Pop", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"pop","by":"letter","order":"forward","delay":0.05,"duration":0.45}})",
    R"({"name":"Letters Drop", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"drop","by":"letter","order":"forward","delay":0.04,"duration":0.5}})",
    R"({"name":"Letters Blur", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"blur","by":"letter","order":"forward","delay":0.04,"duration":0.6}})",
    R"({"name":"Letters Spin", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"spin","by":"letter","order":"center","delay":0.04,"duration":0.6}})",
    R"({"name":"Random Letters", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"fade","by":"letter","order":"random","delay":0.03,"duration":0.4}})",
    R"({"name":"Words Slide", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"slide","by":"word","order":"forward","delay":0.12,"duration":0.5}})",
    R"({"name":"Lines Rise", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"rise","by":"line","order":"forward","delay":0.25,"duration":0.6}})",
    R"({"name":"Letters Out", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"out":"fade","by":"letter","order":"forward","delay":0.03,"duration":0.4}})",
    R"({"name":"Wave", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"loop":"wave","by":"letter"}})",
    R"({"name":"Jitter", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"loop":"jitter","by":"letter"}})",
    R"({"name":"Still Letters", "category":"Letters","applies_to":"text","duration":0.5,"letters":{"in":"none","out":"none","loop":"none"}})",
};


void setKey(const PropertyRef& p, double t, const std::vector<float>& v, Easing easing, const BezierEase& curve) {
    switch (p.kind) {
    case PropertyKind::Float: p.asFloat().setKey(t, v[0], easing); break;
    case PropertyKind::Vec2: p.asVec2().setKey(t, {v[0], v[1]}, easing); break;
    case PropertyKind::Color: p.asColor().setKey(t, {v[0], v[1], v[2], v[3]}, easing); break;
    }
    // setKey keeps an existing key's easing: make the preset's curve win.
    if (const int i = p.anim->findKey(t); i >= 0) {
        p.anim->setKeyEasing(i, easing);
        p.anim->setKeyCurve(i, curve);
    }
}

// A rest value that would leave the layer invisible (e.g. after a previous
// fade/pop out) is useless as a target: fall back to the natural one.
std::vector<float> usableRest(const char* id, std::vector<float> v) {
    if ((!std::strcmp(id, "opacity") || !std::strcmp(id, "reveal") || !std::strcmp(id, "trim_end")) && v[0] < 1.0f)
        v[0] = 100.0f;
    if (!std::strcmp(id, "scale") && std::fabs(v[0]) < 1.0f && std::fabs(v[1]) < 1.0f) v = {100.0f, 100.0f};
    return v;
}

std::vector<float> broadcast(std::vector<float> v, int n) {
    if (v.empty()) v.push_back(0.0f);
    while (static_cast<int>(v.size()) < n) v.push_back(v.size() == 3 && n == 4 ? 1.0f : v.back());
    v.resize(n);
    return v;
}

std::vector<float> readFloats(const json& j) {
    std::vector<float> v;
    if (j.is_number()) v.push_back(j.get<float>());
    else if (j.is_array())
        for (const auto& x : j)
            if (x.is_number()) v.push_back(x.get<float>());
    return v;
}

json writeFloats(const std::vector<float>& v) {
    if (v.size() == 1) return v[0];
    return json(v);
}

} // namespace

std::vector<AnimationPreset> builtinPresets() {
    static const std::vector<AnimationPreset> presets = [] {
        std::vector<AnimationPreset> out;
        for (const char* text : kBuiltinPresets) {
            AnimationPreset p;
            if (parsePreset(text, p)) out.push_back(std::move(p));
        }
        return out;
    }();
    return presets;
}

bool parsePreset(const std::string& text, AnimationPreset& out, std::string* error) {
    try {
        const json j = json::parse(text);
        AnimationPreset p;
        p.name = j.value("name", std::string());
        if (p.name.empty()) throw std::runtime_error("The preset has no name.");
        p.category = j.value("category", std::string("Custom"));
        p.appliesTo = j.value("applies_to", std::string("any"));
        p.duration = std::max(0.01, j.value("duration", 0.6));
        p.perGlyph = std::max(0.0, j.value("per_glyph", 0.0));
        p.enableStroke = j.value("enable_stroke", false);
        for (const auto& tj : j.value("tracks", json::array())) {
            PresetTrack track;
            track.property = tj.value("property", std::string());
            for (const auto& kj : tj.value("keys", json::array())) {
                PresetKey k;
                k.t = std::clamp(kj.value("t", 0.0), 0.0, 1.0);
                k.easing = easingFromId(kj.value("ease", std::string("ease_in_out")).c_str());
                if (const auto c = kj.find("curve"); c != kj.end()) {
                    const std::vector<float> f = readFloats(*c);
                    if (f.size() == 4) k.curve = {f[0], f[1], f[2], f[3]};
                }
                const json& v = kj.contains("v") ? kj["v"] : json("rest");
                if (v.is_string()) k.kind = PresetKey::Kind::Rest;
                else if (v.is_object() && v.contains("offset")) k.kind = PresetKey::Kind::Offset, k.value = readFloats(v["offset"]);
                else if (v.is_object() && v.contains("mul")) k.kind = PresetKey::Kind::Mul, k.value = readFloats(v["mul"]);
                else k.value = readFloats(v);
                track.keys.push_back(std::move(k));
            }
            std::sort(track.keys.begin(), track.keys.end(), [](const PresetKey& a, const PresetKey& b) { return a.t < b.t; });
            if (!track.property.empty() && !track.keys.empty()) p.tracks.push_back(std::move(track));
        }
        if (const auto lj = j.find("letters"); lj != j.end() && lj->is_object()) {
            PresetLetters& l = p.letters;
            l.set = true;
            l.in = lj->value("in", std::string());
            l.out = lj->value("out", std::string());
            l.loop = lj->value("loop", std::string());
            l.by = lj->value("by", std::string());
            l.order = lj->value("order", std::string());
            l.delay = lj->value("delay", -1.0);
            l.duration = lj->value("duration", -1.0);
            p.appliesTo = "text";
        }
        if (p.tracks.empty() && !p.letters.set) throw std::runtime_error("The preset has no keyframes.");
        out = std::move(p);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::string presetToJson(const AnimationPreset& p) {
    json j;
    j["name"] = p.name;
    j["category"] = p.category;
    j["applies_to"] = p.appliesTo;
    j["duration"] = p.duration;
    if (p.perGlyph > 0) j["per_glyph"] = p.perGlyph;
    if (p.enableStroke) j["enable_stroke"] = true;
    json tracks = json::array();
    for (const auto& t : p.tracks) {
        json keys = json::array();
        for (const auto& k : t.keys) {
            json v;
            switch (k.kind) {
            case PresetKey::Kind::Rest: v = "rest"; break;
            case PresetKey::Kind::Offset: v = {{"offset", writeFloats(k.value)}}; break;
            case PresetKey::Kind::Mul: v = {{"mul", writeFloats(k.value)}}; break;
            case PresetKey::Kind::Absolute: v = writeFloats(k.value); break;
            }
            json kj = {{"t", k.t}, {"v", v}, {"ease", easingId(k.easing)}};
            if (k.easing == Easing::Custom) kj["curve"] = {k.curve.x1, k.curve.y1, k.curve.x2, k.curve.y2};
            keys.push_back(std::move(kj));
        }
        tracks.push_back({{"property", t.property}, {"keys", keys}});
    }
    j["tracks"] = tracks;
    if (p.letters.set) {
        json l = json::object();
        for (const auto& [key, v] : {std::pair{"in", &p.letters.in}, {"out", &p.letters.out}, {"loop", &p.letters.loop},
                                     {"by", &p.letters.by}, {"order", &p.letters.order}})
            if (!v->empty()) l[key] = *v;
        if (p.letters.delay >= 0) l["delay"] = p.letters.delay;
        if (p.letters.duration >= 0) l["duration"] = p.letters.duration;
        j["letters"] = l;
    }
    return j.dump(2);
}

bool presetAppliesTo(const AnimationPreset& p, const Layer& layer) {
    if (p.appliesTo == "text") return layer.type == LayerType::Text;
    if (p.appliesTo == "shape") return layer.type == LayerType::Shape;
    return true;
}

double presetDuration(const AnimationPreset& p, const Layer& layer) {
    double d = p.duration;
    if (p.perGlyph > 0 && layer.type == LayerType::Text) {
        int glyphs = 0;
        for (unsigned char c : layer.text.text)
            if ((c & 0xC0) != 0x80 && c != '\n') ++glyphs;
        d = std::max(d, glyphs * p.perGlyph);
    }
    return d;
}

void applyPreset(const AnimationPreset& preset, Layer& layer, double time) {
    if (!presetAppliesTo(preset, layer)) return;
    const double dur = presetDuration(preset, layer);
    // Keep the animation inside the clip.
    const double t0 = std::clamp(time, layer.inPoint, std::max(layer.inPoint, layer.outPoint - dur));
    const double t1 = t0 + dur;
    if (preset.enableStroke && layer.type == LayerType::Shape) layer.shape.strokeEnabled = true;
    if (preset.letters.set && layer.type == LayerType::Text) {
        const PresetLetters& pl = preset.letters;
        TextProps& t = layer.text;
        if (const int i = indexOfId(kLetterAnimIds, pl.in); i >= 0) t.letterIn = static_cast<LetterAnim>(i);
        if (const int i = indexOfId(kLetterAnimIds, pl.out); i >= 0) t.letterOut = static_cast<LetterAnim>(i);
        if (const int i = indexOfId(kLetterLoopIds, pl.loop); i >= 0) t.letterLoop = static_cast<LetterLoop>(i);
        if (const int i = indexOfId(kLetterUnitIds, pl.by); i >= 0) t.letterUnit = static_cast<LetterUnit>(i);
        if (const int i = indexOfId(kLetterOrderIds, pl.order); i >= 0) t.letterOrder = static_cast<LetterOrder>(i);
        if (pl.delay >= 0) t.letterDelay = static_cast<float>(std::min(pl.delay, 10.0));
        if (pl.duration > 0) t.letterDuration = static_cast<float>(std::clamp(pl.duration, 0.01, 30.0));
    }

    auto props = layer.properties();
    for (const PresetTrack& track : preset.tracks) {
        const PropertyRef* p = layer.findProperty(props, track.property.c_str());
        if (!p) continue;
        // Replace whatever this property was doing inside the preset window.
        for (int i = p->anim->keyCount() - 1; i >= 0; --i) {
            const double kt = p->anim->keyTime(i);
            if (kt >= t0 - kTimeEpsilon && kt <= t1 + kTimeEpsilon) p->anim->removeKey(i);
        }
        const int n = static_cast<int>(propertyValue(*p, t1).size());
        const std::vector<float> rest = usableRest(p->id, propertyValue(*p, t1));
        for (const PresetKey& k : track.keys) {
            std::vector<float> v = rest;
            const std::vector<float> arg = broadcast(k.value, n);
            for (int c = 0; c < n; ++c) {
                if (k.kind == PresetKey::Kind::Absolute) v[c] = arg[c];
                else if (k.kind == PresetKey::Kind::Offset) v[c] = rest[c] + arg[c];
                else if (k.kind == PresetKey::Kind::Mul) v[c] = rest[c] * arg[c];
            }
            setKey(*p, t0 + k.t * dur, v, k.easing, k.curve);
        }
    }
}

AnimationPreset presetFromLayer(Layer& layer, double start, double end, const std::string& name) {
    AnimationPreset preset;
    preset.name = name;
    preset.appliesTo = layer.type == LayerType::Text ? "text" : "any";
    double first = end, last = start;
    auto props = layer.properties();
    for (const PropertyRef& p : props)
        for (int i = 0; i < p.anim->keyCount(); ++i) {
            const double kt = p.anim->keyTime(i);
            if (kt < start - kTimeEpsilon || kt > end + kTimeEpsilon) continue;
            first = std::min(first, kt);
            last = std::max(last, kt);
        }
    if (last <= first) return preset; // no keyframes: empty preset
    preset.duration = last - first;
    for (const PropertyRef& p : props) {
        PresetTrack track;
        track.property = p.id;
        for (int i = 0; i < p.anim->keyCount(); ++i) {
            const double kt = p.anim->keyTime(i);
            if (kt < first - kTimeEpsilon || kt > last + kTimeEpsilon) continue;
            PresetKey k;
            k.t = (kt - first) / preset.duration;
            k.value = propertyValue(p, kt);
            k.easing = p.anim->keyEasing(i);
            k.curve = p.anim->keyCurve(i);
            track.keys.push_back(std::move(k));
        }
        if (!track.keys.empty()) preset.tracks.push_back(std::move(track));
    }
    return preset;
}

} // namespace bemotion
