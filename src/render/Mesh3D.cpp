#include "render/Mesh3D.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkVertices.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bemotion::render {

namespace {

struct V3 {
    float x, y, z;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalized(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-12f ? a * (1.0f / l) : V3{0, 0, 1};
}

V3 at(const std::vector<float>& v, size_t i) { return {v[i * 3], v[i * 3 + 1], v[i * 3 + 2]}; }

// Rotation: spin around y, then tilt around x, then roll around z.
struct Rotation {
    float m[9];
    explicit Rotation(const float deg[3]) {
        const float k = 3.14159265f / 180.0f;
        const float cx = std::cos(deg[0] * k), sx = std::sin(deg[0] * k);
        const float cy = std::cos(deg[1] * k), sy = std::sin(deg[1] * k);
        const float cz = std::cos(deg[2] * k), sz = std::sin(deg[2] * k);
        // Rz * Rx * Ry
        const float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
        const float rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
        const float rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
        float t[9];
        mul(rx, ry, t);
        mul(rz, t, m);
    }
    static void mul(const float* a, const float* b, float* out) {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    }
    V3 apply(V3 v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
};

} // namespace

void Mesh::finish() {
    const size_t n = vertexCount();
    positions.resize(n * 3);
    // Indices: whole triangles that point at existing vertices; none given: one triangle per three vertices.
    if (indices.empty()) {
        indices.resize(n - n % 3);
        std::iota(indices.begin(), indices.end(), 0u);
    }
    indices.resize(indices.size() - indices.size() % 3);
    std::vector<uint32_t> kept;
    kept.reserve(indices.size());
    for (size_t t = 0; t + 2 < indices.size(); t += 3)
        if (indices[t] < n && indices[t + 1] < n && indices[t + 2] < n) kept.insert(kept.end(), {indices[t], indices[t + 1], indices[t + 2]});
    indices = std::move(kept);
    if (colors.size() != n) colors.assign(n, Color::fromHex(0xFFFFFF));
    if (normals.size() != n * 3) { // smooth normals: the faces around each vertex, weighted by area
        normals.assign(n * 3, 0.0f);
        for (size_t t = 0; t < indices.size(); t += 3) {
            const V3 a = at(positions, indices[t]), b = at(positions, indices[t + 1]), c = at(positions, indices[t + 2]);
            const V3 f = cross(b - a, c - a);
            for (int k = 0; k < 3; ++k) {
                float* nn = &normals[indices[t + k] * 3];
                nn[0] += f.x, nn[1] += f.y, nn[2] += f.z;
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        const V3 v = normalized(at(normals, i));
        normals[i * 3] = v.x, normals[i * 3 + 1] = v.y, normals[i * 3 + 2] = v.z;
    }
    // Bounds: the box's center and the sphere around it.
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < n; ++i) {
        const V3 p = at(positions, i);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    if (lo.x > hi.x) lo = hi = {0, 0, 0};
    const V3 c = (lo + hi) * 0.5f;
    center[0] = c.x, center[1] = c.y, center[2] = c.z;
    float r = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const V3 d = at(positions, i) - c;
        if (std::isfinite(d.x)) r = std::max(r, dot(d, d));
    }
    radius = r > 0.0f ? std::sqrt(r) : 1.0f;
}

void drawMesh(SkCanvas* canvas, const Mesh& mesh, const MeshView& view) {
    const size_t n = mesh.vertexCount(), tris = mesh.triangleCount();
    if (!n || !tris) return;
    const Rotation rot(view.rotation);
    const V3 c{mesh.center[0], mesh.center[1], mesh.center[2]};
    const float fit = std::min(view.frame.x, view.frame.y) * 0.5f * std::max(0.0f, view.zoom);
    // The camera looks at the center from `dist` (in units of the bounding sphere's radius).
    const bool ortho = view.fov < 0.5f;
    const float dist = ortho ? 0.0f : 1.0f + 1.0f / std::tan(std::clamp(view.fov, 0.5f, 150.0f) * 3.14159265f / 360.0f);

    // Vertices in view space (unit sphere, y up, z toward the viewer) and on screen.
    std::vector<V3> vs(n), ns(n);
    std::vector<SkPoint> screen(n);
    for (size_t i = 0; i < n; ++i) {
        const V3 p = rot.apply((at(mesh.positions, i) - c) * (1.0f / mesh.radius));
        vs[i] = p;
        ns[i] = rot.apply(at(mesh.normals, i));
        const float k = ortho ? 1.0f : (dist - 1.0f) / std::max(1e-3f, dist - p.z);
        screen[i] = {view.offset.x + p.x * k * fit, view.offset.y - p.y * k * fit};
    }

    // Triangles that face the camera (all of them unless culling), farthest first.
    struct Tri {
        uint32_t i;
        float depth;
    };
    std::vector<Tri> order;
    order.reserve(tris);
    const V3 eye{0, 0, ortho ? 1e6f : dist};
    for (uint32_t t = 0; t < tris; ++t) {
        const uint32_t a = mesh.indices[t * 3], b = mesh.indices[t * 3 + 1], d = mesh.indices[t * 3 + 2];
        const V3 centroid = (vs[a] + vs[b] + vs[d]) * (1.0f / 3.0f);
        if (!std::isfinite(centroid.x) || !std::isfinite(centroid.y) || !std::isfinite(centroid.z)) continue;
        if (view.cullBack && dot(cross(vs[b] - vs[a], vs[d] - vs[a]), (ortho ? V3{0, 0, 1} : eye - centroid)) <= 0.0f) continue;
        order.push_back({t, ortho ? centroid.z : -dot(eye - centroid, eye - centroid)});
    }
    std::sort(order.begin(), order.end(), [](const Tri& x, const Tri& y) { return x.depth < y.depth; });
    if (order.empty()) return;

    const V3 light = normalized({view.light[0], view.light[1], view.light[2]});
    const V3 half = normalized(light + V3{0, 0, 1});
    const float ambient = std::clamp(view.ambient, 0.0f, 1.0f), shine = std::clamp(view.shine, 0.0f, 1.0f);
    auto shade = [&](V3 normal, V3 toEye, Color base) {
        if (dot(normal, toEye) < 0.0f) normal = normal * -1.0f; // the inside of an open surface
        const float diffuse = std::max(0.0f, dot(normal, light));
        const float spec = shine * std::pow(std::max(0.0f, dot(normal, half)), 40.0f);
        const float k = ambient + (1.0f - ambient) * diffuse;
        auto ch = [&](float v, float tint) { return std::clamp(v * tint * k + spec, 0.0f, 1.0f); };
        return SkColor4f{ch(base.r, view.tint.r), ch(base.g, view.tint.g), ch(base.b, view.tint.b),
                         std::clamp(base.a * view.tint.a * view.opacity, 0.0f, 1.0f)};
    };

    std::vector<SkPoint> pos;
    std::vector<SkColor> col;
    pos.reserve(order.size() * 3);
    col.reserve(order.size() * 3);
    for (const Tri& tri : order) {
        const uint32_t idx[3] = {mesh.indices[tri.i * 3], mesh.indices[tri.i * 3 + 1], mesh.indices[tri.i * 3 + 2]};
        const V3 faceN = normalized(cross(vs[idx[1]] - vs[idx[0]], vs[idx[2]] - vs[idx[0]]));
        const V3 centroid = (vs[idx[0]] + vs[idx[1]] + vs[idx[2]]) * (1.0f / 3.0f);
        const V3 toEye = ortho ? V3{0, 0, 1} : normalized(eye - centroid);
        for (uint32_t i : idx) {
            pos.push_back(screen[i]);
            V3 normal = view.flat ? faceN : ns[i];
            if (!view.flat && dot(normal, faceN) < 0.0f) normal = normal * -1.0f; // keep smooth normals on the face's side
            col.push_back(shade(normal, toEye, view.meshColors ? mesh.colors[i] : Color::fromHex(0xFFFFFF)).toSkColor());
        }
    }
    const sk_sp<SkVertices> vertices =
        SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, static_cast<int>(pos.size()), pos.data(), nullptr, col.data());
    SkPaint paint;
    paint.setAntiAlias(true);
    canvas->drawVertices(vertices, SkBlendMode::kDst, paint); // kDst: the vertex colors as they are
}

} // namespace bemotion::render
