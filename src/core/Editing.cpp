#include "core/Editing.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bemotion {

namespace {

constexpr double kEps = 1e-6;

// Moves a clip in time, a compound with everything inside it.
void shiftWithContents(Composition& comp, uint64_t id, double delta) {
    if (Layer* l = comp.findLayer(id)) l->shiftTime(delta);
    for (uint64_t child : comp.descendantsOf(id))
        if (Layer* c = comp.findLayer(child)) c->shiftTime(delta);
}

// The clips on `track` starting at or after `t` (but `except`).
std::vector<uint64_t> clipsFrom(const Composition& comp, uint64_t track, double t, uint64_t except = 0) {
    std::vector<uint64_t> ids;
    for (const Layer& l : comp.layers)
        if (l.track == track && l.id != except && l.inPoint >= t - kEps) ids.push_back(l.id);
    return ids;
}

} // namespace

int rippleDelete(Composition& comp, const std::vector<uint64_t>& ids) {
    struct Cut {
        uint64_t id, track;
        double in, out;
    };
    std::vector<Cut> cuts;
    for (uint64_t id : ids)
        if (const Layer* l = comp.findLayer(id)) cuts.push_back({id, l->track, l->inPoint, l->outPoint});
    // The latest first: closing a gap never moves the clips of an earlier one.
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.in > b.in; });
    int removed = 0;
    for (const Cut& c : cuts) {
        if (!comp.findLayer(c.id)) continue; // inside a compound already removed
        const std::vector<uint64_t> later = clipsFrom(comp, c.track, c.out, c.id);
        comp.removeLayerTree(c.id);
        ++removed;
        for (uint64_t id : later) shiftWithContents(comp, id, c.in - c.out);
    }
    return removed;
}

bool closeGap(Composition& comp, uint64_t track, double t) {
    double prevOut = 0.0, nextIn = std::numeric_limits<double>::infinity();
    for (const Layer& l : comp.layers) {
        if (l.track != track) continue;
        if (t > l.inPoint + kEps && t < l.outPoint - kEps) return false; // on a clip, not in a gap
        if (l.outPoint <= t + kEps) prevOut = std::max(prevOut, l.outPoint);
        if (l.inPoint >= t - kEps) nextIn = std::min(nextIn, l.inPoint);
    }
    if (!std::isfinite(nextIn) || nextIn - prevOut < kEps) return false;
    for (uint64_t id : clipsFrom(comp, track, nextIn)) shiftWithContents(comp, id, prevOut - nextIn);
    return true;
}

bool rippleTrim(Composition& comp, uint64_t id, ClipEdge edge, double t) {
    Layer* l = comp.findLayer(id);
    if (!l) return false;
    const double frame = comp.frameDuration();
    double from = 0, to = 0;
    comp.sourceSpan(*l, from, to);
    const std::vector<uint64_t> later = clipsFrom(comp, l->track, l->outPoint, id);
    if (edge == ClipEdge::Out) {
        const double out = std::clamp(t, l->inPoint + frame, std::min(to, comp.duration + kMaxOverhang));
        const double delta = out - l->outPoint;
        if (std::fabs(delta) < kEps) return false;
        l->outPoint = out;
        for (uint64_t other : later) shiftWithContents(comp, other, delta);
        return true;
    }
    const double in = std::clamp(t, from, l->outPoint - frame);
    const double delta = in - l->inPoint;
    if (std::fabs(delta) < kEps) return false;
    // Cut (or uncover) the start, then the clip and everything after it move so it starts where it did.
    l->inPoint = in;
    shiftWithContents(comp, id, -delta);
    for (uint64_t other : later) shiftWithContents(comp, other, -delta);
    return true;
}

uint64_t clipAfterCut(const Composition& comp, uint64_t left) {
    const Layer* a = comp.findLayer(left);
    if (!a) return 0;
    const double eps = comp.frameDuration() * 0.5;
    for (const Layer& b : comp.layers)
        if (b.track == a->track && b.id != a->id && std::fabs(b.inPoint - a->outPoint) < eps) return b.id;
    return 0;
}

bool rollEdit(Composition& comp, uint64_t left, double t) {
    Layer* a = comp.findLayer(left);
    Layer* b = comp.findLayer(clipAfterCut(comp, left));
    if (!a || !b) return false;
    const double frame = comp.frameDuration();
    double aFrom = 0, aTo = 0, bFrom = 0, bTo = 0;
    comp.sourceSpan(*a, aFrom, aTo);
    comp.sourceSpan(*b, bFrom, bTo);
    const double lo = std::max(a->inPoint + frame, bFrom), hi = std::min(b->outPoint - frame, aTo);
    if (lo > hi) return false;
    const double cut = std::clamp(t, lo, hi);
    if (std::fabs(cut - a->outPoint) < kEps && std::fabs(cut - b->inPoint) < kEps) return false;
    a->outPoint = cut;
    b->inPoint = cut;
    return true;
}

double slipClip(Composition& comp, uint64_t id, double delta) {
    Layer* l = comp.findLayer(id);
    if (!l || !std::isfinite(delta)) return 0.0;
    double from = 0, to = 0;
    comp.sourceSpan(*l, from, to);
    // The source has to keep covering the clip.
    if (std::isfinite(from)) delta = std::min(delta, l->inPoint - from);
    if (std::isfinite(to)) delta = std::max(delta, l->outPoint - to);
    if (std::fabs(delta) < kEps) return 0.0;
    const double in = l->inPoint, out = l->outPoint;
    l->shiftTime(delta);
    l->inPoint = in, l->outPoint = out;
    for (uint64_t child : comp.descendantsOf(id)) // a compound's contents move inside it
        if (Layer* c = comp.findLayer(child)) c->shiftTime(delta);
    return delta;
}

bool hasTimeline(const Composition& comp, const Layer& layer) {
    if (layer.type != LayerType::Media) return false;
    const MediaItem* item = comp.findMedia(layer.media.mediaId);
    return item ? item->kind != MediaKind::Image : layer.media.hasAudio;
}

bool setClipSpeed(Composition& comp, uint64_t id, double speed, bool reversed, bool ripple) {
    Layer* l = comp.findLayer(id);
    if (!l || !hasTimeline(comp, *l) || !std::isfinite(speed)) return false;
    speed = std::clamp(speed, 0.01, 100.0);
    MediaProps& m = l->media;
    const double frame = comp.frameDuration();
    const double sIn = m.sourceTime(l->inPoint), sOut = m.sourceTime(l->outPoint);
    const bool wasReversed = m.timeRemap ? sOut < sIn : m.reversed;
    const double span = std::fabs(sOut - sIn);
    // The frame shown first: the start's, or (turning around) the end's.
    const double first = reversed != wasReversed ? sOut : sIn;
    const double oldOut = l->outPoint;
    const std::vector<uint64_t> later = clipsFrom(comp, l->track, oldOut, id);
    m.timeRemap = false;
    m.remap = AnimFloat{0.0f};
    m.speed = speed;
    m.reversed = reversed;
    m.start = reversed ? l->inPoint + first / speed : l->inPoint - first / speed;
    double out = l->inPoint + std::max(frame, span / speed);
    double from = 0, to = 0;
    comp.sourceSpan(*l, from, to);
    out = std::min(out, to);
    if (!ripple) {
        double prevOut = 0, nextIn = 0;
        comp.neighborBounds(id, prevOut, nextIn);
        out = std::min(out, nextIn);
    }
    l->outPoint = std::max(l->inPoint + frame, out);
    if (ripple)
        for (uint64_t other : later) shiftWithContents(comp, other, l->outPoint - oldOut);
    return true;
}

bool setTimeRemap(Composition& comp, uint64_t id, bool on) {
    Layer* l = comp.findLayer(id);
    if (!l || !hasTimeline(comp, *l)) return false;
    MediaProps& m = l->media;
    if (on == m.timeRemap) return true;
    if (on) {
        const float sIn = static_cast<float>(m.sourceTime(l->inPoint)), sOut = static_cast<float>(m.sourceTime(l->outPoint));
        m.remap = AnimFloat{sIn};
        m.remap.setKey(l->inPoint, sIn, Easing::Linear);
        m.remap.setKey(l->outPoint, sOut, Easing::Linear);
    } else {
        m.remap = AnimFloat{0.0f};
    }
    m.timeRemap = on;
    return true;
}

bool freezeFrame(Composition& comp, uint64_t id, double t) {
    Layer* l = comp.findLayer(id);
    if (!l || !hasTimeline(comp, *l)) return false;
    t = std::clamp(t, l->inPoint, std::max(l->inPoint, l->outPoint - comp.frameDuration()));
    const float s = static_cast<float>(std::max(0.0, l->media.sourceTime(t)));
    l->media.remap = AnimFloat{s};
    l->media.timeRemap = true;
    return true;
}

uint64_t separateAudio(Composition& comp, uint64_t id) {
    Layer* l = comp.findLayer(id);
    if (!l || l->type != LayerType::Media || !l->media.hasVideo || !l->media.hasAudio) return 0;
    Layer sound = *l;
    sound.track = 0; // a new track
    sound.name = l->name + " (audio)";
    sound.media.hasVideo = false;
    sound.effects.clear(); // the picture's
    sound.masks.clear();
    sound.matte = MatteMode::None;
    sound.matteLayer = 0;
    sound.transitionIn = sound.transitionOut = {};
    sound.threeD = false;
    l->media.hasAudio = false; // the picture alone now
    l->audioEffects.clear();
    // Right below the video's track.
    int below = comp.indexOf(id) + 1;
    for (const TrackRow& r : comp.trackRows())
        if (r.track == l->track) below = r.first + r.count;
    return comp.addLayer(std::move(sound), below).id;
}

bool linkAudio(Composition& comp, uint64_t videoId, uint64_t audioId, bool* inSync) {
    Layer* video = comp.findLayer(videoId);
    const Layer* audio = comp.findLayer(audioId);
    if (!video || !audio || video == audio || video->type != LayerType::Media || audio->type != LayerType::Media) return false;
    if (!video->media.hasVideo || video->media.hasAudio || audio->media.hasVideo || !audio->media.hasAudio) return false;
    if (video->media.mediaId != audio->media.mediaId) return false;
    if (inSync)
        *inSync = std::fabs(video->media.start - audio->media.start) < comp.frameDuration() * 0.5 &&
                  std::fabs(video->media.speed - audio->media.speed) < 1e-9 && video->media.reversed == audio->media.reversed;
    video->media.hasAudio = true;
    video->media.volume = audio->media.volume;
    video->audioEffects = audio->audioEffects;
    video->muted = audio->muted;
    comp.removeLayerTree(audioId);
    return true;
}

uint64_t audioToLink(const Composition& comp, uint64_t videoId, const std::vector<uint64_t>& preferred) {
    const Layer* video = comp.findLayer(videoId);
    if (!video || video->type != LayerType::Media || !video->media.hasVideo || video->media.hasAudio) return 0;
    auto fits = [&](const Layer& l) {
        return l.id != videoId && l.type == LayerType::Media && !l.media.hasVideo && l.media.hasAudio && l.media.mediaId == video->media.mediaId;
    };
    for (uint64_t id : preferred)
        if (const Layer* l = comp.findLayer(id); l && fits(*l)) return id;
    uint64_t best = 0;
    double bestOverlap = 0.0;
    for (const Layer& l : comp.layers)
        if (fits(l)) {
            const double overlap = std::min(l.outPoint, video->outPoint) - std::max(l.inPoint, video->inPoint);
            if (overlap > bestOverlap) best = l.id, bestOverlap = overlap;
        }
    return best;
}

// --- markers ----------------------------------------------------------------------------------

int markerAt(const Composition& comp, double t) {
    for (size_t i = 0; i < comp.markers.size(); ++i)
        if (std::fabs(comp.markers[i].time - t) < comp.frameDuration() * 0.5) return static_cast<int>(i);
    return -1;
}

namespace {
int insertMarker(Composition& comp, Marker m) {
    const auto at = std::upper_bound(comp.markers.begin(), comp.markers.end(), m.time,
                                     [](double time, const Marker& other) { return time < other.time; });
    const auto placed = comp.markers.insert(at, std::move(m)); // (insert may move the vector: begin() after it)
    return static_cast<int>(placed - comp.markers.begin());
}
int markerColor(int color) { return std::clamp(color, 0, kLabelColorCount - 1); }
} // namespace

int addMarker(Composition& comp, double t, const std::string& name, int color) {
    t = comp.snapToFrame(std::max(0.0, t));
    if (const int i = markerAt(comp, t); i >= 0) {
        if (!name.empty()) comp.markers[i].name = name;
        if (color >= 0) comp.markers[i].color = markerColor(color);
        return i;
    }
    return insertMarker(comp, {t, name, color < 0 ? 0 : markerColor(color)});
}

int moveMarker(Composition& comp, int index, double t) {
    if (index < 0 || index >= static_cast<int>(comp.markers.size())) return -1;
    Marker m = comp.markers[index];
    comp.markers.erase(comp.markers.begin() + index);
    m.time = comp.snapToFrame(std::max(0.0, t));
    if (const int there = markerAt(comp, m.time); there >= 0) { // merges with the one it lands on
        if (m.name.empty()) m.name = comp.markers[there].name;
        comp.markers.erase(comp.markers.begin() + there);
    }
    return insertMarker(comp, std::move(m));
}

bool nextMarker(const Composition& comp, double t, bool forward, double& out) {
    const double eps = comp.frameDuration() * 0.5;
    if (forward) {
        for (const Marker& m : comp.markers)
            if (m.time > t + eps) return out = m.time, true;
    } else {
        for (auto it = comp.markers.rbegin(); it != comp.markers.rend(); ++it)
            if (it->time < t - eps) return out = it->time, true;
    }
    return false;
}

} // namespace bemotion
