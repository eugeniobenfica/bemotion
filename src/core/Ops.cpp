#include "core/Ops.h"

#include "core/Validate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace bemotion {

using json = nlohmann::json;

namespace {

const char* kShapeNames[] = {"rectangle", "ellipse", "polygon", "star", "line"};
const char* kAlignNames[] = {"left", "center", "right"};

// Thrown by the operations; applyOps turns it into OpsResult::error.
struct OpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& message) { throw OpError(message); }

// --- reading fields ------------------------------------------------------------------------

const json& field(const json& op, const char* name) {
    auto it = op.find(name);
    if (it == op.end()) fail(std::string("missing field \"") + name + "\"");
    return *it;
}

bool has(const json& op, const char* name) { return op.contains(name) && !op[name].is_null(); }

double number(const json& v, const char* name) {
    if (!v.is_number()) fail(std::string("\"") + name + "\" must be a number");
    const double d = v.get<double>();
    if (!std::isfinite(d)) fail(std::string("\"") + name + "\" must be a finite number");
    return d;
}

double number(const json& op, const char* name, double fallback) { return has(op, name) ? number(op[name], name) : fallback; }

std::string text(const json& v, const char* name) {
    if (!v.is_string()) fail(std::string("\"") + name + "\" must be a string");
    return v.get<std::string>();
}

bool boolean(const json& v, const char* name) {
    if (!v.is_boolean()) fail(std::string("\"") + name + "\" must be true or false");
    return v.get<bool>();
}

// --- names and suggestions -------------------------------------------------------------------

size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (std::tolower(a[i - 1]) == std::tolower(b[j - 1]) ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// "; did you mean "x"?" for the closest candidate, or "; valid: a, b, c".
std::string suggest(const std::string& wrong, const std::vector<std::string>& candidates) {
    if (candidates.empty()) return "";
    std::string best;
    size_t bestD = SIZE_MAX;
    for (const auto& c : candidates)
        if (const size_t d = editDistance(wrong, c); d < bestD) bestD = d, best = c;
    if (bestD <= std::max<size_t>(2, wrong.size() / 3)) return "; did you mean \"" + best + "\"?";
    std::string list;
    for (size_t i = 0; i < candidates.size() && i < 40; ++i) list += (i ? ", " : "") + candidates[i];
    return "; valid: " + list;
}


std::string typeName(const Layer& l) { return layerTypeName(l); }

} // namespace

std::string layerTypeName(const Layer& l) {
    switch (l.type) {
    case LayerType::Shape: return kShapeNames[static_cast<int>(l.shape.kind)];
    case LayerType::Text: return "text";
    case LayerType::Camera: return "camera";
    case LayerType::Compound: return "compound";
    case LayerType::Media: return "media";
    case LayerType::Plugin: return "plugin:" + l.plugin.objectType;
    }
    return "?";
}

namespace {

int shapeKindOf(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i)
        if (name == kShapeNames[i]) return i;
    return -1;
}

Easing easingOf(const json& v) {
    const std::string name = text(v, "easing");
    std::vector<std::string> names;
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        if (e == Easing::Custom) continue;
        if (name == easingId(e)) return e;
        names.push_back(easingId(e));
    }
    fail("unknown easing \"" + name + "\"" + suggest(name, names));
}

// --- values ------------------------------------------------------------------------------------

bool parseHex(std::string s, Color& c) {
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return false;
    unsigned v = 0;
    for (char ch : s) {
        v <<= 4;
        if (ch >= '0' && ch <= '9') v |= ch - '0';
        else if (ch >= 'a' && ch <= 'f') v |= ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') v |= ch - 'A' + 10;
        else return false;
    }
    if (s.size() == 6) c = Color::fromHex(v);
    else c = Color::fromHex(v >> 8, (v & 0xFF) / 255.0f);
    return true;
}

Color colorOf(const json& v, const char* name) {
    Color c;
    if (v.is_string() && parseHex(v.get<std::string>(), c)) return c;
    if (v.is_array() && (v.size() == 3 || v.size() == 4)) {
        c.r = static_cast<float>(number(v[0], name)), c.g = static_cast<float>(number(v[1], name));
        c.b = static_cast<float>(number(v[2], name)), c.a = v.size() == 4 ? static_cast<float>(number(v[3], name)) : 1.0f;
        return c;
    }
    fail(std::string("\"") + name + "\" must be a color: \"#RRGGBB\", \"#RRGGBBAA\" or [r, g, b(, a)] from 0 to 1");
}

Vec2 vec2Of(const json& v, const char* name) {
    if (v.is_array() && v.size() == 2) return {static_cast<float>(number(v[0], name)), static_cast<float>(number(v[1], name))};
    fail(std::string("\"") + name + "\" must be [x, y]");
}

std::vector<float> valueOf(const PropertyRef& p, const json& v) {
    switch (p.kind) {
    case PropertyKind::Float: return {static_cast<float>(number(v, "value"))};
    case PropertyKind::Vec2:
        if (v.is_number()) { // a uniform value: [v, v] (scale 50 = 50%, 50%)
            const float f = static_cast<float>(number(v, "value"));
            return {f, f};
        } else {
            const Vec2 xy = vec2Of(v, "value");
            return {xy.x, xy.y};
        }
    case PropertyKind::Color: {
        const Color c = colorOf(v, "value");
        return {c.r, c.g, c.b, c.a};
    }
    }
    return {};
}

std::string hexOf(Color c) {
    auto byte = [](float f) { return static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255)); };
    char buf[16];
    if (c.a >= 0.999f) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b), byte(c.a));
    return buf;
}

float round3(float v) { return std::round(v * 1000.0f) / 1000.0f; }

json valueJson(PropertyKind kind, const std::vector<float>& v) {
    switch (kind) {
    case PropertyKind::Float: return round3(v[0]);
    case PropertyKind::Vec2: return json::array({round3(v[0]), round3(v[1])});
    case PropertyKind::Color: return hexOf({v[0], v[1], v[2], v[3]});
    }
    return nullptr;
}

// --- the document ----------------------------------------------------------------------------

class Applier {
public:
    Applier(Project& p, const OpEnvironment& env) : p_(p), c_(p.comp), env_(env) {}
    bool changed = false;

    json apply(const json& op) {
        if (!op.is_object()) fail("an operation must be an object like {\"op\": \"layer.add\", ...}");
        const std::string name = text(field(op, "op"), "op");
        static const std::vector<std::pair<std::string, json (Applier::*)(const json&)>> kOps = {
            {"composition.set", &Applier::compositionSet},
            {"layer.add", &Applier::layerAdd},
            {"layer.remove", &Applier::layerRemove},
            {"layer.set", &Applier::layerSet},
            {"layer.move", &Applier::layerMove},
            {"layer.split", &Applier::layerSplit},
            {"layer.duplicate", &Applier::layerDuplicate},
            {"group", &Applier::group},
            {"ungroup", &Applier::ungroup},
            {"property.set", &Applier::propertySet},
            {"key.add", &Applier::keyAdd},
            {"key.remove", &Applier::keyRemove},
            {"key.clear", &Applier::keyClear},
            {"key.easing", &Applier::keyEasing},
            {"effect.add", &Applier::effectAdd},
            {"effect.remove", &Applier::effectRemove},
            {"effect.set", &Applier::effectSet},
            {"media.import", &Applier::mediaImport},
            {"query.composition", &Applier::queryComposition},
            {"query.layers", &Applier::queryLayers},
            {"query.layer", &Applier::queryLayer},
        };
        std::vector<std::string> names;
        for (const auto& [n, fn] : kOps) {
            if (n == name) {
                const bool query = name.rfind("query.", 0) == 0;
                json r = (this->*fn)(op);
                if (!query) changed = true;
                return r;
            }
            names.push_back(n);
        }
        fail("unknown operation \"" + name + "\"" + suggest(name, names));
    }

private:
    // --- lookups ---
    Layer& layer(const json& op, const char* key = "layer") {
        const json& ref = field(op, key);
        if (ref.is_number_integer()) {
            if (Layer* l = c_.findLayer(ref.get<uint64_t>())) return *l;
            fail("there is no layer with id " + std::to_string(ref.get<uint64_t>()));
        }
        const std::string name = text(ref, key);
        if (name.size() > 1 && name[0] == '#' && std::all_of(name.begin() + 1, name.end(), ::isdigit))
            if (Layer* l = c_.findLayer(std::stoull(name.substr(1)))) return *l;
        std::vector<Layer*> matches;
        std::vector<std::string> names;
        for (Layer& l : c_.layers) {
            names.push_back(l.name);
            if (l.name == name) matches.push_back(&l);
        }
        if (matches.empty()) fail("there is no layer named \"" + name + "\"" + suggest(name, names));
        if (matches.size() > 1) {
            std::string ids;
            for (const Layer* m : matches) ids += (ids.empty() ? "" : ", ") + std::to_string(m->id);
            fail("several layers are named \"" + name + "\" (ids " + ids + "): use the id");
        }
        return *matches.front();
    }

    // The property `prop` of `l` (ids as in docs/OPERATIONS.md: "position", "opacity", "fx0.size", "p.progress").
    PropertyRef property(Layer& l, const json& op) {
        const std::string id = text(field(op, "prop"), "prop");
        std::vector<std::string> ids;
        for (const PropertyRef& p : l.properties()) {
            if (id == p.id) return p;
            ids.push_back(p.id);
        }
        fail("layer \"" + l.name + "\" (" + typeName(l) + ") has no property \"" + id + "\"" + suggest(id, ids));
    }

    int keyAt(const PropertyRef& p, double t) {
        const int i = p.anim->findKey(t);
        if (i >= 0) return i;
        std::string times;
        for (int k = 0; k < p.anim->keyCount(); ++k) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%s%.3f", k ? ", " : "", p.anim->keyTime(k));
            times += buf;
        }
        fail(std::string("no keyframe of \"") + p.id + "\" at " + std::to_string(t) + " s" +
             (times.empty() ? " (it has none)" : " (keys at " + times + ")"));
    }

    Effect& effect(Layer& l, const json& op) {
        const double i = number(field(op, "index"), "index");
        if (i < 0 || i >= static_cast<double>(l.effects.size()) || i != std::floor(i))
            fail("layer \"" + l.name + "\" has " + std::to_string(l.effects.size()) + " effect(s): index must be 0.." +
                 std::to_string(static_cast<int>(l.effects.size()) - 1));
        return l.effects[static_cast<size_t>(i)];
    }

    double time(const json& op, const char* name = "time", double fallback = 0.0) {
        const double t = number(op, name, fallback);
        if (t < 0.0) fail(std::string("\"") + name + "\" cannot be negative");
        return t;
    }

    // --- operations ---
    json compositionSet(const json& op) {
        if (has(op, "name")) p_.name = text(op["name"], "name");
        auto size = [&](const char* key, int& out) {
            if (!has(op, key)) return;
            const double v = number(op[key], key);
            if (v < 16 || v > 16384 || v != std::floor(v)) fail(std::string("\"") + key + "\" must be a whole number from 16 to 16384");
            out = static_cast<int>(v);
        };
        size("width", c_.width);
        size("height", c_.height);
        if (has(op, "fps")) {
            const double fps = number(op["fps"], "fps");
            if (fps < 1 || fps > 240) fail("\"fps\" must be between 1 and 240");
            c_.fps = fps;
        }
        if (has(op, "background")) c_.background = colorOf(op["background"], "background");
        c_.syncCameras();
        return json::object();
    }

    json layerAdd(const json& op) {
        const std::string type = text(field(op, "type"), "type");
        const Vec2 position = has(op, "position") ? vec2Of(op["position"], "position") : c_.center();
        const double start = c_.snapToFrame(time(op, "start"));
        double duration = number(op, "duration", kDefaultLayerLength);
        if (duration < c_.frameDuration()) fail("\"duration\" must be at least one frame");
        Layer l;
        if (const int kind = shapeKindOf(type); kind >= 0) {
            l = makeShapeLayer(static_cast<ShapeKind>(kind), position, duration);
        } else if (type == "text") {
            l = makeTextLayer(has(op, "text") ? text(op["text"], "text") : "Text", position, duration);
        } else if (type == "camera") {
            l = makeCameraLayer({static_cast<float>(c_.width), static_cast<float>(c_.height)},
                                has(op, "duration") ? duration : std::max(duration, c_.duration));
        } else if (type == "media") {
            const double id = number(field(op, "media"), "media");
            const MediaItem* item = c_.findMedia(static_cast<uint64_t>(id));
            if (!item) fail("there is no media item " + std::to_string(static_cast<uint64_t>(id)) + " (import it first with media.import)");
            l = makeMediaLayer(*item, position, 0.0, duration);
            if (item->width > 0 && item->height > 0) { // footage larger than the composition fits inside it
                const float fit = std::min({1.0f, static_cast<float>(c_.width) / item->width, static_cast<float>(c_.height) / item->height});
                l.transform.scale.base = {fit * 100.0f, fit * 100.0f};
            }
        } else if (type == "plugin") {
            const std::string object = text(field(op, "object"), "object");
            std::string error;
            if (!env_.makePluginLayer) fail("plugin objects are not available here");
            if (!env_.makePluginLayer(object, l, error)) fail(error);
            l.transform.position.base = position;
            l.outPoint = l.inPoint + duration;
        } else {
            fail("unknown layer type \"" + type + "\"" +
                 suggest(type, {"text", "rectangle", "ellipse", "polygon", "star", "line", "camera", "media", "plugin"}));
        }
        if (has(op, "name")) l.name = text(op["name"], "name");
        l.name = c_.uniqueLayerName(l.name.empty() ? type : l.name);
        if (l.type != LayerType::Media) l.label = static_cast<int>(c_.nextLayerId % kLabelColorCount);
        if (start > 0.0) l.shiftTime(start - l.inPoint);
        if (has(op, "parent")) {
            Layer& parent = layer(op, "parent");
            if (parent.type != LayerType::Compound) fail("\"parent\" must be a compound (made with \"group\")");
            l.parent = parent.id; // `position` is then in the compound's space
        }
        int index = 0; // on top
        if (has(op, "above")) index = c_.indexOf(layer(op, "above").id);
        else if (has(op, "below")) {
            const Layer& ref = layer(op, "below");
            index = c_.indexOf(ref.id);
            while (index < static_cast<int>(c_.layers.size()) && c_.layers[index].track == ref.track) ++index;
        } else if (has(op, "at") && text(op["at"], "at") == "bottom") index = static_cast<int>(c_.layers.size());
        const Layer& added = c_.addLayer(std::move(l), index);
        return {{"layer", added.id}, {"name", added.name}};
    }

    json layerRemove(const json& op) {
        const uint64_t id = layer(op).id;
        c_.removeLayerTree(id);
        return json::object();
    }

    json layerSet(const json& op) {
        Layer& l = layer(op);
        const uint64_t id = l.id;
        auto requireType = [&](bool ok, const char* key, const char* what) {
            if (!ok) fail(std::string("\"") + key + "\" only applies to " + what + " layers (\"" + l.name + "\" is " + typeName(l) + ")");
        };
        if (has(op, "name")) {
            const std::string name = text(op["name"], "name");
            if (name.empty()) fail("\"name\" cannot be empty");
            if (name != l.name) {
                l.name.clear();
                l.name = c_.uniqueLayerName(name);
            }
        }
        if (has(op, "visible")) l.visible = boolean(op["visible"], "visible");
        if (has(op, "locked")) l.locked = boolean(op["locked"], "locked");
        if (has(op, "label")) {
            const double v = number(op["label"], "label");
            if (v < 0 || v >= kLabelColorCount || v != std::floor(v)) fail("\"label\" must be 0 to 7");
            l.label = static_cast<int>(v);
        }
        if (has(op, "text")) requireType(l.type == LayerType::Text, "text", "text"), l.text.text = text(op["text"], "text");
        if (has(op, "font")) requireType(l.type == LayerType::Text, "font", "text"), l.text.fontFamily = text(op["font"], "font");
        if (has(op, "style")) requireType(l.type == LayerType::Text, "style", "text"), l.text.fontStyle = text(op["style"], "style");
        if (has(op, "align")) {
            requireType(l.type == LayerType::Text, "align", "text");
            const std::string a = text(op["align"], "align");
            int i = 0;
            while (i < 3 && a != kAlignNames[i]) ++i;
            if (i == 3) fail("\"align\" must be left, center or right");
            l.text.align = static_cast<TextAlign>(i);
        }
        if (has(op, "shape")) {
            requireType(l.type == LayerType::Shape, "shape", "shape");
            const std::string s = text(op["shape"], "shape");
            const int kind = shapeKindOf(s);
            if (kind < 0) fail("unknown shape \"" + s + "\"" + suggest(s, {"rectangle", "ellipse", "polygon", "star", "line"}));
            l.shape.kind = static_cast<ShapeKind>(kind);
        }
        if (has(op, "fill")) requireType(l.type == LayerType::Shape, "fill", "shape"), l.shape.fillEnabled = boolean(op["fill"], "fill");
        if (has(op, "stroke")) requireType(l.type == LayerType::Shape, "stroke", "shape"), l.shape.strokeEnabled = boolean(op["stroke"], "stroke");
        json out = json::object();
        if (has(op, "start")) { // moves the clip, keyframes included
            const double delta = c_.snapToFrame(time(op, "start")) - l.inPoint;
            std::vector<uint64_t> moving = c_.descendantsOf(id); // a compound brings its contents
            moving.push_back(id);
            // Past the end is fine: the composition grows to hold it (fitDuration after the batch).
            c_.duration = std::max(c_.duration, l.outPoint + std::max(0.0, delta) + 1.0);
            double lo = 0, hi = 0;
            c_.timeShiftLimits(moving, lo, hi);
            if (delta < lo - kTimeEpsilon || delta > hi + kTimeEpsilon)
                fail("the clip cannot start at " + std::to_string(l.inPoint + delta) + " s: it would overlap another clip on its track");
            for (uint64_t m : moving)
                if (Layer* each = c_.findLayer(m)) each->shiftTime(delta);
        }
        if (has(op, "in")) c_.setInPoint(id, time(op, "in"));
        if (has(op, "out")) c_.setOutPoint(id, time(op, "out"));
        const Layer& now = *c_.findLayer(id);
        out["in"] = now.inPoint;
        out["out"] = now.outPoint;
        return out;
    }

    json layerMove(const json& op) {
        const Layer& l = layer(op);
        const std::string to = text(field(op, "to"), "to");
        const int delta = to == "top" ? -100000 : to == "bottom" ? 100000 : to == "up" ? -1 : to == "down" ? 1 : 0;
        if (!delta) fail("\"to\" must be top, bottom, up or down");
        c_.shiftTracks({l.track}, delta);
        return json::object();
    }

    json layerSplit(const json& op) {
        const Layer& l = layer(op);
        const double t = c_.snapToFrame(time(op));
        const uint64_t copy = c_.splitLayer(l.id, t);
        if (!copy) fail("\"time\" must be strictly inside the clip (" + std::to_string(l.inPoint) + " to " + std::to_string(l.outPoint) + " s)");
        return {{"layer", copy}};
    }

    json layerDuplicate(const json& op) {
        const Layer& l = layer(op);
        if (l.type == LayerType::Compound) fail("duplicating compounds is not available as an operation yet");
        Layer copy = l;
        copy.track = 0;
        copy.name = c_.uniqueLayerName(l.name);
        const int index = c_.indexOf(l.id);
        const Layer& added = c_.addLayer(std::move(copy), index);
        return {{"layer", added.id}, {"name", added.name}};
    }

    json group(const json& op) {
        const json& list = field(op, "layers");
        if (!list.is_array() || list.empty()) fail("\"layers\" must be a list of layers");
        std::vector<uint64_t> ids;
        uint64_t parent = UINT64_MAX;
        for (const json& ref : list) {
            const Layer& l = layer(json{{"layer", ref}});
            if (parent != UINT64_MAX && l.parent != parent) fail("the layers to group must be inside the same compound (or none)");
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

    json ungroup(const json& op) {
        const Layer& l = layer(op);
        if (l.type != LayerType::Compound) fail("\"" + l.name + "\" is not a compound");
        c_.releaseCompound(l.id, time(op));
        return json::object();
    }

    json propertySet(const json& op) {
        Layer& l = layer(op);
        const PropertyRef p = property(l, op);
        setPropertyValue(p, time(op), valueOf(p, field(op, "value")));
        return json::object();
    }

    template <typename T>
    static void addKey(const PropertyRef& p, double t, const std::vector<float>& v, Easing e) {
        auto& a = *static_cast<Animated<T>*>(p.anim);
        T value;
        if constexpr (std::is_same_v<T, float>) value = v[0];
        else if constexpr (std::is_same_v<T, Vec2>) value = {v[0], v[1]};
        else value = {v[0], v[1], v[2], v[3]};
        a.setKey(t, value, e);
    }

    json keyAdd(const json& op) {
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

    json keyRemove(const json& op) {
        Layer& l = layer(op);
        const PropertyRef p = property(l, op);
        p.anim->removeKey(keyAt(p, time(op)));
        return json::object();
    }

    json keyClear(const json& op) {
        Layer& l = layer(op);
        const PropertyRef p = property(l, op);
        const double t = time(op);
        p.anim->clearKeys(t); // keeps the value it had at `t`
        if (has(op, "value")) setPropertyValue(p, t, valueOf(p, op["value"]));
        return json::object();
    }

    json keyEasing(const json& op) {
        Layer& l = layer(op);
        const PropertyRef p = property(l, op);
        const Easing e = easingOf(field(op, "easing"));
        if (has(op, "all") && boolean(op["all"], "all")) {
            for (int i = 0; i < p.anim->keyCount(); ++i) p.anim->setKeyEasing(i, e);
        } else {
            p.anim->setKeyEasing(keyAt(p, time(op)), e);
        }
        return json::object();
    }

    json effectAdd(const json& op) {
        Layer& l = layer(op);
        if (l.type == LayerType::Camera) fail("cameras have no effects");
        const std::string id = text(field(op, "effect"), "effect");
        const EffectType type = effectFromId(id);
        if (type == EffectType::Count) {
            std::vector<std::string> ids;
            for (int i = 0; i < static_cast<int>(EffectType::Count); ++i) ids.push_back(effectId(static_cast<EffectType>(i)));
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

    json effectRemove(const json& op) {
        Layer& l = layer(op);
        Effect& e = effect(l, op);
        l.effects.erase(l.effects.begin() + (&e - l.effects.data()));
        return json::object();
    }

    json effectSet(const json& op) {
        Layer& l = layer(op);
        Effect& e = effect(l, op);
        if (has(op, "enabled")) e.enabled = boolean(op["enabled"], "enabled");
        if (has(op, "samples")) {
            const double s = number(op["samples"], "samples");
            if (s < 2 || s > 64) fail("\"samples\" must be 2 to 64");
            e.samples = static_cast<int>(s);
        }
        return json::object();
    }

    json mediaImport(const json& op) {
        const std::string path = text(field(op, "path"), "path");
        if (!env_.probeMedia) fail("importing media is not available here");
        MediaItem item;
        std::string error;
        if (!env_.probeMedia(path, item, error)) fail("could not import \"" + path + "\": " + error);
        const uint64_t id = c_.addMedia(item);
        const MediaItem& m = *c_.findMedia(id);
        return {{"media", id}, {"name", m.name}, {"kind", mediaKindLabel(m.kind)}, {"width", m.width}, {"height", m.height},
                {"duration", m.duration}};
    }

    // --- queries ---
    json queryComposition(const json&) {
        json media = json::array();
        for (const MediaItem& m : c_.media)
            media.push_back({{"media", m.id}, {"name", m.name}, {"kind", mediaKindLabel(m.kind)}, {"path", m.path}});
        return {{"name", p_.name}, {"width", c_.width}, {"height", c_.height}, {"fps", c_.fps}, {"duration", c_.duration},
                {"background", hexOf(c_.background)}, {"layers", c_.layers.size()}, {"media", media}};
    }

    json layerSummary(const Layer& l) {
        json j = {{"layer", l.id}, {"name", l.name}, {"type", typeName(l)}, {"in", l.inPoint}, {"out", l.outPoint},
                  {"track", c_.trackRowOf(l.id)}};
        if (l.parent) j["parent"] = l.parent;
        if (!l.visible) j["visible"] = false;
        if (l.locked) j["locked"] = true;
        if (!l.effects.empty()) j["effects"] = l.effects.size();
        return j;
    }

    json queryLayers(const json&) {
        json list = json::array();
        for (const Layer& l : c_.layers) list.push_back(layerSummary(l));
        return list;
    }

    json queryLayer(const json& op) {
        Layer& l = layer(op);
        const double t = time(op);
        json j = layerSummary(l);
        if (l.type == LayerType::Text)
            j["text"] = {{"text", l.text.text}, {"font", l.text.fontFamily}, {"style", l.text.fontStyle},
                         {"align", kAlignNames[static_cast<int>(l.text.align)]}};
        if (l.type == LayerType::Shape) j["shape"] = {{"fill", l.shape.fillEnabled}, {"stroke", l.shape.strokeEnabled}};
        if (l.type == LayerType::Media) j["media"] = l.media.mediaId;
        json props = json::object();
        for (const PropertyRef& p : l.properties()) {
            json pj = {{"value", valueJson(p.kind, propertyValue(p, t))}};
            if (p.anim->isAnimated()) {
                json keys = json::array();
                for (int k = 0; k < p.anim->keyCount(); ++k) {
                    const double kt = p.anim->keyTime(k);
                    keys.push_back({{"time", kt}, {"value", valueJson(p.kind, propertyValue(p, kt))}, {"easing", easingId(p.anim->keyEasing(k))}});
                }
                pj["keys"] = keys;
            }
            props[p.id] = pj;
        }
        j["props"] = props;
        json effects = json::array();
        for (size_t i = 0; i < l.effects.size(); ++i)
            effects.push_back({{"index", i}, {"effect", effectId(l.effects[i].type)}, {"enabled", l.effects[i].enabled}});
        j["effect_list"] = effects;
        return j;
    }

    Project& p_;
    Composition& c_;
    const OpEnvironment& env_;
};

} // namespace

OpsResult applyOps(Project& project, const json& ops, const OpEnvironment& env) {
    OpsResult r;
    const json* list = &ops;
    json single;
    if (ops.is_object() && ops.contains("ops")) {
        if (ops.contains("version") && ops["version"] != kOpsVersion) {
            r.error = "unsupported operations version " + ops["version"].dump() + " (this bemotion speaks " + std::to_string(kOpsVersion) + ")";
            return r;
        }
        list = &ops["ops"];
    } else if (ops.is_object()) {
        single = json::array({ops});
        list = &single;
    }
    if (!list->is_array()) {
        r.error = "expected an operation, a list of operations or {\"ops\": [...]}";
        return r;
    }

    Project work = project; // atomic: the original changes only if everything succeeds
    Applier applier(work, env);
    r.results = json::array();
    for (size_t i = 0; i < list->size(); ++i) {
        const json& op = (*list)[i];
        try {
            r.results.push_back(applier.apply(op));
        } catch (const std::exception& e) {
            const std::string name = op.is_object() && op.contains("op") && op["op"].is_string() ? op["op"].get<std::string>() : "?";
            r.error = "operation " + std::to_string(i) + " (" + name + "): " + e.what();
            r.failedIndex = static_cast<int>(i);
            r.results = json::array();
            return r;
        }
    }
    if (applier.changed) {
        work.comp.fitDuration();
        if (const auto issues = validate(work); !issues.empty()) {
            r.error = "the operations would leave the project invalid: " + issues[0].where + ": " + issues[0].what;
            r.results = json::array();
            return r;
        }
        project = std::move(work);
    }
    r.ok = true;
    r.changed = applier.changed;
    return r;
}

} // namespace bemotion
