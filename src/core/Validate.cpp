#include "core/Validate.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace bemotion {

namespace {

bool finite(float v) { return std::isfinite(v); }
bool finite(double v) { return std::isfinite(v); }
bool finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
bool finite(Color c) { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a); }

std::string layerName(const Layer& l) { return "layer " + std::to_string(l.id) + " \"" + l.name + "\""; }

// Drops the vertices with numbers that are not numbers; true when some went.
bool cleanPath(BezierPath& path) {
    const size_t before = path.vertices.size();
    std::erase_if(path.vertices, [](const PathVertex& v) { return !finite(v.point) || !finite(v.in) || !finite(v.out); });
    return path.vertices.size() != before;
}

// Checks every rule and repairs as it goes; validate() runs it on a copy.
class Checker {
public:
    explicit Checker(Project& p) : p_(p) {}
    std::vector<Issue> run() {
        composition();
        mediaItems();
        layers();
        mattes();
        tracks();
        return std::move(issues_);
    }

private:
    void report(std::string where, std::string what) { issues_.push_back({std::move(where), std::move(what)}); }

    void composition() {
        Composition& c = p_.comp;
        if (c.width < 16 || c.width > 16384 || c.height < 16 || c.height > 16384) {
            report("composition", "size " + std::to_string(c.width) + "x" + std::to_string(c.height) + " is outside 16..16384");
            c.width = std::clamp(c.width, 16, 16384), c.height = std::clamp(c.height, 16, 16384);
        }
        if (!finite(c.fps) || c.fps < 1.0 || c.fps > 240.0) {
            report("composition", "frame rate must be between 1 and 240");
            c.fps = finite(c.fps) ? std::clamp(c.fps, 1.0, 240.0) : 30.0;
        }
        for (Layer& l : c.layers) {
            DataTable& t = l.plugin.table;
            if (!l.plugin.hasTable) continue;
            size_t width = t.header.size();
            for (const auto& r : t.rows) width = std::max(width, r.size());
            bool ragged = t.header.size() != width;
            for (auto& r : t.rows) ragged = ragged || r.size() != width;
            if (ragged) {
                report(layerName(l), "table rows must all have as many cells as the header (padded)");
                t.header.resize(width);
                for (auto& r : t.rows) r.resize(width);
            }
        }
        if (!finite(c.maxDuration) || c.maxDuration < 0.0) {
            report("composition", "the length limit must be zero (none) or a positive number of seconds");
            c.maxDuration = 0.0;
        }
        if (!finite(c.duration) || c.duration <= 0.0) {
            report("composition", "duration must be a positive number");
            c.fitDuration();
        }
        if (!finite(c.background)) {
            report("composition", "background color has invalid numbers");
            c.background = Color::fromHex(0x101012);
        }
        for (Guide& g : c.guides)
            if (!std::isfinite(g.position)) {
                report("composition", "a guide has an invalid position");
                g.position = 0.0f;
            }
        // Markers: at a real moment, in order, one per moment, with a label color.
        const auto before = c.markers;
        std::erase_if(c.markers, [](const Marker& m) { return !std::isfinite(m.time) || m.time < 0.0; });
        std::stable_sort(c.markers.begin(), c.markers.end(), [](const Marker& a, const Marker& b) { return a.time < b.time; });
        for (size_t i = 1; i < c.markers.size();)
            if (c.markers[i].time - c.markers[i - 1].time < kTimeEpsilon) c.markers.erase(c.markers.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        for (Marker& m : c.markers) m.color = std::clamp(m.color, 0, kLabelColorCount - 1);
        if (c.markers != before) report("composition", "markers out of order, repeated or at invalid times (put in order)");
    }

    void mediaItems() {
        Composition& c = p_.comp;
        std::set<uint64_t> seen;
        for (MediaItem& m : c.media) {
            if (m.id == 0 || seen.count(m.id)) {
                report("media \"" + m.name + "\"", "duplicate or missing id");
                m.id = c.nextMediaId++;
            }
            seen.insert(m.id);
            if (m.id >= c.nextMediaId) {
                report("composition", "next media id is not above every media id");
                c.nextMediaId = m.id + 1;
            }
        }
        // Folders: named, each once; every item's folder among them.
        std::vector<std::string> folders;
        for (const std::string& f : c.mediaFolders) {
            if (f.empty() || std::find(folders.begin(), folders.end(), f) != folders.end()) {
                report("composition", "a media folder without a name or listed twice (removed)");
                continue;
            }
            folders.push_back(f);
        }
        c.mediaFolders = std::move(folders);
        for (MediaItem& m : c.media)
            if (!m.folder.empty() && std::find(c.mediaFolders.begin(), c.mediaFolders.end(), m.folder) == c.mediaFolders.end()) {
                report("media \"" + m.name + "\"", "is in a folder that does not exist (the folder was added)");
                c.mediaFolders.push_back(m.folder);
            }
    }

    template <typename T>
    void animated(const std::string& where, Animated<T>& a, const T& fallback) {
        if (!finite(a.base)) {
            report(where, "static value is not a number");
            a.base = fallback;
        }
        const size_t before = a.keys.size();
        std::erase_if(a.keys, [](const Keyframe<T>& k) { return !finite(k.time) || k.time < 0.0 || !finite(k.value); });
        if (a.keys.size() != before) report(where, "keyframes with invalid times or values (removed)");
        if (!std::is_sorted(a.keys.begin(), a.keys.end(), [](const auto& x, const auto& y) { return x.time < y.time; })) {
            report(where, "keyframes out of order");
            a.sortKeys();
        }
        for (size_t i = 1; i < a.keys.size(); ++i)
            if (a.keys[i].time - a.keys[i - 1].time < kTimeEpsilon) {
                report(where, "two keyframes at the same time");
                a.removeDuplicateKeys();
                break;
            }
    }

    void layers() {
        Composition& c = p_.comp;
        std::set<uint64_t> ids;
        uint64_t maxTrack = 0;
        for (Layer& l : c.layers) {
            if (l.id == 0 || ids.count(l.id)) {
                report(layerName(l), "duplicate or missing id");
                l.id = c.nextLayerId++;
            }
            ids.insert(l.id);
            if (l.id >= c.nextLayerId) {
                report("composition", "next layer id is not above every layer id");
                c.nextLayerId = l.id + 1;
            }
            if (l.track == 0) {
                report(layerName(l), "no track");
                l.track = c.nextTrackId++;
            }
            maxTrack = std::max(maxTrack, l.track);
        }
        if (maxTrack >= c.nextTrackId) {
            report("composition", "next track id is not above every track id");
            c.nextTrackId = maxTrack + 1;
        }

        for (Layer& l : c.layers) {
            const std::string where = layerName(l);
            if (l.name.empty()) {
                report(where, "empty name");
                l.name = c.uniqueLayerName("Layer");
            }
            if (l.type == LayerType::Text && l.text.counter && l.text.subtitle) {
                report(where, "text cannot be both a counter and subtitles (kept subtitles)");
                l.text.counter = false;
            }
            if (l.type == LayerType::Text && (l.text.counterDecimals < 0 || l.text.counterDecimals > 6)) {
                report(where, "counter decimals must be 0 to 6");
                l.text.counterDecimals = std::clamp(l.text.counterDecimals, 0, 6);
            }
            if (l.type == LayerType::Text) {
                TextProps& tp = l.text;
                const auto fix = [&](auto& v, auto lo, auto hi, const char* what) {
                    if (!(v >= lo && v <= hi)) {
                        report(where, std::string(what) + " out of range");
                        v = std::clamp(v == v ? v : lo, lo, hi);
                    }
                };
                fix(tp.counterDuration, 0.0f, 3600.0f, "counter duration");
                fix(tp.letterDelay, 0.0f, 10.0f, "letter delay");
                fix(tp.letterDuration, 0.01f, 30.0f, "letter duration");
                fix(tp.fitLines, 0, 20, "fit lines");
            }
            if (l.type == LayerType::Text && !isCounterSeparator(l.text.counterSeparator)) {
                report(where, "counter separator must be \"\", \",\", \".\" or \" \" (none kept)");
                l.text.counterSeparator.clear();
            }
            if (static_cast<int>(l.type) < 0 || static_cast<int>(l.type) > static_cast<int>(LayerType::Background)) {
                report(where, "unknown layer type");
                l.type = LayerType::Shape;
            }
            if (static_cast<int>(l.shape.kind) < 0 || l.shape.kind >= ShapeKind::Count) {
                report(where, "unknown shape kind");
                l.shape.kind = ShapeKind::Rectangle;
            }
            for (ShapeFill* fill : {&l.shape.fill, &l.text.fill, &l.plugin.fill})
                if (static_cast<int>(*fill) < 0 || *fill >= ShapeFill::Count) {
                    report(where, "unknown fill");
                    *fill = ShapeFill::Solid;
                }
            if (cleanPath(l.shape.path)) report(where, "path points with invalid numbers (removed)");
            if (static_cast<int>(l.blend) < 0 || l.blend >= BlendMode::Count) {
                report(where, "unknown blending mode");
                l.blend = BlendMode::Normal;
            }
            if (static_cast<int>(l.matte) < 0 || l.matte >= MatteMode::Count) {
                report(where, "unknown track matte");
                l.matte = MatteMode::None;
            }
            if (l.matte == MatteMode::None) l.matteLayer = 0;
            // Paint and pins: finite points (a broken file's NaN would draw garbage), sane widths.
            for (PaintStroke& s : l.paint) {
                if (std::erase_if(s.points, [](const Vec2& v) { return !std::isfinite(v.x) || !std::isfinite(v.y); }) > 0)
                    report(where, "paint points with invalid numbers (removed)");
                if (!std::isfinite(s.width) || s.width < 0.5f || s.width > 2000.0f) {
                    report(where, "a paint stroke with an invalid width");
                    s.width = std::isfinite(s.width) ? std::clamp(s.width, 0.5f, 2000.0f) : 12.0f;
                }
            }
            if (std::erase_if(l.paint, [](const PaintStroke& s) { return s.points.empty(); }) > 0) report(where, "empty paint strokes (removed)");
            for (PuppetPin& p : l.pins)
                if (!std::isfinite(p.rest.x) || !std::isfinite(p.rest.y)) {
                    report(where, "a puppet pin at an invalid place");
                    p.rest = {};
                }
            for (Mask& m : l.masks) {
                if (static_cast<int>(m.mode) < 0 || m.mode >= MaskMode::Count) {
                    report(where, "a mask with an unknown mode");
                    m.mode = MaskMode::Add;
                }
                if (cleanPath(m.path)) report(where, "mask points with invalid numbers (removed)");
                for (Mask::PathKey& k : m.pathKeys)
                    if (cleanPath(k.path)) report(where, "mask path keyframe points with invalid numbers (removed)");
                if (std::erase_if(m.pathKeys, [](const Mask::PathKey& k) { return !std::isfinite(k.time); }) > 0)
                    report(where, "mask path keyframes at invalid times (removed)");
                if (!std::is_sorted(m.pathKeys.begin(), m.pathKeys.end(), [](const auto& a, const auto& b) { return a.time < b.time; })) {
                    report(where, "mask path keyframes out of order (sorted)");
                    std::sort(m.pathKeys.begin(), m.pathKeys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
                }
                if (m.name.empty()) m.name = "Mask";
            }
            if (l.type == LayerType::Camera && !l.masks.empty()) {
                report(where, "cameras take no masks (removed)");
                l.masks.clear();
            }
            if (!finite(l.media.speed) || l.media.speed < 0.01 || l.media.speed > 100.0) {
                report(where, "speed must be 1% to 10000%");
                l.media.speed = finite(l.media.speed) ? std::clamp(l.media.speed, 0.01, 100.0) : 1.0;
            }
            if (static_cast<int>(l.text.align) < 0 || static_cast<int>(l.text.align) > static_cast<int>(TextAlign::Right)) {
                report(where, "unknown text alignment");
                l.text.align = TextAlign::Center;
            }
            if (l.label < 0 || l.label >= kLabelColorCount) {
                report(where, "label color out of range");
                l.label = 0;
            }
            // Timing: [in, out) inside the timeline, at least one frame long.
            if (!finite(l.inPoint) || !finite(l.outPoint) || l.inPoint < 0.0 || l.outPoint - l.inPoint < c.frameDuration() - kTimeEpsilon) {
                report(where, "in / out points must satisfy 0 <= in < out (one frame at least)");
                if (!finite(l.inPoint) || l.inPoint < 0.0) l.inPoint = 0.0;
                if (!finite(l.outPoint) || l.outPoint - l.inPoint < c.frameDuration()) l.outPoint = l.inPoint + std::max(c.frameDuration(), 1.0);
            }
            // Compounds: the parent exists, is a compound, and no layer contains itself.
            if (l.parent != 0) {
                const Layer* parent = c.findLayer(l.parent);
                bool cycle = false;
                size_t steps = 0;
                for (const Layer* q = parent; q && !cycle && steps <= c.layers.size(); ++steps) {
                    if (q->id == l.id) cycle = true;
                    q = q->parent ? c.findLayer(q->parent) : nullptr;
                }
                if (steps > c.layers.size()) cycle = true;
                if (!parent || parent->type != LayerType::Compound || l.parent == l.id || cycle) {
                    report(where, "belongs to a compound that does not exist (now top level)");
                    l.parent = 0;
                }
            }
            if (l.type == LayerType::Text && !l.text.captions.empty()) { // sorted, each one after the last, lasting
                auto& caps = l.text.captions;
                const auto before = caps;
                std::erase_if(caps, [](const Caption& c) { return !std::isfinite(c.start) || !std::isfinite(c.end) || c.end <= c.start; });
                std::stable_sort(caps.begin(), caps.end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
                for (size_t i = 1; i < caps.size(); ++i) caps[i - 1].end = std::min(caps[i - 1].end, caps[i].start);
                std::erase_if(caps, [](const Caption& c) { return c.end <= c.start; });
                if (caps != before) report(where, "captions out of order or overlapping (put in order)");
            }
            if (l.threeD && (l.type == LayerType::Camera || l.type == LayerType::Background)) {
                report(where, "cameras and backgrounds are not 3D layers (switched off)");
                l.threeD = false;
            }
            if (l.type == LayerType::Background && l.background.mediaId && !c.findMedia(l.background.mediaId)) {
                report(layerName(l), "its background picture is not among the media items (cleared)");
                l.background.mediaId = 0;
            }
            if (l.type == LayerType::Media && !c.findMedia(l.media.mediaId)) {
                report(where, "uses a media item that is not in the project (a placeholder was added to relink)");
                MediaItem missing;
                missing.name = "Missing media";
                missing.kind = l.media.hasVideo ? MediaKind::Video : l.media.hasAudio ? MediaKind::Audio : MediaKind::Image;
                missing.hasVideo = l.media.hasVideo;
                missing.hasAudio = l.media.hasAudio;
                l.media.mediaId = c.addMedia(missing);
            }
            const size_t effectsBefore = l.effects.size();
            std::erase_if(l.effects, [](const Effect& e) {
                return static_cast<int>(e.type) < 0 || e.type >= EffectType::Count;
            });
            if (l.effects.size() != effectsBefore) report(where, "unknown effects (removed)");
            for (Effect& e : l.effects) {
                if (e.samples < 2 || e.samples > 64) {
                    report(where, "motion blur samples out of range");
                    e.samples = std::clamp(e.samples, 2, 64);
                }
                for (auto& curve : e.curves) {
                    const std::vector<Vec2> before = curve;
                    cleanCurve(curve);
                    if (curve != before) report(where, "curve points out of order or out of range");
                }
            }
            const size_t audioEffectsBefore = l.audioEffects.size();
            std::erase_if(l.audioEffects, [](const AudioEffect& e) {
                return static_cast<int>(e.type) < 0 || e.type >= AudioEffectType::Count || !std::isfinite(e.value);
            });
            if (l.audioEffects.size() != audioEffectsBefore) report(where, "unknown or invalid audio effects (removed)");
            for (AudioEffect& e : l.audioEffects) {
                const AudioEffect before = e;
                clampAudioEffect(e);
                if (e.value != before.value || e.value2 != before.value2 || e.value3 != before.value3)
                    report(where, std::string(audioEffectId(e.type)) + " values out of range");
            }
            for (ClipTransition* transition : {&l.transitionIn, &l.transitionOut}) {
                if (!std::isfinite(transition->duration) || transition->duration <= 0.0f) transition->id.clear(), transition->duration = 0.5f;
                if (transition->overlap && !transition->joined) transition->overlap = false; // a crossed cut has two sides
            }
            if (std::erase_if(l.animations, [](const AppliedAnimation& a) {
                    return !std::isfinite(a.start) || !std::isfinite(a.duration) || a.duration <= 0.0;
                }) > 0)
                report(where, "quick animation marks with bad times dropped");
            // What a Quick Animation replaced: finite, or forgotten (taking it off then just drops its keys).
            for (AppliedAnimation& a : l.animations) {
                const auto finite = [](const std::vector<float>& v) { return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); }); };
                if (std::erase_if(a.restore, [&](const AnimationRestore& r) {
                        return !finite(r.value) || std::any_of(r.keys.begin(), r.keys.end(), [&](const AnimationRestore::Key& k) {
                                   return !std::isfinite(k.time) || !finite(k.value);
                               });
                    }) > 0)
                    report(where, "quick animation restore values that were not numbers dropped");
                if (!finite(a.lettersBefore) || (!a.lettersBefore.empty() && a.lettersBefore.size() != 7)) a.lettersBefore.clear();
            }

            const Layer pristine = l; // for default values
            for (const PropertyRef& prop : l.properties()) {
                const std::string at = where + " / " + prop.id;
                const std::vector<float> def = defaultPropertyValue(pristine, prop.id, c.center());
                switch (prop.kind) {
                case PropertyKind::Float: animated(at, prop.asFloat(), def.empty() ? 0.0f : def[0]); break;
                case PropertyKind::Vec2: animated(at, prop.asVec2(), def.size() >= 2 ? Vec2{def[0], def[1]} : Vec2{}); break;
                case PropertyKind::Color:
                    animated(at, prop.asColor(), def.size() >= 4 ? Color{def[0], def[1], def[2], def[3]} : Color{1, 1, 1, 1});
                    break;
                }
            }
        }
    }

    // A track matte is another layer beside this one (same compound), not a camera, and no layer
    // ends up being its own matte through others.
    void mattes() {
        Composition& c = p_.comp;
        for (Layer& l : c.layers) {
            if (l.matte == MatteMode::None) continue;
            const Layer* m = c.findLayer(l.matteLayer);
            bool cycle = false;
            size_t steps = 0;
            for (const Layer* q = m; q && q->matte != MatteMode::None && !cycle; ++steps) {
                if (q->matteLayer == l.id || steps > c.layers.size()) cycle = true;
                q = c.findLayer(q->matteLayer);
            }
            if (!m || m->id == l.id || m->parent != l.parent || m->type == LayerType::Camera || cycle) {
                report(layerName(l), "its track matte is missing, elsewhere, a camera or itself (removed)");
                l.matte = MatteMode::None;
                l.matteLayer = 0;
            }
        }
    }

    void tracks() {
        Composition& c = p_.comp;
        // Clips of a track are next to each other in the stack.
        std::set<uint64_t> finished;
        uint64_t current = 0;
        bool split = false;
        for (const Layer& l : c.layers) {
            if (l.track == current) continue;
            if (finished.count(l.track)) split = true;
            if (current) finished.insert(current);
            current = l.track;
        }
        if (split) {
            report("composition", "a track's clips are not next to each other in the stack");
            c.normalizeTracks();
        }
        // ... and never overlap in time: the later clip moves to a track of its own.
        std::unordered_map<uint64_t, std::vector<Layer*>> byTrack;
        for (Layer& l : c.layers) byTrack[l.track].push_back(&l);
        bool moved = false;
        for (auto& [track, clips] : byTrack) {
            std::sort(clips.begin(), clips.end(), [](const Layer* a, const Layer* b) { return a->inPoint < b->inPoint; });
            double end = -1.0;
            for (Layer* l : clips) {
                if (l->inPoint < end - kTimeEpsilon) {
                    report(layerName(*l), "overlaps another clip on its track (moved to a new track)");
                    l->track = c.nextTrackId++;
                    moved = true;
                    continue;
                }
                end = std::max(end, l->outPoint);
            }
        }
        if (moved) c.normalizeTracks();
    }

    Project& p_;
    std::vector<Issue> issues_;
};

} // namespace

std::vector<Issue> validate(const Project& project) {
    Project copy = project; // the checks fix as they go; only the report matters here
    return Checker(copy).run();
}

std::vector<Issue> repair(Project& project) {
    std::vector<Issue> fixed = Checker(project).run();
    // A second pass catches anything the first repairs uncovered.
    for (int pass = 0; pass < 3 && !fixed.empty(); ++pass) {
        std::vector<Issue> more = Checker(project).run();
        if (more.empty()) break;
        fixed.insert(fixed.end(), more.begin(), more.end());
    }
    return fixed;
}

} // namespace bemotion
