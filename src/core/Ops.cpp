#include "core/Ops.h"
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
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace bemotion {

using json = nlohmann::json;

namespace opsimpl {

const char* kShapeNames[] = {"rectangle", "ellipse", "polygon", "star", "line", "path"};
const char* kAlignNames[] = {"left", "center", "right"};

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

// --- names (suggestions: core/Suggest.h) ------------------------------------------------------

std::string typeName(const Layer& l) { return layerTypeName(l); }

} // namespace opsimpl

std::string layerTypeName(const Layer& l) {
    switch (l.type) {
    case LayerType::Shape: return opsimpl::kShapeNames[static_cast<int>(l.shape.kind)];
    case LayerType::Text: return l.text.subtitle ? "subtitle" : l.text.counter ? "counter" : "text";
    case LayerType::Camera: return "camera";
    case LayerType::Compound: return "compound";
    case LayerType::Media: return "media";
    case LayerType::Background: return "background";
    case LayerType::Plugin: return "plugin:" + l.plugin.objectType;
    }
    return "?";
}

namespace opsimpl {

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

double round3(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

json valueJson(PropertyKind kind, const std::vector<float>& v) {
    switch (kind) {
    case PropertyKind::Float: return round3(v[0]);
    case PropertyKind::Vec2: return json::array({round3(v[0]), round3(v[1])});
    case PropertyKind::Color: return hexOf({v[0], v[1], v[2], v[3]});
    }
    return nullptr;
}

// --- the document ----------------------------------------------------------------------------

json Applier::apply(const json& op) {
    if (!op.is_object()) fail("an operation must be an object like {\"op\": \"layer.add\", ...}");
    const std::string name = text(field(op, "op"), "op");
    std::vector<std::string> names;
    for (const auto& [n, fn] : ops()) {
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

const std::vector<std::pair<std::string, Applier::Handler>>& Applier::ops() {
    static const std::vector<std::pair<std::string, Handler>> kOps = {
        {"composition.set", &Applier::compositionSet},
        {"layer.add", &Applier::layerAdd},
        {"layer.remove", &Applier::layerRemove},
        {"layer.set", &Applier::layerSet},
        {"layer.move", &Applier::layerMove},
        {"layer.split", &Applier::layerSplit},
        {"layer.duplicate", &Applier::layerDuplicate},
        {"layer.trim", &Applier::layerTrim},
        {"layer.roll", &Applier::layerRoll},
        {"layer.slip", &Applier::layerSlip},
        {"layer.ripple_delete", &Applier::layerRippleDelete},
        {"track.close_gap", &Applier::trackCloseGap},
        {"layer.speed", &Applier::layerSpeed},
        {"layer.time_remap", &Applier::layerTimeRemap},
        {"layer.freeze", &Applier::layerFreeze},
        {"layer.separate_audio", &Applier::layerSeparateAudio},
        {"layer.link_audio", &Applier::layerLinkAudio},
        {"svg.add", &Applier::svgAdd},
        {"mask.add", &Applier::maskAdd},
        {"mask.set", &Applier::maskSet},
        {"mask.remove", &Applier::maskRemove},
        {"mask.duplicate", &Applier::maskDuplicate},
        {"paint.stroke", &Applier::paintStroke},
        {"paint.clear", &Applier::paintClear},
        {"puppet.pin.add", &Applier::pinAdd},
        {"puppet.pin.remove", &Applier::pinRemove},
        {"marker.add", &Applier::markerAdd},
        {"marker.set", &Applier::markerSet},
        {"marker.remove", &Applier::markerRemove},
        {"group", &Applier::group},
        {"ungroup", &Applier::ungroup},
        {"property.set", &Applier::propertySet},
        {"key.add", &Applier::keyAdd},
        {"expression.set", &Applier::expressionSet},
        {"expression.bake", &Applier::expressionBake},
        {"key.remove", &Applier::keyRemove},
        {"key.clear", &Applier::keyClear},
        {"key.easing", &Applier::keyEasing},
        {"effect.add", &Applier::effectAdd},
        {"effect.remove", &Applier::effectRemove},
        {"effect.set", &Applier::effectSet},
        {"audio_effect.add", &Applier::audioEffectAdd},
        {"audio_effect.remove", &Applier::audioEffectRemove},
        {"audio_effect.set", &Applier::audioEffectSet},
        {"audio.level", &Applier::audioLevel},
        {"audio.remove_silence", &Applier::audioRemoveSilence},
        {"transition.add", &Applier::transitionAdd},
        {"arrange.distribute", &Applier::arrangeDistribute},
        {"arrange.stagger", &Applier::arrangeStagger},
        {"data.set", &Applier::dataSet},
        {"param.set", &Applier::paramSet},
        {"media.import", &Applier::mediaImport},
        {"media.move", &Applier::mediaMove},
        {"folder.add", &Applier::folderAdd},
        {"folder.rename", &Applier::folderRename},
        {"folder.remove", &Applier::folderRemove},
        {"folder.move", &Applier::folderMove},
        {"query.composition", &Applier::queryComposition},
        {"query.layers", &Applier::queryLayers},
        {"query.layer", &Applier::queryLayer},
        {"query.expression", &Applier::queryExpression},
    };
    return kOps;
}

Layer& Applier::layer(const json& op, const char* key) {
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
        for (const Layer* m : matches)
            ids += (ids.empty() ? "" : ", ") + std::to_string(m->id);
        fail("several layers are named \"" + name + "\" (ids " + ids + "): use the id");
    }
    return *matches.front();
}

// The property `prop` of `l` (ids as in docs/OPERATIONS.md: "position", "opacity", "fx0.size", "p.progress").
PropertyRef Applier::property(Layer& l, const json& op) {
    const std::string id = text(field(op, "prop"), "prop");
    std::vector<std::string> ids;
    for (const PropertyRef& p : l.properties()) {
        if (id == p.id) return p;
        ids.push_back(p.id);
    }
    fail("layer \"" + l.name + "\" (" + typeName(l) + ") has no property \"" + id + "\"" + suggest(id, ids));
}

int Applier::keyAt(const PropertyRef& p, double t) {
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

Effect& Applier::effect(Layer& l, const json& op) {
    const double i = number(field(op, "index"), "index");
    if (i < 0 || i >= static_cast<double>(l.effects.size()) || i != std::floor(i))
        fail("layer \"" + l.name + "\" has " + std::to_string(l.effects.size()) + " effect(s): index must be 0.." +
             std::to_string(static_cast<int>(l.effects.size()) - 1));
    return l.effects[static_cast<size_t>(i)];
}

double Applier::time(const json& op, const char* name, double fallback) {
    const double t = number(op, name, fallback);
    if (t < 0.0) fail(std::string("\"") + name + "\" cannot be negative");
    return t;
}

json Applier::compositionSet(const json& op) {
    if (has(op, "name")) p_.name = text(op["name"], "name");
    auto size = [&](const char* key, int& out) {
        if (!has(op, key)) return;
        const double v = number(op[key], key);
        if (v < 16 || v > 16384 || v != std::floor(v))
            fail(std::string("\"") + key + "\" must be a whole number from 16 to 16384");
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
    if (has(op, "max_duration")) {
        const double limit = number(op["max_duration"], "max_duration");
        if (limit < 0 || (limit > 0 && limit < c_.frameDuration()))
            fail("\"max_duration\" must be 0 (no limit) or at least one frame");
        c_.maxDuration = limit;
    }
    c_.syncCameras();
    return json::object();
}

json Applier::layerAdd(const json& op) {
    const std::string type = text(field(op, "type"), "type");
    const Vec2 position = has(op, "position") ? vec2Of(op["position"], "position") : c_.center();
    const double start = c_.snapToFrame(time(op, "start"));
    double duration = number(op, "duration", kDefaultLayerLength);
    if (duration < c_.frameDuration()) fail("\"duration\" must be at least one frame");
    Layer l;
    if (const int kind = shapeKindOf(type); kind >= 0) {
        l = makeShapeLayer(static_cast<ShapeKind>(kind), position, duration);
        if (has(op, "path")) {
            if (l.shape.kind != ShapeKind::Path) fail("\"path\" is for \"path\" layers (the pen tool's shapes)");
            l.shape.path = pathOf(op["path"]);
            if (!l.shape.path.closed) l.shape.fillEnabled = false, l.shape.strokeEnabled = true;
        }
    } else if (type == "text") {
        l = makeTextLayer(has(op, "text") ? text(op["text"], "text") : "Text", position, duration);
    } else if (type == "counter") {
        l = makeCounterLayer(position, duration);
        setCounter(l, op);
    } else if (type == "subtitle") {
        l = makeSubtitleLayer({static_cast<float>(c_.width), static_cast<float>(c_.height)},
                              has(op, "duration") ? duration : std::max(duration, c_.duration));
        if (has(op, "position")) l.transform.position.base = position;
        if (has(op, "captions") || has(op, "srt")) setCaptions(l, op);
    } else if (type == "camera") {
        l = makeCameraLayer({static_cast<float>(c_.width), static_cast<float>(c_.height)},
                            has(op, "duration") ? duration : std::max(duration, c_.duration));
    } else if (type == "media") {
        const double id = number(field(op, "media"), "media");
        const MediaItem* item = c_.findMedia(static_cast<uint64_t>(id));
        if (!item)
            fail("there is no media item " + std::to_string(static_cast<uint64_t>(id)) +
                 " (import it first with media.import)");
        l = makeMediaLayer(*item, position, 0.0, duration);
        if (item->width > 0 && item->height > 0) { // footage larger than the composition fits inside it
            const float fit = std::min(
                {1.0f, static_cast<float>(c_.width) / item->width, static_cast<float>(c_.height) / item->height});
            l.transform.scale.base = {fit * 100.0f, fit * 100.0f};
        }
    } else if (type == "background") {
        l = makeBackgroundLayer(has(op, "duration") ? duration : std::max(duration, c_.duration));
        if (has(op, "fill") || has(op, "media") || has(op, "fit")) setBackground(l, op);
    } else if (type == "plugin") {
        const std::string object = text(field(op, "object"), "object");
        std::string error;
        if (!env_.makePluginLayer) fail("plugin objects are not available here");
        if (!env_.makePluginLayer(object, l, error)) fail(error);
        l.transform.position.base = position;
        l.outPoint = l.inPoint + duration;
    } else {
        fail("unknown layer type \"" + type + "\"" +
             suggest(type, {"text", "counter", "subtitle", "rectangle", "ellipse", "polygon", "star", "line", "camera",
                            "media", "background", "plugin"}));
    }
    if (has(op, "name")) l.name = text(op["name"], "name");
    if (l.type == LayerType::Text) setLetters(l, op);
    // A 3D layer ("three_d": true) at Z position "z" (a "z" alone makes it 3D; a camera's own Z).
    if (has(op, "three_d") || has(op, "z")) {
        const bool on = has(op, "three_d") ? boolean(op["three_d"], "three_d") : l.type != LayerType::Camera;
        if (l.type == LayerType::Background || (on && l.type == LayerType::Camera))
            fail("\"" + std::string(has(op, "three_d") ? "three_d" : "z") + "\" does not apply to " + type + " layers");
        l.threeD = on;
        if (has(op, "z")) l.transform.depth.base = static_cast<float>(number(op["z"], "z"));
    }
    l.name = c_.uniqueLayerName(l.name.empty() ? type : l.name);
    if (l.type != LayerType::Media) l.label = static_cast<int>(c_.nextLayerId % kLabelColorCount);
    if (start > 0.0) l.shiftTime(start - l.inPoint);
    if (has(op, "parent")) {
        Layer& parent = layer(op, "parent");
        if (parent.type != LayerType::Compound) fail("\"parent\" must be a compound (made with \"group\")");
        l.parent = parent.id; // `position` is then in the compound's space
    }
    int index = 0; // on top
    if (has(op, "above"))
        index = c_.indexOf(layer(op, "above").id);
    else if (has(op, "below")) {
        const Layer& ref = layer(op, "below");
        index = c_.indexOf(ref.id);
        while (index < static_cast<int>(c_.layers.size()) && c_.layers[index].track == ref.track)
            ++index;
    } else if ((has(op, "at") && text(op["at"], "at") == "bottom") || (type == "background" && !has(op, "at")))
        index = static_cast<int>(c_.layers.size()); // a background goes under everything unless told otherwise
    const uint64_t addedId = c_.addLayer(std::move(l), index).id;
    if (has(op, "track_of")) joinTrack(addedId, op);
    const Layer& added = *c_.findLayer(addedId);
    return {{"layer", added.id}, {"name", added.name}};
}

// "track_of": the clip goes on that layer's track (a sequence of clips on one row); it must
// not overlap the clips there, and be beside it (same compound, or both at the top).
void Applier::joinTrack(uint64_t id, const json& op) {
    const Layer& other = layer(op, "track_of");
    const Layer& l = *c_.findLayer(id);
    if (other.id == id || other.track == l.track) return;
    if (other.parent != l.parent)
        fail("\"track_of\" must be a layer beside it (in the same compound, or both at the top)");
    if (!c_.moveClipsToTrack({id}, other.track))
        fail("\"" + l.name + "\" does not fit on the track of \"" + other.name + "\": it would overlap a clip there (" +
             std::to_string(l.inPoint) + " to " + std::to_string(l.outPoint) + " s)");
}

// A counter's look: "decimals" (0-6), "prefix", "suffix", "separator" ("", ",", "." or " ").
void Applier::setCounter(Layer& l, const json& op) {
    if (has(op, "decimals")) {
        const double v = number(op["decimals"], "decimals");
        if (v < 0 || v > 6 || v != std::floor(v)) fail("\"decimals\" must be 0 to 6");
        l.text.counterDecimals = static_cast<int>(v);
    }
    if (has(op, "prefix")) l.text.counterPrefix = text(op["prefix"], "prefix");
    if (has(op, "suffix")) l.text.counterSuffix = text(op["suffix"], "suffix");
    if (has(op, "separator")) {
        const std::string sep = text(op["separator"], "separator");
        if (!isCounterSeparator(sep)) fail("\"separator\" must be \"\", \",\", \".\" or \" \"");
        l.text.counterSeparator = sep;
    }
    if (has(op, "from")) l.text.counterFrom.base = static_cast<float>(number(op["from"], "from"));
    if (has(op, "to")) l.text.counterTo.base = static_cast<float>(number(op["to"], "to"));
    if (has(op, "count_duration")) {
        const double v = number(op["count_duration"], "count_duration");
        if (v < 0 || v > 3600) fail("\"count_duration\" must be 0 (the whole clip) to 3600 seconds");
        l.text.counterDuration = static_cast<float>(v);
    }
    if (has(op, "count_easing"))
        l.text.counterEasing = static_cast<CountEasing>(idOf(kCountEasingIds, op, "count_easing"));
}

// Letters coming in, leaving, moving; auto fit.
void Applier::setLetters(Layer& l, const json& op) {
    TextProps& t = l.text;
    if (has(op, "letter_in")) t.letterIn = static_cast<LetterAnim>(idOf(kLetterAnimIds, op, "letter_in"));
    if (has(op, "letter_out")) t.letterOut = static_cast<LetterAnim>(idOf(kLetterAnimIds, op, "letter_out"));
    if (has(op, "letter_loop")) t.letterLoop = static_cast<LetterLoop>(idOf(kLetterLoopIds, op, "letter_loop"));
    if (has(op, "letter_by")) t.letterUnit = static_cast<LetterUnit>(idOf(kLetterUnitIds, op, "letter_by"));
    if (has(op, "letter_order")) t.letterOrder = static_cast<LetterOrder>(idOf(kLetterOrderIds, op, "letter_order"));
    if (has(op, "letter_delay")) {
        const double v = number(op["letter_delay"], "letter_delay");
        if (v < 0 || v > 10) fail("\"letter_delay\" must be 0 to 10 seconds");
        t.letterDelay = static_cast<float>(v);
    }
    if (has(op, "letter_duration")) {
        const double v = number(op["letter_duration"], "letter_duration");
        if (v < 0.01 || v > 30) fail("\"letter_duration\" must be 0.01 to 30 seconds");
        t.letterDuration = static_cast<float>(v);
    }
    if (has(op, "fit_lines")) {
        const double v = number(op["fit_lines"], "fit_lines");
        if (v < 0 || v > 20 || v != std::floor(v)) fail("\"fit_lines\" must be 0 (off) to 20");
        t.fitLines = static_cast<int>(v);
    }
}

// A subtitle layer's captions: "captions" [{"start", "end", "text"}] or "srt" (SubRip text).
void Applier::setCaptions(Layer& l, const json& op) {
    std::vector<Caption> caps;
    if (has(op, "srt")) {
        caps = parseSrt(text(op["srt"], "srt"));
    } else {
        if (!op["captions"].is_array()) fail("\"captions\" must be a list of {\"start\", \"end\", \"text\"}");
        for (const json& c : op["captions"]) {
            if (!c.is_object()) fail("each caption is {\"start\": s, \"end\": s, \"text\": \"...\"}");
            Caption cap{number(field(c, "start"), "start"), number(field(c, "end"), "end"),
                        text(field(c, "text"), "text")};
            if (cap.end <= cap.start) fail("a caption must end after it starts (\"" + cap.text + "\")");
            caps.push_back(std::move(cap));
        }
    }
    std::stable_sort(caps.begin(), caps.end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
    for (size_t i = 1; i < caps.size(); ++i)
        caps[i - 1].end = std::min(caps[i - 1].end, caps[i].start);
    l.text.captions = std::move(caps);
}

// A background's fill ("solid", "linear", "radial", "image"), picture ("media": id) and fit.
void Applier::setBackground(Layer& l, const json& op) {
    BackgroundProps& b = l.background;
    if (has(op, "fill")) {
        const std::string fill = text(op["fill"], "fill");
        std::vector<std::string> ids;
        bool found = false;
        for (int i = 0; i < static_cast<int>(BackgroundFill::Count); ++i) {
            ids.push_back(backgroundFillId(static_cast<BackgroundFill>(i)));
            if (fill == ids.back()) b.fill = static_cast<BackgroundFill>(i), found = true;
        }
        if (!found) fail("unknown fill \"" + fill + "\"" + suggest(fill, ids));
    }
    if (has(op, "media")) {
        const auto id = static_cast<uint64_t>(number(op["media"], "media"));
        const MediaItem* m = c_.findMedia(id);
        if (!m || m->kind == MediaKind::Audio)
            fail("there is no picture or video with media id " + std::to_string(id) + " (media.import it first)");
        b.mediaId = id;
        if (!has(op, "fill")) b.fill = BackgroundFill::Image;
    }
    if (has(op, "fit")) {
        const std::string fit = text(op["fit"], "fit");
        std::vector<std::string> ids;
        bool found = false;
        for (int i = 0; i < static_cast<int>(BackgroundFit::Count); ++i) {
            ids.push_back(backgroundFitId(static_cast<BackgroundFit>(i)));
            if (fit == ids.back()) b.fit = static_cast<BackgroundFit>(i), found = true;
        }
        if (!found) fail("unknown fit \"" + fit + "\"" + suggest(fit, ids));
    }
}

json Applier::layerRemove(const json& op) {
    const uint64_t id = layer(op).id;
    c_.removeLayerTree(id);
    return json::object();
}

json Applier::layerSet(const json& op) {
    Layer& l = layer(op);
    const uint64_t id = l.id;
    auto requireType = [&](bool ok, const char* key, const char* what) {
        if (!ok)
            fail(std::string("\"") + key + "\" only applies to " + what + " layers (\"" + l.name + "\" is " +
                 typeName(l) + ")");
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
    if (has(op, "muted")) l.muted = boolean(op["muted"], "muted");
    if (has(op, "solo")) l.solo = boolean(op["solo"], "solo");
    if (has(op, "blend")) {
        if (l.type == LayerType::Camera) fail("cameras have no blending mode");
        l.blend = static_cast<BlendMode>(idOf(kBlendModeIds, op, "blend"));
    }
    if (has(op, "matte") || has(op, "matte_layer")) setMatte(l, op);
    if (has(op, "three_d")) {
        const bool on = boolean(op["three_d"], "three_d");
        if (on && (l.type == LayerType::Camera || l.type == LayerType::Background))
            fail(std::string("\"three_d\" does not apply to ") + typeName(l) + " layers (cameras always see in 3D)");
        l.threeD = on;
    }
    if (has(op, "label")) {
        const double v = number(op["label"], "label");
        if (v < 0 || v >= kLabelColorCount || v != std::floor(v)) fail("\"label\" must be 0 to 7");
        l.label = static_cast<int>(v);
    }
    if (has(op, "text"))
        requireType(l.type == LayerType::Text && !l.text.counter, "text", "text"),
            l.text.text = text(op["text"], "text");
    for (const char* key :
         {"decimals", "prefix", "suffix", "separator", "from", "to", "count_duration", "count_easing"})
        if (has(op, key)) requireType(l.type == LayerType::Text && l.text.counter, key, "counter");
    setCounter(l, op);
    for (const char* key : {"letter_in", "letter_out", "letter_loop", "letter_by", "letter_order", "letter_delay",
                            "letter_duration", "fit_lines"})
        if (has(op, key)) requireType(l.type == LayerType::Text, key, "text");
    setLetters(l, op);
    if (has(op, "fit_captions")) {
        requireType(l.type == LayerType::Text && l.text.subtitle, "fit_captions", "subtitle");
        if (boolean(op["fit_captions"], "fit_captions")) fitCaptions(l.text.captions);
    }
    if (has(op, "outline"))
        requireType(l.type == LayerType::Text, "outline", "text"),
            l.text.outlineEnabled = boolean(op["outline"], "outline");
    if (has(op, "box"))
        requireType(l.type == LayerType::Text, "box", "text"), l.text.boxEnabled = boolean(op["box"], "box");
    if (has(op, "captions") || has(op, "srt")) {
        requireType(l.type == LayerType::Text && l.text.subtitle, has(op, "srt") ? "srt" : "captions", "subtitle");
        setCaptions(l, op);
    }
    if (has(op, "font"))
        requireType(l.type == LayerType::Text, "font", "text"), l.text.fontFamily = text(op["font"], "font");
    if (has(op, "style"))
        requireType(l.type == LayerType::Text, "style", "text"), l.text.fontStyle = text(op["style"], "style");
    if (has(op, "align")) {
        requireType(l.type == LayerType::Text, "align", "text");
        const std::string a = text(op["align"], "align");
        int i = 0;
        while (i < 3 && a != kAlignNames[i])
            ++i;
        if (i == 3) fail("\"align\" must be left, center or right");
        l.text.align = static_cast<TextAlign>(i);
    }
    if (has(op, "shape")) {
        requireType(l.type == LayerType::Shape, "shape", "shape");
        const std::string s = text(op["shape"], "shape");
        const int kind = shapeKindOf(s);
        if (kind < 0)
            fail("unknown shape \"" + s + "\"" +
                 suggest(s, {"rectangle", "ellipse", "polygon", "star", "line", "path"}));
        // Becoming a path: its outline so far, as Bézier points to edit (unless one is given).
        if (kind == static_cast<int>(ShapeKind::Path) && l.shape.kind != ShapeKind::Path && !has(op, "path"))
            l.shape.path = shapeToBezier(l.shape, time(op));
        l.shape.kind = static_cast<ShapeKind>(kind);
    }
    if (has(op, "path")) {
        requireType(l.type == LayerType::Shape, "path", "shape");
        l.shape.path = pathOf(op["path"]);
        l.shape.kind = ShapeKind::Path;
    }
    if (has(op, "gradient")) {
        requireType(l.type == LayerType::Shape || l.type == LayerType::Text || l.type == LayerType::Plugin, "gradient",
                    "shape, text or plugin");
        const auto fill = static_cast<ShapeFill>(idOf(kShapeFillIds, op, "gradient"));
        (l.type == LayerType::Text ? l.text.fill : l.type == LayerType::Plugin ? l.plugin.fill : l.shape.fill) = fill;
    }
    if (has(op, "repeater"))
        requireType(l.type == LayerType::Shape, "repeater", "shape"),
            l.shape.repeater = boolean(op["repeater"], "repeater");
    if (has(op, "warp")) {
        requireType(l.type != LayerType::Camera, "warp", "layer with a picture");
        l.warp = boolean(op["warp"], "warp");
    }
    for (const char* key : {"flip_h", "flip_v"})
        if (has(op, key)) {
            requireType(l.type == LayerType::Media && l.media.hasVideo, key, "video or image");
            (key[5] == 'h' ? l.media.flipH : l.media.flipV) = boolean(op[key], key);
        }
    if (l.type == LayerType::Background && (has(op, "fill") || has(op, "media") || has(op, "fit")))
        setBackground(l, op);
    else if (has(op, "fill"))
        requireType(l.type == LayerType::Shape, "fill", "shape"), l.shape.fillEnabled = boolean(op["fill"], "fill");
    if (has(op, "stroke"))
        requireType(l.type == LayerType::Shape, "stroke", "shape"),
            l.shape.strokeEnabled = boolean(op["stroke"], "stroke");
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
            fail("the clip cannot start at " + std::to_string(l.inPoint + delta) +
                 " s: it would overlap another clip on its track");
        for (uint64_t m : moving)
            if (Layer* each = c_.findLayer(m)) each->shiftTime(delta);
    }
    if (has(op, "in")) c_.setInPoint(id, time(op, "in"));
    if (has(op, "out")) c_.setOutPoint(id, time(op, "out"));
    if (has(op, "track_of")) joinTrack(id, op);
    const Layer& now = *c_.findLayer(id);
    out["in"] = now.inPoint;
    out["out"] = now.outPoint;
    return out;
}

BezierPath Applier::pathOf(const json& v) {
    BezierPath p;
    std::string error;
    if (!pathFromJson(v, p, &error)) fail("\"path\": " + error);
    if (p.vertices.size() < 2) fail("\"path\" needs at least two vertices");
    if (p.vertices.size() > 10000) fail("\"path\" has too many vertices (10000 at most)");
    return p;
}

// "matte": "alpha" / "alpha_inverted" / "luma" / "luma_inverted" / "none", "matte_layer": the layer
// whose picture decides (beside this one; default: the one right above it, like After Effects).
void Applier::setMatte(Layer& l, const json& op) {
    const MatteMode mode =
        has(op, "matte") ? static_cast<MatteMode>(idOf(kMatteModeIds, op, "matte")) : MatteMode::Alpha;
    if (mode == MatteMode::None) {
        l.matte = MatteMode::None;
        l.matteLayer = 0;
        return;
    }
    if (l.type == LayerType::Camera) fail("cameras take no track matte");
    uint64_t matte = 0;
    if (has(op, "matte_layer")) {
        matte = layer(op, "matte_layer").id;
    } else {
        for (int i = c_.indexOf(l.id) - 1; i >= 0 && !matte; --i)
            if (c_.layers[i].parent == l.parent && c_.layers[i].type != LayerType::Camera) matte = c_.layers[i].id;
        if (!matte) fail("there is no layer above \"" + l.name + "\" to be its matte: give \"matte_layer\"");
    }
    const Layer* m = c_.findLayer(matte);
    if (m->id == l.id) fail("a layer cannot be its own matte");
    if (m->parent != l.parent) fail("the matte must be beside the layer (in the same compound, or both at the top)");
    if (m->type == LayerType::Camera) fail("a camera cannot be a matte");
    l.matte = mode;
    l.matteLayer = matte;
}

json Applier::layerMove(const json& op) {
    const Layer& l = layer(op);
    const std::string to = text(field(op, "to"), "to");
    const int delta = to == "top" ? -100000 : to == "bottom" ? 100000 : to == "up" ? -1 : to == "down" ? 1 : 0;
    if (!delta) fail("\"to\" must be top, bottom, up or down");
    c_.shiftTracks({l.track}, delta);
    return json::object();
}

json Applier::layerSplit(const json& op) {
    const Layer& l = layer(op);
    const double t = c_.snapToFrame(time(op));
    const uint64_t copy = c_.splitLayer(l.id, t);
    if (!copy)
        fail("\"time\" must be strictly inside the clip (" + std::to_string(l.inPoint) + " to " +
             std::to_string(l.outPoint) + " s)");
    return {{"layer", copy}};
}

json Applier::layerDuplicate(const json& op) {
    const Layer& l = layer(op);
    if (l.type == LayerType::Compound) fail("duplicating compounds is not available as an operation yet");
    Layer copy = l;
    copy.track = 0;
    copy.name = c_.uniqueLayerName(l.name);
    const int index = c_.indexOf(l.id);
    const Layer& added = c_.addLayer(std::move(copy), index);
    return {{"layer", added.id}, {"name", added.name}};
}

json Applier::queryComposition(const json&) {
    json media = json::array();
    for (const MediaItem& m : c_.media) {
        json mj = {{"media", m.id}, {"name", m.name}, {"kind", mediaKindLabel(m.kind)}, {"path", m.path}};
        if (!m.folder.empty()) mj["folder"] = m.folder;
        media.push_back(std::move(mj));
    }
    json out = {{"name", p_.name},
                {"width", c_.width},
                {"height", c_.height},
                {"fps", c_.fps},
                {"duration", c_.duration},
                {"max_duration", c_.maxDuration},
                {"background", hexOf(c_.background)},
                {"layers", c_.layers.size()},
                {"media", media}};
    if (!c_.mediaFolders.empty()) out["folders"] = c_.mediaFolders;
    if (!c_.markers.empty()) {
        json markers = json::array();
        for (const Marker& m : c_.markers)
            markers.push_back({{"time", m.time}, {"name", m.name}, {"color", m.color}});
        out["markers"] = markers;
    }
    return out;
}

json Applier::layerSummary(const Layer& l) {
    json j = {{"layer", l.id},   {"name", l.name},    {"type", typeName(l)},
              {"in", l.inPoint}, {"out", l.outPoint}, {"track", c_.trackRowOf(l.id)}};
    if (l.parent) j["parent"] = l.parent;
    if (!l.visible) j["visible"] = false;
    if (l.locked) j["locked"] = true;
    if (l.muted) j["muted"] = true;
    if (l.solo) j["solo"] = true;
    if (l.threeD) j["three_d"] = true;
    if (l.blend != BlendMode::Normal) j["blend"] = kBlendModeIds[static_cast<int>(l.blend)];
    if (l.matte != MatteMode::None)
        j["matte"] = {{"mode", kMatteModeIds[static_cast<int>(l.matte)]}, {"layer", l.matteLayer}};
    if (c_.isMatte(l.id)) j["is_matte"] = true;
    if (!l.masks.empty()) j["masks"] = l.masks.size();
    if (l.type == LayerType::Media && (l.media.speed != 1.0 || l.media.reversed))
        j["speed"] = round3(static_cast<float>(l.media.speed * 100.0));
    if (l.type == LayerType::Media && l.media.reversed) j["reversed"] = true;
    if (l.type == LayerType::Media && l.media.flipH) j["flip_h"] = true;
    if (l.type == LayerType::Media && l.media.flipV) j["flip_v"] = true;
    if (l.type == LayerType::Media && l.media.timeRemap) j["time_remap"] = true;
    if (l.text.subtitle && l.type == LayerType::Text) j["captions"] = l.text.captions.size();
    if (!l.effects.empty()) j["effects"] = l.effects.size();
    return j;
}

json Applier::queryLayers(const json&) {
    json list = json::array();
    for (const Layer& l : c_.layers)
        list.push_back(layerSummary(l));
    return list;
}

// Works an expression out without setting it: `expression` (default: the property's own) at
// `time` or each of `times`. Never fails for the expression's sake: `error` says what is wrong.
json Applier::queryExpression(const json& op) {
    Layer& l = layer(op);
    const PropertyRef p = property(l, op);
    const std::string e = has(op, "expression") ? text(op["expression"], "expression") : p.anim->expression;
    if (e.empty())
        fail("\"" + std::string(p.id) + "\" of \"" + l.name + "\" has no expression: give one in \"expression\"");
    std::vector<double> times;
    if (has(op, "times")) {
        if (!op["times"].is_array() || op["times"].empty() || op["times"].size() > 1000)
            fail("\"times\" must be a list of 1 to 1000 seconds");
        for (const json& x : op["times"]) {
            if (!x.is_number() || x.get<double>() < 0.0) fail("\"times\" must hold seconds (0 or more)");
            times.push_back(x.get<double>());
        }
    } else {
        times.push_back(time(op));
    }
    json values = json::array();
    for (double at : times) {
        std::vector<float> v;
        if (const std::string problem = evaluateExpressionFor(e, l, p, at, v); !problem.empty())
            return {{"ok", false}, {"error", problem}, {"time", at}};
        values.push_back(valueJson(p.kind, v));
    }
    json out = {{"ok", true}};
    if (has(op, "times"))
        out["values"] = values;
    else
        out["value"] = values[0];
    return out;
}

json Applier::queryLayer(const json& op) {
    Layer& l = layer(op);
    const double t = time(op);
    json j = layerSummary(l);
    if (l.type == LayerType::Text)
        j["text"] = {{"text", l.text.text},
                     {"font", l.text.fontFamily},
                     {"style", l.text.fontStyle},
                     {"align", kAlignNames[static_cast<int>(l.text.align)]},
                     {"outline", l.text.outlineEnabled},
                     {"box", l.text.boxEnabled},
                     {"gradient", kShapeFillIds[static_cast<int>(l.text.fill)]}};
    if (l.type == LayerType::Text && l.text.subtitle) {
        json caps = json::array();
        for (const Caption& c : l.text.captions)
            caps.push_back({{"start", c.start}, {"end", c.end}, {"text", c.text}});
        j["captions"] = std::move(caps);
    }
    if (l.type == LayerType::Shape) {
        j["shape"] = {{"fill", l.shape.fillEnabled},
                      {"stroke", l.shape.strokeEnabled},
                      {"gradient", kShapeFillIds[static_cast<int>(l.shape.fill)]},
                      {"repeater", l.shape.repeater}};
        if (l.shape.kind == ShapeKind::Path) j["shape"]["path"] = pathToJson(l.shape.path);
    }
    if (!l.masks.empty()) {
        json masks = json::array();
        for (size_t i = 0; i < l.masks.size(); ++i) {
            const Mask& m = l.masks[i];
            masks.push_back({{"index", i},
                             {"name", m.name},
                             {"mode", kMaskModeIds[static_cast<int>(m.mode)]},
                             {"inverted", m.inverted},
                             {"enabled", m.enabled},
                             {"path", pathToJson(m.path)}});
            if (!m.pathKeys.empty()) {
                json keys = json::array();
                for (const Mask::PathKey& k : m.pathKeys)
                    keys.push_back({{"time", k.time}, {"path", pathToJson(k.path)}});
                masks.back()["path_keys"] = std::move(keys);
            }
        }
        j["mask_list"] = masks;
    }
    if (l.type == LayerType::Media) j["media"] = l.media.mediaId;
    if (l.type == LayerType::Background)
        j["background"] = {{"fill", backgroundFillId(l.background.fill)},
                           {"media", l.background.mediaId},
                           {"fit", backgroundFitId(l.background.fit)}};
    if (l.plugin.hasRows) {
        json rows = json::array();
        for (const DataRow& r : l.plugin.rows)
            rows.push_back({{"label", r.label}, {"value", r.value}, {"color", hexOf(r.color)}});
        j["rows"] = rows;
    }
    if (l.plugin.hasTable) j["table"] = {{"header", l.plugin.table.header}, {"rows", l.plugin.table.rows}};
    if (l.type == LayerType::Plugin) { // the parameters that are not properties (param.set)
        json params = json::object();
        for (const PluginParam& pp : l.plugin.params)
            switch (pp.kind) {
            case PluginParamKind::Toggle: params[pp.id] = pp.toggle; break;
            case PluginParamKind::Choice:
                params[pp.id] = {
                    {"value", pp.choice < static_cast<int>(pp.choices.size()) ? pp.choices[pp.choice] : ""},
                    {"choices", pp.choices}};
                break;
            case PluginParamKind::Text: params[pp.id] = pp.text; break;
            case PluginParamKind::File: params[pp.id] = {{"file", pp.text}, {"accept", pp.accept}}; break;
            default: break;
            }
        if (!params.empty()) j["params"] = params;
    }
    json props = json::object();
    for (const PropertyRef& p : l.properties()) {
        json pj = {{"value", valueJson(p.kind, propertyValue(p, t))}};
        if (p.anim->hasExpression()) {
            pj["expression"] = p.anim->expression;
            if (const std::string problem = expressionError(*p.anim); !problem.empty())
                pj["expression_error"] = problem;
        }
        if (p.anim->isAnimated()) {
            json keys = json::array();
            for (int k = 0; k < p.anim->keyCount(); ++k) {
                const double kt = p.anim->keyTime(k);
                float kv[4] = {0, 0, 0, 0}; // what the key holds, whatever an expression makes of it
                p.anim->keyedValue(kt, kv);
                keys.push_back({{"time", kt},
                                {"value", valueJson(p.kind, std::vector<float>(kv, kv + p.anim->components()))},
                                {"easing", easingId(p.anim->keyEasing(k))}});
            }
            pj["keys"] = keys;
        }
        props[p.id] = pj;
    }
    j["props"] = props;
    json effects = json::array();
    for (size_t i = 0; i < l.effects.size(); ++i) {
        const Effect& e = l.effects[i];
        json ej = {{"index", i}, {"effect", effectId(e.type)}, {"enabled", e.enabled}};
        if (e.type == EffectType::Lut) ej["file"] = e.file;
        if (e.type == EffectType::Curves) {
            json curves = json::object();
            for (int c = 0; c < 4; ++c) {
                json pts = json::array();
                for (const Vec2& p : e.curves[c])
                    pts.push_back({p.x, p.y});
                curves[curveId(c)] = std::move(pts);
            }
            ej["curves"] = std::move(curves);
        }
        effects.push_back(std::move(ej));
    }
    j["effect_list"] = effects;
    if (l.type == LayerType::Media && l.media.hasAudio) {
        json audio = json::array();
        for (size_t i = 0; i < l.audioEffects.size(); ++i) {
            const AudioEffect& e = l.audioEffects[i];
            json values = json::object();
            for (const AudioEffectParam& p : audioEffectParams(e.type))
                values[p.id] = audioEffectValue(e, p.index);
            audio.push_back(
                {{"index", i}, {"effect", audioEffectId(e.type)}, {"enabled", e.enabled}, {"values", values}});
        }
        j["audio_effects"] = std::move(audio);
    }
    return j;
}

} // namespace opsimpl

using opsimpl::Applier;
using opsimpl::OpError;

std::vector<std::string> operationNames() {
    std::vector<std::string> names;
    for (const auto& [name, fn] : Applier::ops()) names.push_back(name);
    return names;
}

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
    const ExpressionScope expressions(work.comp); // what expressions read: the copy being edited
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
