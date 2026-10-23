#include "ui/Draw.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace bemotion::ui {

namespace {

QPen pen(Rgba c, float thickness) {
    QPen p(qcolor(c));
    p.setWidthF(thickness);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

constexpr float kRadToDeg = 57.29577951f;

} // namespace

void Draw::line(Pt a, Pt b, Rgba c, float thickness) {
    if (!alpha(c)) return;
    p_.setPen(pen(c, thickness));
    p_.drawLine(QPointF(a), QPointF(b));
}

void Draw::rect(Pt a, Pt b, Rgba c, float rounding, float thickness) {
    if (!alpha(c)) return;
    p_.setPen(pen(c, thickness));
    p_.setBrush(Qt::NoBrush);
    const QRectF r = rectOf(a, b).adjusted(thickness * 0.5, thickness * 0.5, -thickness * 0.5, -thickness * 0.5);
    if (rounding > 0.0f) p_.drawRoundedRect(r, rounding, rounding);
    else p_.drawRect(r);
}

void Draw::rectFilled(Pt a, Pt b, Rgba c, float rounding) {
    if (!alpha(c)) return;
    p_.setPen(Qt::NoPen);
    p_.setBrush(qcolor(c));
    const QRectF r = rectOf(a, b);
    if (rounding > 0.0f) p_.drawRoundedRect(r, rounding, rounding);
    else p_.drawRect(r);
}

void Draw::circle(Pt center, float r, Rgba c, float thickness) {
    if (!alpha(c)) return;
    p_.setPen(pen(c, thickness));
    p_.setBrush(Qt::NoBrush);
    p_.drawEllipse(QPointF(center), r, r);
}

void Draw::circleFilled(Pt center, float r, Rgba c) {
    if (!alpha(c)) return;
    p_.setPen(Qt::NoPen);
    p_.setBrush(qcolor(c));
    p_.drawEllipse(QPointF(center), r, r);
}

void Draw::ellipse(Pt center, Pt radii, Rgba c, float thickness) {
    if (!alpha(c)) return;
    p_.setPen(pen(c, thickness));
    p_.setBrush(Qt::NoBrush);
    p_.drawEllipse(QPointF(center), radii.x, radii.y);
}

void Draw::triangleFilled(Pt a, Pt b, Pt c, Rgba col) {
    const Pt pts[3] = {a, b, c};
    polyFilled(pts, 3, col);
}

void Draw::polyFilled(const Pt* pts, int n, Rgba c) {
    if (!alpha(c) || n < 3) return;
    QPolygonF poly;
    for (int i = 0; i < n; ++i) poly << QPointF(pts[i]);
    p_.setPen(Qt::NoPen);
    p_.setBrush(qcolor(c));
    p_.drawPolygon(poly, Qt::WindingFill);
}

void Draw::polyline(const Pt* pts, int n, Rgba c, bool closed, float thickness) {
    if (!alpha(c) || n < 2) return;
    QPolygonF poly;
    for (int i = 0; i < n; ++i) poly << QPointF(pts[i]);
    p_.setPen(pen(c, thickness));
    p_.setBrush(Qt::NoBrush);
    if (closed) p_.drawPolygon(poly);
    else p_.drawPolyline(poly);
}

void Draw::bezier(Pt p0, Pt p1, Pt p2, Pt p3, Rgba c, float thickness) {
    if (!alpha(c)) return;
    QPainterPath path{QPointF(p0)};
    path.cubicTo(QPointF(p1), QPointF(p2), QPointF(p3));
    p_.setPen(pen(c, thickness));
    p_.setBrush(Qt::NoBrush);
    p_.drawPath(path);
}

void Draw::arc(Pt center, float r, float from, float to, Rgba c, float thickness) {
    if (!alpha(c)) return;
    // Qt: degrees counter-clockwise from 3 o'clock, in 1/16ths.
    const QRectF box(center.x - r, center.y - r, r * 2, r * 2);
    p_.setPen(pen(c, thickness));
    p_.setBrush(Qt::NoBrush);
    p_.drawArc(box, static_cast<int>(std::lround(-from * kRadToDeg * 16)), static_cast<int>(std::lround(-(to - from) * kRadToDeg * 16)));
}

void Draw::pie(Pt center, float r, float from, float to, Rgba c) {
    if (!alpha(c)) return;
    const QRectF box(center.x - r, center.y - r, r * 2, r * 2);
    p_.setPen(Qt::NoPen);
    p_.setBrush(qcolor(c));
    p_.drawPie(box, static_cast<int>(std::lround(-from * kRadToDeg * 16)), static_cast<int>(std::lround(-(to - from) * kRadToDeg * 16)));
}

void Draw::text(Pt pos, Rgba c, const QString& s) {
    if (!alpha(c) || s.isEmpty()) return;
    p_.setPen(qcolor(c));
    const QFontMetricsF fm(p_.font());
    p_.drawText(QPointF(pos.x, pos.y + fm.ascent()), s);
}

float Draw::textWidth(const QString& s) const { return static_cast<float>(QFontMetricsF(p_.font()).horizontalAdvance(s)); }
float Draw::textHeight() const { return static_cast<float>(QFontMetricsF(p_.font()).height()); }

void Draw::pushClip(Pt a, Pt b) {
    p_.save();
    p_.setClipRect(rectOf(a, b), Qt::IntersectClip);
}

} // namespace bemotion::ui
