#include "core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace bemotion {

namespace {

void appendArc(std::vector<Vec2>& out, Vec2 center, float radius, float fromDeg, float toDeg, int segments) {
    for (int i = 0; i <= segments; ++i) {
        const float a = degToRad(lerp(fromDeg, toDeg, static_cast<float>(i) / segments));
        out.push_back({center.x + std::cos(a) * radius, center.y + std::sin(a) * radius});
    }
}

Path rectangle(Vec2 size, float radius) {
    Path p;
    p.closed = true;
    const float hw = size.x * 0.5f, hh = size.y * 0.5f;
    radius = std::clamp(radius, 0.0f, std::min(hw, hh));
    if (radius < 0.01f) {
        p.points = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        return p;
    }
    const int seg = std::clamp(static_cast<int>(radius * 0.5f), 4, 24);
    appendArc(p.points, {hw - radius, -hh + radius}, radius, -90, 0, seg);
    appendArc(p.points, {hw - radius, hh - radius}, radius, 0, 90, seg);
    appendArc(p.points, {-hw + radius, hh - radius}, radius, 90, 180, seg);
    appendArc(p.points, {-hw + radius, -hh + radius}, radius, 180, 270, seg);
    return p;
}

Path ellipse(Vec2 size) {
    Path p;
    p.closed = true;
    const float rx = size.x * 0.5f, ry = size.y * 0.5f;
    const int seg = std::clamp(static_cast<int>(std::max(rx, ry) * 0.35f), 24, 128);
    for (int i = 0; i < seg; ++i) {
        const float a = -kPi * 0.5f + 2.0f * kPi * i / seg; // start at the top
        p.points.push_back({std::cos(a) * rx, std::sin(a) * ry});
    }
    return p;
}

Path polygon(Vec2 size, int sides) {
    Path p;
    p.closed = true;
    const float rx = size.x * 0.5f, ry = size.y * 0.5f;
    for (int i = 0; i < sides; ++i) {
        const float a = -kPi * 0.5f + 2.0f * kPi * i / sides;
        p.points.push_back({std::cos(a) * rx, std::sin(a) * ry});
    }
    return p;
}

Path star(Vec2 size, int points, float innerRatio) {
    Path p;
    p.closed = true;
    p.convex = false;
    const float rx = size.x * 0.5f, ry = size.y * 0.5f;
    for (int i = 0; i < points * 2; ++i) {
        const float a = -kPi * 0.5f + kPi * i / points;
        const float r = (i % 2 == 0) ? 1.0f : innerRatio;
        p.points.push_back({std::cos(a) * rx * r, std::sin(a) * ry * r});
    }
    return p;
}

} // namespace

Path buildShapePath(const ShapeProps& shape, double t) {
    const Vec2 size = shape.size.valueAt(t);
    const int sides = std::clamp(static_cast<int>(std::lround(shape.sides.valueAt(t))), 3, 64);
    switch (shape.kind) {
    case ShapeKind::Rectangle: return rectangle(size, shape.roundness.valueAt(t));
    case ShapeKind::Ellipse: return ellipse(size);
    case ShapeKind::Polygon: return polygon(size, sides);
    case ShapeKind::Star: return star(size, sides, std::clamp(shape.innerRadius.valueAt(t) * 0.01f, 0.01f, 1.0f));
    case ShapeKind::Line: {
        Path p;
        p.points = {{-size.x * 0.5f, 0.0f}, {size.x * 0.5f, 0.0f}};
        return p;
    }
    case ShapeKind::Count: break;
    }
    return {};
}

Path trimPath(const Path& path, float start, float end) {
    start = clamp01(start);
    end = clamp01(end);
    if (start > end) std::swap(start, end);

    std::vector<Vec2> pts = path.points;
    if (path.closed && !pts.empty()) pts.push_back(pts.front());

    Path out;
    if (pts.size() < 2 || end - start <= 0.0f) return out;

    std::vector<float> cumulative(pts.size(), 0.0f);
    for (size_t i = 1; i < pts.size(); ++i) cumulative[i] = cumulative[i - 1] + (pts[i] - pts[i - 1]).length();
    const float total = cumulative.back();
    if (total <= 0.0f) return out;

    const float a = start * total, b = end * total;
    auto pointAt = [&](float dist) {
        auto it = std::lower_bound(cumulative.begin(), cumulative.end(), dist);
        size_t i = std::clamp<size_t>(static_cast<size_t>(it - cumulative.begin()), 1, pts.size() - 1);
        const float segLen = cumulative[i] - cumulative[i - 1];
        const float u = segLen > 0.0f ? (dist - cumulative[i - 1]) / segLen : 0.0f;
        return lerp(pts[i - 1], pts[i], u);
    };

    out.points.push_back(pointAt(a));
    for (size_t i = 0; i < pts.size(); ++i)
        if (cumulative[i] > a && cumulative[i] < b) out.points.push_back(pts[i]);
    out.points.push_back(pointAt(b));
    return out;
}

} // namespace bemotion
