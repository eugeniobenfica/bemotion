#include "core/Describe.h"

#include "core/Expressions.h"
#include "core/Ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace bemotion {

namespace {

std::string fmt(const char* f, double a) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, a);
    return buf;
}

std::string seconds(double t) { return fmt("%.2f", t); }

std::string hex(Color c) {
    char buf[16];
    auto byte = [](float v) { return static_cast<int>(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255)); };
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

std::string inQuotes(const std::string& s, size_t max = 40) {
    std::string out = s.size() > max ? s.substr(0, max) + "..." : s;
    for (char& ch : out)
        if (ch == '\n') ch = ' ';
    return "\"" + out + "\"";
}

void describeLayer(std::string& out, Composition& c, Layer& l, int depth) {
    std::string line(2 + depth * 2, ' ');
    line += std::to_string(l.id) + " " + inQuotes(l.name) + " " + layerTypeName(l);
    if (l.type == LayerType::Text) line += " " + inQuotes(l.text.text, 30);
    if (l.type == LayerType::Media)
        if (const MediaItem* m = c.findMedia(l.media.mediaId)) line += " " + inQuotes(m->name);
    line += "  " + seconds(l.inPoint) + "-" + seconds(l.outPoint) + " s";
    const Vec2 pos = l.transform.position.valueAt(l.inPoint);
    if (l.type != LayerType::Camera) line += "  at (" + fmt("%.0f", pos.x) + ", " + fmt("%.0f", pos.y) + ")";
    if (l.threeD || l.type == LayerType::Camera) line += "  z " + fmt("%.0f", l.transform.depth.valueAt(l.inPoint)) + (l.threeD ? " (3D)" : "");
    if (!l.visible) line += "  hidden";
    if (l.locked) line += "  locked";
    if (l.solo) line += "  solo";
    if (l.muted) line += "  muted";
    if (l.type == LayerType::Media && l.media.timeRemap) line += l.media.remap.isAnimated() ? "  time remap" : "  frozen frame";
    else if (l.type == LayerType::Media && (l.media.speed != 1.0 || l.media.reversed))
        line += "  speed " + fmt("%g", l.media.speed * 100.0) + "%" + (l.media.reversed ? " reversed" : "");
    if (l.blend != BlendMode::Normal) line += "  blend " + std::string(kBlendModeIds[static_cast<int>(l.blend)]);
    if (l.matte != MatteMode::None) line += "  matte " + std::string(kMatteModeIds[static_cast<int>(l.matte)]) + " from " + std::to_string(l.matteLayer);
    if (c.isMatte(l.id)) line += "  (a track matte)";
    if (l.type == LayerType::Shape && l.shape.kind == ShapeKind::Path)
        line += "  " + std::to_string(l.shape.path.vertices.size()) + " points" + (l.shape.path.closed ? "" : " open");
    if (!l.masks.empty()) {
        std::string masks;
        for (size_t i = 0; i < l.masks.size(); ++i)
            masks += (i ? ", " : "") + std::to_string(i) + ":" + kMaskModeIds[static_cast<int>(l.masks[i].mode)] + (l.masks[i].inverted ? " inverted" : "") +
                     (l.masks[i].enabled ? "" : " (off)");
        line += "\n" + std::string(6 + depth * 2, ' ') + "masks: " + masks;
    }
    // What moves: property, keyframe count and span.
    std::string animated;
    for (const PropertyRef& p : l.properties()) {
        if (!p.anim->isAnimated()) continue;
        const int n = p.anim->keyCount();
        animated += (animated.empty() ? "" : ", ") + std::string(p.id) + " " + std::to_string(n) + (n == 1 ? " key" : " keys");
        if (n > 1) animated += " " + seconds(p.anim->keyTime(0)) + "-" + seconds(p.anim->keyTime(n - 1)) + " s";
    }
    if (!animated.empty()) line += "\n" + std::string(6 + depth * 2, ' ') + "animated: " + animated;
    // What a formula drives (core/Expressions.h).
    for (const PropertyRef& p : l.properties())
        if (p.anim->hasExpression()) {
            std::string e = p.anim->expression;
            std::replace(e.begin(), e.end(), '\n', ' ');
            if (e.size() > 80) e = e.substr(0, 77) + "...";
            line += "\n" + std::string(6 + depth * 2, ' ') + "expression: " + p.id + " = " + e;
        }
    if (!l.effects.empty()) {
        std::string fx;
        for (size_t i = 0; i < l.effects.size(); ++i)
            fx += (i ? ", " : "") + std::to_string(i) + ":" + effectId(l.effects[i].type) + (l.effects[i].enabled ? "" : " (off)");
        line += "\n" + std::string(6 + depth * 2, ' ') + "effects: " + fx;
    }
    out += line + "\n";
    // A compound's contents, indented under it.
    for (Layer& child : c.layers)
        if (child.parent == l.id) describeLayer(out, c, child, depth + 1);
}

} // namespace

std::string describeProject(const Project& project) {
    Project p = project; // properties() needs mutable layers
    Composition& c = p.comp;
    const ExpressionScope expressions(c); // positions as they render
    std::string out = "Project " + inQuotes(p.name) + ": " + std::to_string(c.width) + "x" + std::to_string(c.height) + ", " +
                      fmt("%g", c.fps) + " fps, " + seconds(c.duration) + " s, background " + hex(c.background) + "\n";
    out += "Layers, top first (id \"name\" type, time, position; refer to layers by id or name):\n";
    if (c.layers.empty()) out += "  (none)\n";
    for (Layer& l : c.layers)
        if (!l.parent || !c.findLayer(l.parent)) describeLayer(out, c, l, 0);
    if (!c.markers.empty()) {
        out += "Markers:";
        for (const Marker& m : c.markers) out += " " + seconds(m.time) + " s" + (m.name.empty() ? "" : " " + inQuotes(m.name)) + ";";
        out.back() = '\n';
    }
    if (!c.media.empty()) {
        out += "Media:\n";
        for (const MediaItem& m : c.media) {
            out += "  " + std::to_string(m.id) + " " + inQuotes(m.name) + " " + mediaKindLabel(m.kind);
            if (m.width > 0) out += " " + std::to_string(m.width) + "x" + std::to_string(m.height);
            if (m.duration > 0) out += " " + seconds(m.duration) + " s";
            out += "\n";
        }
    }
    return out;
}

} // namespace bemotion
