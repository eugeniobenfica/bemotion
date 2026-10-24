// Minimal, dependency-free unit tests for the document model.

#include "core/Animated.h"
#include "core/Arrange.h"
#include "core/Clipboard.h"
#include "core/Data.h"
#include "core/Describe.h"
#include "core/Expressions.h"
#include "core/Ops.h"
#include "core/Validate.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/Presets.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Snapping.h"
#include "core/Transitions.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    CHECK(c.frameAt(99.0) == 2970); // past the end: the composition grows to hold what goes there
    CHECK(c.frameAt(-1.0) == 0);
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

void testMedia() {
    Project p;
    MediaItem video;
    video.name = "clip.mp4";
    video.path = "C:/footage/clip.mp4";
    video.kind = MediaKind::Video;
    video.width = 1280;
    video.height = 720;
    video.duration = 4.0;
    video.fps = 25.0;
    video.hasVideo = video.hasAudio = true;
    const uint64_t id = p.comp.addMedia(video);
    CHECK(p.comp.addMedia(video) == id); // same file: imported once
    CHECK(p.comp.media.size() == 1);

    // Footage lasts as long as its source; shifting moves where its frame 0 sits.
    Layer& l = p.comp.addLayer(makeMediaLayer(*p.comp.findMedia(id), {10, 20}, 1.0, 10.0));
    CHECK(near(static_cast<float>(l.outPoint - l.inPoint), 4.0f));
    CHECK(near(static_cast<float>(l.media.sourceTime(1.5)), 0.5f));
    l.shiftTime(2.0);
    CHECK(near(static_cast<float>(l.media.start), 3.0f) && near(static_cast<float>(l.inPoint), 3.0f));
    l.media.volume.setKey(3.0, 50.0f);
    const uint64_t layerId = l.id; // `l` does not survive adding more layers

    // Trimming stops at the source's ends; stills can be stretched freely.
    p.comp.setOutPoint(l.id, 20.0);
    CHECK(near(static_cast<float>(l.outPoint), 7.0f));
    p.comp.setInPoint(l.id, 0.0);
    CHECK(near(static_cast<float>(l.inPoint), 3.0f));

    MediaItem image;
    image.name = "logo.png";
    image.path = "C:/footage/logo.png";
    image.kind = MediaKind::Image;
    image.width = image.height = 64;
    image.hasVideo = true;
    const Layer& still = p.comp.addLayer(makeMediaLayer(*p.comp.findMedia(p.comp.addMedia(image)), {0, 0}, 2.0, 10.0));
    CHECK(near(static_cast<float>(still.outPoint), 12.0f));
    p.comp.fitDuration(); // the composition ends with its last clip
    CHECK(near(static_cast<float>(p.comp.duration), 12.0f));

    Project q;
    std::string err;
    CHECK(deserializeProject(serializeProject(p), q, &err));
    CHECK(q.comp.media.size() == 2);
    const MediaItem* v = q.comp.findMedia(id);
    CHECK(v && v->kind == MediaKind::Video && v->width == 1280 && near(static_cast<float>(v->fps), 25.0f) && v->hasAudio);
    const Layer* ql = q.comp.findLayer(layerId);
    CHECK(ql && ql->type == LayerType::Media && ql->media.mediaId == id);
    CHECK(ql && near(static_cast<float>(ql->media.start), 3.0f) && ql->media.volume.keyCount() == 1);
    CHECK(q.comp.nextMediaId == p.comp.nextMediaId);
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

// --- operations, validation ------------------------------------------------------------------

OpsResult run(Project& p, const char* ops) { return applyOps(p, nlohmann::json::parse(ops)); }

void testOpsBuildAndQuery() {
    Project p;
    OpsResult r = run(p, R"([
        {"op": "layer.add", "type": "text", "text": "Hello", "name": "Title", "position": [960, 400]},
        {"op": "layer.add", "type": "rectangle", "name": "Card", "start": 1, "duration": 3},
        {"op": "property.set", "layer": "Card", "prop": "fill_color", "value": "#4F8EF7"},
        {"op": "key.add", "layer": "Title", "prop": "opacity", "time": 0, "value": 0, "easing": "ease_out"},
        {"op": "key.add", "layer": "Title", "prop": "opacity", "time": 1, "value": 100},
        {"op": "query.layer", "layer": "Title", "time": 0.5}
    ])");
    CHECK(r.ok);
    CHECK(r.changed);
    CHECK(p.comp.layers.size() == 2);
    const nlohmann::json& title = r.results[5];
    CHECK(title["name"] == "Title");
    CHECK(title["props"]["opacity"]["keys"].size() == 2);
    CHECK(title["props"]["opacity"]["keys"][0]["easing"] == "ease_out");
    const Layer* card = nullptr;
    for (const Layer& l : p.comp.layers)
        if (l.name == "Card") card = &l;
    CHECK(card && near(static_cast<float>(card->inPoint), 1.0f) && near(static_cast<float>(card->outPoint), 4.0f));
    CHECK(card && near(card->shape.fillColor.base.b, 0xF7 / 255.0f));
    CHECK(validate(p).empty());
}

void testOpsAtomic() {
    Project p;
    run(p, R"({"op": "layer.add", "type": "ellipse", "name": "Dot"})");
    const std::string before = serializeProject(p);
    // The second operation fails: the first must not stay applied either.
    OpsResult r = run(p, R"([
        {"op": "property.set", "layer": "Dot", "prop": "opacity", "value": 10},
        {"op": "property.set", "layer": "Dot", "prop": "opacty", "value": 10}
    ])");
    CHECK(!r.ok);
    CHECK(r.failedIndex == 1);
    CHECK(r.error.find("did you mean \"opacity\"") != std::string::npos);
    CHECK(serializeProject(p) == before);
    // Unknown names and operations explain themselves too.
    CHECK(run(p, R"({"op": "layer.set", "layer": "Dott", "visible": false})").error.find("\"Dot\"") != std::string::npos);
    CHECK(run(p, R"({"op": "layer.ad", "type": "text"})").error.find("layer.add") != std::string::npos);
    CHECK(!run(p, R"({"version": 99, "ops": []})").ok);
}

void testOpsStructure() {
    Project p;
    OpsResult r = run(p, R"([
        {"op": "layer.add", "type": "star", "name": "A", "position": [800, 540]},
        {"op": "layer.add", "type": "polygon", "name": "B", "position": [1100, 540]},
        {"op": "group", "layers": ["A", "B"], "name": "Pair"},
        {"op": "effect.add", "layer": "Pair", "effect": "glow", "params": {"size": 30}},
        {"op": "layer.split", "layer": "A", "time": 2},
        {"op": "layer.move", "layer": "Pair", "to": "bottom"}
    ])");
    CHECK(r.ok);
    const uint64_t pair = r.results[2]["layer"];
    const Layer* g = p.comp.findLayer(pair);
    CHECK(g && g->type == LayerType::Compound && g->name == "Pair");
    CHECK(g && g->effects.size() == 1);
    CHECK(r.results[4]["layer"].get<uint64_t>() != 0);
    CHECK(validate(p).empty());
    // Ungrouping keeps the layers.
    CHECK(run(p, R"({"op": "ungroup", "layer": "Pair"})").ok);
    CHECK(p.comp.layers.size() == 3);
    CHECK(validate(p).empty());
    // A clip cannot be moved onto another clip of its track.
    run(p, R"([{"op": "layer.add", "type": "line", "name": "L1", "start": 0, "duration": 2}])");
    const std::string before = serializeProject(p);
    const OpsResult bad = run(p, R"({"op": "layer.split", "layer": "L1", "time": 5})");
    CHECK(!bad.ok);
    CHECK(serializeProject(p) == before);
}

void testOpsRoundTrip() {
    Project p;
    run(p, R"([
        {"op": "composition.set", "width": 1080, "height": 1920, "fps": 25, "background": "#202020", "name": "Reel"},
        {"op": "layer.add", "type": "text", "text": "Hi", "name": "T"},
        {"op": "key.add", "layer": "T", "prop": "position", "time": 0, "value": [540, 2000]},
        {"op": "key.add", "layer": "T", "prop": "position", "time": 0.8, "value": [540, 960], "easing": "back_out"}
    ])");
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    CHECK(serializeProject(back) == serializeProject(p));
    CHECK(p.comp.width == 1080 && p.comp.fps == 25 && p.name == "Reel");
    const std::string text = describeProject(p);
    CHECK(text.find("\"T\" text \"Hi\"") != std::string::npos);
    CHECK(text.find("position 2 keys") != std::string::npos);
}

void testValidateAndRepair() {
    Project p;
    run(p, R"([
        {"op": "layer.add", "type": "rectangle", "name": "R1"},
        {"op": "layer.add", "type": "rectangle", "name": "R2"},
        {"op": "layer.add", "type": "text", "name": "T"}
    ])");
    CHECK(validate(p).empty());
    // Break every rule at once, like a damaged or hand-edited file would.
    auto& L = p.comp.layers;
    L[0].id = L[1].id;                                     // duplicate id
    L[1].transform.opacity.base = std::nanf("");           // not a number
    L[1].track = L[2].track;                               // overlaps T on its track
    L[2].parent = 12345;                                   // missing compound
    L[2].inPoint = 3.0, L[2].outPoint = 1.0;               // out before in
    L[0].transform.rotation.keys = {{2.0, 10.0f}, {1.0, 0.0f}}; // keys out of order
    L[0].type = LayerType::Media, L[0].media.mediaId = 77; // media that is not there
    CHECK(validate(p).size() >= 6);
    const auto fixed = repair(p);
    CHECK(!fixed.empty());
    CHECK(validate(p).empty());
    CHECK(p.comp.layers.size() == 3); // nothing lost
    CHECK(std::isfinite(p.comp.layers[1].transform.opacity.base) || std::isfinite(p.comp.layers[0].transform.opacity.base));
}

void testCsvAndNumbers() {
    const Cells c = parseCsv("\xEF\xBB\xBFMonth;Sales\r\nJan;1.234,5\n\n\"Feb; late\";\"R$ 12\"\n");
    CHECK(c.size() == 3);
    CHECK(c[0][0] == "Month" && c[2][0] == "Feb; late");
    float v = 0;
    CHECK(parseNumber("1.234,5", v) && near(v, 1234.5f));
    CHECK(parseNumber("1,234.5", v) && near(v, 1234.5f));
    CHECK(parseNumber("R$ 12", v) && near(v, 12.0f));
    CHECK(parseNumber("45%", v) && near(v, 45.0f));
    CHECK(parseNumber("3,5", v) && near(v, 3.5f));
    CHECK(!parseNumber("Jan", v));
    const auto rows = rowsFromCells(c);
    CHECK(rows.size() == 2 && rows[0].label == "Jan" && near(rows[0].value, 1234.5f) && near(rows[1].value, 12.0f));
    const DataTable t = tableFromCells(parseCsv("a,b,c\n1,2\n"));
    CHECK(t.header.size() == 3 && t.rows.size() == 1 && t.rows[0].size() == 3);
    CHECK(parseCsv(toCsv(cellsOf(t))) == cellsOf(t));
}

void testLengthLimit() {
    Project p;
    run(p, R"({"op": "layer.add", "type": "rectangle", "duration": 8})");
    CHECK(p.comp.duration > 7.9);
    CHECK(run(p, R"({"op": "composition.set", "max_duration": 5})").ok);
    CHECK(near(static_cast<float>(p.comp.duration), 5.0f));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && near(static_cast<float>(back.comp.maxDuration), 5.0f));
    CHECK(!run(p, R"({"op": "composition.set", "max_duration": -1})").ok);
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
    CHECK(run(p, R"({"op": "transition.add", "layer": "A", "transition": "fade", "edge": "both", "duration": 0.5})").ok);
    Layer* a = nullptr;
    for (Layer& l : p.comp.layers)
        if (l.name == "A") a = &l;
    CHECK(a && a->transform.opacity.keyCount() == 4);
    CHECK(near(a->transform.opacity.valueAt(a->inPoint), 0.0f) && near(a->transform.opacity.valueAt(a->inPoint + 1.0), 100.0f));
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
                     {"op": "layer.add", "type": "camera"}])").ok);
    Composition& c = p.comp;
    Layer* first = nullptr;
    for (Layer& l : c.layers)
        if (l.name == "Clip" && near(static_cast<float>(l.inPoint), 0.0f)) first = &l;
    CHECK(first && nextOnTrack(c, *first));
    const std::string cut = R"({"op": "transition.add", "transition": "fade", "edge": "cut", "duration": 1, "layer": )" +
                            std::to_string(first ? first->id : 0) + "}";
    const uint64_t firstId = first ? first->id : 0;
    CHECK(run(p, cut.c_str()).ok);
    first = c.findLayer(firstId); // operations replace the composition as a whole
    Layer* second = first ? nextOnTrack(c, *first) : nullptr;
    CHECK(first && second && first->transitionOut.joined && second->transitionIn.joined);
    CHECK(near(first->transitionOut.duration, 0.5f) && near(second->transitionIn.duration, 0.5f)); // half each side of the cut
    CHECK(near(first->transform.opacity.valueAt(1.999), 0.0f, 2.0f) && near(second->transform.opacity.valueAt(2.5), 100.0f));
    CHECK(transitionPartner(c, *second, TransitionEdge::In) == first);
    CHECK(resizeTransition(c, *second, TransitionEdge::In, 0.8));
    CHECK(near(first->transitionOut.duration, 0.8f)); // the other half follows
    CHECK(removeTransition(c, *first, TransitionEdge::Out));
    CHECK(!first->transitionOut.active() && !second->transitionIn.active());
    // Cameras and sound clips are refused; 3D transitions make the layer 3D.
    for (const Layer& l : c.layers)
        if (l.type == LayerType::Camera) CHECK(!canTakeTransition(l));
    CHECK(canTakeTransition(*first, "turn") && applyTransition(*first, "turn", TransitionEdge::In, 0.5) && first->threeD);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && validate(back).empty());
}

void testFileParams() {
    // A plugin object with a file parameter (like a 3D model), its path saved relative to the project.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bemotion-test-files";
    fs::create_directories(dir / "models");
    { std::ofstream(dir / "models" / "cube.obj") << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; }
    Project p;
    Layer l;
    l.type = LayerType::Plugin;
    l.name = "Model";
    l.outPoint = 2.0;
    l.plugin.objectType = "models3d.model";
    PluginParam file;
    file.id = "model";
    file.kind = PluginParamKind::File;
    file.accept = {"obj", "glb"};
    PluginParam shading;
    shading.id = "shading";
    shading.kind = PluginParamKind::Choice;
    shading.choices = {"Smooth", "Flat"};
    l.plugin.params = {file, shading};
    p.comp.addLayer(l);
    const std::string obj = (dir / "models" / "cube.obj").generic_string();
    CHECK(run(p, (R"({"op": "param.set", "layer": "Model", "param": "model", "value": ")" + obj + R"("})").c_str()).ok);
    CHECK(run(p, R"({"op": "param.set", "layer": "Model", "param": "shading", "value": "Flat"})").ok);
    CHECK(!run(p, R"({"op": "param.set", "layer": "Model", "param": "shading", "value": "Flta"})").ok);
    CHECK(!run(p, R"({"op": "param.set", "layer": "Model", "param": "model", "value": "missing.obj"})").ok);
    CHECK(!run(p, (R"({"op": "param.set", "layer": "Model", "param": "model", "value": ")" + (dir / "a.txt").generic_string() + R"("})").c_str()).ok);
    CHECK(saveProject(p, dir / "test.bemotion"));
    // Moved together with its models, the project still finds them.
    fs::remove_all(dir / "moved");
    fs::create_directories(dir / "moved");
    fs::copy(dir / "models", dir / "moved" / "models", fs::copy_options::recursive);
    fs::copy_file(dir / "test.bemotion", dir / "moved" / "test.bemotion");
    fs::remove_all(dir / "models");
    Project back;
    CHECK(loadProject(dir / "moved" / "test.bemotion", back));
    const PluginParam* m = back.comp.layers.empty() ? nullptr : back.comp.layers[0].plugin.find("model");
    CHECK(m && m->kind == PluginParamKind::File && fs::exists(fs::path(std::u8string(m->text.begin(), m->text.end()))) &&
          m->accept.size() == 2);
    CHECK(back.comp.layers[0].plugin.find("shading")->choice == 1);
    fs::remove_all(dir);
}

void testBackground() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "text", "text": "Hi", "name": "T"},
                     {"op": "layer.add", "type": "background", "name": "BG", "fill": "radial"},
                     {"op": "property.set", "layer": "BG", "prop": "bg_darken", "value": 30}])").ok);
    CHECK(p.comp.layers.back().type == LayerType::Background); // under everything
    CHECK(p.comp.layers.back().background.fill == BackgroundFill::Radial);
    CHECK(!run(p, R"({"op": "layer.set", "layer": "BG", "fill": "plaid"})").ok);
    CHECK(!run(p, R"({"op": "layer.set", "layer": "BG", "media": 99})").ok);
    // It never moves: no position, and its matrix is the identity whatever the transform says.
    Layer& bg = p.comp.layers.back();
    bg.transform.position.base = {500, 500};
    CHECK(near(bg.matrixAt(0).apply({10, 20}).x, 10) && near(bg.matrixAt(0).apply({10, 20}).y, 20));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    CHECK(back.comp.layers.back().type == LayerType::Background && back.comp.layers.back().background.fill == BackgroundFill::Radial);
    CHECK(near(back.comp.layers.back().background.darken.valueAt(0), 30));
    CHECK(validate(back).empty());
}

void testCamera3D() {
    Project p; // 1920x1080: focal length 2666.7
    CHECK(run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "Far", "z": 2666.6667, "position": [1060, 540]},
                     {"op": "layer.add", "type": "rectangle", "name": "Flat", "position": [1060, 540]},
                     {"op": "layer.add", "type": "ellipse", "name": "Near", "three_d": true}])").ok);
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
                     {"op": "property.set", "layer": "Card", "prop": "rotation_y", "value": 60}])").ok);
    Composition& c = p.comp;
    Projective m;
    CHECK(c.viewFor(c.layers[0], 0, m));
    // Turned 60° around Y: the right side goes away (smaller offset), the left side comes closer.
    const float right = m.apply({1060, 540}).x - 960, left = 960 - m.apply({860, 540}).x;
    CHECK(right > 0 && left > right && right < 100 && left > 50);
    CHECK(near(m.apply({960, 540}).x, 960) && near(m.apply({960, 540}).y, 540)); // turns around its position
    // The free view: from the front, no perspective (cos 60° = half as wide).
    CHECK(c.viewFor(c.layers[0], 0, m, false) && near(m.apply({1060, 540}).x, 1010));
    // Unturned plane (where the position moves): as if facing the camera.
    CHECK(c.viewFor(c.layers[0], 0, m, true, false) && near(m.apply({1060, 540}).x, 1060));
    // Turned 90° it is seen edge-on; far enough to the side, points are behind the camera.
    CHECK(run(p, R"({"op": "property.set", "layer": "Card", "prop": "rotation_y", "value": 90})").ok);
    CHECK(c.viewFor(c.layers[0], 0, m) && !m.inFront({960 - 3000, 540}) && m.inFront({960 + 3000, 540}));
    // A camera turned 10° around X looks down: what was at the center moves up on screen.
    CHECK(run(p, R"([{"op": "property.set", "layer": "Card", "prop": "rotation_y", "value": 0},
                     {"op": "layer.add", "type": "camera"},
                     {"op": "property.set", "layer": "Camera", "prop": "rotation_x", "value": 10}])").ok);
    for (const Layer& l : c.layers)
        if (l.name == "Card") CHECK(c.viewFor(l, 0, m) && m.apply({960, 540}).y < 540 - 100);
}

void testSubtitles() {
    const std::string srt = "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello there\r\n\r\n2\n00:00:03.000 --> 00:00:04.000 align:start\nSecond\nline\n";
    const std::vector<Caption> caps = parseSrt(srt);
    CHECK(caps.size() == 2 && near(static_cast<float>(caps[0].start), 1.0f) && near(static_cast<float>(caps[0].end), 2.5f));
    CHECK(caps.size() == 2 && caps[0].text == "Hello there" && caps[1].text == "Second\nline");
    CHECK(parseSrt(toSrt(caps)) == caps);
    std::vector<Caption> longOne = {{0, 10, "one two three four five six seven eight nine ten eleven twelve"}};
    splitLongCaptions(longOne, 20);
    CHECK(longOne.size() >= 3 && near(static_cast<float>(longOne.front().start), 0) && near(static_cast<float>(longOne.back().end), 10));
    for (size_t i = 1; i < longOne.size(); ++i) CHECK(longOne[i].text.size() <= 20 && near(static_cast<float>(longOne[i].start), static_cast<float>(longOne[i - 1].end)));
    double t = 0;
    CHECK(parseCaptionTime("0:01.50", t) && near(static_cast<float>(t), 1.5f) && !parseCaptionTime("abc", t));
    Project p;
    nlohmann::json op = {{"op", "layer.add"}, {"type", "subtitle"}, {"srt", toSrt(caps)}};
    CHECK(run(p, op.dump().c_str()).ok);
    const Layer& l = p.comp.layers.front();
    CHECK(l.type == LayerType::Text && l.text.subtitle && l.text.captions.size() == 2 && l.text.boxEnabled);
    CHECK(l.text.textAt(1.2) == "Hello there" && l.text.textAt(2.7).empty() && l.text.textAt(3.5) == "Second\nline");
    CHECK(layerTypeName(l) == "subtitle");
    CHECK(run(p, R"({"op": "layer.set", "layer": "Subtitles", "captions": [{"start": 5, "end": 6, "text": "Bye"}, {"start": 0, "end": 1, "text": "Hi"}]})").ok);
    CHECK(p.comp.layers.front().text.captions.front().text == "Hi"); // sorted
    CHECK(!run(p, R"({"op": "layer.set", "layer": "Subtitles", "captions": [{"start": 2, "end": 1, "text": "x"}]})").ok);
    CHECK(run(p, R"([{"op": "layer.add", "type": "text", "name": "T"}, {"op": "layer.set", "layer": "T", "outline": true, "box": true}])").ok);
    CHECK(!run(p, R"({"op": "layer.set", "layer": "T", "srt": ""})").ok); // only subtitles have captions
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && validate(back).empty());
    const Layer* sub = nullptr;
    for (const Layer& x : back.comp.layers)
        if (x.text.subtitle) sub = &x;
    CHECK(sub && sub->text.captions.size() == 2 && sub->text.captions.back().text == "Bye");
    // Overlapping captions are put in order.
    for (Layer& x : back.comp.layers)
        if (x.text.subtitle) x.text.captions = {{3, 5, "b"}, {1, 4, "a"}};
    CHECK(!validate(back).empty() && !repair(back).empty() && validate(back).empty());
    for (const Layer& x : back.comp.layers)
        if (x.text.subtitle) CHECK(x.text.captions.size() == 2 && x.text.captions[0].text == "a" && near(static_cast<float>(x.text.captions[0].end), 3.0f));
}

void testLens() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "Flat", "position": [1060, 540]},
                     {"op": "layer.add", "type": "rectangle", "name": "Deep", "z": 1000, "position": [1060, 540]},
                     {"op": "layer.add", "type": "camera", "z": 200},
                     {"op": "property.set", "layer": "Camera", "prop": "rotation_y", "value": 10}])").ok);
    const Composition& c = p.comp;
    const Lens lens = c.lensAt(0);
    CHECK(near(lens.z, 200.0f - c.focalLength()) && near(lens.ry, 10.0f));
    // The camera's view of a 3D layer is the lens's.
    for (const Layer& l : c.layers)
        if (l.name == "Deep") {
            Projective a, b;
            CHECK(c.viewFor(l, 0, a) && c.viewThrough(lens, l, 0, b));
            CHECK(near(a.apply({1000, 500}).x, b.apply({1000, 500}).x) && near(a.apply({1000, 500}).y, b.apply({1000, 500}).y));
            Vec2 q;
            CHECK(c.project(lens, 1000, 500, 1000, q) && near(q.x, a.apply({1000, 500}).x, 0.05f));
        }
    // Behind the eye: not projected. 2D layers are planes at Z 0 in the 3D view; backgrounds are not there.
    Vec2 q;
    CHECK(!c.project(lens, lens.x, lens.y, lens.z - 100, q));
    for (const Layer& l : c.layers)
        if (l.name == "Flat") {
            Projective v;
            CHECK(c.viewThrough(lens, l, 0, v, true, true));
            Vec2 at0;
            CHECK(c.project(lens, 1060, 540, 0, at0) && near(v.apply({1060, 540}).x, at0.x, 0.05f));
        }
    float r[3], d[3], f[3];
    lensAxes(lens, r, d, f);
    CHECK(near(r[0] * f[0] + r[1] * f[1] + r[2] * f[2], 0.0f) && near(f[0] * f[0] + f[1] * f[1] + f[2] * f[2], 1.0f));
    CHECK(f[0] < 0.0f); // turned +10 on Y: it looks to the left
    // From the 3D view, everything is ordered by distance and the camera is left out.
    Lens side = lens;
    side.z = 3000.0f, side.ry = 180.0f; // behind the scene, looking back
    const auto order = c.drawOrder(0, false, &side);
    CHECK(order.size() == 2 && order.front()->name == "Flat"); // farthest from that eye first
}

void testExpressions() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "ellipse", "name": "Ball", "position": [100, 200]},
                     {"op": "layer.add", "type": "rectangle", "name": "Box", "start": 1, "duration": 4},
                     {"op": "expression.set", "layer": "Box", "prop": "rotation", "expression": "time * 90"},
                     {"op": "expression.set", "layer": "Box", "prop": "position", "expression": "layer(\"Ball\").position + [0, -50]"},
                     {"op": "expression.set", "layer": "Box", "prop": "opacity", "expression": "progress < 0.5 ? 20 : 100"}])").ok);
    CHECK(!run(p, R"({"op": "expression.set", "layer": "Box", "prop": "scale", "expression": "time * (2"})").ok); // does not read
    Composition& c = p.comp;
    const ExpressionScope scope(c);
    Layer* box = nullptr;
    for (Layer& l : c.layers)
        if (l.name == "Box") box = &l;
    CHECK(box && near(box->transform.rotation.valueAt(2.0), 180.0f));
    CHECK(box && near(box->transform.position.valueAt(2.0).x, 100.0f) && near(box->transform.position.valueAt(2.0).y, 150.0f));
    CHECK(box && near(box->transform.opacity.valueAt(1.5), 20.0f) && near(box->transform.opacity.valueAt(4.5), 100.0f));
    CHECK(box && near(box->transform.rotation.keyedAt(2.0), 0.0f)); // the keyframes are untouched
    // value, wiggle (smooth, bounded, the same each time), loop, math on vectors.
    AnimFloat f(10.0f);
    f.expression = "wiggle(3, 5)"; // like After Effects: the value, shaken
    const float w1 = f.valueAt(1.234f), w2 = f.valueAt(1.234f);
    CHECK(near(w1, w2) && w1 >= 5.0f && w1 <= 15.0f);
    AnimFloat looped;
    looped.setKey(0, 0, Easing::Linear); // the segment's easing is on the key it starts at
    looped.setKey(1, 100);
    looped.expression = "loop()";
    CHECK(near(looped.valueAt(1.5), 50.0f) && near(looped.valueAt(2.25), 25.0f));
    AnimVec2 v(Vec2{1, 2});
    v.expression = "value * 2 + [10, 20]";
    CHECK(near(v.valueAt(0).x, 12.0f) && near(v.valueAt(0).y, 24.0f));
    // A broken one falls back to the keyframes and says why.
    AnimFloat broken(7.0f);
    broken.expression = "layer(\"Nobody\").opacity";
    CHECK(near(broken.valueAt(0), 7.0f) && !expressionError(broken).empty());
    CHECK(checkExpression("clamp(sin(time) * 2, -1, 1)").empty() && !checkExpression("1 +").empty());
    // Saved with the project.
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    for (const Layer& l : back.comp.layers)
        if (l.name == "Box") CHECK(l.transform.rotation.expression == "time * 90");
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
        {"media", testMedia},
        {"operations: build and query", testOpsBuildAndQuery},
        {"operations: atomic batches and errors", testOpsAtomic},
        {"operations: compounds, effects, split, order", testOpsStructure},
        {"operations: file round trip and describe", testOpsRoundTrip},
        {"validate and repair", testValidateAndRepair},
        {"csv and numbers", testCsvAndNumbers},
        {"length limit", testLengthLimit},
        {"copy and paste", testCopyPaste},
        {"arrange and transitions", testArrangeAndTransitions},
        {"file parameters", testFileParams},
        {"background", testBackground},
        {"3d camera", testCamera3D},
        {"3d camera tilt", testCamera3DTilt},
        {"cut transitions", testCutTransitions},
        {"subtitles", testSubtitles},
        {"lens", testLens},
        {"expressions", testExpressions},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::printf("[%s] %s\n", g_failures == before ? " ok " : "FAIL", name);
    }
    std::printf("%s\n", g_failures == 0 ? "All tests passed." : "Some tests failed.");
    return g_failures == 0 ? 0 : 1;
}
