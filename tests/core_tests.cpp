// Minimal, dependency-free unit tests for the document model.

#include "core/Animated.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/Presets.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Snapping.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace bemotion;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

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
    CHECK(c.frameAt(99.0) == 59);
}

void testSerializationRoundTrip() {
    Project p;
    p.name = "Round Trip";
    p.comp.width = 1080;
    p.comp.height = 1920;
    Layer& rect = p.comp.addLayer(makeShapeLayer(ShapeKind::Star, {10, 20}, 5.0));
    rect.transform.opacity.setKey(0.5, 25.0f, Easing::Bounce);
    rect.transform.opacity.setKey(1.5, 75.0f);
    Layer& text = p.comp.addLayer(makeTextLayer("Olá, bemotion", {0, 0}, 5.0));
    text.text.fontFamily = "Arial";
    text.text.fontStyle = "Bold Italic";

    Project q;
    std::string err;
    CHECK(deserializeProject(serializeProject(p), q, &err));
    CHECK(q.name == "Round Trip");
    CHECK(q.comp.width == 1080 && q.comp.height == 1920);
    CHECK(q.comp.layers.size() == 2);
    CHECK(q.comp.layers[0].text.text == "Olá, bemotion");
    CHECK(q.comp.layers[0].text.fontFamily == "Arial" && q.comp.layers[0].text.fontStyle == "Bold Italic");
    CHECK(q.comp.layers[1].shape.kind == ShapeKind::Star);
    CHECK(q.comp.layers[1].transform.opacity.keyCount() == 2);
    CHECK(q.comp.layers[1].transform.opacity.keys[0].easing == Easing::Bounce);
    CHECK(near(q.comp.layers[1].transform.opacity.valueAt(1.5), 75.0f));
    CHECK(q.comp.nextLayerId == p.comp.nextLayerId);
    CHECK(serializeProject(p) == serializeProject(q));
}

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
    for (const auto& l : c.layers) s += l.name;
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
        for (int i = r.first; i < r.first + r.count; ++i) s += c.layers[i].name;
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

void testGuidesRoundTrip() {
    Project p;
    p.comp.guides = {{true, 480.0f}, {false, 270.5f}};
    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    CHECK(q.comp.guides.size() == 2 && !q.comp.guides[1].vertical && near(q.comp.guides[1].position, 270.5f));
}

void testEffectsRoundTrip() {
    Project p;
    Layer& l = p.comp.addLayer(makeTextLayer("Hi", {0, 0}, 5.0));
    l.effects.push_back(makeEffect(EffectType::DropShadow));
    l.effects.push_back(makeEffect(EffectType::Glow));
    l.effects[1].size.setKey(1.0, 10.0f);
    l.effects[1].size.setKey(2.0, 40.0f);
    // Effect parameters are regular properties (keyframes, timeline rows).
    auto props = l.properties();
    CHECK(l.findProperty(props, "fx1.size") != nullptr);
    CHECK(l.findProperty(props, "fx1.size")->effectIndex == 1);
    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    const Layer& m = q.comp.layers[0];
    CHECK(m.effects.size() == 2 && m.effects[0].type == EffectType::DropShadow && m.effects[1].type == EffectType::Glow);
    CHECK(near(m.effects[1].size.valueAt(1.5), 25.0f));
    CHECK(serializeProject(p) == serializeProject(q));
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

void testPluginLayerRoundTrip() {
    PluginProps props;
    props.objectType = "charts.bars";
    props.defaultSize = {800, 400};
    PluginParam progress;
    progress.id = "progress";
    progress.label = "Progress";
    progress.defaultNumber = 100;
    progress.max = 100;
    PluginParam mode;
    mode.id = "orientation";
    mode.kind = PluginParamKind::Choice;
    mode.choices = {"Columns", "Bars"};
    mode.choice = 1;
    PluginParam color;
    color.id = "bar_color";
    color.kind = PluginParamKind::Color;
    color.defaultColor = Color::fromHex(0x336699);
    props.params = {progress, mode, color};
    props.hasRows = true;
    props.rows = {{"A", 3, Color::fromHex(0xFF0000)}, {"B", 7.5f, Color::fromHex(0x00FF00)}};

    Project p;
    Layer& l = p.comp.addLayer(makePluginLayer("Chart", props, {960, 540}, 5.0));
    l.plugin.find("progress")->number.setKey(0.0, 0.0f);
    l.plugin.find("progress")->number.setKey(1.0, 100.0f);
    auto lp = l.properties();
    CHECK(l.findProperty(lp, "p.progress") != nullptr && l.findProperty(lp, "p.bar_color") != nullptr);
    CHECK(l.findProperty(lp, "p.orientation") == nullptr); // choices are not animatable

    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    if (q.comp.layers.empty()) return;
    Layer& r = q.comp.layers[0];
    CHECK(r.type == LayerType::Plugin && r.plugin.objectType == "charts.bars");
    CHECK(r.plugin.params.size() == 3 && r.plugin.find("orientation")->choice == 1);
    CHECK(near(r.plugin.find("progress")->number.valueAt(0.5), l.plugin.find("progress")->number.valueAt(0.5)));
    CHECK(r.plugin.rows.size() == 2 && r.plugin.rows[1].label == "B" && near(r.plugin.rows[1].value, 7.5f));
    CHECK(near(defaultPropertyValue(r, "p.bar_color", {960, 540})[2], Color::fromHex(0x336699).b));
    CHECK(near(defaultPropertyValue(r, "size", {960, 540})[0], 800.0f));
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

void testTracksRoundTrip() {
    Project p;
    p.comp = makeSplitStack();
    Project q;
    CHECK(deserializeProject(serializeProject(p), q));
    CHECK(rowsText(q.comp) == rowsText(p.comp));
    CHECK(q.comp.nextTrackId == p.comp.nextTrackId);
}

void testLegacyFormatId() {
    Project p;
    std::string text = serializeProject(p);
    const auto pos = text.find("\"bemotion\"");
    CHECK(pos != std::string::npos);
    text.replace(pos, 10, "\"mirage\"");
    Project q;
    CHECK(deserializeProject(text, q));
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
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"static value", testStaticValue},
        {"linear interpolation", testLinearInterpolation},
        {"keys stay sorted", testKeysStaySorted},
        {"removing last key keeps value", testRemovingLastKeyKeepsValue},
        {"easing endpoints", testEasingEndpoints},
        {"trim path", testTrimPath},
        {"frames", testFrames},
        {"serialization round trip", testSerializationRoundTrip},
        {"move tracks", testMoveTracks},
        {"shift tracks", testShiftTracks},
        {"split layer", testSplitLayer},
        {"clips between tracks", testClipsBetweenTracks},
        {"time shift limits", testTimeShiftLimits},
        {"tracks round trip", testTracksRoundTrip},
        {"presets stack", testPresetsStack},
        {"preset json round trip", testPresetJsonRoundTrip},
        {"default values", testDefaultValues},
        {"snapping", testSnapping},
        {"guides round trip", testGuidesRoundTrip},
        {"effects round trip", testEffectsRoundTrip},
        {"effect presets", testEffectPresets},
        {"camera", testCamera},
        {"custom easing", testCustomEasing},
        {"motion path", testMotionPath},
        {"plugin layer round trip", testPluginLayerRoundTrip},
        {"compounds", testCompounds},
        {"legacy format id", testLegacyFormatId},
        {"history", testHistory},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::printf("[%s] %s\n", g_failures == before ? " ok " : "FAIL", name);
    }
    std::printf("%s\n", g_failures == 0 ? "All tests passed." : "Some tests failed.");
    return g_failures == 0 ? 0 : 1;
}
