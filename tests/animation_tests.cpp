// Keyframes, easing, presets, cameras and motion: how values change over time.

#include "Check.h"

#include "core/Animated.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/Ops.h"
#include "core/Presets.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Snapping.h"
#include "core/Transitions.h"
#include "core/Validate.h"

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

void testStaticValue() {
    AnimFloat a{5.0f};
    CHECK(!a.isAnimated());
    CHECK(near(a.valueAt(3.0), 5.0f));
    a.set(1.0, 7.0f);
    CHECK(near(a.valueAt(0.0), 7.0f));
}

void testLinearInterpolation() {
    AnimFloat a;
    a.setKey(0.0, 0.0f, Easing::Linear);
    a.setKey(2.0, 10.0f, Easing::Linear);
    CHECK(near(a.valueAt(-1.0), 0.0f));
    CHECK(near(a.valueAt(1.0), 5.0f));
    CHECK(near(a.valueAt(5.0), 10.0f));
}

void testKeysStaySorted() {
    AnimVec2 a;
    a.setKey(2.0, {2, 2});
    a.setKey(0.0, {0, 0});
    a.setKey(1.0, {1, 1});
    CHECK(a.keyCount() == 3);
    CHECK(a.keyTime(0) == 0.0 && a.keyTime(1) == 1.0 && a.keyTime(2) == 2.0);
    a.setKey(1.0, {5, 5}); // same instant replaces
    CHECK(a.keyCount() == 3);
    CHECK(near(a.valueAt(1.0).x, 5.0f));
}

void testRemovingLastKeyKeepsValue() {
    AnimFloat a;
    a.setKey(1.0, 42.0f);
    a.removeKey(0);
    CHECK(!a.isAnimated());
    CHECK(near(a.valueAt(0.0), 42.0f));
}

void testEasingEndpoints() {
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        CHECK(near(applyEasing(e, 0.0f), 0.0f));
        CHECK(near(applyEasing(e, 1.0f), 1.0f));
        CHECK(easingFromId(easingId(e)) == e);
    }
}

void testTrimPath() {
    Path p;
    p.points = {{0, 0}, {10, 0}};
    Path half = trimPath(p, 0.0f, 0.5f);
    CHECK(half.points.size() == 2);
    CHECK(near(half.points.back().x, 5.0f));
    CHECK(trimPath(p, 0.3f, 0.3f).points.empty());
}

void testFrames() {
    Composition c;
    c.fps = 30;
    c.duration = 2;
    CHECK(c.frameCount() == 60);
    CHECK(c.frameAt(1.0) == 30);
    CHECK(c.frameAt(99.0) == 2970); // past the end: the composition grows to hold what goes there
    CHECK(c.frameAt(-1.0) == 0);
}

const AnimationPreset& builtin(const char* name) {
    static const auto presets = builtinPresets();
    for (const auto& p : presets)
        if (p.name == name) return p;
    static AnimationPreset none;
    return none;
}

void testPresetsStack() {
    Layer l = makeShapeLayer(ShapeKind::Rectangle, {500, 500}, 10.0);
    // Fade in, then fade out later: the fade in must still reach 100%.
    applyPreset(builtin("Fade In"), l, 1.0);
    applyPreset(builtin("Fade Out"), l, 5.0);
    CHECK(near(l.transform.opacity.valueAt(1.0), 0.0f));
    CHECK(near(l.transform.opacity.valueAt(3.0), 100.0f));
    CHECK(near(l.transform.opacity.valueAt(9.0), 0.0f));
    // Applying a fade in after a fade out (opacity rests at 0) still fades to 100%.
    applyPreset(builtin("Fade In"), l, 7.0);
    CHECK(near(l.transform.opacity.valueAt(7.6), 100.0f));
    // Re-applying at the same time replaces instead of piling up keys.
    const int keys = l.transform.opacity.keyCount();
    applyPreset(builtin("Fade In"), l, 7.0);
    CHECK(l.transform.opacity.keyCount() == keys);
    // Pop In after Pop Out keeps a visible scale.
    applyPreset(builtin("Pop Out"), l, 2.0);
    applyPreset(builtin("Pop In"), l, 4.0);
    CHECK(near(l.transform.scale.valueAt(4.6).x, 100.0f));
    // Presets stay inside the clip.
    l.outPoint = 5.0;
    Layer m = l;
    applyPreset(builtin("Spin"), m, 4.9);
    CHECK(m.transform.rotation.keyTime(m.transform.rotation.keyCount() - 1) <= 5.0 + 1e-6);
}

void testPresetJsonRoundTrip() {
    for (const auto& p : builtinPresets()) {
        AnimationPreset q;
        CHECK(parsePreset(presetToJson(p), q));
        CHECK(presetToJson(q) == presetToJson(p));
    }
    CHECK(builtinPresets().size() == 43); // none failed to parse
    // Letter presets set the text's Letter Animation (and only text takes them).
    for (const auto& p : builtinPresets())
        if (p.name == "Words Slide") {
            Layer t = makeTextLayer("Two words", {0, 0}, 5.0), r = makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 5.0);
            CHECK(presetAppliesTo(p, t) && !presetAppliesTo(p, r));
            applyPreset(p, t, 0.0);
            CHECK(t.text.letterIn == LetterAnim::Slide && t.text.letterUnit == LetterUnit::Word &&
                  near(t.text.letterDelay, 0.12f));
        }
    AnimationPreset bad;
    std::string err;
    CHECK(!parsePreset("{\"name\":\"x\"}", bad, &err) && !err.empty());
}

void testDefaultValues() {
    Layer l = makeShapeLayer(ShapeKind::Star, {10, 10}, 5.0);
    CHECK(defaultPropertyValue(l, "opacity", {960, 540}) == std::vector<float>{100.0f});
    CHECK((defaultPropertyValue(l, "position", {960, 540}) == std::vector<float>{960.0f, 540.0f}));
}

void testSnapping() {
    // Edges 98 and 150 against targets 0, 100, 200: 98 -> 100 is the closest.
    SnapResult r = snap1D({98, 150}, {0, 100, 200}, 5);
    CHECK(r.snapped && near(static_cast<float>(r.offset), 2.0f) && near(static_cast<float>(r.target), 100.0f));
    CHECK(!snap1D({50}, {0, 100}, 5).snapped); // nothing close enough
}

void testEffectPresets() {
    for (const auto& preset : builtinEffectPresets()) {
        EffectPreset again;
        CHECK(parseEffectPreset(effectPresetToJson(preset), again));
        CHECK(again.effects.size() == preset.effects.size());
    }
    Effect shadow = makeEffect(EffectType::DropShadow);
    CHECK(effectMargin(shadow, 0) > shadow.offset.base.length());
}

void testCamera() {
    Composition c; // 1920x1080
    c.addLayer(makeShapeLayer(ShapeKind::Rectangle, {960, 540}, 10.0));
    CHECK(near(c.cameraAt(1.0).apply({100, 100}).x, 100.0f)); // no camera: identity
    Layer& cam = c.addLayer(makeCameraLayer({1920, 1080}, 10.0));
    CHECK(near(c.cameraAt(1.0).apply({100, 100}).x, 100.0f)); // default camera: identity too
    cam.camera.zoom.base = 200.0f;
    cam.transform.position.base = {500, 300};
    const Affine m = c.cameraAt(1.0);
    CHECK(near(m.apply({500, 300}).x, 960.0f) && near(m.apply({500, 300}).y, 540.0f)); // looked-at point -> center
    CHECK(near(m.apply({510, 300}).x, 980.0f));                                        // 2x zoom
    Project p;
    p.comp = c;
    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    CHECK(q.comp.layers[0].type == LayerType::Camera && near(q.comp.layers[0].camera.zoom.base, 200.0f));
}

void testCustomEasing() {
    CHECK(near(applyBezierEase({0, 0, 1, 1}, 0.3f), 0.3f));
    const BezierEase ease = bezierFor(Easing::EaseInOut);
    CHECK(near(applyBezierEase(ease, 0.0f), 0.0f) && near(applyBezierEase(ease, 1.0f), 1.0f));
    CHECK(std::fabs(applyBezierEase(ease, 0.5f) - 0.5f) < 0.03f);
    CHECK(applyBezierEase(ease, 0.25f) < 0.25f); // slow start
    AnimFloat a{0.0f};
    a.setKey(0.0, 0.0f, Easing::Custom);
    a.setKey(1.0, 10.0f);
    a.keys[0].curve = {0, 0, 1, 1};
    CHECK(near(a.valueAt(0.5), 5.0f));
}

void testMotionPath() {
    AnimVec2 p{Vec2{0, 0}};
    p.setKey(0.0, {0, 0}, Easing::Linear);
    p.setKey(1.0, {100, 0}, Easing::Linear);
    CHECK(near(p.valueAt(0.5).x, 50.0f) && near(p.valueAt(0.5).y, 0.0f)); // two keys: straight, even speed
    p.setKey(2.0, {100, 100}, Easing::Linear);
    const Vec2 auto1 = p.valueAt(0.5);
    CHECK(auto1.y < -1.0f); // auto bezier bows out before the corner
    p.keys[1].path = PathMode::Linear;
    CHECK(near(p.valueAt(0.5).y, 0.0f)); // corner: straight segments
    CHECK(near(p.valueAt(1.0).x, 100.0f) && near(p.valueAt(1.0).y, 0.0f));
    p.keys[1].path = PathMode::Custom;
    p.keys[1].inTangent = {0, -60};
    p.keys[1].outTangent = {0, 60};
    Project proj;
    Layer& l = proj.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 3.0));
    l.transform.position = p;
    l.transform.position.keys[0].easing = Easing::Custom;
    l.transform.position.keys[0].curve = {0.1f, 0.7f, 0.2f, 1.0f};
    Project q;
    CHECK(deserializeProject(serializeProject(proj), q));
    const auto& keys = q.comp.layers[0].transform.position.keys;
    CHECK(keys.size() == 3 && keys[1].path == PathMode::Custom && near(keys[1].outTangent.y, 60.0f));
    CHECK(keys[0].easing == Easing::Custom && near(keys[0].curve.y1, 0.7f));
    CHECK(near(q.comp.layers[0].transform.position.valueAt(1.5).x, l.transform.position.valueAt(1.5).x));
}

void testCompounds() {
    Composition c; // 1920x1080, 10 s
    Layer& a = c.addLayer(makeShapeLayer(ShapeKind::Rectangle, {400, 300}, 10.0));
    const uint64_t aId = a.id;
    a.transform.position.setKey(1.0, {400, 300});
    a.transform.position.setKey(2.0, {800, 300});
    const uint64_t bId = c.addLayer(makeTextLayer("B", {600, 500}, 10.0)).id;
    const uint64_t other = c.addLayer(makeShapeLayer(ShapeKind::Star, {100, 100}, 10.0)).id;
    auto worldPos = [&](uint64_t id, double t) { return c.worldMatrix(*c.findLayer(id), t).apply({0, 0}); };
    const Vec2 a15 = worldPos(aId, 1.5), b0 = worldPos(bId, 0.0);

    const uint64_t cid = c.createCompound({aId, bId}, 0.0);
    CHECK(cid != 0 && c.findLayer(cid)->type == LayerType::Compound);
    CHECK(c.findLayer(aId)->parent == cid && c.findLayer(bId)->parent == cid && c.findLayer(other)->parent == 0);
    CHECK(near(worldPos(aId, 1.5).x, a15.x) && near(worldPos(bId, 0.0).y, b0.y)); // nothing moved on screen
    CHECK(c.trackRowOf(aId) > c.trackRowOf(cid) && c.trackRowOf(bId) > c.trackRowOf(cid));
    CHECK(c.splitLayer(cid, 5.0) == 0); // compounds are not split

    // Moving / scaling the compound moves its contents.
    Layer& g = *c.findLayer(cid);
    g.transform.position.base = g.transform.position.base + Vec2{100, 0};
    g.transform.scale.base = {200, 200};
    const Vec2 moved = worldPos(bId, 0.0);
    CHECK(near(moved.x, g.transform.position.base.x + (b0.x - (g.transform.position.base.x - 100)) * 2));

    // Release keeps the look and removes the compound.
    const Vec2 before = worldPos(aId, 1.5);
    c.releaseCompound(cid, 0.0);
    CHECK(c.findLayer(cid) == nullptr && c.findLayer(aId)->parent == 0);
    CHECK(near(worldPos(aId, 1.5).x, before.x, 0.05f) && near(worldPos(aId, 1.5).y, before.y, 0.05f));

    // Deleting a compound deletes its contents; the link survives save / load.
    const uint64_t c2 = c.createCompound({aId, bId}, 0.0);
    Project p;
    p.comp = c;
    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    CHECK(q.comp.findLayer(aId) && q.comp.findLayer(aId)->parent == c2);
    c.removeLayerTree(c2);
    CHECK(c.layers.size() == 1 && c.findLayer(other));
}

void testHistory() {
    Project p;
    History h;
    h.reset(p);
    CHECK(!h.isDirty());
    p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
    h.commit(p);
    CHECK(h.isDirty());
    CHECK(h.undo(p));
    CHECK(p.comp.layers.empty());
    CHECK(h.redo(p));
    CHECK(p.comp.layers.size() == 1);

    // Capped by bytes as well as steps: the oldest snapshots go, the last one stays.
    Project q;
    History steps(3);
    steps.reset(q);
    for (int i = 0; i < 5; ++i) {
        q.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
        steps.commit(q);
    }
    CHECK(steps.undoSteps() == 3);
    const size_t snapshot = serializeProject(q).size();
    Project r;
    History small(200, snapshot * 3);
    small.reset(r);
    for (int i = 0; i < 10; ++i) {
        r.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
        small.commit(r);
    }
    CHECK(small.bytes() <= snapshot * 3);
    CHECK(small.undoSteps() >= 1 && small.undoSteps() < 10);
    while (small.undo(r)) {}
    CHECK(!r.comp.layers.empty()); // the first steps are gone
    while (small.redo(r)) {}
    CHECK(r.comp.layers.size() == 10);
    History tiny(200, 1);
    tiny.reset(r);
    r.comp.layers.clear();
    tiny.commit(r);
    r.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
    tiny.commit(r);
    CHECK(tiny.undoSteps() == 1); // over the limit, one step is still kept
    CHECK(tiny.undo(r) && r.comp.layers.empty());
}

void testHistoryLimits() {
    // Steps: only the newest ones stay.
    Project p;
    History steps(3);
    steps.reset(p);
    for (int i = 0; i < 6; ++i) {
        p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
        steps.commit(p);
    }
    CHECK(steps.undoSteps() == 3);
    while (steps.undo(p)) {}
    CHECK(p.comp.layers.size() == 3);

    // Bytes: a tiny budget keeps the last step, and undo and redo keep the count right.
    Project q;
    History bytes(200, 1);
    bytes.reset(q);
    for (int i = 0; i < 4; ++i) {
        q.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
        bytes.commit(q);
    }
    CHECK(bytes.undoSteps() == 1);
    const size_t kept = bytes.bytes();
    CHECK(bytes.undo(q) && q.comp.layers.size() == 3);
    CHECK(bytes.redo(q) && q.comp.layers.size() == 4);
    CHECK(bytes.bytes() == kept);

    // Saved state: back to it by undo is clean again.
    Project r;
    History saved;
    saved.reset(r);
    r.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0));
    saved.commit(r);
    saved.markSaved();
    CHECK(!saved.isDirty());
    CHECK(saved.undo(r) && saved.isDirty());
    CHECK(saved.redo(r) && !saved.isDirty());
}

void testPlaceMotion() {
    // Transitions and Quick Animations go anywhere alike: an end, along the clip, the cut.
    Composition c;
    c.duration = 10.0;
    Layer a = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 4.0);
    a.track = 1;
    Layer b = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 4.0);
    b.track = 1;
    b.inPoint = 4.0, b.outPoint = 8.0;
    const uint64_t ia = c.addLayer(a).id, ib = c.addLayer(b).id;
    CHECK(motionSpotAt(*c.findLayer(ia), 0.2, 0.5) == MotionSpot::In);
    CHECK(motionSpotAt(*c.findLayer(ia), 2.0, 0.5) == MotionSpot::At);
    CHECK(motionSpotAt(*c.findLayer(ia), 3.8, 0.5) == MotionSpot::Out);
    // A transition along the clip: its way in, as an animation there.
    CHECK(placeMotion(c, *c.findLayer(ia), "slide_left", nullptr, MotionSpot::At, 1.0, 0.5));
    const Layer* la = c.findLayer(ia);
    CHECK(la->animations.size() == 1 && la->animations[0].name == "Slide Left In" &&
          near(static_cast<float>(la->animations[0].start), 1.0f));
    CHECK(!la->transitionIn.active());
    // ... on an end: a transition.
    CHECK(placeMotion(c, *c.findLayer(ia), "fade", nullptr, MotionSpot::Out, 3.5, 0.5));
    CHECK(c.findLayer(ia)->transitionOut.id == "fade");
    // A Quick Animation on the cut: on the clip that comes in, from its start.
    AnimationPreset pop;
    CHECK(parsePreset(R"({"name": "Pop In", "category": "Entrance", "duration": 0.5,
                         "tracks": [{"property": "scale", "keys": [{"t": 0, "v": {"mul": 0}}, {"t": 1, "v": "rest"}]}]})",
                      pop));
    Layer* placed = placeMotion(c, *c.findLayer(ia), {}, &pop, MotionSpot::Cut, 4.0, 0.5);
    CHECK(placed && placed->id == ib && placed->animations.size() == 1 &&
          near(static_cast<float>(placed->animations[0].start), 4.0f));
}

void testQuickAnimationMarks() {
    Layer l = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 5.0);
    AnimationPreset fade;
    CHECK(parsePreset(R"({"name": "Fade In", "category": "Entrance", "duration": 1,
                         "tracks": [{"property": "opacity", "keys": [{"t": 0, "v": 0}, {"t": 1, "v": "rest"}]}]})",
                      fade));
    applyQuickAnimation(fade, l, 1.0);
    CHECK(l.animations.size() == 1 && near(static_cast<float>(l.animations[0].start), 1.0f) &&
          l.animations[0].properties.size() == 1);
    CHECK(l.transform.opacity.keyCount() == 2);
    // Stretched to 2 s: its keys spread with it.
    CHECK(retimeQuickAnimation(l, 0, 1.0, 2.0));
    CHECK(near(static_cast<float>(l.transform.opacity.keyTime(1)), 3.0f));
    // Moved with its clip.
    l.shiftTime(1.0);
    CHECK(near(static_cast<float>(l.animations[0].start), 2.0f));
    // Taken off: its keys go, and the opacity is what it was (not its last key's value).
    CHECK(removeQuickAnimation(l, 0));
    CHECK(l.animations.empty() && l.transform.opacity.keyCount() == 0 && near(l.transform.opacity.base, 100.0f));
    // An exit (ending at 0, 50% scale) on a static media-like layer: taken off, the layer is whole again.
    {
        Layer m = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 5.0);
        m.transform.scale.base = {40.0f, 40.0f};
        applyQuickAnimation(builtin("Shrink Out"), m, 4.0);
        CHECK(m.transform.scale.isAnimated() && m.transform.opacity.isAnimated());
        CHECK(removeQuickAnimation(m, 0));
        CHECK(!m.transform.scale.isAnimated() && near(m.transform.scale.base.x, 40.0f) &&
              near(m.transform.opacity.base, 100.0f));
    }
    // The layer's own keys inside the span come back; its keys outside stay; it survives a save.
    {
        Layer k = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 6.0);
        k.transform.opacity.setKey(0.0, 30.0f);
        k.transform.opacity.setKey(1.5, 60.0f);
        k.transform.opacity.setKey(5.0, 90.0f);
        applyQuickAnimation(fade, k, 1.0); // over 1..2 s: replaces the key at 1.5
        CHECK(k.transform.opacity.findKey(1.5) < 0);
        Project p;
        p.comp.layers.push_back(k);
        Project q;
        std::string err;
        CHECK(deserializeProject(serializeProject(p), q, &err));
        Layer& back = q.comp.layers.front();
        CHECK(back.animations.size() == 1 && back.animations[0].restore.size() == 1);
        back.shiftTime(1.0); // moved with its clip: the keys it gives back move too
        CHECK(removeQuickAnimation(back, 0));
        CHECK(back.transform.opacity.keyCount() == 3 && back.transform.opacity.findKey(2.5) >= 0 &&
              near(back.transform.opacity.valueAt(2.5), 60.0f));
    }
    // Text letters set by a preset go back to what they were.
    {
        Layer t = makeTextLayer("Hi", {100, 100}, 4.0);
        applyQuickAnimation(builtin("Letters Pop"), t, 0.0);
        CHECK(t.text.letterIn != LetterAnim::None);
        CHECK(removeQuickAnimation(t, 0));
        CHECK(t.text.letterIn == LetterAnim::None);
    }
    // A new one over an old one: the old one is taken off first (nothing of it left behind).
    {
        Layer o = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 5.0);
        applyQuickAnimation(builtin("Shrink Out"), o, 4.0);
        applyQuickAnimation(fade, o, 4.0);
        CHECK(o.animations.size() == 1 && !o.transform.scale.isAnimated() && near(o.transform.scale.base.x, 100.0f));
        CHECK(removeQuickAnimation(o, 0));
        CHECK(!o.transform.opacity.isAnimated() && near(o.transform.opacity.base, 100.0f));
    }
    // A transition moved to the other end.
    Layer t = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 4.0);
    CHECK(applyTransition(t, "fade", TransitionEdge::In, 0.5));
    CHECK(moveTransition(t, TransitionEdge::In));
    CHECK(!t.transitionIn.active() && t.transitionOut.active() && t.transitionOut.id == "fade");
    CHECK(near(t.transform.opacity.valueAt(4.0), 0.0f, 0.5f) && near(t.transform.opacity.valueAt(0.1), 100.0f));
}

void testCamera3D() {
    Project p; // 1920x1080: focal length 2666.7
    CHECK(run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "Far", "z": 2666.6667, "position": [1060, 540]},
                     {"op": "layer.add", "type": "rectangle", "name": "Flat", "position": [1060, 540]},
                     {"op": "layer.add", "type": "ellipse", "name": "Near", "three_d": true}])")
              .ok);
    Composition& c = p.comp;
    const uint64_t farId = c.layers[2].id;
    CHECK(c.layers[2].threeD && c.layers[2].name == "Far");
    Projective m;
    // Without a camera: twice as far, half the size (around the center).
    CHECK(c.viewFor(*c.findLayer(farId), 0, m) && near(m.apply({1060, 540}).x, 1010.0f));
    CHECK(c.viewFor(c.layers[1], 0, m) && near(m.apply({1060, 540}).x, 1060.0f)); // 2D: not affected
    // Drawn by depth among the 3D layers: the far one first even though it is not at the bottom.
    CHECK(run(p, R"({"op": "layer.move", "layer": "Far", "to": "top"})").ok);
    const auto order = c.drawOrder(0);
    CHECK(order.size() == 3 && order.front()->name == "Flat" && order[1]->name == "Far" && order[2]->name == "Near");
    // A camera at Z 2666.7 reaches the far layer's plane: seen at its size; the near one is behind it.
    CHECK(run(p, R"([{"op": "layer.add", "type": "camera", "z": 2666.6667}])").ok);
    CHECK(c.viewFor(*c.findLayer(farId), 0, m) && near(m.apply({1060, 540}).x, 1060.0f));
    for (const Layer& l : c.layers)
        if (l.name == "Near") CHECK(!c.viewFor(l, 0, m));
    CHECK(!run(p, R"({"op": "layer.set", "layer": "Camera", "three_d": true})").ok); // cameras always see in 3D
    CHECK(run(p, R"({"op": "property.set", "layer": "Near", "prop": "position_z", "value": 100})").ok);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer* near3D = nullptr;
    for (const Layer& l : back.comp.layers)
        if (l.name == "Near") near3D = &l;
    CHECK(near3D && near3D->threeD && near(near3D->transform.depth.valueAt(0), 100.0f));
    CHECK(validate(back).empty());
}

void testCamera3DTilt() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "Card", "z": 0},
                     {"op": "property.set", "layer": "Card", "prop": "rotation_y", "value": 60}])")
              .ok);
    Composition& c = p.comp;
    Projective m;
    CHECK(c.viewFor(c.layers[0], 0, m));
    // Turned 60° around Y: the right side goes away (smaller offset), the left side comes closer.
    const float right = m.apply({1060, 540}).x - 960, left = 960 - m.apply({860, 540}).x;
    CHECK(right > 0 && left > right && right < 100 && left > 50);
    CHECK(near(m.apply({960, 540}).x, 960) && near(m.apply({960, 540}).y, 540)); // turns around its position
    // The free view: as without a camera, in perspective (never flat).
    Projective f;
    CHECK(c.viewFor(c.layers[0], 0, f, false) && near(f.apply({1060, 540}).x, m.apply({1060, 540}).x));
    // Unturned plane (where the position moves): as if facing the camera.
    CHECK(c.viewFor(c.layers[0], 0, m, true, false) && near(m.apply({1060, 540}).x, 1060));
    // Turned 90° it is seen edge-on; far enough to the side, points are behind the camera.
    CHECK(run(p, R"({"op": "property.set", "layer": "Card", "prop": "rotation_y", "value": 90})").ok);
    CHECK(c.viewFor(c.layers[0], 0, m) && !m.inFront({960 - 3000, 540}) && m.inFront({960 + 3000, 540}));
    // A camera turned 10° around X looks down: what was at the center moves up on screen.
    CHECK(run(p, R"([{"op": "property.set", "layer": "Card", "prop": "rotation_y", "value": 0},
                     {"op": "layer.add", "type": "camera"},
                     {"op": "property.set", "layer": "Camera", "prop": "rotation_x", "value": 10}])")
              .ok);
    for (const Layer& l : c.layers)
        if (l.name == "Card") CHECK(c.viewFor(l, 0, m) && m.apply({960, 540}).y < 540 - 100);
}

} // namespace

Tests animationTests() {
    return {
        {"static value", testStaticValue},
        {"linear interpolation", testLinearInterpolation},
        {"keys stay sorted", testKeysStaySorted},
        {"removing last key keeps value", testRemovingLastKeyKeepsValue},
        {"easing endpoints", testEasingEndpoints},
        {"trim path", testTrimPath},
        {"frames", testFrames},
        {"presets stack", testPresetsStack},
        {"preset json round trip", testPresetJsonRoundTrip},
        {"default values", testDefaultValues},
        {"snapping", testSnapping},
        {"effect presets", testEffectPresets},
        {"camera", testCamera},
        {"custom easing", testCustomEasing},
        {"motion path", testMotionPath},
        {"compounds", testCompounds},
        {"history", testHistory},
        {"history limits", testHistoryLimits},
        {"place motion", testPlaceMotion},
        {"quick animation marks", testQuickAnimationMarks},
        {"3d camera", testCamera3D},
        {"3d camera tilt", testCamera3DTilt},
    };
}
