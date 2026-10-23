#include "render/PluginCanvas.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"

#include <algorithm>
#include <cmath>

namespace bemotion::render {

namespace {

SkPaint fillPaint(Color c) {
    SkPaint p(SkColor4f{c.r, c.g, c.b, std::clamp(c.a, 0.0f, 1.0f)});
    p.setAntiAlias(true);
    return p;
}

SkPaint strokePaint(Color c, float width) {
    SkPaint p = fillPaint(c);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(width);
    p.setStrokeJoin(SkPaint::kRound_Join);
    p.setStrokeCap(SkPaint::kRound_Cap);
    return p;
}

} // namespace

void PluginCanvas::rect(Vec2 a, Vec2 b, Color color, float radius) {
    const SkRect r = SkRect::MakeLTRB(a.x, a.y, b.x, b.y).makeSorted();
    const float rr = std::clamp(radius, 0.0f, std::min(r.width(), r.height()) * 0.5f);
    canvas_->drawRRect(SkRRect::MakeRectXY(r, rr, rr), fillPaint(faded(color)));
}

void PluginCanvas::rectOutline(Vec2 a, Vec2 b, Color color, float width, float radius) {
    const SkRect r = SkRect::MakeLTRB(a.x, a.y, b.x, b.y).makeSorted();
    const float rr = std::clamp(radius, 0.0f, std::min(r.width(), r.height()) * 0.5f);
    canvas_->drawRRect(SkRRect::MakeRectXY(r, rr, rr), strokePaint(faded(color), width));
}

void PluginCanvas::circle(Vec2 c, float radius, Color color) {
    canvas_->drawCircle(c.x, c.y, radius, fillPaint(faded(color)));
}

void PluginCanvas::line(Vec2 a, Vec2 b, float width, Color color) {
    SkPaint p = strokePaint(faded(color), width);
    p.setStrokeCap(SkPaint::kButt_Cap);
    canvas_->drawLine(a.x, a.y, b.x, b.y, p);
}

void PluginCanvas::polyline(const std::vector<Vec2>& points, float width, Color color, bool closed) {
    if (points.size() < 2) return;
    SkPathBuilder b;
    b.moveTo(points[0].x, points[0].y);
    for (size_t i = 1; i < points.size(); ++i) b.lineTo(points[i].x, points[i].y);
    if (closed) b.close();
    canvas_->drawPath(b.detach(), strokePaint(faded(color), width));
}

void PluginCanvas::polygon(const std::vector<Vec2>& points, Color color) {
    if (points.size() < 3) return;
    SkPathBuilder b;
    b.moveTo(points[0].x, points[0].y);
    for (size_t i = 1; i < points.size(); ++i) b.lineTo(points[i].x, points[i].y);
    b.close();
    canvas_->drawPath(b.detach(), fillPaint(faded(color)));
}

void PluginCanvas::arc(Vec2 c, float r0, float r1, float fromDeg, float toDeg, Color color) {
    if (toDeg < fromDeg) std::swap(fromDeg, toDeg);
    const float span = std::min(toDeg - fromDeg, 360.0f);
    if (span <= 0.01f || r1 <= 0.0f) return;
    r0 = std::clamp(r0, 0.0f, r1);
    // Skia angles start at 3 o'clock; ours at 12.
    const float start = fromDeg - 90.0f;
    SkPathBuilder b;
    const SkRect outer = SkRect::MakeLTRB(c.x - r1, c.y - r1, c.x + r1, c.y + r1);
    if (span >= 360.0f) {
        b.addOval(outer);
        if (r0 > 0.0f) b.addOval(SkRect::MakeLTRB(c.x - r0, c.y - r0, c.x + r0, c.y + r0), SkPathDirection::kCCW);
    } else {
        b.arcTo(outer, start, span, true);
        if (r0 > 0.0f) b.arcTo(SkRect::MakeLTRB(c.x - r0, c.y - r0, c.x + r0, c.y + r0), start + span, -span, false);
        else b.lineTo(c.x, c.y);
        b.close();
    }
    canvas_->drawPath(b.detach(), fillPaint(faded(color)));
}

float PluginCanvas::measure(const std::string& s, float size, bool bold) const { return measurePlainText(s, size, bold); }

void PluginCanvas::text(const std::string& s, Vec2 at, float size, Color color, TextAnchor anchor, bool bold) {
    drawPlainText(canvas_, s, at, size, faded(color), anchor, bold);
}

} // namespace bemotion::render
