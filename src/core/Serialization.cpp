#include "core/Serialization.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

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
        for (const auto& k : a.keys) keys.push_back({{"t", k.time}, {"v", encode(k.value)}, {"ease", easingId(k.easing)}});
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

// --- layers ------------------------------------------------------------------

json encodeLayer(const Layer& constLayer) {
    // properties() hands out mutable refs; encoding never mutates.
    Layer& layer = const_cast<Layer&>(constLayer);
    json j;
    j["id"] = layer.id;
    j["track"] = layer.track;
    j["name"] = layer.name;
    j["type"] = layer.type == LayerType::Text ? "text" : "shape";
    j["visible"] = layer.visible;
    j["locked"] = layer.locked;
    j["label"] = layer.label;
    j["in"] = layer.inPoint;
    j["out"] = layer.outPoint;
    if (layer.type == LayerType::Shape) {
        j["shape"] = {{"kind", kShapeIds[static_cast<int>(layer.shape.kind)]},
                      {"fill", layer.shape.fillEnabled},
                      {"stroke", layer.shape.strokeEnabled}};
    } else {
        j["text"] = {{"text", layer.text.text},
                     {"weight", kWeightIds[static_cast<int>(layer.text.weight)]},
                     {"align", kAlignIds[static_cast<int>(layer.text.align)]}};
    }
    json props = json::object();
    for (const auto& p : layer.properties()) props[p.id] = encodeProperty(p);
    j["properties"] = std::move(props);
    return j;
}

Layer decodeLayer(const json& j) {
    Layer layer;
    layer.id = j.value("id", uint64_t{0});
    layer.track = j.value("track", uint64_t{0});
    layer.name = j.value("name", std::string("Layer"));
    layer.type = j.value("type", std::string("shape")) == "text" ? LayerType::Text : LayerType::Shape;
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
        layer.text.weight = enumFromId(kWeightIds, t.value("weight", std::string()), FontWeight::Regular);
        layer.text.align = enumFromId(kAlignIds, t.value("align", std::string()), TextAlign::Center);
    }
    if (j.contains("properties")) {
        const auto& props = j["properties"];
        for (auto& p : layer.properties())
            if (props.contains(p.id)) decodeProperty(props[p.id], p);
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
