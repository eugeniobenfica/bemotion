// Tracks and clips: moving, splitting, trimming, transitions, copy and paste.

#include "Check.h"

#include "core/Arrange.h"
#include "core/Clipboard.h"
#include "core/Editing.h"
#include "core/Ops.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Transitions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace bemotion;

namespace {

// Builds a composition with layers named "A".."E" from top to bottom.
Composition makeStack() {
    Composition c;
    for (const char* name : {"E", "D", "C", "B", "A"}) {
        Layer l = makeShapeLayer(ShapeKind::Rectangle, {}, 10.0);
        l.name = name;
        c.addLayer(std::move(l));
    }
    return c;
}

std::string order(const Composition& c) {
    std::string s;
    for (const auto& l : c.layers)
        s += l.name;
    return s;
}

uint64_t idOf(const Composition& c, const char* name) {
    for (const auto& l : c.layers)
        if (l.name == name) return l.id;
    return 0;
}

uint64_t trackOf(const Composition& c, const char* name) { return c.findLayer(idOf(c, name))->track; }

// Track rows as text: "A|BC|D" means three rows, the middle one holding B and C.
std::string rowsText(const Composition& c) {
    std::string s;
    for (const TrackRow& r : c.trackRows()) {
        if (!s.empty()) s += "|";
        for (int i = r.first; i < r.first + r.count; ++i)
            s += c.layers[i].name;
    }
    return s;
}

void testMoveTracks() {
    Composition c = makeStack();
    CHECK(rowsText(c) == "A|B|C|D|E");
    c.moveTracks({trackOf(c, "B"), trackOf(c, "D")}, 0);
    CHECK(order(c) == "BDACE");
    c.moveTracks({trackOf(c, "B")}, 99);
    CHECK(order(c) == "DACEB");
}

void testShiftTracks() {
    Composition c = makeStack();
    c.shiftTracks({trackOf(c, "C"), trackOf(c, "D")}, -1);
    CHECK(order(c) == "ACDBE");
    c.shiftTracks({trackOf(c, "A")}, -1); // already on top
    CHECK(order(c) == "ACDBE");
    c.shiftTracks({trackOf(c, "A"), trackOf(c, "C")}, 99); // to the bottom
    CHECK(order(c) == "DBEAC");
}

void testSplitLayer() {
    Composition c;
    c.fps = 10;
    Layer& l = c.addLayer(makeShapeLayer(ShapeKind::Star, {}, 4.0));
    l.transform.opacity.setKey(0.0, 0.0f, Easing::Linear);
    l.transform.opacity.setKey(4.0, 100.0f);
    const uint64_t original = l.id;
    CHECK(c.splitLayer(original, 0.0) == 0); // at the edge: nothing to cut
    const uint64_t tail = c.splitLayer(original, 1.5);
    CHECK(tail != 0);
    CHECK(c.layers.size() == 2);
    CHECK(c.trackRows().size() == 1); // both halves stay on the same track
    CHECK(c.findLayer(tail)->track == c.findLayer(original)->track);
    CHECK(c.findLayer(original)->outPoint == 1.5);
    CHECK(c.findLayer(tail)->inPoint == 1.5 && c.findLayer(tail)->outPoint == 4.0);
    // Animation continues seamlessly across the cut.
    CHECK(near(c.findLayer(tail)->transform.opacity.valueAt(2.0), 50.0f));
}

// Splits "A" (0..10 s) at 5 s: A keeps 0..5, "A2" gets 5..10 on the same track.
Composition makeSplitStack() {
    Composition c = makeStack();
    const uint64_t tail = c.splitLayer(idOf(c, "A"), 5.0);
    c.findLayer(tail)->name = "a";
    return c;
}

void testClipsBetweenTracks() {
    Composition c = makeSplitStack();
    CHECK(rowsText(c) == "aA|B|C|D|E");
    // Pull the tail onto its own new track below C, then put it back.
    c.moveClipsToNewTracks({idOf(c, "a")}, 3);
    CHECK(rowsText(c) == "A|B|C|a|D|E");
    CHECK(c.clipsFitTrack({idOf(c, "a")}, trackOf(c, "A")));  // 5..10 next to 0..5: fits
    CHECK(!c.clipsFitTrack({idOf(c, "a")}, trackOf(c, "B"))); // B spans 0..10: overlaps
    CHECK(!c.moveClipsToTrack({idOf(c, "a")}, trackOf(c, "B")));
    CHECK(rowsText(c) == "A|B|C|a|D|E"); // refused moves change nothing
    CHECK(c.moveClipsToTrack({idOf(c, "a")}, trackOf(c, "A")));
    CHECK(c.trackRows().size() == 5); // the empty track disappeared
    CHECK(trackOf(c, "a") == trackOf(c, "A"));
}

void testTimeShiftLimits() {
    Composition c = makeSplitStack(); // A: 0..5, a: 5..10 on one track
    double lo = 0, hi = 0;
    c.timeShiftLimits({idOf(c, "A")}, lo, hi);
    CHECK(near(static_cast<float>(lo), 0.0f)); // A starts at 0: cannot move earlier
    CHECK(near(static_cast<float>(hi), 0.0f)); // touching "a" already
    c.findLayer(idOf(c, "a"))->inPoint = 7.0;
    c.timeShiftLimits({idOf(c, "A")}, lo, hi);
    CHECK(near(static_cast<float>(hi), 2.0f));
    double prevOut = 0, nextIn = 0;
    c.neighborBounds(idOf(c, "a"), prevOut, nextIn);
    CHECK(near(static_cast<float>(prevOut), 5.0f) && std::isinf(nextIn));
}

void testTracksRoundTrip() {
    Project p;
    p.comp = makeSplitStack();
    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    CHECK(rowsText(q.comp) == rowsText(p.comp));
    CHECK(q.comp.nextTrackId == p.comp.nextTrackId);
}

void testCopyPaste() {
    Project p;
    run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "A", "start": 1},
               {"op": "layer.add", "type": "text", "name": "B", "start": 2},
               {"op": "group", "layers": ["A", "B"], "name": "G"}])");
    const uint64_t g = p.comp.layers.front().id;
    const std::string clip = copyLayers(p.comp, {g});
    CHECK(isCopiedLayers(clip) && !isCopiedLayers(serializeProject(p)));
    const size_t before = p.comp.layers.size();
    const auto added = pasteLayers(p.comp, clip, 4.0, 0);
    CHECK(added.size() == 1 && p.comp.layers.size() == before + 3); // the compound and its two children
    const Layer* copy = p.comp.findLayer(added[0]);
    CHECK(copy && copy->type == LayerType::Compound && near(static_cast<float>(copy->inPoint), 4.0f));
    CHECK(p.comp.descendantsOf(added[0]).size() == 2);
    CHECK(validate(p).empty());
    CHECK(pasteLayers(p.comp, "not a clip", 0, 0).empty());
}

void testArrangeAndTransitions() {
    Project p;
    run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "A", "position": [100, 500]},
               {"op": "layer.add", "type": "rectangle", "name": "B", "position": [900, 500]},
               {"op": "layer.add", "type": "rectangle", "name": "C", "position": [300, 500]}])");
    CHECK(run(p, R"({"op": "arrange.distribute", "layers": ["A", "B", "C"]})").ok);
    auto x = [&](const char* name) {
        for (const Layer& l : p.comp.layers)
            if (l.name == name) return l.transform.position.valueAt(0).x;
        return -1.0f;
    };
    CHECK(near(x("A"), 100) && near(x("C"), 500) && near(x("B"), 900));
    CHECK(run(p, R"({"op": "arrange.stagger", "layers": "all", "step": 0.5})").ok);
    CHECK(validate(p).empty());
    CHECK(
        run(p, R"({"op": "transition.add", "layer": "A", "transition": "fade", "edge": "both", "duration": 0.5})").ok);
    Layer* a = nullptr;
    for (Layer& l : p.comp.layers)
        if (l.name == "A") a = &l;
    CHECK(a && a->transform.opacity.keyCount() == 4);
    CHECK(near(a->transform.opacity.valueAt(a->inPoint), 0.0f) &&
          near(a->transform.opacity.valueAt(a->inPoint + 1.0), 100.0f));
    CHECK(a && resizeTransition(*a, TransitionEdge::In, 0.75));
    CHECK(a && near(a->transitionIn.duration, 0.75f));
    CHECK(a && removeTransition(*a, TransitionEdge::Both));
    CHECK(a && !a->transitionIn.active() && !a->transitionOut.active() && a->transform.opacity.keyCount() == 0);
    CHECK(!run(p, R"({"op": "transition.add", "layer": "A", "transition": "fdae"})").ok);
}

void testCutTransitions() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "Clip", "duration": 4},
                     {"op": "layer.split", "layer": "Clip", "time": 2},
                     {"op": "layer.add", "type": "camera"}])")
              .ok);
    Composition& c = p.comp;
    Layer* first = nullptr;
    for (Layer& l : c.layers)
        if (l.name == "Clip" && near(static_cast<float>(l.inPoint), 0.0f)) first = &l;
    CHECK(first && nextOnTrack(c, *first));
    const std::string cut =
        R"({"op": "transition.add", "transition": "fade", "edge": "cut", "duration": 1, "layer": )" +
        std::to_string(first ? first->id : 0) + "}";
    const uint64_t firstId = first ? first->id : 0;
    CHECK(run(p, cut.c_str()).ok);
    first = c.findLayer(firstId); // operations replace the composition as a whole
    Layer* second = first ? nextOnTrack(c, *first) : nullptr;
    CHECK(first && second && first->transitionOut.joined && second->transitionIn.joined);
    CHECK(near(first->transitionOut.duration, 0.5f) &&
          near(second->transitionIn.duration, 0.5f)); // half each side of the cut
    // A cross dissolve: the clips overlap around the cut, the next one fading in above the first,
    // which stays whole under it (no dip to the background between two fades).
    CHECK(first->transitionOut.overlap && second->transitionIn.overlap);
    CHECK(first->activeAt(2.25) && second->activeAt(1.75) && !first->activeAt(2.6) && !second->activeAt(1.4));
    CHECK(near(first->transform.opacity.valueAt(2.25), 100.0f) && near(second->transform.opacity.valueAt(2.5), 100.0f));
    CHECK(second->transform.opacity.valueAt(2.0) > 10.0f && second->transform.opacity.valueAt(2.0) < 90.0f);
    CHECK(c.indexOf(second->id) < c.indexOf(first->id)); // above it in the stack
    {
        Project saved;
        CHECK(deserializeProject(serializeProject(p), saved));
        const Layer* s = saved.comp.findLayer(second->id);
        CHECK(s && s->transitionIn.overlap);
    }
    CHECK(transitionPartner(c, *second, TransitionEdge::In) == first);
    CHECK(resizeTransition(c, *second, TransitionEdge::In, 0.8));
    CHECK(near(first->transitionOut.duration, 0.8f)); // the other half follows
    CHECK(removeTransition(c, *first, TransitionEdge::Out));
    CHECK(!first->transitionOut.active() && !second->transitionIn.active());
    // Cameras and sound clips are refused; 3D transitions make the layer 3D.
    for (const Layer& l : c.layers)
        if (l.type == LayerType::Camera) CHECK(!canTakeTransition(l));
    CHECK(canTakeTransition(*first, "turn") && applyTransition(*first, "turn", TransitionEdge::In, 0.5) &&
          first->threeD);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && validate(back).empty());
}

void testSeparateAudio() {
    Project p;
    MediaItem m;
    m.name = "talk.mp4";
    m.path = "talk.mp4";
    m.kind = MediaKind::Video;
    m.hasVideo = m.hasAudio = true;
    m.width = 640;
    m.height = 360;
    m.duration = 4.0;
    const uint64_t mediaId = p.comp.addMedia(m);
    CHECK(run(p, (R"([{"op": "layer.add", "type": "media", "name": "Talk", "media": )" + std::to_string(mediaId) + "}]")
                     .c_str())
              .ok);
    Layer* video = nullptr;
    for (Layer& l : p.comp.layers)
        if (l.name == "Talk") video = &l;
    CHECK(video);
    const uint64_t videoId = video ? video->id : 0;
    const OpsResult r = run(p, R"({"op": "layer.separate_audio", "layer": "Talk"})");
    CHECK(r.ok);
    const Layer* v = p.comp.findLayer(videoId);
    const Layer* a = nullptr;
    for (const Layer& l : p.comp.layers)
        if (l.name == "Talk (audio)") a = &l;
    CHECK(v && a && v->media.hasVideo && !v->media.hasAudio && a->media.hasAudio && !a->media.hasVideo);
    CHECK(a && v && a->track != v->track && near(static_cast<float>(a->inPoint), static_cast<float>(v->inPoint)) &&
          near(static_cast<float>(a->outPoint), static_cast<float>(v->outPoint)));
    CHECK(a && v && p.comp.indexOf(a->id) > p.comp.indexOf(v->id));          // below the picture
    CHECK(!run(p, R"({"op": "layer.separate_audio", "layer": "Talk"})").ok); // no sound left to separate
    CHECK(validate(p).empty());
    // And back: the sound clip goes into the video again.
    const OpsResult linked = run(p, R"({"op": "layer.link_audio", "layer": "Talk"})");
    CHECK(linked.ok);
    v = p.comp.findLayer(videoId);
    CHECK(v && v->media.hasVideo && v->media.hasAudio);
    bool gone = true;
    for (const Layer& l : p.comp.layers)
        gone = gone && l.name != "Talk (audio)";
    CHECK(gone);
    CHECK(!run(p, R"({"op": "layer.link_audio", "layer": "Talk"})").ok); // it has its sound already
}

void testClipTiming() {
    Project p;
    MediaItem video;
    video.name = "take.mp4";
    video.path = "C:/footage/take.mp4";
    video.kind = MediaKind::Video;
    video.width = 640;
    video.height = 360;
    video.duration = 10.0;
    video.fps = 30.0;
    video.hasVideo = video.hasAudio = true;
    const uint64_t media = p.comp.addMedia(video);
    Composition& c = p.comp;
    c.duration = 60.0;
    const uint64_t id = c.addLayer(makeMediaLayer(*c.findMedia(media), {0, 0}, 0.0, 5.0)).id;
    c.setInPoint(id, 2.0); // shows source 2..10
    auto clip = [&]() -> Layer& { return *c.findLayer(id); };

    // Twice as fast: the frame at the start stays, the clip lasts half as long.
    CHECK(setClipSpeed(c, id, 2.0, false));
    CHECK(near(static_cast<float>(clip().media.sourceTime(2.0)), 2.0f));
    CHECK(near(static_cast<float>(clip().outPoint), 6.0f)); // 8 s of source at 200%
    CHECK(near(static_cast<float>(clip().media.sourceTime(5.0)), 8.0f));
    double minIn = 0, maxOut = 0;
    c.clipLimits(id, minIn, maxOut);
    CHECK(near(static_cast<float>(maxOut), 6.0f) &&
          near(static_cast<float>(minIn), 1.0f)); // the source's ends at this speed
    // Backwards: the same footage, from its end.
    CHECK(setClipSpeed(c, id, 2.0, true));
    CHECK(near(static_cast<float>(clip().media.sourceTime(2.0)), 10.0f) &&
          near(static_cast<float>(clip().outPoint), 6.0f));
    CHECK(near(static_cast<float>(clip().media.sourceTime(5.9)), 2.2f));
    // Time remap: keys at the ends with what they showed; a frozen frame: one value.
    CHECK(setTimeRemap(c, id, true));
    CHECK(clip().media.timeRemap && clip().media.remap.keyCount() == 2 &&
          near(clip().media.remap.keys[0].value, 10.0f));
    auto props = clip().properties();
    CHECK(clip().findProperty(props, "time_remap") != nullptr);
    CHECK(freezeFrame(c, id, 4.0));
    CHECK(near(static_cast<float>(clip().media.sourceTime(2.0)), 6.0f) &&
          near(static_cast<float>(clip().media.sourceTime(5.0)), 6.0f));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer& again = *back.comp.findLayer(id);
    CHECK(again.media.timeRemap && again.media.reversed && near(static_cast<float>(again.media.speed), 2.0f) &&
          near(again.media.remap.base, 6.0f));
    CHECK(setTimeRemap(c, id, false) && !clip().media.timeRemap);
    // Stills have no speed.
    MediaItem still;
    still.name = "logo.png";
    still.path = "C:/footage/logo.png";
    still.kind = MediaKind::Image;
    still.hasVideo = true;
    const uint64_t logo = c.addLayer(makeMediaLayer(*c.findMedia(c.addMedia(still)), {0, 0}, 0.0, 3.0)).id;
    CHECK(!setClipSpeed(c, logo, 2.0, false) && !freezeFrame(c, logo, 1.0));

    // Ripple, roll, slip on one track: A [0, 2), B [2, 4), C [5, 7).
    Composition t;
    t.duration = 30.0;
    const uint64_t A = t.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 2.0)).id;
    Layer bLayer = makeShapeLayer(ShapeKind::Ellipse, {}, 2.0);
    bLayer.track = t.findLayer(A)->track;
    bLayer.shiftTime(2.0);
    bLayer.transform.opacity.setKey(3.0, 50.0f);
    const uint64_t B = t.addLayer(bLayer).id;
    Layer cLayer = makeShapeLayer(ShapeKind::Star, {}, 2.0);
    cLayer.track = bLayer.track;
    cLayer.shiftTime(5.0);
    const uint64_t C = t.addLayer(cLayer).id;
    auto in = [&](uint64_t x) { return static_cast<float>(t.findLayer(x)->inPoint); };
    auto out = [&](uint64_t x) { return static_cast<float>(t.findLayer(x)->outPoint); };
    CHECK(t.trackRows().size() == 1);
    CHECK(rollEdit(t, A, 1.5) && near(out(A), 1.5f) && near(in(B), 1.5f) && near(out(B), 4.0f));
    CHECK(!rollEdit(t, B, 4.5)); // a gap after B: no cut to roll
    CHECK(rippleTrim(t, A, ClipEdge::Out, 1.0) && near(out(A), 1.0f) && near(in(B), 1.0f) && near(in(C), 4.5f));
    CHECK(rippleTrim(t, B, ClipEdge::In, 1.5)); // the start cut away, B still starts at 1
    CHECK(near(in(B), 1.0f) && near(out(B), 3.0f) && near(in(C), 4.0f));
    CHECK(near(t.findLayer(B)->transform.opacity.keys[0].time, 2.0f)); // its content moved with it
    CHECK(near(slipClip(t, B, 0.25), 0.25f) && near(in(B), 1.0f) &&
          near(t.findLayer(B)->transform.opacity.keys[0].time, 2.25f));
    CHECK(closeGap(t, t.findLayer(C)->track, 3.5) && near(in(C), 3.0f));
    CHECK(rippleDelete(t, {A}) == 1 && near(in(B), 0.0f) && near(in(C), 2.0f));
    CHECK(validate(Project{"t", t}).empty());

    // The same through operations.
    Project ops;
    CHECK(applyOps(ops, nlohmann::json::parse(R"([
        {"op": "layer.add", "type": "rectangle", "name": "One", "duration": 2},
        {"op": "layer.add", "type": "rectangle", "name": "Two", "duration": 2, "start": 2},
        {"op": "marker.add", "time": 1, "name": "Beat"},
        {"op": "marker.add", "time": 0.5, "color": 3}
    ])"))
              .ok);
    // Two on One's track, right after it.
    Composition& oc = ops.comp;
    CHECK(applyOps(ops, nlohmann::json::parse(R"({"op": "layer.set", "layer": "Two", "track_of": "One"})")).ok);
    CHECK(oc.trackRows().size() == 1);
    CHECK(!applyOps(ops,
                    nlohmann::json::parse(R"({"op": "layer.add", "type": "ellipse", "start": 1, "track_of": "One"})"))
               .ok); // overlaps
    auto r = applyOps(ops, nlohmann::json::parse(R"([
        {"op": "layer.roll", "layer": "One", "time": 1.5},
        {"op": "layer.trim", "layer": "Two", "edge": "out", "time": 3, "ripple": true},
        {"op": "marker.set", "time": 1, "to": 2, "name": "Drop"},
        {"op": "query.composition"}
    ])"));
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.error.c_str());
    CHECK(oc.markers.size() == 2 && oc.markers[0].color == 3 && oc.markers[1].name == "Drop" &&
          near(static_cast<float>(oc.markers[1].time), 2.0f));
    CHECK(r.results.size() == 4 && r.results[3]["markers"].size() == 2);
    CHECK(
        applyOps(ops, nlohmann::json::parse(
                          R"([{"op": "layer.ripple_delete", "layers": ["One"]}, {"op": "marker.remove", "index": 0}])"))
            .ok);
    CHECK(oc.markers.size() == 1 && oc.layers.size() == 1 && near(static_cast<float>(oc.layers[0].inPoint), 0.0f));
    CHECK(!applyOps(ops, nlohmann::json::parse(R"({"op": "layer.speed", "layer": "Two", "speed": 50})"))
               .ok); // not footage
    // Markers: one per moment, in order, stepping between them.
    Composition m;
    m.duration = 10.0;
    addMarker(m, 3.0, "C");
    addMarker(m, 1.0, "A");
    CHECK(addMarker(m, 1.01, "", 2) == 0 && m.markers.size() == 2 && m.markers[0].name == "A" &&
          m.markers[0].color == 2);
    double next = 0;
    CHECK(nextMarker(m, 1.0, true, next) && near(static_cast<float>(next), 3.0f));
    CHECK(nextMarker(m, 3.0, false, next) && near(static_cast<float>(next), 1.0f) && !nextMarker(m, 3.0, true, next));
    CHECK(moveMarker(m, 1, 0.5) == 0 && m.markers[0].name == "C");
    Project mp;
    mp.comp = m;
    mp.comp.markers.push_back({0.2, "late", 9});
    CHECK(!validate(mp).empty());
    repair(mp);
    CHECK(validate(mp).empty() && mp.comp.markers.front().name == "late" &&
          mp.comp.markers.front().color == kLabelColorCount - 1);
    Project mback;
    CHECK(deserializeProject(serializeProject(mp), mback) && mback.comp.markers == mp.comp.markers);
}

} // namespace

Tests timelineTests() {
    return {
        {"move tracks", testMoveTracks},
        {"shift tracks", testShiftTracks},
        {"split layer", testSplitLayer},
        {"clips between tracks", testClipsBetweenTracks},
        {"time shift limits", testTimeShiftLimits},
        {"tracks round trip", testTracksRoundTrip},
        {"copy and paste", testCopyPaste},
        {"arrange and transitions", testArrangeAndTransitions},
        {"cut transitions", testCutTransitions},
        {"separate audio", testSeparateAudio},
        {"clip speed, ripple, roll, slip, markers", testClipTiming},
    };
}
