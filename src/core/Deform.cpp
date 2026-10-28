#include "core/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>

namespace bemotion {

Vec2 puppetPoint(const std::vector<PinMove>& pins, Vec2 v) {
    if (pins.empty()) return v;
    if (pins.size() == 1) return v + (pins[0].second - pins[0].first);
    // Weights fall with the square of the distance; on a pin, the pin itself.
    double sw = 0.0;
    Vec2 pStar{}, qStar{};
    std::vector<double> w(pins.size());
    for (size_t i = 0; i < pins.size(); ++i) {
        const Vec2 d = pins[i].first - v;
        const double d2 = static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y;
        if (d2 < 1e-6) return pins[i].second;
        w[i] = 1.0 / d2;
        sw += w[i];
        pStar = pStar + pins[i].first * static_cast<float>(w[i]);
        qStar = qStar + pins[i].second * static_cast<float>(w[i]);
    }
    pStar = pStar * static_cast<float>(1.0 / sw);
    qStar = qStar * static_cast<float>(1.0 / sw);
    // Rigid moving least squares: the best rotation (no scaling) of v around p* onto q*.
    const Vec2 d = v - pStar;
    double fx = 0.0, fy = 0.0;
    for (size_t i = 0; i < pins.size(); ++i) {
        const Vec2 p = pins[i].first - pStar, q = pins[i].second - qStar;
        const double pd = static_cast<double>(p.x) * d.x + static_cast<double>(p.y) * d.y;
        const double pxd = static_cast<double>(p.x) * d.y - static_cast<double>(p.y) * d.x;
        fx += w[i] * (q.x * pd - q.y * pxd);
        fy += w[i] * (q.x * pxd + q.y * pd);
    }
    const double len = std::hypot(fx, fy), dist = std::hypot(static_cast<double>(d.x), static_cast<double>(d.y));
    if (len < 1e-9) return v + (qStar - pStar);
    return qStar + Vec2{static_cast<float>(fx / len * dist), static_cast<float>(fy / len * dist)};
}

namespace {
// Catmull-Rom weights of the four points around `u` (0..1 between the middle two).
void catmullRom(float u, float out[4]) {
    const float u2 = u * u, u3 = u2 * u;
    out[0] = -0.5f * u3 + u2 - 0.5f * u;
    out[1] = 1.5f * u3 - 2.5f * u2 + 1.0f;
    out[2] = -1.5f * u3 + 2.0f * u2 + 0.5f * u;
    out[3] = 0.5f * u3 - 0.5f * u2;
}
} // namespace

Vec2 warpPoint(const Vec2* offsets, Vec2 mn, Vec2 mx, Vec2 v) {
    constexpr int n = kWarpSide;
    const float w = std::max(1e-3f, mx.x - mn.x), h = std::max(1e-3f, mx.y - mn.y);
    const float gx = std::clamp((v.x - mn.x) / w, 0.0f, 1.0f) * (n - 1), gy = std::clamp((v.y - mn.y) / h, 0.0f, 1.0f) * (n - 1);
    const int cx = std::min(static_cast<int>(gx), n - 2), cy = std::min(static_cast<int>(gy), n - 2);
    float wx[4], wy[4];
    catmullRom(gx - cx, wx);
    catmullRom(gy - cy, wy);
    Vec2 off{};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const int x = std::clamp(cx - 1 + i, 0, n - 1), y = std::clamp(cy - 1 + j, 0, n - 1);
            off = off + offsets[y * n + x] * (wx[i] * wy[j]);
        }
    return v + off;
}

namespace {

std::mutex puppetMutex;
std::map<uint64_t, std::shared_ptr<const PuppetShape>>& shapes() {
    static std::map<uint64_t, std::shared_ptr<const PuppetShape>> m;
    return m;
}

// A solved mesh: the grid's corners that belong to it (vertex index, -1: none), where each
// vertex rests and where it went.
struct PuppetSolved {
    Vec2 lo, hi;
    int cols = 0, rows = 0;
    std::vector<uint8_t> inside;
    std::vector<int> vertex;   // (cols + 1) * (rows + 1)
    std::vector<int> nearCell; // each cell: itself when inside, else the closest inside cell
    std::vector<Vec2> rest, moved;
};
std::map<uint64_t, std::pair<std::string, std::shared_ptr<const PuppetSolved>>>& solvedCache() {
    static std::map<uint64_t, std::pair<std::string, std::shared_ptr<const PuppetSolved>>> m;
    return m;
}

int findRoot(std::vector<int>& parent, int i) {
    while (parent[static_cast<size_t>(i)] != i) i = parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
    return i;
}

// The grid over the picture (its shape when the renderer made one, else the whole box) and its
// mesh: vertices, edges, pieces. Then each piece is moved by its pins.
std::shared_ptr<PuppetSolved> solvePuppet(const PuppetShape* shape, Vec2 mn, Vec2 mx, const std::vector<PinMove>& pins,
                                          const std::vector<Vec2>& starch) {
    auto out = std::make_shared<PuppetSolved>();
    PuppetSolved& m = *out;
    if (shape && shape->cols > 0 && shape->rows > 0 && shape->inside.size() == static_cast<size_t>(shape->cols * shape->rows)) {
        m.lo = shape->lo, m.hi = shape->hi, m.cols = shape->cols, m.rows = shape->rows, m.inside = shape->inside;
        if (std::none_of(m.inside.begin(), m.inside.end(), [](uint8_t c) { return c != 0; })) std::fill(m.inside.begin(), m.inside.end(), 1);
    } else {
        m.lo = mn, m.hi = mx;
        puppetGrid(mn, mx, m.cols, m.rows);
        m.inside.assign(static_cast<size_t>(m.cols * m.rows), 1);
    }
    const int cols = m.cols, rows = m.rows, stride = cols + 1;
    const float cw = std::max(1e-3f, (m.hi.x - m.lo.x) / cols), ch = std::max(1e-3f, (m.hi.y - m.lo.y) / rows);
    m.vertex.assign(static_cast<size_t>(stride * (rows + 1)), -1);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x)
            if (m.inside[static_cast<size_t>(y * cols + x)])
                for (int c : {y * stride + x, y * stride + x + 1, (y + 1) * stride + x, (y + 1) * stride + x + 1})
                    if (m.vertex[static_cast<size_t>(c)] < 0) {
                        m.vertex[static_cast<size_t>(c)] = static_cast<int>(m.rest.size());
                        m.rest.push_back({m.lo.x + cw * static_cast<float>(c % stride), m.lo.y + ch * static_cast<float>(c / stride)});
                    }
    // Every cell's closest inside cell (where points off the picture take their motion from).
    m.nearCell.assign(m.inside.size(), -1);
    {
        std::vector<int> queue;
        for (int c = 0; c < cols * rows; ++c)
            if (m.inside[static_cast<size_t>(c)]) m.nearCell[static_cast<size_t>(c)] = c, queue.push_back(c);
        for (size_t q = 0; q < queue.size(); ++q) {
            const int c = queue[q], x = c % cols, y = c / cols;
            const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& step : steps) {
                const int nx = x + step[0], ny = y + step[1];
                if (nx < 0 || ny < 0 || nx >= cols || ny >= rows || m.nearCell[static_cast<size_t>(ny * cols + nx)] >= 0) continue;
                m.nearCell[static_cast<size_t>(ny * cols + nx)] = m.nearCell[static_cast<size_t>(c)];
                queue.push_back(ny * cols + nx);
            }
        }
    }
    const int n = static_cast<int>(m.rest.size());
    m.moved = m.rest;
    if (n == 0 || pins.empty()) return out;
    // Edges: each inside cell's sides and diagonals, stiffer near starch pins.
    std::vector<std::vector<std::pair<int, float>>> adj(static_cast<size_t>(n));
    std::vector<int> parent(static_cast<size_t>(n));
    std::iota(parent.begin(), parent.end(), 0);
    const float reach = std::max(cw, ch) * 4.0f;
    auto link = [&](int a, int b) {
        for (const auto& e : adj[static_cast<size_t>(a)])
            if (e.first == b) return;
        float w = 1.0f;
        const Vec2 mid = (m.rest[static_cast<size_t>(a)] + m.rest[static_cast<size_t>(b)]) * 0.5f;
        for (const Vec2& s : starch)
            if (const float d = (mid - s).length(); d < reach) w += 12.0f * (1.0f - d / reach);
        adj[static_cast<size_t>(a)].push_back({b, w});
        adj[static_cast<size_t>(b)].push_back({a, w});
        parent[static_cast<size_t>(findRoot(parent, a))] = findRoot(parent, b);
    };
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            if (!m.inside[static_cast<size_t>(y * cols + x)]) continue;
            const int a = m.vertex[static_cast<size_t>(y * stride + x)], b = m.vertex[static_cast<size_t>(y * stride + x + 1)];
            const int c = m.vertex[static_cast<size_t>((y + 1) * stride + x)], d = m.vertex[static_cast<size_t>((y + 1) * stride + x + 1)];
            link(a, b), link(c, d), link(a, c), link(b, d), link(a, d), link(b, c);
        }
    // Each pin holds the vertex of its piece closest to it.
    auto cellOf = [&](Vec2 v) {
        const int x = std::clamp(static_cast<int>(std::floor((v.x - m.lo.x) / cw)), 0, cols - 1);
        const int y = std::clamp(static_cast<int>(std::floor((v.y - m.lo.y) / ch)), 0, rows - 1);
        return m.nearCell[static_cast<size_t>(y * cols + x)];
    };
    std::vector<int> held(static_cast<size_t>(n), -1); // pin index
    std::map<int, std::vector<int>> piecePins;          // piece root -> its pins
    for (size_t i = 0; i < pins.size(); ++i) {
        const int cell = cellOf(pins[i].first);
        if (cell < 0) continue;
        const int piece = findRoot(parent, m.vertex[static_cast<size_t>((cell / cols) * stride + cell % cols)]);
        int best = -1;
        float bestD = 1e30f;
        for (int v = 0; v < n; ++v)
            if (findRoot(parent, v) == piece)
                if (const float d = (m.rest[static_cast<size_t>(v)] - pins[i].first).length(); d < bestD) bestD = d, best = v;
        if (best < 0) continue;
        held[static_cast<size_t>(best)] = static_cast<int>(i);
        piecePins[piece].push_back(static_cast<int>(i));
    }
    std::vector<Vec2> turn(static_cast<size_t>(n), Vec2{1.0f, 0.0f});
    for (const auto& [piece, list] : piecePins) {
        std::vector<int> members;
        for (int v = 0; v < n; ++v)
            if (findRoot(parent, v) == piece) members.push_back(v);
        std::vector<PinMove> own;
        for (int i : list) own.push_back(pins[static_cast<size_t>(i)]);
        // A start close to the answer: the pins' moving least squares (one pin: a slide).
        for (int v : members) m.moved[static_cast<size_t>(v)] = puppetPoint(own, m.rest[static_cast<size_t>(v)]);
        for (int v : members)
            if (const int p = held[static_cast<size_t>(v)]; p >= 0)
                m.moved[static_cast<size_t>(v)] = pins[static_cast<size_t>(p)].second + (m.rest[static_cast<size_t>(v)] - pins[static_cast<size_t>(p)].first);
        if (own.size() < 2) continue;
        // As rigid as possible: each vertex's best turn of its edges (local), then the vertices
        // placed to fit those turns (global, Gauss-Seidel), a few rounds.
        for (int round = 0; round < 14; ++round) {
            for (int v : members) {
                double a = 0.0, b = 0.0;
                for (const auto& [j, w] : adj[static_cast<size_t>(v)]) {
                    const Vec2 e = m.rest[static_cast<size_t>(v)] - m.rest[static_cast<size_t>(j)];
                    const Vec2 f = m.moved[static_cast<size_t>(v)] - m.moved[static_cast<size_t>(j)];
                    a += w * (static_cast<double>(e.x) * f.x + static_cast<double>(e.y) * f.y);
                    b += w * (static_cast<double>(e.x) * f.y - static_cast<double>(e.y) * f.x);
                }
                const double len = std::hypot(a, b);
                turn[static_cast<size_t>(v)] = len > 1e-12 ? Vec2{static_cast<float>(a / len), static_cast<float>(b / len)} : Vec2{1.0f, 0.0f};
            }
            for (int sweep = 0; sweep < 12; ++sweep)
                for (int v : members) {
                    if (held[static_cast<size_t>(v)] >= 0) continue;
                    Vec2 sum{};
                    float total = 0.0f;
                    const Vec2 rv = turn[static_cast<size_t>(v)];
                    for (const auto& [j, w] : adj[static_cast<size_t>(v)]) {
                        const Vec2 r = (rv + turn[static_cast<size_t>(j)]) * 0.5f;
                        const Vec2 e = m.rest[static_cast<size_t>(v)] - m.rest[static_cast<size_t>(j)];
                        sum += (m.moved[static_cast<size_t>(j)] + Vec2{r.x * e.x - r.y * e.y, r.y * e.x + r.x * e.y}) * w;
                        total += w;
                    }
                    if (total > 0.0f) m.moved[static_cast<size_t>(v)] = sum / total;
                }
        }
    }
    return out;
}

// Where `v` lands on a solved mesh: through the triangle of its cell (or of the closest cell
// of the picture, carried along with it).
Vec2 throughMesh(const PuppetSolved& m, Vec2 v) {
    if (m.rest.empty()) return v;
    const float cw = std::max(1e-3f, (m.hi.x - m.lo.x) / m.cols), ch = std::max(1e-3f, (m.hi.y - m.lo.y) / m.rows);
    const int x0 = std::clamp(static_cast<int>(std::floor((v.x - m.lo.x) / cw)), 0, m.cols - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor((v.y - m.lo.y) / ch)), 0, m.rows - 1);
    const int cell = m.nearCell[static_cast<size_t>(y0 * m.cols + x0)];
    if (cell < 0) return v;
    const int x = cell % m.cols, y = cell / m.cols, stride = m.cols + 1;
    const int a = m.vertex[static_cast<size_t>(y * stride + x)], b = m.vertex[static_cast<size_t>(y * stride + x + 1)];
    const int c = m.vertex[static_cast<size_t>((y + 1) * stride + x)], d = m.vertex[static_cast<size_t>((y + 1) * stride + x + 1)];
    // Its place in the cell (0..1, may reach past it): the triangle a-b-d above the diagonal, a-d-c below.
    const Vec2 o = m.rest[static_cast<size_t>(a)];
    const float u = (v.x - o.x) / cw, w = (v.y - o.y) / ch;
    const Vec2 A = m.moved[static_cast<size_t>(a)], B = m.moved[static_cast<size_t>(b)], C = m.moved[static_cast<size_t>(c)],
               D = m.moved[static_cast<size_t>(d)];
    if (u >= w) return A + (B - A) * u + (D - B) * w;
    return A + (C - A) * w + (D - C) * u;
}

std::shared_ptr<const PuppetSolved> solvedFor(const Layer& layer, double t, Vec2 mn, Vec2 mx) {
    std::vector<PinMove> moves;
    std::vector<Vec2> starch;
    std::string key;
    char part[96];
    for (const PuppetPin& p : layer.pins) {
        if (p.starch) {
            starch.push_back(p.rest);
            std::snprintf(part, sizeof part, "s%.2f,%.2f;", p.rest.x, p.rest.y);
        } else {
            const Vec2 to = p.position.valueAt(t);
            moves.emplace_back(p.rest, to);
            std::snprintf(part, sizeof part, "%.2f,%.2f>%.3f,%.3f;", p.rest.x, p.rest.y, to.x, to.y);
        }
        key += part;
    }
    std::shared_ptr<const PuppetShape> shape;
    {
        const std::lock_guard lock(puppetMutex);
        if (auto it = shapes().find(layer.id); it != shapes().end()) shape = it->second, key += "|" + shape->key;
        else { // the whole box until the renderer has seen the picture
            std::snprintf(part, sizeof part, "|%.2f,%.2f,%.2f,%.2f", mn.x, mn.y, mx.x, mx.y);
            key += part;
        }
        if (auto it = solvedCache().find(layer.id); it != solvedCache().end() && it->second.first == key) return it->second.second;
    }
    std::shared_ptr<const PuppetSolved> solved = solvePuppet(shape.get(), mn, mx, moves, starch);
    const std::lock_guard lock(puppetMutex);
    solvedCache()[layer.id] = {key, solved};
    return solved;
}

} // namespace

void puppetGrid(Vec2 lo, Vec2 hi, int& cols, int& rows) {
    const float w = std::max(1e-3f, hi.x - lo.x), h = std::max(1e-3f, hi.y - lo.y);
    constexpr float kCells = 24.0f;
    cols = std::max(2, static_cast<int>(std::lround(kCells * w / std::max(w, h))));
    rows = std::max(2, static_cast<int>(std::lround(kCells * h / std::max(w, h))));
}

void setPuppetShape(uint64_t layerId, PuppetShape shape) {
    const std::lock_guard lock(puppetMutex);
    shapes()[layerId] = std::make_shared<const PuppetShape>(std::move(shape));
}

std::string puppetShapeKey(uint64_t layerId) {
    const std::lock_guard lock(puppetMutex);
    auto it = shapes().find(layerId);
    return it == shapes().end() ? std::string() : it->second->key;
}

bool puppetOnMesh(const Layer& layer, Vec2 v, Vec2 mn, Vec2 mx) {
    std::shared_ptr<const PuppetShape> shape;
    {
        const std::lock_guard lock(puppetMutex);
        if (auto it = shapes().find(layer.id); it != shapes().end()) shape = it->second;
    }
    if (!shape || shape->cols <= 0 || shape->rows <= 0 || std::none_of(shape->inside.begin(), shape->inside.end(), [](uint8_t c) { return c != 0; }))
        return v.x >= mn.x && v.y >= mn.y && v.x <= mx.x && v.y <= mx.y;
    const float cw = (shape->hi.x - shape->lo.x) / shape->cols, ch = (shape->hi.y - shape->lo.y) / shape->rows;
    if (cw <= 0.0f || ch <= 0.0f) return false;
    const int x = static_cast<int>(std::floor((v.x - shape->lo.x) / cw)), y = static_cast<int>(std::floor((v.y - shape->lo.y) / ch));
    return x >= 0 && y >= 0 && x < shape->cols && y < shape->rows && shape->inside[static_cast<size_t>(y * shape->cols + x)];
}

std::vector<std::pair<Vec2, Vec2>> puppetMeshLines(const Layer& layer, double t, Vec2 mn, Vec2 mx) {
    std::vector<std::pair<Vec2, Vec2>> lines;
    const std::shared_ptr<const PuppetSolved> m = solvedFor(layer, t, mn, mx);
    if (m->rest.empty()) return lines;
    const int stride = m->cols + 1;
    Vec2 offsets[kWarpPoints];
    if (layer.warp)
        for (int i = 0; i < kWarpPoints; ++i) offsets[i] = layer.warpPoints[static_cast<size_t>(i)].valueAt(t);
    auto at = [&](int corner) {
        const int v = m->vertex[static_cast<size_t>(corner)];
        Vec2 p = m->moved[static_cast<size_t>(v)];
        if (layer.warp) { // the warp comes first: the mesh where the picture really is
            const Vec2 rest = m->rest[static_cast<size_t>(v)];
            p = p + (warpPoint(offsets, mn, mx, rest) - rest);
        }
        return p;
    };
    for (int y = 0; y < m->rows; ++y)
        for (int x = 0; x < m->cols; ++x) {
            if (!m->inside[static_cast<size_t>(y * m->cols + x)]) continue;
            const int a = y * stride + x, b = a + 1, c = a + stride, d = c + 1;
            lines.push_back({at(a), at(b)});
            lines.push_back({at(a), at(c)});
            lines.push_back({at(a), at(d)});
            if (x + 1 >= m->cols || !m->inside[static_cast<size_t>(y * m->cols + x + 1)]) lines.push_back({at(b), at(d)});
            if (y + 1 >= m->rows || !m->inside[static_cast<size_t>((y + 1) * m->cols + x)]) lines.push_back({at(c), at(d)});
        }
    return lines;
}

bool deforms(const Layer& layer, double t) {
    for (const PuppetPin& p : layer.pins)
        if (!p.starch && (p.position.valueAt(t) - p.rest).length() > 0.01f) return true;
    if (layer.warp)
        for (const AnimVec2& w : layer.warpPoints)
            if (w.valueAt(t).length() > 0.01f) return true;
    return false;
}

Vec2 deformPoint(const Layer& layer, double t, Vec2 v, Vec2 mn, Vec2 mx) {
    if (layer.warp) {
        Vec2 offsets[kWarpPoints];
        for (int i = 0; i < kWarpPoints; ++i) offsets[i] = layer.warpPoints[static_cast<size_t>(i)].valueAt(t);
        v = warpPoint(offsets, mn, mx, v);
    }
    if (std::any_of(layer.pins.begin(), layer.pins.end(), [](const PuppetPin& p) { return !p.starch; }))
        v = throughMesh(*solvedFor(layer, t, mn, mx), v);
    return v;
}

} // namespace bemotion
