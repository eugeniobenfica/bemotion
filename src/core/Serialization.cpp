#include "core/Serialization.h"

#include "core/Validate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <type_traits>

namespace bemotion {

using json = nlohmann::json;

namespace {

// --- value encoding ----------------------------------------------------------

json encode(float v) { return v; }
json encode(Vec2 v) { return json::array({v.x, v.y}); }
json encode(Color c) { return json::array({c.r, c.g, c.b, c.a}); }

void decode(const json& j, float& v) { if (j.is_number()) v = j.get<float>(); }
void decode(const json& j, Vec2& v) {
    if (j.is_array() && j.size() >= 2) v = {j[0].get<float>(), j[1].get<float>()};
}
void decode(const json& j, Color& c) {
    if (j.is_array() && j.size() >= 3) c = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j.size() > 3 ? j[3].get<float>() : 1.0f};
}

template <typename T>
json encodeAnimated(const Animated<T>& a) {
    json j;
    j["value"] = encode(a.base);
    if (!a.keys.empty()) {
        json keys = json::array();
        for (const auto& k : a.keys) {
            json kj = {{"t", k.time}, {"v", encode(k.value)}, {"ease", easingId(k.easing)}};
            if (k.easing == Easing::Custom) kj["curve"] = {k.curve.x1, k.curve.y1, k.curve.x2, k.curve.y2};
            if constexpr (std::is_same_v<T, Vec2>) {
                if (k.path == PathMode::Linear) kj["path"] = "linear";
                if (k.path == PathMode::Custom) {
                    kj["path"] = "custom";
                    kj["ti"] = encode(k.inTangent);
                    kj["to"] = encode(k.outTangent);
                }
            }
            keys.push_back(std::move(kj));
        }
        j["keys"] = std::move(keys);
    }
    if (a.hasExpression()) j["expression"] = a.expression;
    return j;
}

template <typename T>
void decodeAnimated(const json& j, Animated<T>& a) {
    if (j.contains("value")) decode(j["value"], a.base);
    a.expression = j.value("expression", std::string());
    a.keys.clear();
    if (j.contains("keys") && j["keys"].is_array()) {
        for (const auto& kj : j["keys"]) {
            Keyframe<T> k;
            k.time = kj.value("t", 0.0);
            if (kj.contains("v")) decode(kj["v"], k.value);
            k.easing = easingFromId(kj.value("ease", "linear").c_str());
            if (const auto c = kj.find("curve"); c != kj.end() && c->is_array() && c->size() == 4)
                k.curve = {(*c)[0].get<float>(), (*c)[1].get<float>(), (*c)[2].get<float>(), (*c)[3].get<float>()};
            if constexpr (std::is_same_v<T, Vec2>) {
                const std::string path = kj.value("path", std::string("auto"));
                k.path = path == "linear" ? PathMode::Linear : path == "custom" ? PathMode::Custom : PathMode::Auto;
                if (kj.contains("ti")) decode(kj["ti"], k.inTangent);
                if (kj.contains("to")) decode(kj["to"], k.outTangent);
            }
            a.keys.push_back(k);
        }
        a.removeDuplicateKeys();
    }
}

json encodeProperty(const PropertyRef& p) {
    switch (p.kind) {
    case PropertyKind::Float: return encodeAnimated(p.asFloat());
    case PropertyKind::Vec2: return encodeAnimated(p.asVec2());
    case PropertyKind::Color: return encodeAnimated(p.asColor());
    }
    return {};
}

void decodeProperty(const json& j, const PropertyRef& p) {
    switch (p.kind) {
    case PropertyKind::Float: decodeAnimated(j, p.asFloat()); break;
    case PropertyKind::Vec2: decodeAnimated(j, p.asVec2()); break;
    case PropertyKind::Color: decodeAnimated(j, p.asColor()); break;
    }
}

// --- enums -------------------------------------------------------------------

constexpr const char* kShapeIds[] = {"rectangle", "ellipse", "polygon", "star", "line"};
constexpr const char* kWeightIds[] = {"regular", "medium", "semibold", "bold"};
constexpr const char* kAlignIds[] = {"left", "center", "right"};

template <typename E, size_t N>
E enumFromId(const char* const (&ids)[N], const std::string& id, E fallback) {
    for (size_t i = 0; i < N; ++i)
        if (id == ids[i]) return static_cast<E>(i);
    return fallback;
}

// --- plugin objects --------------------------------------------------------------

constexpr const char* kParamKindIds[] = {"number", "color", "toggle", "choice", "text", "file"};

// Animated values (numbers, colors) are saved with the layer's properties ("p.<id>").
json encodePlugin(const PluginProps& p) {
    json params = json::array();
    for (const PluginParam& pp : p.params) {
        json j = {{"id", pp.id}, {"kind", kParamKindIds[static_cast<int>(pp.kind)]}, {"label", pp.label}, {"group", pp.group}};
        switch (pp.kind) {
        case PluginParamKind::Number:
            j["default"] = pp.defaultNumber;
            j["min"] = pp.min;
            j["max"] = pp.max;
            j["speed"] = pp.speed;
            j["format"] = pp.format;
            break;
        case PluginParamKind::Color: j["default"] = encode(pp.defaultColor); break;
        case PluginParamKind::Toggle: j["value"] = pp.toggle; break;
        case PluginParamKind::Choice:
            j["value"] = pp.choice;
            j["choices"] = pp.choices;
            break;
        case PluginParamKind::Text: j["value"] = pp.text; break;
        case PluginParamKind::File:
            j["value"] = pp.text;
            if (!pp.relative.empty()) j["relative"] = pp.relative;
            j["accept"] = pp.accept;
            break;
        }
        params.push_back(std::move(j));
    }
    json j = {{"type", p.objectType}, {"default_size", encode(p.defaultSize)}, {"params", std::move(params)}};
    if (p.hasRows) {
        json rows = json::array();
        for (const DataRow& r : p.rows) rows.push_back({{"label", r.label}, {"value", r.value}, {"color", encode(r.color)}});
        j["rows"] = std::move(rows);
    }
    if (p.hasTable) j["table"] = {{"header", p.table.header}, {"rows", p.table.rows}};
    return j;
}

void decodePlugin(const json& j, PluginProps& p) {
    p.objectType = j.value("type", std::string());
    if (j.contains("default_size")) decode(j["default_size"], p.defaultSize);
    p.size = AnimVec2{p.defaultSize};
    p.params.clear();
    for (const auto& pj : j.value("params", json::array())) {
        PluginParam pp;
        pp.id = pj.value("id", std::string());
        if (pp.id.empty()) continue;
        pp.kind = enumFromId(kParamKindIds, pj.value("kind", std::string()), PluginParamKind::Number);
        pp.label = pj.value("label", pp.id);
        pp.group = pj.value("group", std::string("Parameters"));
        if (pj.contains("default") && pj["default"].is_number()) pp.defaultNumber = pj["default"].get<float>();
        pp.min = pj.value("min", 0.0f);
        pp.max = pj.value("max", 0.0f);
        pp.speed = pj.value("speed", 0.5f);
        pp.format = pj.value("format", std::string("%.1f"));
        if (pp.kind == PluginParamKind::Color && pj.contains("default")) decode(pj["default"], pp.defaultColor);
        pp.number = AnimFloat{pp.defaultNumber};
        pp.color = AnimColor{pp.defaultColor};
        pp.toggle = pp.kind == PluginParamKind::Toggle && pj.value("value", false);
        if (pp.kind == PluginParamKind::Choice) {
            pp.choices = pj.value("choices", std::vector<std::string>{});
            pp.choice = std::clamp(pj.value("value", 0), 0, std::max(0, static_cast<int>(pp.choices.size()) - 1));
        }
        if (pp.kind == PluginParamKind::Text) pp.text = pj.value("value", std::string());
        if (pp.kind == PluginParamKind::File) {
            pp.text = pj.value("value", std::string());
            pp.relative = pj.value("relative", std::string());
            pp.accept = pj.value("accept", std::vector<std::string>{});
        }
        p.params.push_back(std::move(pp));
    }
    p.hasRows = j.contains("rows");
    p.rows.clear();
    for (const auto& rj : j.value("rows", json::array())) {
        DataRow r;
        r.label = rj.value("label", std::string());
        r.value = rj.value("value", 0.0f);
        if (rj.contains("color")) decode(rj["color"], r.color);
        p.rows.push_back(std::move(r));
    }
    p.hasTable = j.contains("table") && j["table"].is_object();
    p.table = {};
    if (p.hasTable) {
        const json& tj = j["table"];
        for (const auto& h : tj.value("header", json::array())) p.table.header.push_back(h.is_string() ? h.get<std::string>() : h.dump());
        for (const auto& rj : tj.value("rows", json::array())) {
            std::vector<std::string> row;
            if (rj.is_array())
                for (const auto& c : rj) row.push_back(c.is_string() ? c.get<std::string>() : c.dump());
            p.table.rows.push_back(std::move(row));
        }
    }
}

// --- layers ------------------------------------------------------------------

json encodeLayer(const Layer& constLayer) {
    // properties() hands out mutable refs; encoding never mutates.
    Layer& layer = const_cast<Layer&>(constLayer);
    json j;
    j["id"] = layer.id;
    j["track"] = layer.track;
    if (layer.parent) j["parent"] = layer.parent;
    j["name"] = layer.name;
    j["type"] = layer.type == LayerType::Text     ? "text"
                : layer.type == LayerType::Camera ? "camera"
                : layer.type == LayerType::Plugin   ? "plugin"
                : layer.type == LayerType::Compound ? "compound"
                : layer.type == LayerType::Media    ? "media"
                : layer.type == LayerType::Background ? "background"
                                                    : "shape";
    j["visible"] = layer.visible;
    j["locked"] = layer.locked;
    if (layer.threeD) j["three_d"] = true;
    j["label"] = layer.label;
    j["in"] = layer.inPoint;
    j["out"] = layer.outPoint;
    if (layer.type == LayerType::Shape) {
        j["shape"] = {{"kind", kShapeIds[static_cast<int>(layer.shape.kind)]},
                      {"fill", layer.shape.fillEnabled},
                      {"stroke", layer.shape.strokeEnabled}};
    } else if (layer.type == LayerType::Plugin) {
        j["plugin"] = encodePlugin(layer.plugin);
    } else if (layer.type == LayerType::Media) {
        const MediaProps& m = layer.media;
        j["media"] = {{"id", m.mediaId}, {"size", encode(m.size)}, {"start", m.start}, {"video", m.hasVideo},
                      {"audio", m.hasAudio}};
    } else if (layer.type == LayerType::Background) {
        j["background"] = {{"fill", backgroundFillId(layer.background.fill)}, {"media", layer.background.mediaId},
                           {"fit", backgroundFitId(layer.background.fit)}};
    } else if (layer.type == LayerType::Text) {
        j["text"] = {{"text", layer.text.text},
                     {"font_family", layer.text.fontFamily},
                     {"font_style", layer.text.fontStyle},
                     {"align", kAlignIds[static_cast<int>(layer.text.align)]},
                     {"outline", layer.text.outlineEnabled},
                     {"box", layer.text.boxEnabled}};
        if (layer.text.counter) {
            j["text"]["counter"] = true;
            if (layer.text.counterDecimals) j["text"]["decimals"] = layer.text.counterDecimals;
            if (!layer.text.counterPrefix.empty()) j["text"]["prefix"] = layer.text.counterPrefix;
            if (!layer.text.counterSuffix.empty()) j["text"]["suffix"] = layer.text.counterSuffix;
            if (!layer.text.counterSeparator.empty()) j["text"]["separator"] = layer.text.counterSeparator;
        }
        if (layer.text.subtitle) {
            json captions = json::array();
            for (const Caption& c : layer.text.captions) captions.push_back({{"start", c.start}, {"end", c.end}, {"text", c.text}});
            j["text"]["subtitle"] = true;
            j["text"]["captions"] = std::move(captions);
        }
    }
    json props = json::object();
    for (const auto& p : layer.properties())
        if (p.effectIndex < 0) props[p.id] = encodeProperty(p); // effect parameters live in "effects"
    j["properties"] = std::move(props);
    if (!layer.effects.empty()) {
        json effects = json::array();
        for (const Effect& e : layer.effects)
            effects.push_back({{"type", effectId(e.type)},
                               {"enabled", e.enabled},
                               {"samples", e.samples},
                               {"params",
                                {{"color", encodeAnimated(e.color)},
                                 {"opacity", encodeAnimated(e.opacity)},
                                 {"size", encodeAnimated(e.size)},
                                 {"offset", encodeAnimated(e.offset)},
                                 {"shutter", encodeAnimated(e.shutter)},
                                 {"v1", encodeAnimated(e.value1)},
                                 {"v2", encodeAnimated(e.value2)},
                                 {"v3", encodeAnimated(e.value3)}}}});
        j["effects"] = std::move(effects);
    }
    if (!layer.audioEffects.empty()) {
        json effects = json::array();
        for (const AudioEffect& e : layer.audioEffects)
            effects.push_back({{"type", audioEffectId(e.type)}, {"enabled", e.enabled}, {"value", e.value}});
        j["audio_effects"] = std::move(effects);
    }
    if (layer.transitionIn.active() || layer.transitionOut.active())
        j["transitions"] = {{"in", {{"id", layer.transitionIn.id}, {"duration", layer.transitionIn.duration}, {"joined", layer.transitionIn.joined}}},
                            {"out", {{"id", layer.transitionOut.id}, {"duration", layer.transitionOut.duration}, {"joined", layer.transitionOut.joined}}}};
    return j;
}

Layer decodeLayer(const json& j) {
    Layer layer;
    layer.id = j.value("id", uint64_t{0});
    layer.track = j.value("track", uint64_t{0});
    layer.parent = j.value("parent", uint64_t{0});
    layer.name = j.value("name", std::string("Layer"));
    const std::string type = j.value("type", std::string("shape"));
    layer.type = type == "text"     ? LayerType::Text
                 : type == "camera" ? LayerType::Camera
                 : type == "plugin"   ? LayerType::Plugin
                 : type == "compound" ? LayerType::Compound
                 : type == "media"    ? LayerType::Media
                 : type == "background" ? LayerType::Background
                                      : LayerType::Shape;
    if (j.contains("plugin")) decodePlugin(j["plugin"], layer.plugin); // before properties: creates the params
    if (const auto m = j.find("media"); m != j.end() && m->is_object()) { // before properties: decides which exist
        layer.media.mediaId = m->value("id", uint64_t{0});
        if (m->contains("size")) decode((*m)["size"], layer.media.size);
        layer.media.start = m->value("start", 0.0);
        layer.media.hasVideo = m->value("video", true);
        layer.media.hasAudio = m->value("audio", false);
    }
    if (const auto b = j.find("background"); b != j.end() && b->is_object()) { // before properties: the fill decides which exist
        const std::string fill = b->value("fill", std::string("linear")), fit = b->value("fit", std::string("cover"));
        for (int i = 0; i < static_cast<int>(BackgroundFill::Count); ++i)
            if (fill == backgroundFillId(static_cast<BackgroundFill>(i))) layer.background.fill = static_cast<BackgroundFill>(i);
        for (int i = 0; i < static_cast<int>(BackgroundFit::Count); ++i)
            if (fit == backgroundFitId(static_cast<BackgroundFit>(i))) layer.background.fit = static_cast<BackgroundFit>(i);
        layer.background.mediaId = b->value("media", uint64_t{0});
    }
    layer.visible = j.value("visible", true);
    layer.locked = j.value("locked", false);
    layer.threeD = j.value("three_d", false); // before properties: a 3D layer has a Z position
    layer.label = j.value("label", 0);
    layer.inPoint = j.value("in", 0.0);
    layer.outPoint = j.value("out", 10.0);
    if (j.contains("shape")) {
        const auto& s = j["shape"];
        layer.shape.kind = enumFromId(kShapeIds, s.value("kind", std::string()), ShapeKind::Rectangle);
        layer.shape.fillEnabled = s.value("fill", true);
        layer.shape.strokeEnabled = s.value("stroke", false);
    }
    if (j.contains("text")) {
        const auto& t = j["text"];
        layer.text.text = t.value("text", std::string());
        layer.text.fontFamily = t.value("font_family", std::string("Inter"));
        if (t.contains("font_style")) layer.text.fontStyle = t.value("font_style", std::string("Regular"));
        else // files from before font families: Inter weights
            layer.text.fontStyle = fontWeightLabel(enumFromId(kWeightIds, t.value("weight", std::string()), FontWeight::Regular));
        layer.text.align = enumFromId(kAlignIds, t.value("align", std::string()), TextAlign::Center);
        layer.text.outlineEnabled = t.value("outline", false);
        layer.text.boxEnabled = t.value("box", false);
        layer.text.subtitle = t.value("subtitle", false);
        layer.text.counter = t.value("counter", false);
        layer.text.counterDecimals = t.value("decimals", 0);
        layer.text.counterPrefix = t.value("prefix", std::string());
        layer.text.counterSuffix = t.value("suffix", std::string());
        layer.text.counterSeparator = t.value("separator", std::string());
        if (const auto cj = t.find("captions"); cj != t.end() && cj->is_array())
            for (const json& c : *cj)
                if (c.is_object())
                    layer.text.captions.push_back({c.value("start", 0.0), c.value("end", 0.0), c.value("text", std::string())});
    }
    if (j.contains("properties")) {
        const auto& props = j["properties"];
        for (auto& p : layer.properties())
            if (props.contains(p.id)) decodeProperty(props[p.id], p);
    }
    for (const auto& ej : j.value("effects", json::array())) {
        const EffectType type = effectFromId(ej.value("type", std::string()));
        if (type == EffectType::Count) continue; // effect from a newer version: skip
        Effect e = makeEffect(type);
        e.enabled = ej.value("enabled", true);
        e.samples = std::clamp(ej.value("samples", e.samples), 2, 64);
        const json params = ej.value("params", json::object());
        if (params.contains("color")) decodeAnimated(params["color"], e.color);
        if (params.contains("opacity")) decodeAnimated(params["opacity"], e.opacity);
        if (params.contains("size")) decodeAnimated(params["size"], e.size);
        if (params.contains("offset")) decodeAnimated(params["offset"], e.offset);
        if (params.contains("shutter")) decodeAnimated(params["shutter"], e.shutter);
        if (params.contains("v1")) decodeAnimated(params["v1"], e.value1);
        if (params.contains("v2")) decodeAnimated(params["v2"], e.value2);
        if (params.contains("v3")) decodeAnimated(params["v3"], e.value3);
        layer.effects.push_back(std::move(e));
    }
    for (const auto& ej : j.value("audio_effects", json::array())) {
        const std::string type = ej.value("type", std::string());
        for (int i = 0; i < static_cast<int>(AudioEffectType::Count); ++i) {
            const auto candidate = static_cast<AudioEffectType>(i);
            if (type != audioEffectId(candidate)) continue;
            AudioEffect e = makeAudioEffect(candidate);
            e.enabled = ej.value("enabled", true);
            e.value = ej.value("value", e.value);
            layer.audioEffects.push_back(e);
            break;
        }
    }
    if (const auto transitions = j.find("transitions"); transitions != j.end() && transitions->is_object()) {
        auto decodeTransition = [&](const char* edge, ClipTransition& out) {
            if (const auto value = transitions->find(edge); value != transitions->end() && value->is_object()) {
                out.id = value->value("id", std::string());
                out.duration = value->value("duration", 0.5f);
                out.joined = value->value("joined", false);
            }
        };
        decodeTransition("in", layer.transitionIn);
        decodeTransition("out", layer.transitionOut);
    }
    return layer;
}

} // namespace

std::string serializeProject(const Project& project) {
    const Composition& c = project.comp;
    json comp;
    comp["width"] = c.width;
    comp["height"] = c.height;
    comp["fps"] = c.fps;
    comp["duration"] = c.duration;
    if (c.maxDuration > 0.0) comp["max_duration"] = c.maxDuration;
    comp["background"] = encode(c.background);
    comp["next_layer_id"] = c.nextLayerId;
    comp["next_track_id"] = c.nextTrackId;
    json guides = json::array();
    for (const Guide& g : c.guides) guides.push_back({{"vertical", g.vertical}, {"position", g.position}});
    comp["guides"] = std::move(guides);
    json media = json::array();
    for (const MediaItem& m : c.media)
        media.push_back({{"id", m.id}, {"name", m.name}, {"path", m.path}, {"relative", m.relative},
                         {"kind", mediaKindLabel(m.kind)}, {"width", m.width}, {"height", m.height},
                         {"duration", m.duration}, {"fps", m.fps}, {"video", m.hasVideo}, {"audio", m.hasAudio}});
    comp["media"] = std::move(media);
    comp["next_media_id"] = c.nextMediaId;
    json layers = json::array();
    for (const auto& l : c.layers) layers.push_back(encodeLayer(l));
    comp["layers"] = std::move(layers);

    json root;
    root["format"] = "bemotion";
    root["version"] = Project::kFormatVersion;
    root["name"] = project.name;
    root["composition"] = std::move(comp);
    return root.dump(2);
}

bool deserializeProject(const std::string& text, Project& out, std::string* error, std::vector<Issue>* repairs) {
    try {
        const json root = json::parse(text);
        // "mirage" is the format id used before the project was renamed.
        const std::string format = root.value("format", std::string());
        if (format != "bemotion" && format != "mirage") {
            if (error) *error = "Not a bemotion project file.";
            return false;
        }
        if (root.value("version", 0) > Project::kFormatVersion) {
            if (error) *error = "This project was created by a newer version of bemotion.";
            return false;
        }
        Project p;
        p.name = root.value("name", std::string("Untitled"));
        if (root.contains("composition")) {
            const auto& cj = root["composition"];
            Composition& c = p.comp;
            c.width = std::max(1, cj.value("width", 1920));
            c.height = std::max(1, cj.value("height", 1080));
            c.fps = std::max(1.0, cj.value("fps", 30.0));
            c.duration = std::max(c.frameDuration(), cj.value("duration", 10.0));
            if (cj.contains("max_duration") && cj["max_duration"].is_number()) c.maxDuration = cj["max_duration"].get<double>();
            if (cj.contains("background")) decode(cj["background"], c.background);
            uint64_t maxId = 0, maxTrack = 0;
            if (cj.contains("layers"))
                for (const auto& lj : cj["layers"]) {
                    c.layers.push_back(decodeLayer(lj));
                    maxId = std::max(maxId, c.layers.back().id);
                    maxTrack = std::max(maxTrack, c.layers.back().track);
                }
            c.nextLayerId = std::max(cj.value("next_layer_id", uint64_t{1}), maxId + 1);
            c.nextTrackId = std::max(cj.value("next_track_id", uint64_t{1}), maxTrack + 1);
            // Files from before tracks existed: one track per layer.
            for (auto& l : c.layers)
                if (l.track == 0) l.track = c.nextTrackId++;
            c.normalizeTracks();
            c.syncCameras();
            for (const auto& mj : cj.value("media", json::array())) {
                if (!mj.is_object()) continue;
                MediaItem m;
                m.id = mj.value("id", uint64_t{0});
                m.name = mj.value("name", std::string());
                m.path = mj.value("path", std::string());
                m.relative = mj.value("relative", std::string());
                const std::string kind = mj.value("kind", std::string("Image"));
                m.kind = kind == "Video" ? MediaKind::Video : kind == "Audio" ? MediaKind::Audio : MediaKind::Image;
                m.width = mj.value("width", 0);
                m.height = mj.value("height", 0);
                m.duration = mj.value("duration", 0.0);
                m.fps = mj.value("fps", 0.0);
                m.hasVideo = mj.value("video", m.kind != MediaKind::Audio);
                m.hasAudio = mj.value("audio", m.kind == MediaKind::Audio);
                c.nextMediaId = std::max(c.nextMediaId, m.id + 1);
                c.media.push_back(std::move(m));
            }
            c.nextMediaId = std::max(c.nextMediaId, cj.value("next_media_id", uint64_t{1}));
            for (const auto& gj : cj.value("guides", json::array()))
                c.guides.push_back({gj.value("vertical", true), gj.value("position", 0.0f)});
        }
        // A damaged or hand-edited file still opens: whatever breaks the rules is fixed here,
        // so the editor (and everything else) only ever sees valid projects.
        std::vector<Issue> fixed = repair(p);
        if (repairs) *repairs = std::move(fixed);
        out = std::move(p);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

namespace {
std::filesystem::path pathFromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string utf8FromPath(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
} // namespace

bool saveProject(const Project& source, const std::filesystem::path& path, std::string* error) {
    // Media paths are also stored relative to the project, so a moved folder still finds its footage.
    Project project = source;
    auto relativeTo = [&](const std::string& file) {
        std::error_code rel;
        const auto r = std::filesystem::relative(pathFromUtf8(file), path.parent_path(), rel);
        return rel || r.empty() ? std::string() : utf8FromPath(r.generic_u8string());
    };
    for (MediaItem& m : project.comp.media) m.relative = relativeTo(m.path);
    for (Layer& l : project.comp.layers) // files used by plugin objects (3D models...) travel the same way
        for (PluginParam& pp : l.plugin.params)
            if (pp.kind == PluginParamKind::File) pp.relative = pp.text.empty() ? std::string() : relativeTo(pp.text);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    // Write to a temporary file first so a crash never leaves a half-written project.
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "Could not open the file for writing.";
            return false;
        }
        f << serializeProject(project);
        if (!f) {
            if (error) *error = "Could not write the file.";
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    return true;
}

bool loadProject(const std::filesystem::path& path, Project& out, std::string* error, std::vector<Issue>* repairs) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "Could not open the file.";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!deserializeProject(ss.str(), out, error, repairs)) return false;
    auto resolve = [&](const std::string& relative, std::string& absolute) { // the project's own folder first, then where the file was
        std::error_code ec;
        if (!relative.empty() && std::filesystem::exists(path.parent_path() / pathFromUtf8(relative), ec))
            absolute = utf8FromPath((path.parent_path() / pathFromUtf8(relative)).lexically_normal());
    };
    for (MediaItem& m : out.comp.media) resolve(m.relative, m.path);
    for (Layer& l : out.comp.layers)
        for (PluginParam& pp : l.plugin.params)
            if (pp.kind == PluginParamKind::File) resolve(pp.relative, pp.text);
    return true;
}

} // namespace bemotion
