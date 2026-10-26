#include "core/Lottie.h"

#include "core/Easing.h"
#include "core/Geometry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <type_traits>

namespace bemotion {

namespace {

using json = nlohmann::json;

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string base64(const std::string& bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) | static_cast<uint8_t>(bytes[i + 2]);
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += table[(n >> 6) & 63], out += table[n & 63];
    }
    if (i + 1 == bytes.size()) {
        const uint32_t n = static_cast<uint8_t>(bytes[i]) << 16;
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += "==";
    } else if (i + 2 == bytes.size()) {
        const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8);
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += table[(n >> 6) & 63], out += '=';
    }
    return out;
}

json xy(Vec2 v) { return json::array({v.x, v.y}); }
json xyz(Vec2 v, float z = 0.0f) { return json::array({v.x, v.y, z}); }

json shapeJson(const BezierPath& p) {
    json in = json::array(), out = json::array(), v = json::array();
    for (const PathVertex& pv : p.vertices) {
        in.push_back(xy(pv.in));
        out.push_back(xy(pv.out));
        v.push_back(xy(pv.point));
    }
    return {{"i", in}, {"o", out}, {"v", v}, {"c", p.closed}};
}

class Writer {
public:
    Writer(const Composition& comp, const LottieOptions& options, std::vector<std::string>* leftOut)
        : c_(comp), options_(options), leftOut_(leftOut), fps_(comp.fps > 0 ? comp.fps : 30.0) {}

    json run() {
        const double frames = std::max(1.0, std::round(c_.duration * fps_));
        json root = {{"v", "5.7.4"}, {"fr", fps_}, {"ip", 0}, {"op", frames}, {"w", c_.width}, {"h", c_.height},
                     {"nm", "bemotion"}, {"ddd", 0}, {"assets", json::array()}, {"layers", json::array()}};
        // Lottie indexes layers from 1; a compound's children name it as their parent.
        for (size_t i = 0; i < c_.layers.size(); ++i) index_[c_.layers[i].id] = static_cast<int>(i) + 1;
        for (const Layer& l : c_.layers)
            if (std::optional<json> layer = layerJson(l)) root["layers"].push_back(std::move(*layer));
        root["assets"] = assets_;
        if (!fonts_.empty()) {
            json list = json::array();
            for (const auto& [name, f] : fonts_) list.push_back({{"fName", name}, {"fFamily", f.first}, {"fStyle", f.second}, {"ascent", 75}});
            root["fonts"] = {{"list", list}};
        }
        json markers = json::array();
        for (const Marker& m : c_.markers) markers.push_back({{"tm", m.time * fps_}, {"cm", m.name}, {"dr", 0}});
        root["markers"] = markers;
        return root;
    }

private:
    void note(const std::string& what) {
        if (leftOut_ && seen_.insert(what).second) leftOut_->push_back(what);
    }
    double frame(double t) const { return t * fps_; }

    // --- properties ----------------------------------------------------------------------------

    // A property as Lottie writes it: a value, or keyframes with their easing (as cubic Bézier
    // handles). Expressions, Elastic and Bounce are sampled every frame between `from` and `to`.
    template <typename T, typename Conv>
    json prop(const Animated<T>& a, double from, double to, Conv conv, bool spatial = false) {
        auto value = [&](const T& v) {
            json j = conv(v);
            return j.is_array() ? j : json::array({j});
        };
        if (a.hasExpression()) {
            json keys = json::array();
            const int f0 = static_cast<int>(std::floor(frame(from))), f1 = static_cast<int>(std::ceil(frame(to)));
            for (int f = f0; f <= f1; ++f)
                keys.push_back({{"t", f}, {"s", value(a.valueAt(f / fps_))}, {"o", {{"x", {0}}, {"y", {0}}}}, {"i", {{"x", {1}}, {"y", {1}}}}});
            return {{"a", 1}, {"k", keys}};
        }
        if (a.keys.empty()) return {{"a", 0}, {"k", conv(a.base)}};
        if (a.keys.size() == 1) return {{"a", 0}, {"k", conv(a.keys[0].value)}};
        json keys = json::array();
        for (size_t i = 0; i < a.keys.size(); ++i) {
            const Keyframe<T>& k = a.keys[i];
            json key = {{"t", frame(k.time)}, {"s", value(k.value)}};
            if (i + 1 == a.keys.size()) {
                keys.push_back(key);
                break;
            }
            if (k.easing == Easing::Hold) {
                key["h"] = 1;
                keys.push_back(key);
                continue;
            }
            if (k.easing == Easing::Elastic || k.easing == Easing::Bounce) { // no curve for these: every frame
                const double t1 = a.keys[i + 1].time;
                for (double f = std::ceil(frame(k.time)); f < frame(t1) - 1e-6; f += 1.0) {
                    const double t = std::max(k.time, f / fps_);
                    keys.push_back({{"t", frame(t)}, {"s", value(a.valueAt(t))}, {"o", {{"x", {0}}, {"y", {0}}}}, {"i", {{"x", {1}}, {"y", {1}}}}});
                }
                continue;
            }
            const BezierEase b = k.easing == Easing::Custom ? k.curve : bezierFor(k.easing);
            key["o"] = {{"x", {b.x1}}, {"y", {b.y1}}};
            key["i"] = {{"x", {b.x2}}, {"y", {b.y2}}};
            if constexpr (std::is_same_v<T, Vec2>) {
                if (spatial) { // the motion path between the two positions
                    Vec2 in0, out0, in1, out1;
                    pathTangents(a.keys, i, in0, out0);
                    pathTangents(a.keys, i + 1, in1, out1);
                    key["to"] = xyz(out0);
                    key["ti"] = xyz(in1);
                }
            }
            keys.push_back(key);
        }
        return {{"a", 1}, {"k", keys}};
    }

    static json number(float v) { return v; }
    static json percent2(Vec2 v) { return xyz(v, 100.0f); }
    static json point(Vec2 v) { return xyz(v); }
    static json color3(Color c) { return json::array({c.r, c.g, c.b, 1.0f}); }

    json transform(const Layer& l, Vec2 anchorShift = {0, 0}) {
        const TransformProps& t = l.transform;
        auto anchor = [anchorShift](Vec2 v) { return xyz(v + anchorShift); };
        return {{"a", prop(t.anchor, l.inPoint, l.outPoint, anchor)},
                {"p", prop(t.position, l.inPoint, l.outPoint, point, true)},
                {"s", prop(t.scale, l.inPoint, l.outPoint, percent2)},
                {"r", prop(t.rotation, l.inPoint, l.outPoint, number)},
                {"o", prop(t.opacity, l.inPoint, l.outPoint, number)}};
    }

    json masks(const Layer& l, Vec2 shift) {
        json out = json::array();
        for (const Mask& m : l.masks) {
            if (!m.enabled || m.path.empty()) continue;
            BezierPath p = m.path;
            for (PathVertex& v : p.vertices) v.point = v.point + shift;
            static const char* modes[] = {"a", "s", "i", "f"};
            out.push_back({{"inv", m.inverted}, {"mode", modes[static_cast<int>(m.mode)]}, {"pt", {{"a", 0}, {"k", shapeJson(p)}}},
                           {"o", prop(m.opacity, l.inPoint, l.outPoint, number)}, {"x", prop(m.expansion, l.inPoint, l.outPoint, number)},
                           {"nm", m.name}});
            if (m.feather.base != 0.0f || m.feather.isAnimated()) note("Mask feather (Lottie players do not soften mask edges)");
        }
        return out;
    }

    // --- layers --------------------------------------------------------------------------------

    std::optional<json> layerJson(const Layer& l) {
        json j = {{"ddd", 0}, {"ind", index_[l.id]}, {"nm", l.name}, {"sr", 1}, {"ao", 0}, {"st", 0},
                  {"ip", frame(l.inPoint)}, {"op", frame(l.outPoint)}, {"bm", static_cast<int>(l.blend)}};
        if (l.parent) j["parent"] = index_[l.parent];
        if (!l.visible) j["hd"] = true;
        if (l.threeD) note("3D layers (exported flat)");
        if (!l.effects.empty()) note("Effects (" + l.name + " and others)");
        if (l.matte != MatteMode::None) note("Track mattes");
        Vec2 shift{0, 0}; // Lottie's pictures and solids start at their top-left corner, ours at their middle
        switch (l.type) {
        case LayerType::Shape:
            j["ty"] = 4;
            j["shapes"] = json::array({shapeGroup(l)});
            break;
        case LayerType::Compound:
            j["ty"] = 3; // a null: the children follow its transform
            if (l.transform.opacity.base < 100.0f || l.transform.opacity.isAnimated()) note("A compound's opacity (its layers keep their own)");
            break;
        case LayerType::Text:
            if (!textLayer(l, j)) return std::nullopt;
            break;
        case LayerType::Media: {
            const MediaItem* item = c_.findMedia(l.media.mediaId);
            if (!item || item->kind != MediaKind::Image) {
                if (item && item->kind == MediaKind::Video) note("Videos (Lottie has no video)");
                if (item && item->kind == MediaKind::Audio) note("Sound");
                return std::nullopt;
            }
            const std::string asset = imageAsset(*item);
            if (asset.empty()) return std::nullopt;
            j["ty"] = 2;
            j["refId"] = asset;
            shift = {item->width * 0.5f, item->height * 0.5f};
            break;
        }
        case LayerType::Background:
            if (!background(l, j)) return std::nullopt;
            j["ks"] = {{"a", {{"a", 0}, {"k", {0, 0, 0}}}}, {"p", {{"a", 0}, {"k", {0, 0, 0}}}}, {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
                       {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}};
            return j;
        case LayerType::Camera: note("Cameras"); return std::nullopt;
        case LayerType::Plugin: note("Plugin objects (charts, 3D models...)"); return std::nullopt;
        }
        j["ks"] = transform(l, shift);
        if (const json m = masks(l, shift); !m.empty()) {
            j["hasMask"] = true;
            j["masksProperties"] = m;
        }
        return j;
    }

    json shapeGroup(const Layer& l) {
        const ShapeProps& s = l.shape;
        const double from = l.inPoint, to = l.outPoint;
        json items = json::array();
        // The outline.
        switch (s.kind) {
        case ShapeKind::Rectangle:
            items.push_back({{"ty", "rc"}, {"d", 1}, {"p", {{"a", 0}, {"k", {0, 0}}}}, {"s", prop(s.size, from, to, xy)},
                             {"r", prop(s.roundness, from, to, number)}, {"nm", "Rectangle"}});
            break;
        case ShapeKind::Ellipse:
            items.push_back({{"ty", "el"}, {"d", 1}, {"p", {{"a", 0}, {"k", {0, 0}}}}, {"s", prop(s.size, from, to, xy)}, {"nm", "Ellipse"}});
            break;
        case ShapeKind::Path: items.push_back({{"ty", "sh"}, {"d", 1}, {"ks", {{"a", 0}, {"k", shapeJson(s.path)}}}, {"nm", "Path"}}); break;
        default: { // polygon, star, line: their outline (every frame while it changes)
            json ks;
            const bool moving = s.size.isAnimated() || s.size.hasExpression() || s.sides.isAnimated() || s.innerRadius.isAnimated() ||
                                s.sides.hasExpression() || s.innerRadius.hasExpression();
            if (!moving) {
                ks = {{"a", 0}, {"k", shapeJson(shapeToBezier(s, from))}};
            } else {
                json keys = json::array();
                for (int f = static_cast<int>(std::floor(frame(from))); f <= static_cast<int>(std::ceil(frame(to))); ++f)
                    keys.push_back({{"t", f}, {"s", json::array({shapeJson(shapeToBezier(s, f / fps_))})}, {"o", {{"x", {0}}, {"y", {0}}}},
                                    {"i", {{"x", {1}}, {"y", {1}}}}});
                ks = {{"a", 1}, {"k", keys}};
            }
            items.push_back({{"ty", "sh"}, {"d", 1}, {"ks", ks}, {"nm", "Outline"}});
        }
        }
        // Trim (draw on), when it cuts anything.
        if (s.trimStart.isAnimated() || s.trimEnd.isAnimated() || s.trimStart.base > 0.0f || s.trimEnd.base < 100.0f || s.trimStart.hasExpression() ||
            s.trimEnd.hasExpression())
            items.push_back({{"ty", "tm"}, {"s", prop(s.trimStart, from, to, number)}, {"e", prop(s.trimEnd, from, to, number)},
                             {"o", {{"a", 0}, {"k", 0}}}, {"m", 1}, {"nm", "Trim"}});
        // The stroke above the fill (as drawn).
        const bool line = s.kind == ShapeKind::Line || (s.kind == ShapeKind::Path && !s.path.closed);
        if (s.strokeEnabled) {
            json st = {{"ty", "st"}, {"c", prop(s.strokeColor, from, to, color3)}, {"o", alphaOf(s.strokeColor, from, to)},
                       {"w", prop(s.strokeWidth, from, to, number)}, {"lc", 2}, {"lj", 2}, {"ml", 4}, {"nm", "Stroke"}};
            if (s.dash.base > 0.0f || s.dash.isAnimated())
                st["d"] = json::array({{{"n", "d"}, {"nm", "dash"}, {"v", prop(s.dash, from, to, number)}},
                                       {{"n", "g"}, {"nm", "gap"}, {"v", prop(s.dashGap, from, to, number)}},
                                       {{"n", "o"}, {"nm", "offset"}, {"v", prop(s.dashOffset, from, to, number)}}});
            items.push_back(st);
        }
        if (s.fillEnabled && !line) items.push_back(fill(s, from, to));
        if (s.repeater)
            items.push_back({{"ty", "rp"}, {"c", prop(s.repeatCopies, from, to, number)}, {"o", {{"a", 0}, {"k", 0}}}, {"m", 1},
                             {"tr", {{"ty", "tr"}, {"a", {{"a", 0}, {"k", {0, 0}}}}, {"p", prop(s.repeatOffset, from, to, xy)},
                                     {"s", prop(s.repeatScale, from, to, xy)}, {"r", prop(s.repeatRotation, from, to, number)},
                                     {"so", {{"a", 0}, {"k", 100}}}, {"eo", prop(s.repeatEndOpacity, from, to, number)}}},
                             {"nm", "Repeater"}});
        items.push_back({{"ty", "tr"}, {"a", {{"a", 0}, {"k", {0, 0}}}}, {"p", {{"a", 0}, {"k", {0, 0}}}}, {"s", {{"a", 0}, {"k", {100, 100}}}},
                         {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}});
        return {{"ty", "gr"}, {"it", items}, {"nm", l.name}};
    }

    // A color's alpha as Lottie's separate opacity (percent).
    json alphaOf(const AnimColor& c, double from, double to) {
        return prop(c, from, to, [](Color v) { return json(v.a * 100.0f); });
    }

    json fill(const ShapeProps& s, double from, double to) {
        if (s.fill == ShapeFill::Solid)
            return {{"ty", "fl"}, {"c", prop(s.fillColor, from, to, color3)}, {"o", alphaOf(s.fillColor, from, to)}, {"r", 1}, {"nm", "Fill"}};
        // A gradient across the shape's box, from its first color to its second.
        const Vec2 size = s.kind == ShapeKind::Path ? [&] {
            Vec2 mn, mx;
            return bezierBounds(s.path, mn, mx) ? mx - mn : Vec2{100, 100};
        }()
                                                    : s.size.valueAt(from);
        const Color a = s.fillColor.valueAt(from), b = s.fillColor2.valueAt(from);
        if (s.fillColor.isAnimated() || s.fillColor2.isAnimated()) note("Animated gradient colors (their first value is kept)");
        const json stops = {{"p", 2}, {"k", {{"a", 0}, {"k", {0, a.r, a.g, a.b, 1, b.r, b.g, b.b, 0, a.a, 1, b.a}}}}};
        json g = {{"ty", "gf"}, {"o", {{"a", 0}, {"k", 100}}}, {"r", 1}, {"g", stops}, {"nm", "Gradient"}};
        if (s.fill == ShapeFill::Linear) {
            const float angle = s.fillAngle.valueAt(from) * 3.14159265f / 180.0f;
            const Vec2 dir{std::cos(angle), std::sin(angle)};
            const float half = (std::fabs(size.x * dir.x) + std::fabs(size.y * dir.y)) * 0.5f;
            g["t"] = 1;
            g["s"] = {{"a", 0}, {"k", xy(dir * -half)}};
            g["e"] = {{"a", 0}, {"k", xy(dir * half)}};
        } else {
            g["t"] = 2;
            g["s"] = {{"a", 0}, {"k", {0, 0}}};
            g["e"] = {{"a", 0}, {"k", {std::max(size.x, size.y) * 0.5f, 0}}};
            g["h"] = {{"a", 0}, {"k", 0}};
            g["a"] = {{"a", 0}, {"k", 0}};
        }
        return g;
    }

    bool textLayer(const Layer& l, json& j) {
        const TextProps& t = l.text;
        if (t.subtitle || t.counter) note("Subtitles and counters (as their text at the clip's start)");
        if (t.letterIn != LetterAnim::None || t.letterOut != LetterAnim::None || t.letterLoop != LetterLoop::None) note("Letter animations");
        if (t.outlineEnabled || t.boxEnabled) note("Text outlines and boxes");
        std::string fontName = t.fontFamily + "-" + t.fontStyle;
        fontName.erase(std::remove(fontName.begin(), fontName.end(), ' '), fontName.end());
        fonts_[fontName] = {t.fontFamily, t.fontStyle};
        const float size = t.fontSize.valueAt(l.inPoint);
        std::string text = t.text;
        std::replace(text.begin(), text.end(), '\n', '\r'); // Lottie's line breaks
        const Color fc = t.color.valueAt(l.inPoint);
        const int justify = t.align == TextAlign::Left ? 0 : t.align == TextAlign::Right ? 1 : 2;
        const json doc = {{"s", size}, {"f", fontName}, {"t", text}, {"ca", 0}, {"j", justify}, {"tr", t.tracking.valueAt(l.inPoint) / std::max(1.0f, size) * 1000.0f},
                          {"lh", size * t.lineHeight.valueAt(l.inPoint) / 100.0f}, {"ls", 0}, {"fc", {fc.r, fc.g, fc.b}}};
        j["ty"] = 5;
        j["t"] = {{"d", {{"k", json::array({{{"s", doc}, {"t", 0}}})}}}, {"p", json::object()}, {"m", {{"g", 1}, {"a", {{"a", 0}, {"k", {0, 0}}}}}}, {"a", json::array()}};
        // Lottie puts the first line's baseline at the layer's origin; ours is the text's middle.
        const int lines = static_cast<int>(std::count(text.begin(), text.end(), '\r')) + 1;
        const float lineHeight = size * t.lineHeight.valueAt(l.inPoint) / 100.0f;
        j["ks"] = transform(l, {0, -(lineHeight * (lines - 1) * 0.5f + size * 0.35f)});
        return true;
    }

    bool background(const Layer& l, json& j) {
        const BackgroundProps& b = l.background;
        if (b.fill == BackgroundFill::Image) {
            note("Picture backgrounds");
            return false;
        }
        if (b.fill == BackgroundFill::Solid && !b.color.isAnimated()) {
            const Color c = b.color.valueAt(l.inPoint);
            char hex[8];
            std::snprintf(hex, sizeof hex, "#%02x%02x%02x", static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)),
                          static_cast<int>(std::lround(c.b * 255)));
            j["ty"] = 1;
            j["sc"] = hex;
            j["sw"] = c_.width;
            j["sh"] = c_.height;
            return true;
        }
        // A gradient (or a changing color): a rectangle over the whole frame.
        ShapeProps s;
        s.kind = ShapeKind::Rectangle;
        s.size.base = {static_cast<float>(c_.width), static_cast<float>(c_.height)};
        s.fill = b.fill == BackgroundFill::Radial ? ShapeFill::Radial : b.fill == BackgroundFill::Linear ? ShapeFill::Linear : ShapeFill::Solid;
        s.fillColor = b.color;
        s.fillColor2 = b.color2;
        s.fillAngle = b.angle;
        json items = json::array({{{"ty", "rc"}, {"d", 1}, {"p", {{"a", 0}, {"k", {c_.width * 0.5f, c_.height * 0.5f}}}}, {"s", {{"a", 0}, {"k", xy(s.size.base)}}},
                                   {"r", {{"a", 0}, {"k", 0}}}},
                                  fill(s, l.inPoint, l.outPoint)});
        json& f = items.back();
        if (f.contains("s") && f["ty"] == "gf") { // around the frame's middle
            const auto shiftPoint = [&](json& p) { p["k"] = {p["k"][0].get<float>() + c_.width * 0.5f, p["k"][1].get<float>() + c_.height * 0.5f}; };
            shiftPoint(f["s"]);
            shiftPoint(f["e"]);
        }
        items.push_back({{"ty", "tr"}, {"a", {{"a", 0}, {"k", {0, 0}}}}, {"p", {{"a", 0}, {"k", {0, 0}}}}, {"s", {{"a", 0}, {"k", {100, 100}}}},
                         {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}});
        j["ty"] = 4;
        j["shapes"] = json::array({{{"ty", "gr"}, {"it", items}, {"nm", "Background"}}});
        return true;
    }

    std::string imageAsset(const MediaItem& item) {
        const std::string id = "image_" + std::to_string(item.id);
        if (embedded_.count(id)) return id;
        const std::string bytes = options_.readFile ? options_.readFile(item) : std::string();
        if (bytes.empty()) {
            note("Pictures whose file could not be read");
            return {};
        }
        std::string ext = pathOf(item.path).extension().string();
        for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const std::string mime = ext == ".png" ? "image/png" : ext == ".gif" ? "image/gif" : ext == ".bmp" ? "image/bmp" : "image/jpeg";
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif") note("Pictures other than PNG, JPEG and GIF may not show in every player");
        assets_.push_back({{"id", id}, {"w", item.width}, {"h", item.height}, {"u", ""}, {"p", "data:" + mime + ";base64," + base64(bytes)}, {"e", 1}});
        embedded_.insert(id);
        return id;
    }

    const Composition& c_;
    const LottieOptions& options_;
    std::vector<std::string>* leftOut_;
    double fps_;
    std::map<uint64_t, int> index_;
    json assets_ = json::array();
    std::set<std::string> embedded_, seen_;
    std::map<std::string, std::pair<std::string, std::string>> fonts_; // name -> family, style
};

} // namespace

std::string toLottie(const Composition& comp, const LottieOptions& options, std::vector<std::string>* leftOut) {
    return Writer(comp, options, leftOut).run().dump();
}

bool exportLottie(const Composition& comp, const std::filesystem::path& file, std::vector<std::string>* leftOut, std::string* error) {
    LottieOptions options;
    options.readFile = [](const MediaItem& item) {
        std::ifstream in(pathOf(item.path), std::ios::binary);
        std::ostringstream bytes;
        bytes << in.rdbuf();
        return in ? bytes.str() : std::string();
    };
    const std::string text = toLottie(comp, options, leftOut);
    std::ofstream out(file, std::ios::binary);
    out << text;
    if (!out) {
        if (error) *error = "The file could not be written.";
        return false;
    }
    return true;
}

} // namespace bemotion
