#pragma once

// Small, dependency-free math types used by the document model.
// The UI layer converts these to/from ImGui types at the boundary.

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

} // namespace bemotion
