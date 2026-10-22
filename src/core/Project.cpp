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

int Composition::frameCount() const { return std::max(1, static_cast<int>(std::lround(duration * fps))); }

int Composition::frameAt(double t) const {
    // Small bias so times exactly on a frame boundary never round down.
    return std::clamp(static_cast<int>(std::floor(t * fps + 1e-6)), 0, frameCount() - 1);
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
        // Stay inside the composition (clips that already stick out may still move back in).
        minDelta = std::max(minDelta, std::min(0.0, -l->inPoint));
        maxDelta = std::min(maxDelta, std::max(0.0, duration - l->outPoint));
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
    maxOut = std::min(duration, nextIn);
}

uint64_t Composition::splitLayer(uint64_t id, double t) {
    const int index = indexOf(id);
    if (index < 0) return 0;
    Layer& original = layers[index];
    const double eps = frameDuration() * 0.5;
    if (t <= original.inPoint + eps || t >= original.outPoint - eps) return 0;
    Layer tail = original; // same track; keyframes kept on both halves so motion stays continuous
    original.outPoint = t;
    tail.inPoint = t;
    return addLayer(std::move(tail)).id;
}

} // namespace bemotion
