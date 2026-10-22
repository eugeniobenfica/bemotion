#include "core/Serialization.h"

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
    return j;
}

template <typename T>
void decodeAnimated(const json& j, Animated<T>& a) {
    if (j.contains("value")) decode(j["value"], a.base);
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

constexpr const char* kParamKindIds[] = {"number", "color", "toggle", "choice", "text"};

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
        }
        params.push_back(std::move(j));
    }
    json j = {{"type", p.objectType}, {"default_size", encode(p.defaultSize)}, {"params", std::move(params)}};
    if (p.hasRows) {
        json rows = json::array();
        for (const DataRow& r : p.rows) rows.push_back({{"label", r.label}, {"value", r.value}, {"color", encode(r.color)}});
        j["rows"] = std::move(rows);
    }
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
}

// --- layers ------------------------------------------------------------------

json encodeLayer(const Layer& constLayer) {
    // properties() hands out mutable refs; encoding never mutates.
    Layer& layer = const_cast<Layer&>(constLayer);
    json j;
    j["id"] = layer.id;
    j["track"] = layer.track;
    j["name"] = layer.name;
    j["type"] = layer.type == LayerType::Text     ? "text"
                : layer.type == LayerType::Camera ? "camera"
                : layer.type == LayerType::Plugin ? "plugin"
                                                  : "shape";
    j["visible"] = layer.visible;
    j["locked"] = layer.locked;
    j["label"] = layer.label;
    j["in"] = layer.inPoint;
    j["out"] = layer.outPoint;
    if (layer.type == LayerType::Shape) {
        j["shape"] = {{"kind", kShapeIds[static_cast<int>(layer.shape.kind)]},
                      {"fill", layer.shape.fillEnabled},
                      {"stroke", layer.shape.strokeEnabled}};
    } else if (layer.type == LayerType::Plugin) {
        j["plugin"] = encodePlugin(layer.plugin);
    } else if (layer.type == LayerType::Text) {
        j["text"] = {{"text", layer.text.text},
                     {"font_family", layer.text.fontFamily},
                     {"font_style", layer.text.fontStyle},
                     {"align", kAlignIds[static_cast<int>(layer.text.align)]}};
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
                                 {"shutter", encodeAnimated(e.shutter)}}}});
        j["effects"] = std::move(effects);
    }
    return j;
}

Layer decodeLayer(const json& j) {
    Layer layer;
    layer.id = j.value("id", uint64_t{0});
    layer.track = j.value("track", uint64_t{0});
    layer.name = j.value("name", std::string("Layer"));
    const std::string type = j.value("type", std::string("shape"));
    layer.type = type == "text"     ? LayerType::Text
                 : type == "camera" ? LayerType::Camera
                 : type == "plugin" ? LayerType::Plugin
                                    : LayerType::Shape;
    if (j.contains("plugin")) decodePlugin(j["plugin"], layer.plugin); // before properties: creates the params
    layer.visible = j.value("visible", true);
    layer.locked = j.value("locked", false);
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
        layer.effects.push_back(std::move(e));
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
    comp["background"] = encode(c.background);
    comp["next_layer_id"] = c.nextLayerId;
    comp["next_track_id"] = c.nextTrackId;
    json guides = json::array();
    for (const Guide& g : c.guides) guides.push_back({{"vertical", g.vertical}, {"position", g.position}});
    comp["guides"] = std::move(guides);
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

bool deserializeProject(const std::string& text, Project& out, std::string* error) {
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
            for (const auto& gj : cj.value("guides", json::array()))
                c.guides.push_back({gj.value("vertical", true), gj.value("position", 0.0f)});
        }
        out = std::move(p);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool saveProject(const Project& project, const std::filesystem::path& path, std::string* error) {
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

bool loadProject(const std::filesystem::path& path, Project& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "Could not open the file.";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return deserializeProject(ss.str(), out, error);
}

} // namespace bemotion
