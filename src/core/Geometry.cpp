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
    radius = std::clamp(radius, 0.0f, std::max(0.0f, std::min(std::fabs(hw), std::fabs(hh)))); // negative sizes mirror
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
    case ShapeKind::Path: return flattenBezier(shape.path);
    case ShapeKind::Count: break;
    }
    return {};
}

namespace {
constexpr float kKappa = 0.5522847f; // a quarter circle's handles, as a fraction of its radius
} // namespace

BezierPath rectanglePath(Vec2 mn, Vec2 mx) {
    BezierPath p;
    p.vertices = {{mn, {}, {}}, {{mx.x, mn.y}, {}, {}}, {mx, {}, {}}, {{mn.x, mx.y}, {}, {}}};
    return p;
}

BezierPath ellipsePath(Vec2 mn, Vec2 mx) {
    const Vec2 c = (mn + mx) * 0.5f, r = (mx - mn) * 0.5f;
    const float kx = r.x * kKappa, ky = r.y * kKappa;
    BezierPath p; // from the top, clockwise (as the ellipse shape)
    p.vertices = {{{c.x, mn.y}, {-kx, 0}, {kx, 0}},
                  {{mx.x, c.y}, {0, -ky}, {0, ky}},
                  {{c.x, mx.y}, {kx, 0}, {-kx, 0}},
                  {{mn.x, c.y}, {0, ky}, {0, -ky}}};
    return p;
}

BezierPath shapeToBezier(const ShapeProps& shape, double t) {
    if (shape.kind == ShapeKind::Path) return shape.path;
    const Vec2 size = shape.size.valueAt(t);
    const Vec2 h{std::fabs(size.x) * 0.5f, std::fabs(size.y) * 0.5f};
    if (shape.kind == ShapeKind::Ellipse) return ellipsePath(h * -1.0f, h);
    if (shape.kind == ShapeKind::Rectangle) {
        const float r = std::clamp(shape.roundness.valueAt(t), 0.0f, std::min(h.x, h.y));
        if (r < 0.01f) return rectanglePath(h * -1.0f, h);
        const float k = r * kKappa;
        BezierPath p; // straight edges, and a quarter circle between each edge and the next
        p.vertices = {{{-h.x + r, -h.y}, {-k, 0}, {}}, {{h.x - r, -h.y}, {}, {k, 0}}, {{h.x, -h.y + r}, {0, -k}, {}},
                      {{h.x, h.y - r}, {}, {0, k}},    {{h.x - r, h.y}, {k, 0}, {}},   {{-h.x + r, h.y}, {}, {-k, 0}},
                      {{-h.x, h.y - r}, {0, k}, {}},   {{-h.x, -h.y + r}, {}, {0, -k}}};
        return p;
    }
    const Path flat = buildShapePath(shape, t);
    BezierPath p;
    p.closed = flat.closed;
    for (const Vec2& v : flat.points) p.vertices.push_back({v, {}, {}});
    return p;
}

size_t segmentCount(const BezierPath& path) {
    const size_t n = path.vertices.size();
    return n < 2 ? 0 : path.closed ? n : n - 1;
}

Vec2 bezierPoint(const BezierPath& path, size_t i, float u) {
    const PathVertex& a = path.vertices[i];
    const PathVertex& b = path.vertices[(i + 1) % path.vertices.size()];
    const Vec2 p0 = a.point, p1 = a.point + a.out, p2 = b.point + b.in, p3 = b.point;
    const float v = 1.0f - u;
    return p0 * (v * v * v) + p1 * (3 * v * v * u) + p2 * (3 * v * u * u) + p3 * (u * u * u);
}

Path flattenBezier(const BezierPath& path, int steps) {
    Path out;
    out.closed = path.closed;
    out.convex = false;
    const size_t segments = segmentCount(path);
    if (segments == 0) return out;
    for (size_t i = 0; i < segments; ++i) {
        const PathVertex& a = path.vertices[i];
        const PathVertex& b = path.vertices[(i + 1) % path.vertices.size()];
        const bool straight = a.out == Vec2{} && b.in == Vec2{};
        const int n = straight ? 1 : std::max(1, steps);
        for (int s = 0; s < n; ++s) out.points.push_back(bezierPoint(path, i, static_cast<float>(s) / n));
    }
    if (!path.closed) out.points.push_back(path.vertices.back().point);
    return out;
}

bool bezierBounds(const BezierPath& path, Vec2& mn, Vec2& mx) {
    if (path.vertices.empty()) return false;
    mn = {1e30f, 1e30f}, mx = {-1e30f, -1e30f};
    for (const PathVertex& v : path.vertices)
        for (const Vec2& q : {v.point, v.point + v.in, v.point + v.out}) {
            mn = {std::min(mn.x, q.x), std::min(mn.y, q.y)};
            mx = {std::max(mx.x, q.x), std::max(mx.y, q.y)};
        }
    return true;
}

bool nearestSegment(const BezierPath& path, Vec2 p, float maxDistance, size_t& segment, float& u) {
    float best = maxDistance;
    bool found = false;
    for (size_t i = 0; i < segmentCount(path); ++i) {
        constexpr int kSteps = 48;
        for (int s = 1; s < kSteps; ++s) { // not at the vertices themselves
            const float v = static_cast<float>(s) / kSteps;
            const float d = (bezierPoint(path, i, v) - p).length();
            if (d <= best) best = d, segment = i, u = v, found = true;
        }
    }
    return found;
}

void insertVertex(BezierPath& path, size_t segment, float u) {
    const size_t n = path.vertices.size();
    if (segment >= segmentCount(path)) return;
    PathVertex& a = path.vertices[segment];
    PathVertex& b = path.vertices[(segment + 1) % n];
    const Vec2 p0 = a.point, p1 = a.point + a.out, p2 = b.point + b.in, p3 = b.point;
    const Vec2 q0 = lerp(p0, p1, u), q1 = lerp(p1, p2, u), q2 = lerp(p2, p3, u);
    const Vec2 r0 = lerp(q0, q1, u), r1 = lerp(q1, q2, u), s = lerp(r0, r1, u);
    const bool straight = a.out == Vec2{} && b.in == Vec2{};
    a.out = straight ? Vec2{} : q0 - p0;
    b.in = straight ? Vec2{} : q2 - p3;
    const PathVertex mid = straight ? PathVertex{s, {}, {}} : PathVertex{s, r0 - s, r1 - s};
    path.vertices.insert(path.vertices.begin() + static_cast<std::ptrdiff_t>(segment + 1), mid);
}

BezierPath transformed(const BezierPath& path, const Affine& m) {
    BezierPath out = path;
    for (PathVertex& v : out.vertices) {
        const Vec2 p = m.apply(v.point);
        v.in = m.apply(v.point + v.in) - p;
        v.out = m.apply(v.point + v.out) - p;
        v.point = p;
    }
    return out;
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
