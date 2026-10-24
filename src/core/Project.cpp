#include "core/Project.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace bemotion {

namespace {

bool contains(const std::vector<uint64_t>& ids, uint64_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// Clips closer than this are touching, not overlapping (cut points share a time).
constexpr double kOverlapEpsilon = 1e-6;

bool overlaps(const Layer& a, const Layer& b) {
    return a.inPoint < b.outPoint - kOverlapEpsilon && b.inPoint < a.outPoint - kOverlapEpsilon;
}

} // namespace

void Composition::fitDuration() {
    double end = 0.0;
    for (const Layer& l : layers)
        if (!findLayer(l.parent)) end = std::max(end, l.outPoint); // inside a compound, the compound bounds it
    // Whole frames, so the last clip's last frame is part of the composition.
    double frames = std::ceil(std::max(end, kMinCompDuration) * fps - 1e-6);
    if (maxDuration > 0.0) frames = std::max(1.0, std::min(frames, std::ceil(maxDuration * fps - 1e-6)));
    duration = frames / fps;
}

int Composition::frameCount() const { return std::max(1, static_cast<int>(std::lround(duration * fps))); }

int Composition::frameAt(double t) const {
    // Small bias so times exactly on a frame boundary never round down. Not capped at the end:
    // the playhead and clips may go past it (the composition grows to hold them).
    return std::max(0, static_cast<int>(std::floor(t * fps + 1e-6)));
}

Layer& Composition::addLayer(Layer layer, int index) {
    layer.id = nextLayerId++;
    if (layer.track == 0) layer.track = nextTrackId++;
    else nextTrackId = std::max(nextTrackId, layer.track + 1);

    const auto rows = trackRows();
    int at = -1;
    for (const TrackRow& r : rows)
        if (r.track == layer.track) at = r.first; // join the existing track
    if (at < 0) {
        // A new track: never split another track's block.
        at = std::clamp(index, 0, static_cast<int>(layers.size()));
        for (const TrackRow& r : rows)
            if (at > r.first && at < r.first + r.count) at = r.first;
    }
    return *layers.insert(layers.begin() + at, std::move(layer));
}

MediaItem* Composition::findMedia(uint64_t id) {
    for (auto& m : media)
        if (m.id == id) return &m;
    return nullptr;
}

const MediaItem* Composition::findMedia(uint64_t id) const {
    for (const auto& m : media)
        if (m.id == id) return &m;
    return nullptr;
}

uint64_t Composition::addMedia(MediaItem item) {
    for (const auto& m : media)
        if (!item.path.empty() && m.path == item.path) return m.id;
    item.id = nextMediaId++;
    media.push_back(std::move(item));
    return media.back().id;
}

Layer* Composition::findLayer(uint64_t id) {
    for (auto& l : layers)
        if (l.id == id) return &l;
    return nullptr;
}

const Layer* Composition::findLayer(uint64_t id) const {
    for (const auto& l : layers)
        if (l.id == id) return &l;
    return nullptr;
}

int Composition::indexOf(uint64_t id) const {
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i].id == id) return static_cast<int>(i);
    return -1;
}

void Composition::removeLayer(uint64_t id) {
    std::erase_if(layers, [id](const Layer& l) { return l.id == id; });
}

std::string Composition::uniqueLayerName(const std::string& base) const {
    auto taken = [&](const std::string& n) {
        return std::any_of(layers.begin(), layers.end(), [&](const Layer& l) { return l.name == n; });
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
}

// --- tracks ------------------------------------------------------------------------

std::vector<TrackRow> Composition::trackRows() const {
    std::vector<TrackRow> rows;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (rows.empty() || rows.back().track != layers[i].track) rows.push_back({layers[i].track, i, 0});
        ++rows.back().count;
    }
    return rows;
}

int Composition::trackRowOf(uint64_t layerId) const {
    const int index = indexOf(layerId);
    if (index < 0) return -1;
    const auto rows = trackRows();
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
        if (index >= rows[r].first && index < rows[r].first + rows[r].count) return r;
    return -1;
}

void Composition::normalizeTracks() {
    std::vector<std::vector<Layer>> rows;
    std::vector<uint64_t> order;
    for (auto& l : layers) {
        auto it = std::find(order.begin(), order.end(), l.track);
        if (it == order.end()) {
            order.push_back(l.track);
            rows.emplace_back();
            rows.back().push_back(std::move(l));
        } else {
            rows[it - order.begin()].push_back(std::move(l));
        }
    }
    rebuild(std::move(rows));
}

void Composition::rebuild(std::vector<std::vector<Layer>> rows) {
    std::vector<Layer> out;
    out.reserve(layers.size());
    for (auto& row : rows)
        for (auto& l : row) out.push_back(std::move(l));
    layers = std::move(out);
}

void Composition::moveTracks(const std::vector<uint64_t>& trackIds, int targetRow) {
    std::vector<std::vector<Layer>> moving, rest;
    for (const TrackRow& r : trackRows()) {
        auto& dst = contains(trackIds, r.track) ? moving : rest;
        dst.emplace_back(std::make_move_iterator(layers.begin() + r.first),
                         std::make_move_iterator(layers.begin() + r.first + r.count));
    }
    targetRow = std::clamp(targetRow, 0, static_cast<int>(rest.size()));
    rest.insert(rest.begin() + targetRow, std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
    rebuild(std::move(rest));
}

void Composition::shiftTracks(const std::vector<uint64_t>& trackIds, int delta) {
    std::vector<std::vector<Layer>> rows;
    for (const TrackRow& r : trackRows())
        rows.emplace_back(std::make_move_iterator(layers.begin() + r.first),
                          std::make_move_iterator(layers.begin() + r.first + r.count));
    auto moving = [&](size_t i) { return contains(trackIds, rows[i].front().track); };
    const int n = static_cast<int>(rows.size());
    for (int step = 0; step < std::abs(delta); ++step) {
        bool moved = false;
        if (delta < 0) {
            for (int i = 1; i < n; ++i)
                if (moving(i) && !moving(i - 1)) std::swap(rows[i], rows[i - 1]), moved = true;
        } else {
            for (int i = n - 2; i >= 0; --i)
                if (moving(i) && !moving(i + 1)) std::swap(rows[i], rows[i + 1]), moved = true;
        }
        if (!moved) break;
    }
    rebuild(std::move(rows));
}

void Composition::moveClipsToNewTracks(const std::vector<uint64_t>& layerIds, int gapRow) {
    std::vector<std::vector<Layer>> rest, fresh;
    for (const TrackRow& r : trackRows()) {
        std::vector<Layer> keep, take;
        for (int i = r.first; i < r.first + r.count; ++i)
            (contains(layerIds, layers[i].id) ? take : keep).push_back(std::move(layers[i]));
        if (!take.empty()) {
            const uint64_t track = nextTrackId++;
            for (auto& l : take) l.track = track;
            fresh.push_back(std::move(take));
        }
        rest.push_back(std::move(keep)); // may be empty; keeps row indices aligned with gapRow
    }
    gapRow = std::clamp(gapRow, 0, static_cast<int>(rest.size()));
    rest.insert(rest.begin() + gapRow, std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
    std::erase_if(rest, [](const std::vector<Layer>& row) { return row.empty(); });
    rebuild(std::move(rest));
}

bool Composition::clipsFitTrack(const std::vector<uint64_t>& layerIds, uint64_t trackId) const {
    std::vector<const Layer*> moving;
    for (uint64_t id : layerIds)
        if (const Layer* l = findLayer(id)) moving.push_back(l);
    for (size_t i = 0; i < moving.size(); ++i) {
        for (size_t j = i + 1; j < moving.size(); ++j)
            if (overlaps(*moving[i], *moving[j])) return false;
        for (const Layer& other : layers)
            if (other.track == trackId && !contains(layerIds, other.id) && overlaps(*moving[i], other)) return false;
    }
    return !moving.empty();
}

bool Composition::moveClipsToTrack(const std::vector<uint64_t>& layerIds, uint64_t trackId) {
    if (!clipsFitTrack(layerIds, trackId)) return false;
    std::vector<Layer> moving, rest;
    for (auto& l : layers) (contains(layerIds, l.id) ? moving : rest).push_back(std::move(l));
    layers = std::move(rest);
    for (auto& l : moving) l.track = trackId;
    // Insert right after the target track's clips (or on top if it no longer exists).
    int at = 0;
    for (const TrackRow& r : trackRows())
        if (r.track == trackId) at = r.first + r.count;
    layers.insert(layers.begin() + at, std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
    normalizeTracks();
    return true;
}

void Composition::timeShiftLimits(const std::vector<uint64_t>& layerIds, double& minDelta, double& maxDelta) const {
    minDelta = -std::numeric_limits<double>::infinity();
    maxDelta = std::numeric_limits<double>::infinity();
    for (uint64_t id : layerIds) {
        const Layer* l = findLayer(id);
        if (!l) continue;
        // Clips may run past the end of the composition (like After Effects), but
        // never start before 0 or after the last frame.
        minDelta = std::max(minDelta, std::min(0.0, -l->inPoint));
        maxDelta = std::min(maxDelta, std::max(0.0, duration - frameDuration() - l->inPoint));
        for (const Layer& other : layers) {
            if (other.track != l->track || contains(layerIds, other.id)) continue;
            if (other.outPoint <= l->inPoint + kOverlapEpsilon) minDelta = std::max(minDelta, other.outPoint - l->inPoint);
            else if (other.inPoint >= l->outPoint - kOverlapEpsilon)
                maxDelta = std::min(maxDelta, other.inPoint - l->outPoint);
        }
    }
}

void Composition::neighborBounds(uint64_t layerId, double& prevOut, double& nextIn) const {
    prevOut = -std::numeric_limits<double>::infinity();
    nextIn = std::numeric_limits<double>::infinity();
    const Layer* l = findLayer(layerId);
    if (!l) return;
    for (const Layer& other : layers) {
        if (other.track != l->track || other.id == l->id) continue;
        if (other.outPoint <= l->inPoint + kOverlapEpsilon) prevOut = std::max(prevOut, other.outPoint);
        else if (other.inPoint >= l->outPoint - kOverlapEpsilon) nextIn = std::min(nextIn, other.inPoint);
    }
}

void Composition::clipLimits(uint64_t layerId, double& minIn, double& maxOut) const {
    double prevOut = 0, nextIn = 0;
    neighborBounds(layerId, prevOut, nextIn);
    minIn = std::max(0.0, prevOut);
    maxOut = std::min(nextIn, duration + kMaxOverhang); // the out point may pass the composition end
    // Video and audio clips cannot be trimmed past their source (stills can).
    const Layer* l = findLayer(layerId);
    const MediaItem* item = l && l->type == LayerType::Media ? findMedia(l->media.mediaId) : nullptr;
    if (item && item->kind != MediaKind::Image && item->duration > 0) {
        minIn = std::max(minIn, l->media.start);
        maxOut = std::min(maxOut, l->media.start + item->duration);
    }
}

void Composition::setInPoint(uint64_t layerId, double t) {
    Layer* l = findLayer(layerId);
    if (!l) return;
    double minIn = 0, maxOut = 0;
    clipLimits(layerId, minIn, maxOut);
    const double in = std::clamp(t, minIn, std::max(minIn, l->outPoint - frameDuration()));
    if (in < l->outPoint) l->inPoint = in;
}

void Composition::setOutPoint(uint64_t layerId, double t) {
    Layer* l = findLayer(layerId);
    if (!l) return;
    double minIn = 0, maxOut = 0;
    clipLimits(layerId, minIn, maxOut);
    const double out = std::clamp(t, std::min(maxOut, l->inPoint + frameDuration()), maxOut);
    if (out > l->inPoint) l->outPoint = out;
}

uint64_t Composition::splitLayer(uint64_t id, double t) {
    const int index = indexOf(id);
    if (index < 0) return 0;
    Layer& original = layers[index];
    if (original.type == LayerType::Compound) return 0; // its contents would have to be split too
    const double eps = frameDuration() * 0.5;
    if (t <= original.inPoint + eps || t >= original.outPoint - eps) return 0;
    Layer tail = original; // same track; keyframes kept on both halves so motion stays continuous
    original.outPoint = t;
    tail.inPoint = t;
    return addLayer(std::move(tail)).id;
}

namespace {
const Layer* activeCamera(const Composition& c, double t) {
    for (const Layer& l : c.layers)
        if (l.type == LayerType::Camera && l.activeAt(t)) return &l;
    return nullptr;
}
} // namespace

Affine Composition::cameraAt(double t) const {
    const Layer* cam = activeCamera(*this, t);
    if (!cam) return {};
    // The camera frame (composition-sized, scaled by 1/zoom) maps onto the composition.
    const float zoom = std::max(0.01f, cam->camera.zoom.valueAt(t) * 0.01f);
    const Affine frame = cam->matrixAt(t) * Affine::scale(1.0f / zoom);
    return Affine::translate(center()) * frame.inverse();
}

namespace {
// 3D rotations (world: x right, y down, z away from the viewer), degrees.
struct Mat3 {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return r;
    }
    Mat3 transposed() const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        return r;
    }
    void apply(const float v[3], float out[3]) const {
        for (int i = 0; i < 3; ++i) out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    }
};
Mat3 rotX(float deg) { // the top tips away
    const float c = std::cos(degToRad(deg)), s = std::sin(degToRad(deg));
    Mat3 r;
    r.m[1][1] = c, r.m[1][2] = s, r.m[2][1] = -s, r.m[2][2] = c;
    return r;
}
Mat3 rotY(float deg) { // the right side turns away
    const float c = std::cos(degToRad(deg)), s = std::sin(degToRad(deg));
    Mat3 r;
    r.m[0][0] = c, r.m[0][2] = -s, r.m[2][0] = s, r.m[2][2] = c;
    return r;
}
Mat3 rotZ(float deg) { // clockwise on screen, like Affine::rotateDeg
    const float c = std::cos(degToRad(deg)), s = std::sin(degToRad(deg));
    Mat3 r;
    r.m[0][0] = c, r.m[0][1] = -s, r.m[1][0] = s, r.m[1][1] = c;
    return r;
}

const Layer* topLevel(const Composition& c, const Layer& layer) {
    const Layer* top = &layer; // what is inside a compound lies flat in it: the compound's plane counts
    for (int guard = 0; top->parent && guard < 32; ++guard) {
        const Layer* p = c.findLayer(top->parent);
        if (!p) break;
        top = p;
    }
    return top;
}
} // namespace

bool Composition::viewFor(const Layer& layer, double t, Projective& out, bool throughCamera, bool tilted) const {
    const Layer* top = topLevel(*this, layer);
    if (!top->threeD || top->type == LayerType::Camera || top->type == LayerType::Background) {
        out = throughCamera && top->type != LayerType::Background ? Projective(cameraAt(t)) : Projective();
        return true;
    }
    const Vec2 p = top->transform.position.valueAt(t);
    const Mat3 turn = tilted ? rotX(top->transform.rotationX.valueAt(t)) * rotY(top->transform.rotationY.valueAt(t)) : Mat3{};
    if (!throughCamera) { // from the front, without perspective: only turned around the position
        Affine a;
        a.a = turn.m[0][0], a.c = turn.m[0][1], a.b = turn.m[1][0], a.d = turn.m[1][1];
        a.tx = p.x - (a.a * p.x + a.c * p.y), a.ty = p.y - (a.b * p.x + a.d * p.y);
        out = a;
        return true;
    }
    // The layer is a plane in the world: its point (u, v) (scene coordinates of the unturned
    // plane) is at (p, z) + turn * (u - p.x, v - p.y, 0). The camera stands `focal` in front of
    // its own Z position, turned by its X / Y / Z rotation, and projects with that focal length
    // times its zoom. At the camera's Z, facing it, this is exactly the 2D camera (cameraAt).
    Vec2 eye = center();
    float eyeZ = 0.0f, zoom = 1.0f;
    Mat3 lens;
    if (const Layer* cam = activeCamera(*this, t)) {
        const TransformProps& ct = cam->transform;
        eye = ct.position.valueAt(t);
        eyeZ = ct.depth.valueAt(t);
        zoom = std::max(0.01f, cam->camera.zoom.valueAt(t) * 0.01f);
        lens = rotY(ct.rotationY.valueAt(t)) * rotX(ct.rotationX.valueAt(t)) * rotZ(ct.rotation.valueAt(t));
    }
    const float focal = focalLength(), nearPlane = focal * 0.01f; // closer than that: 100x, not drawn
    const Mat3 toCamera = lens.transposed();
    const Mat3 a = toCamera * turn;
    const float rel[3] = {p.x - eye.x, p.y - eye.y, top->transform.depth.valueAt(t) - eyeZ + focal};
    float b[3];
    toCamera.apply(rel, b);
    for (int i = 0; i < 3; ++i) b[i] -= a.m[i][0] * p.x + a.m[i][1] * p.y; // (u, v) relative to the position
    // Camera space of (u, v, 1): columns a0, a1, b. Facing the camera and behind it: not seen.
    if (std::fabs(a.m[2][0]) < 1e-6f && std::fabs(a.m[2][1]) < 1e-6f && b[2] < nearPlane) return false;
    const float k = zoom * focal;
    const Vec2 c = center();
    for (int j = 0; j < 3; ++j) {
        const float cx = j < 2 ? a.m[0][j] : b[0], cy = j < 2 ? a.m[1][j] : b[1], cz = j < 2 ? a.m[2][j] : b[2];
        out.m[0][j] = (k * cx + c.x * cz) / nearPlane;
        out.m[1][j] = (k * cy + c.y * cz) / nearPlane;
        out.m[2][j] = cz / nearPlane;
    }
    return true;
}

std::vector<const Layer*> Composition::drawOrder(double t, bool cameras) const {
    std::vector<const Layer*> order;
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->activeAt(t) || (it->type == LayerType::Camera && !cameras)) continue;
        if (it->parent && findLayer(it->parent)) continue; // drawn by its compound
        order.push_back(&*it);
    }
    for (size_t i = 0; i < order.size();) { // each run of neighboring 3D layers: farthest first
        size_t j = i;
        while (j < order.size() && order[j]->threeD) ++j;
        if (j > i + 1) {
            // Distance from the camera to each layer's position (w), whatever the view.
            std::vector<std::pair<float, const Layer*>> run;
            for (size_t k = i; k < j; ++k) {
                Projective view;
                const float w = viewFor(*order[k], t, view) ? view.w(order[k]->transform.position.valueAt(t)) : -1e30f;
                run.emplace_back(w, order[k]);
            }
            std::stable_sort(run.begin(), run.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t k = i; k < j; ++k) order[k] = run[k - i].second;
        }
        i = j > i ? j : i + 1;
    }
    return order;
}

// --- compounds -----------------------------------------------------------------------

Affine Composition::parentMatrix(const Layer& layer, double t) const {
    Affine m;
    const Layer* p = layer.parent ? findLayer(layer.parent) : nullptr;
    for (int depth = 0; p && depth < 32; ++depth) { // depth guard: never loops on a damaged file
        m = p->matrixAt(t) * m;
        p = p->parent ? findLayer(p->parent) : nullptr;
    }
    return m;
}

Affine Composition::worldMatrix(const Layer& layer, double t) const { return parentMatrix(layer, t) * layer.matrixAt(t); }

bool Composition::visibleAt(const Layer& layer, double t) const {
    if (!layer.activeAt(t)) return false;
    const Layer* p = layer.parent ? findLayer(layer.parent) : nullptr;
    for (int depth = 0; p && depth < 32; ++depth) {
        if (!p->activeAt(t)) return false;
        p = p->parent ? findLayer(p->parent) : nullptr;
    }
    return true;
}

bool Composition::isInside(const Layer& layer, uint64_t compoundId) const {
    const Layer* p = layer.parent ? findLayer(layer.parent) : nullptr;
    for (int depth = 0; p && depth < 32; ++depth) {
        if (p->id == compoundId) return true;
        p = p->parent ? findLayer(p->parent) : nullptr;
    }
    return false;
}

std::vector<uint64_t> Composition::descendantsOf(uint64_t compoundId) const {
    std::vector<uint64_t> out;
    for (const Layer& l : layers)
        if (isInside(l, compoundId)) out.push_back(l.id);
    return out;
}

uint64_t Composition::createCompound(const std::vector<uint64_t>& layerIds, double t) {
    // Only the outermost selected layers move in (the contents of a selected compound go along with it).
    std::vector<uint64_t> members;
    for (uint64_t id : layerIds) {
        const Layer* l = findLayer(id);
        if (!l || l->type == LayerType::Camera) continue;
        bool insideSelected = false;
        for (uint64_t other : layerIds)
            if (other != id && isInside(*l, other)) insideSelected = true;
        if (!insideSelected) members.push_back(id);
    }
    if (members.empty()) return 0;
    // The new compound lives where the first member lives, centered on the members.
    const uint64_t outerParent = findLayer(members.front())->parent;
    std::vector<uint64_t> grouped;
    for (uint64_t id : members)
        if (findLayer(id)->parent == outerParent) grouped.push_back(id); // one level at a time
    Vec2 center{0, 0};
    double in = 1e30, out = -1e30;
    int top = static_cast<int>(layers.size());
    for (uint64_t id : grouped) {
        const Layer& l = *findLayer(id);
        center = center + l.transform.position.valueAt(t);
        in = std::min(in, l.inPoint);
        out = std::max(out, l.outPoint);
        top = std::min(top, indexOf(id));
    }
    center = center / static_cast<float>(grouped.size());
    Layer compound = makeCompoundLayer(center, out - in);
    compound.inPoint = in;
    compound.outPoint = out;
    compound.parent = outerParent;
    const uint64_t cid = addLayer(std::move(compound), top).id;
    // Children keep their look: positions become relative to the compound's center.
    for (uint64_t id : grouped) {
        Layer& l = *findLayer(id);
        l.parent = cid;
        l.transform.position.base = l.transform.position.base - center;
        for (auto& k : l.transform.position.keys) k.value = k.value - center;
    }
    std::vector<uint64_t> tracks;
    for (uint64_t id : grouped) tracks.push_back(findLayer(id)->track);
    const uint64_t compoundTrack = findLayer(cid)->track;
    std::vector<TrackRow> rows = trackRows();
    int below = 0, removedAbove = 0;
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        if (rows[r].track == compoundTrack) below = r + 1;
        else if (std::find(tracks.begin(), tracks.end(), rows[r].track) != tracks.end() && below == 0) ++removedAbove;
    }
    moveTracks(tracks, below - removedAbove);
    return cid;
}

void Composition::releaseCompound(uint64_t compoundId, double t) {
    const Layer* found = findLayer(compoundId);
    if (!found || found->type != LayerType::Compound) return;
    const Layer compound = *found;
    for (Layer& l : layers) {
        if (l.parent != compoundId) continue;
        // Bake the compound's transform (at each key's time) into the child.
        auto& pos = l.transform.position;
        pos.base = compound.matrixAt(t).apply(pos.base);
        for (auto& k : pos.keys) {
            const Affine m = compound.matrixAt(k.time);
            k.value = m.apply(k.value);
            k.inTangent = m.applyVector(k.inTangent);
            k.outTangent = m.applyVector(k.outTangent);
        }
        auto& rot = l.transform.rotation;
        rot.base += compound.transform.rotation.valueAt(t);
        for (auto& k : rot.keys) k.value += compound.transform.rotation.valueAt(k.time);
        auto& scl = l.transform.scale;
        scl.base = scl.base * (compound.transform.scale.valueAt(t) * 0.01f);
        for (auto& k : scl.keys) k.value = k.value * (compound.transform.scale.valueAt(k.time) * 0.01f);
        auto& op = l.transform.opacity;
        op.base *= compound.transform.opacity.valueAt(t) * 0.01f;
        for (auto& k : op.keys) k.value *= compound.transform.opacity.valueAt(k.time) * 0.01f;
        // The compound's clip hid its children outside it: keep them hidden there.
        l.inPoint = std::max(l.inPoint, compound.inPoint);
        l.outPoint = std::min(l.outPoint, compound.outPoint);
        if (l.outPoint <= l.inPoint) l.outPoint = l.inPoint + frameDuration();
        l.parent = compound.parent;
        l.effects.insert(l.effects.end(), compound.effects.begin(), compound.effects.end());
    }
    removeLayer(compoundId);
}

void Composition::removeLayerTree(uint64_t id) {
    for (uint64_t child : descendantsOf(id)) removeLayer(child);
    removeLayer(id);
}

void Composition::syncCameras() {
    for (Layer& l : layers)
        if (l.type == LayerType::Camera) l.camera.frameSize = {static_cast<float>(width), static_cast<float>(height)};
}

} // namespace bemotion
