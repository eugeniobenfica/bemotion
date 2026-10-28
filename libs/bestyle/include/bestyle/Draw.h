#pragma once

// Small drawing vocabulary over QPainter for the custom-drawn parts of the
// interface (icons, timeline, graph editor, viewport overlays, cards):
// points, packed colors and the handful of primitives they use, always
// anti-aliased.

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <string>

namespace bestyle {

// A color packed as 0xAABBGGRR (red in the low byte).
using Rgba = uint32_t;
constexpr Rgba rgba(int r, int g, int b, int a = 255) {
    return static_cast<Rgba>(r & 255) | (static_cast<Rgba>(g & 255) << 8) | (static_cast<Rgba>(b & 255) << 16) |
           (static_cast<Rgba>(a & 255) << 24);
}
constexpr int red(Rgba c) { return c & 255; }
constexpr int green(Rgba c) { return (c >> 8) & 255; }
constexpr int blue(Rgba c) { return (c >> 16) & 255; }
constexpr int alpha(Rgba c) { return (c >> 24) & 255; }
inline QColor qcolor(Rgba c) { return QColor(red(c), green(c), blue(c), alpha(c)); }
inline Rgba fromQColor(const QColor& c) { return rgba(c.red(), c.green(), c.blue(), c.alpha()); }

struct Pt {
    float x = 0.0f, y = 0.0f;
    constexpr Pt() = default;
    constexpr Pt(float x_, float y_) : x(x_), y(y_) {}
    Pt(const QPointF& p) : x(static_cast<float>(p.x())), y(static_cast<float>(p.y())) {}
    operator QPointF() const { return {x, y}; }
    Pt operator+(Pt o) const { return {x + o.x, y + o.y}; }
    Pt operator-(Pt o) const { return {x - o.x, y - o.y}; }
    Pt operator*(float s) const { return {x * s, y * s}; }
};

inline QRectF rectOf(Pt a, Pt b) { return QRectF(QPointF(a.x, a.y), QPointF(b.x, b.y)).normalized(); }

class Draw {
public:
    explicit Draw(QPainter& p) : p_(p) { p_.setRenderHint(QPainter::Antialiasing); }
    QPainter& painter() { return p_; }

    void line(Pt a, Pt b, Rgba c, float thickness = 1.0f);
    void rect(Pt a, Pt b, Rgba c, float rounding = 0.0f, float thickness = 1.0f);
    void rectFilled(Pt a, Pt b, Rgba c, float rounding = 0.0f);
    void circle(Pt center, float r, Rgba c, float thickness = 1.0f);
    void circleFilled(Pt center, float r, Rgba c);
    void ellipse(Pt center, Pt radii, Rgba c, float thickness = 1.0f);
    void triangleFilled(Pt a, Pt b, Pt c, Rgba col);
    void polyFilled(const Pt* pts, int n, Rgba c);
    void polyline(const Pt* pts, int n, Rgba c, bool closed, float thickness);
    void bezier(Pt p0, Pt p1, Pt p2, Pt p3, Rgba c, float thickness);
    // Angles in radians, clockwise from 3 o'clock (y down), like the vector code everywhere else.
    void arc(Pt center, float r, float from, float to, Rgba c, float thickness);
    void pie(Pt center, float r, float from, float to, Rgba c); // filled wedge
    // Text with its top-left at `pos` in the painter's current font.
    void text(Pt pos, Rgba c, const QString& s);
    void text(Pt pos, Rgba c, const std::string& s) { text(pos, c, QString::fromStdString(s)); }
    float textWidth(const QString& s) const;
    float textHeight() const;

    void pushClip(Pt a, Pt b);
    void popClip() { p_.restore(); }

private:
    QPainter& p_;
};

} // namespace bestyle
