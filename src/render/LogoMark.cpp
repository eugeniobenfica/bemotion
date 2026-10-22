#include "render/LogoMark.h"

#include <algorithm>
#include <cmath>

namespace bemotion::logo {

namespace {

constexpr float kPi = 3.14159265358979f;

float len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
Vec2 onCircle(Vec2 c, float r, float angle) { return {c.x + std::cos(angle) * r, c.y + std::sin(angle) * r}; }

// Points along a circle from angle a0 to a1 (either direction), endpoints included. A point that
// repeats the previous one (where two arcs meet) is skipped: polygon fills choke on duplicates.
void arc(std::vector<Vec2>& out, Vec2 c, float r, float a0, float a1, int segments) {
    const int n = std::max(2, static_cast<int>(std::ceil(std::fabs(a1 - a0) / (2.0f * kPi) * segments)));
    for (int i = 0; i <= n; ++i) {
        const Vec2 p = onCircle(c, r, a0 + (a1 - a0) * static_cast<float>(i) / n);
        if (!out.empty() && len(p - out.back()) < 1e-4f) continue;
        out.push_back(p);
    }
}

float angleOf(Vec2 c, Vec2 p) { return std::atan2(p.y - c.y, p.x - c.x); }

} // namespace

float distance(Vec2 p) {
    const float disc = len(p) - 1.0f;
    const float ring = len(p - kBowlCenter) - kRingRadius;
    const float bowl = len(p - kBowlCenter) - kBowlRadius;
    // The stem: an infinitely tall column above the bowl's center.
    const float sx = std::max(kStemLeft - p.x, p.x - kStemRight);
    const float sy = p.y - kBowlCenter.y;
    const float stem = sy > 0.0f ? (sx > 0.0f ? std::hypot(sx, sy) : sy) : sx;
    const float body = std::max({disc, -ring, -stem}); // disc minus the cuts
    return std::min(body, bowl);
}

std::vector<std::vector<Vec2>> outlines(int segments) {
    const Vec2 o{0.0f, 0.0f}, c = kBowlCenter;
    // Where the disc meets the ring circle (top and bottom right).
    const float d = len(c);
    const float a = (1.0f - kRingRadius * kRingRadius + d * d) / (2.0f * d);
    const float h = std::sqrt(std::max(0.0f, 1.0f - a * a));
    const Vec2 u = c * (1.0f / d), base = u * a, perp{-u.y, u.x};
    Vec2 top = base + perp * h, bottom = base - perp * h;
    if (top.y > bottom.y) std::swap(top, bottom);

    std::vector<std::vector<Vec2>> shapes;
    // Left and lower part: from the stem's left edge around the disc, back along the ring.
    {
        std::vector<Vec2> s;
        const float a0 = angleOf(o, {kStemLeft, -std::sqrt(1.0f - kStemLeft * kStemLeft)});
        float a1 = angleOf(o, bottom);
        while (a1 > a0) a1 -= 2.0f * kPi; // counterclockwise on screen: through the left and the bottom
        arc(s, o, 1.0f, a0, a1, segments);
        arc(s, c, kRingRadius, angleOf(c, bottom), kPi, segments); // under the bowl, up to the stem
        shapes.push_back(std::move(s));
    }
    // Upper right part: between the stem and the ring.
    {
        std::vector<Vec2> s;
        const float a0 = angleOf(o, {kStemRight, -std::sqrt(1.0f - kStemRight * kStemRight)});
        float a1 = angleOf(o, top);
        while (a1 < a0) a1 += 2.0f * kPi;
        arc(s, o, 1.0f, a0, a1, segments);
        const float qy = c.y - std::sqrt(kRingRadius * kRingRadius - (kStemRight - c.x) * (kStemRight - c.x));
        float b1 = angleOf(c, {kStemRight, qy});
        const float b0 = angleOf(c, top);
        while (b1 > b0) b1 -= 2.0f * kPi;
        arc(s, c, kRingRadius, b0, b1, segments);
        shapes.push_back(std::move(s));
    }
    // The bowl.
    {
        std::vector<Vec2> s;
        arc(s, c, kBowlRadius, 0.0f, 2.0f * kPi, segments);
        s.pop_back(); // closed: the last point repeats the first
        shapes.push_back(std::move(s));
    }
    return shapes;
}

} // namespace bemotion::logo
