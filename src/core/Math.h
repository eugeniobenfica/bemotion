#pragma once

// Small, dependency-free math types used by the document model.
// The interface converts them to its own types (QPointF, ui::Pt) where it draws.

#include <algorithm>
#include <cmath>

namespace bemotion {

constexpr float kPi = 3.14159265358979323846f;

inline float degToRad(float d) { return d * (kPi / 180.0f); }
inline float radToDeg(float r) { return r * (180.0f / kPi); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator*(Vec2 o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    bool operator==(const Vec2&) const = default;

    float length() const { return std::sqrt(x * x + y * y); }
};

inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

// Linear RGBA color, each channel in [0, 1].
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    bool operator==(const Color&) const = default;

    static constexpr Color fromHex(unsigned rgb, float alpha = 1.0f) {
        return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
    }
};

inline Color lerp(Color a, Color b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

// 2D affine transform:
//   x' = a*x + c*y + tx
//   y' = b*x + d*y + ty
struct Affine {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

    static Affine translate(Vec2 t) { Affine m; m.tx = t.x; m.ty = t.y; return m; }
    static Affine scale(Vec2 s) { Affine m; m.a = s.x; m.d = s.y; return m; }
    static Affine scale(float s) { return scale({s, s}); }
    static Affine rotateDeg(float deg) {
        const float r = degToRad(deg), cs = std::cos(r), sn = std::sin(r);
        Affine m; m.a = cs; m.b = sn; m.c = -sn; m.d = cs; return m;
    }

    Affine operator*(const Affine& o) const {
        Affine m;
        m.a = a * o.a + c * o.b;
        m.b = b * o.a + d * o.b;
        m.c = a * o.c + c * o.d;
        m.d = b * o.c + d * o.d;
        m.tx = a * o.tx + c * o.ty + tx;
        m.ty = b * o.tx + d * o.ty + ty;
        return m;
    }

    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Vec2 applyVector(Vec2 v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }

    float determinant() const { return a * d - b * c; }

    Affine inverse() const {
        const float det = determinant();
        if (std::fabs(det) < 1e-12f) return {};
        const float inv = 1.0f / det;
        Affine m;
        m.a = d * inv;
        m.b = -b * inv;
        m.c = -c * inv;
        m.d = a * inv;
        m.tx = -(m.a * tx + m.c * ty);
        m.ty = -(m.b * tx + m.d * ty);
        return m;
    }
};

// 2D projective transform (a 3x3 matrix): what a plane seen in perspective becomes
// on screen. Points map to (x / w, y / w); w is kept so callers can tell points in
// front of the camera (w >= 1, see Composition::viewFor) from those behind it.
// Affine transforms convert to it (w = 1 everywhere).
struct Projective {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    Projective() = default;
    Projective(const Affine& a) : m{{a.a, a.c, a.tx}, {a.b, a.d, a.ty}, {0, 0, 1}} {}

    bool hasPerspective() const { return m[2][0] != 0.0f || m[2][1] != 0.0f; }
    float w(Vec2 p) const { return m[2][0] * p.x + m[2][1] * p.y + m[2][2]; }
    bool inFront(Vec2 p) const { return w(p) > 0.999f; }
    Vec2 apply(Vec2 p) const {
        const float ww = w(p), iw = std::fabs(ww) > 1e-12f ? 1.0f / ww : 0.0f;
        return {(m[0][0] * p.x + m[0][1] * p.y + m[0][2]) * iw, (m[1][0] * p.x + m[1][1] * p.y + m[1][2]) * iw};
    }
    float determinant() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
    // How much a small area around `p` is scaled (square root of the Jacobian's determinant).
    float localScale(Vec2 p) const {
        const Vec2 o = apply(p), dx = apply({p.x + 1.0f, p.y}) - o, dy = apply({p.x, p.y + 1.0f}) - o;
        return std::sqrt(std::fabs(dx.x * dy.y - dx.y * dy.x));
    }
    Projective inverse() const {
        const float det = determinant();
        if (std::fabs(det) < 1e-20f) return {};
        const float k = 1.0f / det;
        Projective r;
        r.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * k;
        r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
        r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
        r.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * k;
        r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
        r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
        r.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * k;
        r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
        r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
        return r;
    }
};

inline Projective operator*(const Projective& x, const Projective& y) {
    Projective r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = x.m[i][0] * y.m[0][j] + x.m[i][1] * y.m[1][j] + x.m[i][2] * y.m[2][j];
    return r;
}

} // namespace bemotion
