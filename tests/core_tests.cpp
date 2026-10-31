// Minimal, dependency-free unit tests for the document model.

#include "core/Animated.h"
#include "core/Arrange.h"
#include "core/AudioDsp.h"
#include "core/Clipboard.h"
#include "core/Data.h"
#include "core/Deform.h"
#include "core/Describe.h"
#include "core/Editing.h"
#include "core/Expressions.h"
#include "core/Ops.h"
#include "core/PluginCatalog.h"
#include "core/Validate.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/Language.h"
#include "core/MeshFile.h"
#include "core/Lottie.h"
#include "core/Presets.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Snapping.h"
#include "core/Svg.h"
#include "core/Tracking.h"
#include "core/Transitions.h"
#include "core/Updates.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
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
    CHECK(defaultPropertyValue(l, "volume", p.comp.center()) == std::vector<float>{100.0f});
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
    CHECK(builtinPresets().size() == 43); // none failed to parse
    // Letter presets set the text's Letter Animation (and only text takes them).
    for (const auto& p : builtinPresets())
        if (p.name == "Words Slide") {
            Layer t = makeTextLayer("Two words", {0, 0}, 5.0), r = makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 5.0);
            CHECK(presetAppliesTo(p, t) && !presetAppliesTo(p, r));
            applyPreset(p, t, 0.0);
            CHECK(t.text.letterIn == LetterAnim::Slide && t.text.letterUnit == LetterUnit::Word && near(t.text.letterDelay, 0.12f));
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
    CHECK(canTakeTransition(*first, "turn") && applyTransition(*first, "turn", TransitionEdge::In, 0.5) && first->threeD);
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
    CHECK(run(p, (R"([{"op": "layer.add", "type": "media", "name": "Talk", "media": )" + std::to_string(mediaId) + "}]").c_str()).ok);
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
    CHECK(a && v && p.comp.indexOf(a->id) > p.comp.indexOf(v->id)); // below the picture
    CHECK(!run(p, R"({"op": "layer.separate_audio", "layer": "Talk"})").ok); // no sound left to separate
    CHECK(validate(p).empty());
    // And back: the sound clip goes into the video again.
    const OpsResult linked = run(p, R"({"op": "layer.link_audio", "layer": "Talk"})");
    CHECK(linked.ok);
    v = p.comp.findLayer(videoId);
    CHECK(v && v->media.hasVideo && v->media.hasAudio);
    bool gone = true;
    for (const Layer& l : p.comp.layers) gone = gone && l.name != "Talk (audio)";
    CHECK(gone);
    CHECK(!run(p, R"({"op": "layer.link_audio", "layer": "Talk"})").ok); // it has its sound already
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
    CHECK(la->animations.size() == 1 && la->animations[0].name == "Slide Left In" && near(static_cast<float>(la->animations[0].start), 1.0f));
    CHECK(!la->transitionIn.active());
    // ... on an end: a transition.
    CHECK(placeMotion(c, *c.findLayer(ia), "fade", nullptr, MotionSpot::Out, 3.5, 0.5));
    CHECK(c.findLayer(ia)->transitionOut.id == "fade");
    // A Quick Animation on the cut: on the clip that comes in, from its start.
    AnimationPreset pop;
    CHECK(parsePreset(R"({"name": "Pop In", "category": "Entrance", "duration": 0.5,
                         "tracks": [{"property": "scale", "keys": [{"t": 0, "v": {"mul": 0}}, {"t": 1, "v": "rest"}]}]})", pop));
    Layer* placed = placeMotion(c, *c.findLayer(ia), {}, &pop, MotionSpot::Cut, 4.0, 0.5);
    CHECK(placed && placed->id == ib && placed->animations.size() == 1 && near(static_cast<float>(placed->animations[0].start), 4.0f));
}

void testSvgImport() {
    // Shapes, a group's transform, a style sheet class, a gradient, a hole, an arc.
    const std::string svg = R"svg(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100">
  <style>.red { fill: #ff0000; stroke: rgb(0, 0, 255); stroke-width: 2 }</style>
  <defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#000"/></linearGradient></defs>
  <rect width="200" height="100" fill="url(#g)"/>
  <g transform="translate(50 50)"><circle class="red" r="20"/></g>
  <path id="ring" d="M120 20h60v60h-60z M135 35h30v30h-30z" fill="gold"/>
  <path d="M10 90 A 10 10 0 0 1 30 90" fill="none" stroke="black"/>
  <text x="0" y="0">left out</text>
</svg>)svg";
    SvgDrawing d;
    std::string error;
    CHECK(parseSvg(svg, d, &error));
    CHECK(d.shapes.size() == 4);
    CHECK(near(d.size.x, 200.0f) && near(d.size.y, 100.0f));
    if (d.shapes.size() == 4) {
        CHECK(d.shapes[0].fillKind == ShapeFill::Linear && near(d.shapes[0].fillAngle, 90.0f) && near(d.shapes[0].fillColor.r, 1.0f));
        // The circle went through its group's translate; its class colored it.
        Vec2 mn, mx;
        CHECK(bezierBounds(d.shapes[1].outline, mn, mx) && near(mn.x, 30.0f, 0.1f) && near(mx.y, 70.0f, 0.1f));
        CHECK(near(d.shapes[1].fillColor.r, 1.0f) && d.shapes[1].stroke && near(d.shapes[1].strokeColor.b, 1.0f) && near(d.shapes[1].strokeWidth, 2.0f));
        // The inner square is a hole of the outer one.
        CHECK(d.shapes[2].name == "ring" && d.shapes[2].holes.size() == 1 && d.shapes[2].outline.vertices.size() == 4);
        // The arc: an open, stroked outline.
        CHECK(!d.shapes[3].outline.closed && !d.shapes[3].fill && d.shapes[3].stroke);
    }
    CHECK(!parseSvg("<html></html>", d, &error) && !error.empty());

    // As layers (svg.add): a compound fitted in the composition, the hole a Subtract mask.
    Project p;
    p.comp.width = 1000, p.comp.height = 1000, p.comp.duration = 4.0;
    nlohmann::json op = {{"op", "svg.add"}, {"svg", svg}, {"name", "Logo"}, {"time", 1.0}};
    const OpsResult r = applyOps(p, nlohmann::json::array({op}));
    CHECK(r.ok);
    const Layer* group = p.comp.findLayer(r.results[0]["layer"].get<uint64_t>());
    CHECK(group && group->type == LayerType::Compound && group->name == "Logo" && near(static_cast<float>(group->inPoint), 1.0f));
    const Layer* ring = nullptr;
    int children = 0;
    for (const Layer& l : p.comp.layers)
        if (l.parent && group && l.parent == group->id) {
            ++children;
            CHECK(l.type == LayerType::Shape && l.shape.kind == ShapeKind::Path);
            if (l.name == "ring") ring = &l;
        }
    CHECK(children == 4);
    CHECK(ring && ring->masks.size() == 1 && ring->masks[0].mode == MaskMode::Subtract);
    // 200 x 100 fits 800 wide (80%): four times bigger, the ring 240 px.
    if (ring) {
        Vec2 mn, mx;
        CHECK(bezierBounds(ring->shape.path, mn, mx) && near(mx.x - mn.x, 240.0f, 0.5f));
    }
}

void testLottieExport() {
    Project p;
    p.comp.width = 800, p.comp.height = 600, p.comp.fps = 30, p.comp.duration = 3.0;
    Layer box = makeShapeLayer(ShapeKind::Rectangle, {400, 300}, 3.0);
    box.name = "Box";
    box.transform.position.setKey(0.0, {200, 300}, Easing::EaseOut);
    box.transform.position.setKey(1.0, {600, 300}, Easing::Hold);
    box.transform.position.setKey(2.0, {600, 100}, Easing::Linear);
    box.transform.rotation.expression = "time * 90";
    const uint64_t boxId = p.comp.addLayer(box).id;
    Layer star = makeShapeLayer(ShapeKind::Star, {100, 100}, 3.0);
    star.shape.strokeEnabled = true;
    p.comp.addLayer(star);
    Layer cam;
    cam.type = LayerType::Camera;
    cam.name = "Cam";
    p.comp.addLayer(cam);
    p.comp.createCompound({boxId}, 0.0);
    std::vector<std::string> leftOut;
    const nlohmann::json j = nlohmann::json::parse(toLottie(p.comp, {}, &leftOut));
    CHECK(j["w"] == 800 && j["h"] == 600 && j["fr"] == 30.0 && j["op"] == 90.0);
    CHECK(leftOut.size() == 1); // the camera
    const nlohmann::json* boxJson = nullptr;
    const nlohmann::json* nullJson = nullptr;
    for (const auto& l : j["layers"]) {
        if (l["nm"] == "Box") boxJson = &l;
        if (l["ty"] == 3) nullJson = &l;
    }
    CHECK(boxJson && nullJson && (*boxJson)["parent"] == (*nullJson)["ind"]);
    if (boxJson) {
        const auto& pos = (*boxJson)["ks"]["p"];
        CHECK(pos["a"] == 1 && pos["k"].size() == 3);
        CHECK(pos["k"][0].contains("o") && pos["k"][0].contains("to")); // eased, along its motion path
        CHECK(pos["k"][1]["h"] == 1);                                  // held
        // An expression: baked every frame of the clip.
        CHECK((*boxJson)["ks"]["r"]["a"] == 1 && (*boxJson)["ks"]["r"]["k"].size() >= 90);
        CHECK((*boxJson)["shapes"][0]["it"][0]["ty"] == "rc");
    }
}

void testQuickAnimationMarks() {
    Layer l = makeShapeLayer(ShapeKind::Rectangle, {100, 100}, 5.0);
    AnimationPreset fade;
    CHECK(parsePreset(R"({"name": "Fade In", "category": "Entrance", "duration": 1,
                         "tracks": [{"property": "opacity", "keys": [{"t": 0, "v": 0}, {"t": 1, "v": "rest"}]}]})", fade));
    applyQuickAnimation(fade, l, 1.0);
    CHECK(l.animations.size() == 1 && near(static_cast<float>(l.animations[0].start), 1.0f) && l.animations[0].properties.size() == 1);
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
        CHECK(!m.transform.scale.isAnimated() && near(m.transform.scale.base.x, 40.0f) && near(m.transform.opacity.base, 100.0f));
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
    CHECK(l.text.outlineEnabled && l.text.boxColor.base.a > 0.7f && l.text.wrapWidth.base > p.comp.width * 0.75f);
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

void testCounter() {
    Project p;
    CHECK(run(p, R"({"op":"layer.add","type":"counter","duration":4,"start":1})").ok);
    auto shown = [&p](double t) {
        const Layer& c = p.comp.layers.front();
        return c.text.textAt(t, c.inPoint, c.outPoint);
    };
    const Layer& l = p.comp.layers.front();
    CHECK(layerTypeName(l) == "counter" && l.text.counter && !l.text.subtitle && !l.text.counterValue.isAnimated());
    // It counts by itself from its clip's start: 0 to 100 in 2 s, easing out, then stays.
    CHECK(shown(1) == "0" && shown(2) == "88" && shown(3) == "100" && shown(4.5) == "100");
    CHECK(run(p, R"({"op":"layer.set","layer":"Counter","from":10,"to":20,"count_duration":0,"count_easing":"linear"})").ok);
    CHECK(shown(1) == "10" && shown(3) == "15" && shown(5) == "20"); // over the whole clip
    CHECK(!run(p, R"({"op":"layer.set","layer":"Counter","count_easing":"wobbly"})").ok);
    CHECK(run(p, R"({"op":"layer.set","layer":"Counter","start":2})").ok);
    CHECK(shown(2) == "10" && shown(4) == "15"); // moved: counts from its new start
    // Keyframes on its value take over.
    CHECK(run(p, R"({"op":"key.add","layer":"Counter","prop":"counter_value","time":4,"value":250})").ok);
    CHECK(p.comp.layers.front().text.textAt(4) == "250");
    Project loaded;
    CHECK(deserializeProject(serializeProject(p), loaded) && validate(loaded).empty());
    CHECK(loaded.comp.layers.front().text.counter && loaded.comp.layers.front().text.textAt(4) == "250");
    loaded.comp.layers.front().text.subtitle = true;
    CHECK(!validate(loaded).empty() && !repair(loaded).empty());
    CHECK(loaded.comp.layers.front().text.subtitle && !loaded.comp.layers.front().text.counter);

    // Its look: decimals, thousands, prefix and suffix, kept in the file.
    CHECK(formatCounter(1234567.891, 2, ",") == "1,234,567.89" && formatCounter(1234.5, 1, ".") == "1.234,5");
    CHECK(formatCounter(-1234, 0, " ") == "-1 234" && formatCounter(999, 0, ",") == "999" && formatCounter(-0.001, 1, "") == "0.0");
    CHECK(run(p, R"({"op":"layer.set","layer":"Counter","decimals":1,"prefix":"R$ ","suffix":" mil","separator":"."})").ok);
    CHECK(p.comp.layers.front().text.textAt(4) == "R$ 250,0 mil");
    CHECK(!run(p, R"({"op":"layer.set","layer":"Counter","decimals":9})").ok);
    CHECK(!run(p, R"({"op":"layer.set","layer":"Counter","separator":"_"})").ok);
    Project again;
    CHECK(deserializeProject(serializeProject(p), again) && validate(again).empty());
    CHECK(again.comp.layers.front().text.textAt(4) == "R$ 250,0 mil");
    CHECK(run(p, R"({"op":"layer.add","type":"text","name":"Plain"})").ok);
    CHECK(!run(p, R"({"op":"layer.set","layer":"Plain","suffix":"%"})").ok);
}

void testLetters() {
    Project p;
    CHECK(run(p, R"({"op":"layer.add","type":"text","name":"T","text":"Hello world","letter_in":"rise","letter_by":"word",
                     "letter_order":"center","letter_delay":0.1,"letter_duration":0.4,"letter_loop":"wave","fit_lines":2})").ok);
    const TextProps& t = p.comp.layers.front().text;
    CHECK(t.letterIn == LetterAnim::Rise && t.letterUnit == LetterUnit::Word && t.letterOrder == LetterOrder::Center);
    CHECK(t.letterLoop == LetterLoop::Wave && near(t.letterDelay, 0.1f) && t.fitLines == 2 && t.lettersAnimated());
    CHECK(!run(p, R"({"op":"layer.set","layer":"T","letter_in":"explode"})").ok);
    CHECK(!run(p, R"({"op":"layer.set","layer":"T","letter_delay":-1})").ok);
    CHECK(run(p, R"({"op":"layer.set","layer":"T","letter_out":"fade","letter_in":"none"})").ok);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && validate(back).empty());
    const TextProps& b = back.comp.layers.front().text;
    CHECK(b.letterIn == LetterAnim::None && b.letterOut == LetterAnim::Fade && b.letterUnit == LetterUnit::Word && b.fitLines == 2);
    CHECK(run(p, R"({"op":"layer.add","type":"rectangle","name":"R"})").ok);
    CHECK(!run(p, R"({"op":"layer.set","layer":"R","letter_in":"fade"})").ok);

    // Subtitles fit by themselves: two lines at most, and time to read each caption.
    CHECK(run(p, R"({"op":"layer.add","type":"subtitle","captions":[{"start":0,"end":0.2,"text":"Hi"},
                     {"start":0.5,"end":3,"text":"A much longer caption that keeps on going well past what two lines of subtitles can hold, really"}]})").ok);
    for (Layer& l : p.comp.layers)
        if (l.text.subtitle) CHECK(l.text.fitLines == 2);
    CHECK(run(p, R"({"op":"layer.set","layer":"Subtitles","fit_captions":true})").ok);
    for (const Layer& l : p.comp.layers)
        if (l.text.subtitle) {
            const auto& c = l.text.captions;
            CHECK(c.size() == 3 && near(static_cast<float>(c[0].end), 0.5f)); // lengthened up to the next one
            for (size_t i = 0; i + 1 < c.size(); ++i) CHECK(c[i].end <= c[i + 1].start + 1e-9 && c[i].text.size() <= 84);
        }
    CHECK(!run(p, R"({"op":"layer.set","layer":"T","fit_captions":true})").ok);
}

void testCrop() {
    Project p;
    MediaItem image;
    image.name = "photo.png";
    image.path = "C:/footage/photo.png";
    image.kind = MediaKind::Image;
    image.width = 400;
    image.height = 200;
    image.hasVideo = true;
    const uint64_t media = p.comp.addMedia(image);
    p.comp.addLayer(makeMediaLayer(*p.comp.findMedia(media), {0, 0}, 0.0, 5.0)).name = "Photo";
    Layer& l = p.comp.layers.front();
    auto props = l.properties();
    CHECK(l.findProperty(props, "crop_left") && l.findProperty(props, "crop_bottom"));
    CHECK(defaultPropertyValue(l, "crop_top", p.comp.center()) == std::vector<float>{0.0f});
    Vec2 mn, mx;
    l.media.cropRect(0.0, mn, mx);
    CHECK(near(mn.x, -200.0f) && near(mx.y, 100.0f) && !l.media.cropped(0.0));
    // A quarter off the left, half off the top: what is left stays where it was.
    CHECK(run(p, R"({"op":"property.set","layer":"Photo","prop":"crop_left","value":25})").ok);
    CHECK(run(p, R"({"op":"property.set","layer":"Photo","prop":"crop_top","value":50})").ok);
    const Layer& c = p.comp.layers.front();
    c.media.cropRect(0.0, mn, mx);
    CHECK(near(mn.x, -100.0f) && near(mn.y, 0.0f) && near(mx.x, 200.0f) && near(mx.y, 100.0f) && c.media.cropped(0.0));
    // Opposite sides never meet: at least 1% of the picture stays.
    CHECK(run(p, R"({"op":"property.set","layer":"Photo","prop":"crop_right","value":90})").ok);
    p.comp.layers.front().media.cropRect(0.0, mn, mx);
    CHECK(mx.x - mn.x >= 400.0f * 0.0099f);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && validate(back).empty());
    CHECK(near(back.comp.layers.front().media.cropTop.valueAt(0.0), 50.0f));
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
    // Several lines: names (var / let as in After Effects, or none), // comments, the last line is the result.
    AnimFloat lines(10.0f);
    lines.expression = "var speed = 90; // degrees a second\nlet turns = time * speed\nhalf = turns / 2\nhalf + value";
    CHECK(checkExpression(lines.expression).empty() && near(lines.valueAt(2.0), 100.0f));
    CHECK(!checkExpression("a = 1").empty() && !checkExpression("var b 2").empty());
    CHECK(checkExpression("value; ").empty());
    // A word on its way to a call is not an expression yet; names from earlier lines are.
    CHECK(!checkExpression("wiggle").empty() && !checkExpression("turns * 2").empty());
    CHECK(checkExpression("var speed = 90\nspeed * time").empty());
    // A vector on its own line is the result, not an index into the line before.
    AnimVec2 next(Vec2{0, 0});
    next.expression = "var a = 2 * 3\n[a, a + 1]";
    CHECK(checkExpression(next.expression).empty() && near(next.valueAt(0).x, 6.0f) && near(next.valueAt(0).y, 7.0f));
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

// Scripts, AI tools and the command line reach expressions through operations: they read the
// batch's own copy, try a formula without setting it, turn one into keyframes.
void testExpressionsThroughOperations() {
    Project p;
    OpsResult r = run(p, R"j([{"op": "layer.add", "type": "ellipse", "name": "Ball", "position": [400, 300]},
                             {"op": "effect.add", "layer": "Ball", "effect": "gaussian_blur", "params": {"size": 12}},
                             {"op": "layer.add", "type": "rectangle", "name": "Box"},
                             {"op": "expression.set", "layer": "Box", "prop": "opacity", "expression": "layer(\"Ball\").position.x / 8"},
                             {"op": "expression.set", "layer": "Box", "prop": "rotation", "expression": "layer(\"Ball\").fx0.size * 2"},
                             {"op": "query.layer", "layer": "Box"}])j");
    CHECK(r.ok);
    if (r.ok) { // no scope open: the operations bring their own
        const nlohmann::json& props = r.results[5]["props"];
        CHECK(props["opacity"]["value"] == 50.0 && props["opacity"]["expression"] == "layer(\"Ball\").position.x / 8");
        CHECK(props["rotation"]["value"] == 24.0 && !props["rotation"].contains("expression_error"));
    }
    // Tried, not set: values at several times, or what is wrong (with a suggestion), never a failure.
    r = run(p, R"j([{"op": "query.expression", "layer": "Box", "prop": "rotation", "expression": "time * 90", "times": [0, 1, 2]},
                   {"op": "query.expression", "layer": "Box", "prop": "rotation", "expression": "wigle(2, 3)"},
                   {"op": "query.expression", "layer": "Box", "prop": "opacity"},
                   {"op": "query.expression", "layer": "Box", "prop": "position", "expression": "layer(\"Nobody\").position"}])j");
    CHECK(r.ok && !r.changed);
    if (r.ok) {
        CHECK(r.results[0]["values"] == nlohmann::json::array({0.0, 90.0, 180.0}));
        CHECK(r.results[1]["ok"] == false && r.results[1]["error"].get<std::string>().find("did you mean \"wiggle\"") != std::string::npos);
        CHECK(r.results[2]["value"] == 50.0);
        CHECK(r.results[3]["ok"] == false);
    }
    CHECK(!run(p, R"j({"op": "expression.set", "layer": "Box", "prop": "scale", "expression": "wigle(2, 3)"})j").ok); // checked when set
    // velocity, speed and valueAt read the property's own keyframes.
    CHECK(run(p, R"j([{"op": "key.add", "layer": "Box", "prop": "position", "time": 0, "value": [0, 0], "easing": "linear"},
                     {"op": "key.add", "layer": "Box", "prop": "position", "time": 2, "value": [200, 0]}])j").ok);
    r = run(p, R"j([{"op": "query.expression", "layer": "Box", "prop": "position", "expression": "velocity", "time": 1},
                   {"op": "query.expression", "layer": "Box", "prop": "rotation", "expression": "layer(\"Box\").position.x + speed", "time": 1},
                   {"op": "query.expression", "layer": "Box", "prop": "position", "expression": "valueAt(time - 1)", "time": 2}])j");
    CHECK(r.ok);
    if (r.ok) {
        CHECK(near(r.results[0]["value"][0].get<float>(), 100.0f) && near(r.results[0]["value"][1].get<float>(), 0.0f));
        CHECK(near(r.results[1]["value"].get<float>(), 100.0f)); // rotation has no keys: speed 0
        CHECK(near(r.results[2]["value"][0].get<float>(), 100.0f));
    }
    // Keyframes are listed with what they hold, the value with what the expression makes of it.
    r = run(p, R"j([{"op": "expression.set", "layer": "Box", "prop": "position", "expression": "value + [1000, 0]"},
                    {"op": "query.layer", "layer": "Box", "time": 2}])j");
    CHECK(r.ok);
    if (r.ok) {
        const nlohmann::json& pos = r.results[1]["props"]["position"];
        CHECK(pos["value"][0] == 1200.0 && pos["keys"][0]["value"][0] == 0.0 && pos["keys"][1]["value"][0] == 200.0);
    }
    // Baked: keyframes a frame (or `step` frames) apart, the same values, no expression.
    CHECK(run(p, R"j({"op": "expression.set", "layer": "Box", "prop": "rotation", "expression": "time * 90"})j").ok);
    r = run(p, R"j([{"op": "expression.bake", "layer": "Box", "prop": "rotation", "from": 0, "to": 1, "step": 15},
                   {"op": "query.layer", "layer": "Box", "time": 0.5}])j");
    CHECK(r.ok && r.results[0]["keys"] == 3);
    if (r.ok) {
        const nlohmann::json& rot = r.results[1]["props"]["rotation"];
        CHECK(!rot.contains("expression") && rot["keys"].size() == 3 && rot["value"] == 45.0);
    }
    CHECK(!run(p, R"j({"op": "expression.bake", "layer": "Box", "prop": "rotation"})j").ok); // none left to bake
    // The description says what a formula drives.
    CHECK(describeProject(p).find("expression: opacity = layer(\"Ball\").position.x / 8") != std::string::npos);
    // Every operation has a name in the list AI tools get.
    const std::vector<std::string> names = operationNames();
    CHECK(std::find(names.begin(), names.end(), "query.expression") != names.end() &&
          std::find(names.begin(), names.end(), "expression.bake") != names.end());
}

// Media folders: names that keep the Media panel tidy, saved with the project.
void testMediaFolders() {
    Project p;
    MediaItem a, b;
    a.name = "intro.mp4", b.name = "logo.png";
    const uint64_t ida = p.comp.addMedia(a), idb = p.comp.addMedia(b);
    const std::string ops = R"j([{"op": "folder.add", "name": "Footage"}, {"op": "folder.add", "name": " Logos "},
                                 {"op": "media.move", "media": )j" + std::to_string(ida) + R"j(, "folder": "Footage"},
                                 {"op": "media.move", "media": )j" + std::to_string(idb) + R"j(, "folder": "Logos"},
                                 {"op": "folder.rename", "folder": "Footage", "name": "Video"},
                                 {"op": "query.composition"}])j";
    OpsResult r = run(p, ops.c_str());
    CHECK(r.ok);
    if (r.ok) CHECK(r.results[5]["folders"] == nlohmann::json::array({"Video", "Logos"}) && r.results[5]["media"][0]["folder"] == "Video");
    CHECK(!run(p, R"j({"op": "folder.add", "name": "Video"})j").ok);                         // each name once
    CHECK(!run(p, R"j({"op": "folder.rename", "folder": "Vdeo", "name": "X"})j").ok);       // a folder that is not there
    CHECK(!run(p, R"j({"op": "media.move", "media": 1, "folder": "Nowhere"})j").ok);
    // Saved, and read back; a folder an item names but the list lost comes back.
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    CHECK(back.comp.mediaFolders.size() == 2 && back.comp.findMedia(ida)->folder == "Video");
    back.comp.mediaFolders.clear();
    CHECK(!validate(back).empty());
    repair(back);
    CHECK(back.comp.mediaFolders.size() == 2);
    // Folders keep the order they are put in.
    CHECK(run(p, R"j({"op": "folder.move", "folder": "Logos", "index": 0})j").ok);
    CHECK(p.comp.mediaFolders == std::vector<std::string>({"Logos", "Video"}));
    CHECK(run(p, R"j({"op": "folder.move", "folder": "Logos", "index": 9})j").ok); // past the end: last
    CHECK(p.comp.mediaFolders == std::vector<std::string>({"Video", "Logos"}));
    CHECK(!run(p, R"j({"op": "folder.move", "folder": "Logos", "index": 0.5})j").ok);
    // Removing a folder keeps its media, at the top.
    CHECK(run(p, R"j({"op": "folder.remove", "folder": "Video"})j").ok);
    CHECK(p.comp.mediaFolders.size() == 1 && p.comp.findMedia(ida)->folder.empty());
}

// Gradient fills on text and plugin objects, and a picture's frame (corners, border, flips):
// set by operations, saved and read back.
void testGradientsAndFrames() {
    Project p;
    MediaItem img;
    img.name = "photo.png", img.kind = MediaKind::Image, img.width = 640, img.height = 480;
    const uint64_t mid = p.comp.addMedia(img);
    p.comp.addLayer(makeTextLayer("Hello", {100, 100}, 5.0));
    p.comp.addLayer(makeMediaLayer(*p.comp.findMedia(mid), {300, 300}, 0.0, 5.0));
    const uint64_t textId = p.comp.layers[1].id, mediaId = p.comp.layers[0].id;
    const std::string ops = R"j([{"op": "layer.set", "layer": )j" + std::to_string(textId) + R"j(, "gradient": "linear"},
        {"op": "property.set", "layer": )j" + std::to_string(textId) + R"j(, "prop": "text_color2", "value": [0, 0, 1, 1]},
        {"op": "layer.set", "layer": )j" + std::to_string(mediaId) + R"j(, "flip_h": true},
        {"op": "property.set", "layer": )j" + std::to_string(mediaId) + R"j(, "prop": "corner_radius", "value": 24},
        {"op": "property.set", "layer": )j" + std::to_string(mediaId) + R"j(, "prop": "border_width", "value": 3}])j";
    OpsResult r = run(p, ops.c_str());
    CHECK(r.ok);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer* t = back.comp.findLayer(textId);
    const Layer* m = back.comp.findLayer(mediaId);
    CHECK(t && t->text.fill == ShapeFill::Linear && t->text.color2.base.b == 1.0f);
    CHECK(m && m->media.flipH && !m->media.flipV && m->media.cornerRadius.base == 24.0f && m->media.borderWidth.base == 3.0f);
    CHECK(!run(p, (R"j({"op": "layer.set", "layer": )j" + std::to_string(textId) + R"j(, "flip_h": true})j").c_str()).ok); // text has no picture
}

// The docs list every word of the expression language and every operation.
void testReferencesComplete() {
    auto read = [](const char* file) {
        std::ifstream in(std::filesystem::path(BEMOTION_SOURCE_DIR) / file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const std::string expressions = read("docs/EXPRESSIONS.md"), operations = read("docs/OPERATIONS.md");
    CHECK(!expressions.empty() && !operations.empty());
    for (const std::string& w : expressionNames())
        if (expressions.find("`" + w) == std::string::npos && expressions.find(" " + w + "`") == std::string::npos) {
            std::printf("  docs/EXPRESSIONS.md does not mention the name %s\n", w.c_str());
            CHECK(false);
        }
    for (const std::string& w : expressionFunctions())
        if (expressions.find(w + "(") == std::string::npos && expressions.find(" " + w + " ") == std::string::npos &&
            expressions.find("`" + w) == std::string::npos && expressions.find(" " + w + "`") == std::string::npos) {
            std::printf("  docs/EXPRESSIONS.md does not mention the function %s\n", w.c_str());
            CHECK(false);
        }
    for (const std::string& op : operationNames())
        if (operations.find("`" + op + "`") == std::string::npos) {
            std::printf("  docs/OPERATIONS.md does not describe %s\n", op.c_str());
            CHECK(false);
        }
}

// The official plugin catalog: only plugins whose every file has a safe path and a hash.
void testPluginCatalog() {
    const std::string hash(64, 'a');
    const std::string text = R"({"catalog": 1, "plugins": [
        {"id": "good", "name": "Good", "version": "1.2.0", "api": 7, "permissions": ["edit"],
         "files": [{"path": "plugin.json", "size": 10, "sha256": ")" + hash + R"("},
                   {"path": "maps/world.lua", "size": 5, "sha256": ")" + hash + R"("}]},
        {"id": "escape", "files": [{"path": "plugin.json", "size": 1, "sha256": ")" + hash + R"("},
                                   {"path": "../../evil.lua", "size": 1, "sha256": ")" + hash + R"("}]},
        {"id": "nohash", "files": [{"path": "plugin.json", "size": 1, "sha256": "xyz"}]},
        {"id": "no/slash", "files": [{"path": "plugin.json", "size": 1, "sha256": ")" + hash + R"("}]},
        {"id": "nomanifest", "files": [{"path": "main.lua", "size": 1, "sha256": ")" + hash + R"("}]}]})";
    std::vector<CatalogPlugin> plugins;
    std::string error;
    CHECK(parsePluginCatalog(text, plugins, &error));
    CHECK(plugins.size() == 1 && plugins[0].id == "good" && plugins[0].size() == 15 && plugins[0].api == 7 && !error.empty());
    CHECK(!parsePluginCatalog("[1, 2]", plugins) && !parsePluginCatalog("not json", plugins));
    CHECK(safeCatalogPath("main.lua") && safeCatalogPath("maps/world.lua"));
    CHECK(!safeCatalogPath("/etc/x") && !safeCatalogPath("a/../b") && !safeCatalogPath("C:/x") && !safeCatalogPath("a\\b") && !safeCatalogPath("a//b"));
    CHECK(compareVersions("1.10.0", "1.9") > 0 && compareVersions("1.0", "1.0.0") == 0 && compareVersions("0.9.1", "1.0") < 0);
}

void testUpdates() {
    CHECK(compareReleaseVersions("0.2.0", "0.2.0-beta.3") > 0 && compareReleaseVersions("0.2.0-beta.3", "0.2.0-beta.2") > 0);
    CHECK(compareReleaseVersions("0.2.0-beta.10", "0.2.0-beta.9") > 0 && compareReleaseVersions("0.2.0-beta.2", "0.1.9") > 0);
    CHECK(compareReleaseVersions("v1.0.0", "1.0") == 0 && compareReleaseVersions("1.0.0-rc.1", "1.0.0-beta.5") > 0);
    CHECK(compareReleaseVersions("1.0.0-beta", "1.0.0-beta.1") < 0 && compareReleaseVersions("nonsense", "0.0.1") < 0);
    CHECK(isPrerelease("0.1.0-beta.2") && !isPrerelease("0.1.0"));

    const std::string text = R"([
        {"tag_name": "v0.3.0-beta.1", "prerelease": true, "html_url": "https://x/3b1",
         "assets": [{"name": "bemotion-0.3.0-beta.1-setup.exe", "browser_download_url": "https://x/s3", "size": 30},
                    {"name": "SHA256SUMS.txt", "browser_download_url": "https://x/sums3", "size": 2}]},
        {"tag_name": "v0.4.0", "draft": true, "assets": []},
        {"tag_name": "nightly", "assets": []},
        {"tag_name": "v0.2.0", "name": "bemotion 0.2.0", "body": "Notes",
         "assets": [{"name": "bemotion-0.2.0-setup.exe", "browser_download_url": "https://x/s2", "size": 20},
                    {"name": "bemotion-0.2.0-portable.zip", "browser_download_url": "https://x/z2", "size": 21},
                    {"name": "bemotion-x86_64.AppImage", "browser_download_url": "https://x/a2", "size": 22}]}])";
    std::vector<Release> releases;
    CHECK(parseReleases(text, releases) && releases.size() == 2);
    std::string error;
    CHECK(!parseReleases(R"({"message": "API rate limit exceeded"})", releases, &error) && error.find("rate limit") != std::string::npos);
    parseReleases(text, releases);

    auto beta = newestRelease(releases, "0.1.0-beta.2", true);
    CHECK(beta && beta->version == "0.3.0-beta.1" && beta->prerelease);
    auto stable = newestRelease(releases, "0.1.0-beta.2", false);
    CHECK(stable && stable->version == "0.2.0" && stable->notes == "Notes");
    CHECK(!newestRelease(releases, "0.3.0-beta.1", true) && !newestRelease(releases, "0.2.0", false));
    const ReleaseAsset* win = packageFor(*stable, UpdatePlatform::Windows);
    CHECK(win && win->url == "https://x/s2" && win->size == 20);
    CHECK(packageFor(*stable, UpdatePlatform::Linux) && !packageFor(*stable, UpdatePlatform::Mac));
    CHECK(beta->asset("SHA256SUMS.txt") && !beta->asset("missing"));

    const std::string a(64, 'A'), b(64, 'b');
    const auto sums = parseSha256Sums(a + " *bemotion-0.2.0-setup.exe\r\n" + b + "  bemotion-x86_64.AppImage\nnot a line\n");
    CHECK(sums.size() == 2 && sums.at("bemotion-0.2.0-setup.exe") == std::string(64, 'a') && sums.at("bemotion-x86_64.AppImage") == b);
}

// A project closed and another opened: expressions read the new one (the closed one is gone,
// reading it crashed when a project was opened again).
void testExpressionsAcrossProjects() {
    auto make = [](float x) {
        auto p = std::make_unique<Project>();
        CHECK(run(*p, R"([{"op": "layer.add", "type": "ellipse", "name": "Ball"},
                          {"op": "layer.add", "type": "rectangle", "name": "Box"},
                          {"op": "expression.set", "layer": "Box", "prop": "position", "expression": "layer(\"Ball\").position"}])").ok);
        for (Layer& l : p->comp.layers)
            if (l.name == "Ball") l.transform.position.base = Vec2{x, 0};
        return p;
    };
    auto first = make(10.0f);
    setExpressionComposition(&first->comp);
    { const ExpressionScope s(first->comp); }
    first.reset(); // closed
    auto second = make(20.0f);
    setExpressionComposition(&second->comp);
    { const ExpressionScope s(second->comp); } // ends back on the editor's composition: the second
    const Layer* box = nullptr;
    for (const Layer& l : second->comp.layers)
        if (l.name == "Box") box = &l;
    CHECK(box && near(box->transform.position.valueAt(0).x, 20.0f));
    setExpressionComposition(nullptr);
}


// --- paths, masks, blending, mattes -----------------------------------------------------------

void testPathsAndMasks() {
    // Shapes become Bézier paths that keep their outline.
    ShapeProps rect = makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 1.0).shape;
    rect.size.base = {200, 100};
    rect.roundness.base = 20;
    const BezierPath rounded = shapeToBezier(rect, 0.0);
    CHECK(rounded.vertices.size() == 8 && rounded.closed);
    Vec2 mn, mx;
    CHECK(bezierBounds(rounded, mn, mx) && near(mn.x, -100) && near(mx.y, 50));
    const BezierPath oval = ellipsePath({-50, -50}, {50, 50});
    CHECK(oval.vertices.size() == 4 && near(bezierPoint(oval, 0, 0.5f).length(), 50.0f, 0.5f)); // on the circle
    // A new vertex splits a curve without changing it.
    BezierPath split = oval;
    const Vec2 before = bezierPoint(oval, 0, 0.5f);
    insertVertex(split, 0, 0.5f);
    CHECK(split.vertices.size() == 5 && near((split.vertices[1].point - before).length(), 0.0f, 0.01f));
    CHECK(near((bezierPoint(split, 0, 0.5f) - bezierPoint(oval, 0, 0.25f)).length(), 0.0f, 0.05f));
    size_t segment = 99;
    float u = 0;
    CHECK(nearestSegment(oval, before + Vec2{0.5f, 0.5f}, 4.0f, segment, u) && segment == 0 && near(u, 0.5f, 0.05f));

    // Operations: a path layer, a mask, a blending mode, a track matte.
    Project p;
    auto r = applyOps(p, nlohmann::json::parse(R"([
        {"op": "layer.add", "type": "path", "name": "Wave", "path": {"closed": false, "vertices": [[0, 0], [100, 0, -30, -40, 30, 40], [200, 0]]}},
        {"op": "layer.add", "type": "ellipse", "name": "Spot"},
        {"op": "layer.add", "type": "rectangle", "name": "Card", "below": "Spot"},
        {"op": "layer.set", "layer": "Card", "matte": "alpha", "blend": "multiply", "gradient": "linear", "repeater": true},
        {"op": "mask.add", "layer": "Card", "shape": "ellipse", "mode": "subtract", "feather": 12},
        {"op": "layer.set", "layer": "Spot", "shape": "path"}
    ])"));
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.error.c_str());
    Layer* wave = nullptr;
    Layer* card = nullptr;
    Layer* spot = nullptr;
    for (Layer& l : p.comp.layers) (l.name == "Wave" ? wave : l.name == "Card" ? card : spot) = &l;
    CHECK(wave && wave->shape.kind == ShapeKind::Path && wave->shape.path.vertices.size() == 3 && !wave->shape.path.closed);
    CHECK(wave && !wave->shape.fillEnabled && wave->shape.strokeEnabled); // an open path is a line
    CHECK(card && card->matte == MatteMode::Alpha && card->matteLayer == spot->id && card->blend == BlendMode::Multiply);
    CHECK(p.comp.isMatte(spot->id) && !p.comp.isMatte(card->id));
    CHECK(card->masks.size() == 1 && card->masks[0].mode == MaskMode::Subtract && near(card->masks[0].feather.base, 12.0f));
    CHECK(card->masks[0].path.vertices.size() == 4);
    CHECK(spot && spot->shape.kind == ShapeKind::Path && spot->shape.path.vertices.size() == 4); // the ellipse, as a path
    const BezierPath wavePath = wave->shape.path;
    const uint64_t spotId = spot->id; // (the layers move when the next batch replaces the project)
    // Mask values are properties; the repeater's and the gradient's too.
    CHECK(applyOps(p, nlohmann::json::parse(R"([{"op": "property.set", "layer": "Card", "prop": "mask0.opacity", "value": 50},
                                     {"op": "property.set", "layer": "Card", "prop": "repeat_copies", "value": 5},
                                     {"op": "property.set", "layer": "Card", "prop": "fill_color2", "value": "#336699"},
                                     {"op": "property.set", "layer": "Card", "prop": "stroke_dash", "value": 8}])")).ok);
    card = nullptr;
    for (Layer& l : p.comp.layers) if (l.name == "Card") card = &l;
    CHECK(card && card->shape.copiesAt(0) == 5 && near(card->masks[0].opacity.base, 50.0f));

    // Everything survives the file.
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer* c2 = nullptr;
    const Layer* w2 = nullptr;
    for (const Layer& l : back.comp.layers) {
        if (l.name == "Card") c2 = &l;
        if (l.name == "Wave") w2 = &l;
    }
    CHECK(c2 && c2->matte == MatteMode::Alpha && c2->blend == BlendMode::Multiply && c2->shape.fill == ShapeFill::Linear);
    CHECK(c2 && c2->masks.size() == 1 && c2->masks[0].path == card->masks[0].path && near(c2->masks[0].opacity.base, 50.0f));
    CHECK(c2 && c2->shape.repeater && near(c2->shape.repeatCopies.base, 5.0f) && near(c2->shape.dash.base, 8.0f));
    CHECK(w2 && w2->shape.path == wavePath);
    CHECK(validate(back).empty());

    // Track matte rules: never itself, never in a loop, gone with its layer.
    Project bad = back;
    for (Layer& l : bad.comp.layers)
        if (l.name == "Spot") l.matte = MatteMode::Luma, l.matteLayer = c2->id; // Spot <-> Card
    CHECK(!validate(bad).empty());
    repair(bad);
    CHECK(validate(bad).empty());
    int matted = 0;
    for (const Layer& l : bad.comp.layers) matted += l.matte != MatteMode::None;
    CHECK(matted == 1);
    CHECK(!applyOps(back, nlohmann::json::parse(R"({"op": "layer.set", "layer": "Card", "matte": "alpha", "matte_layer": "Card"})")).ok);
    back.comp.removeLayer(spotId);
    for (const Layer& l : back.comp.layers)
        if (l.name == "Card") CHECK(l.matte == MatteMode::None && l.matteLayer == 0);

    // Copied together, a layer keeps its matte (the copy's); grouped apart, it loses it.
    Project two;
    CHECK(applyOps(two, nlohmann::json::parse(R"([{"op": "layer.add", "type": "ellipse", "name": "M"},
        {"op": "layer.add", "type": "rectangle", "name": "R", "below": "M"}, {"op": "layer.set", "layer": "R", "matte": "luma"}])")).ok);
    std::vector<uint64_t> both;
    for (const Layer& l : two.comp.layers) both.push_back(l.id);
    const std::vector<uint64_t> pasted = pasteLayers(two.comp, copyLayers(two.comp, both), 0.0, 0, 0);
    int pastedMattes = 0;
    for (uint64_t id : pasted) {
        const Layer* l = two.comp.findLayer(id);
        if (l && l->matte == MatteMode::Luma) pastedMattes += std::find(pasted.begin(), pasted.end(), l->matteLayer) != pasted.end();
    }
    CHECK(pasted.size() == 2 && pastedMattes == 1);
    uint64_t rId = 0;
    for (const Layer& l : two.comp.layers)
        if (l.name == "R") rId = l.id;
    two.comp.createCompound({rId}, 0.0);
    CHECK(two.comp.findLayer(rId)->matte == MatteMode::None && validate(two).empty());

    // Solo: only the solo layers among their neighbors.
    Composition solo;
    const uint64_t a = solo.addLayer(makeShapeLayer(ShapeKind::Rectangle, {}, 1.0)).id;
    const uint64_t b = solo.addLayer(makeShapeLayer(ShapeKind::Ellipse, {}, 1.0)).id;
    CHECK(!solo.soloedOut(*solo.findLayer(a)));
    solo.findLayer(b)->solo = true;
    CHECK(solo.soloedOut(*solo.findLayer(a)) && !solo.soloedOut(*solo.findLayer(b)));
}

// --- speed, time remap, ripple, roll, slip, markers --------------------------------------------

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
    CHECK(near(static_cast<float>(maxOut), 6.0f) && near(static_cast<float>(minIn), 1.0f)); // the source's ends at this speed
    // Backwards: the same footage, from its end.
    CHECK(setClipSpeed(c, id, 2.0, true));
    CHECK(near(static_cast<float>(clip().media.sourceTime(2.0)), 10.0f) && near(static_cast<float>(clip().outPoint), 6.0f));
    CHECK(near(static_cast<float>(clip().media.sourceTime(5.9)), 2.2f));
    // Time remap: keys at the ends with what they showed; a frozen frame: one value.
    CHECK(setTimeRemap(c, id, true));
    CHECK(clip().media.timeRemap && clip().media.remap.keyCount() == 2 && near(clip().media.remap.keys[0].value, 10.0f));
    auto props = clip().properties();
    CHECK(clip().findProperty(props, "time_remap") != nullptr);
    CHECK(freezeFrame(c, id, 4.0));
    CHECK(near(static_cast<float>(clip().media.sourceTime(2.0)), 6.0f) && near(static_cast<float>(clip().media.sourceTime(5.0)), 6.0f));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer& again = *back.comp.findLayer(id);
    CHECK(again.media.timeRemap && again.media.reversed && near(static_cast<float>(again.media.speed), 2.0f) && near(again.media.remap.base, 6.0f));
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
    CHECK(near(slipClip(t, B, 0.25), 0.25f) && near(in(B), 1.0f) && near(t.findLayer(B)->transform.opacity.keys[0].time, 2.25f));
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
    ])")).ok);
    // Two on One's track, right after it.
    Composition& oc = ops.comp;
    CHECK(applyOps(ops, nlohmann::json::parse(R"({"op": "layer.set", "layer": "Two", "track_of": "One"})")).ok);
    CHECK(oc.trackRows().size() == 1);
    CHECK(!applyOps(ops, nlohmann::json::parse(R"({"op": "layer.add", "type": "ellipse", "start": 1, "track_of": "One"})")).ok); // overlaps
    auto r = applyOps(ops, nlohmann::json::parse(R"([
        {"op": "layer.roll", "layer": "One", "time": 1.5},
        {"op": "layer.trim", "layer": "Two", "edge": "out", "time": 3, "ripple": true},
        {"op": "marker.set", "time": 1, "to": 2, "name": "Drop"},
        {"op": "query.composition"}
    ])"));
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.error.c_str());
    CHECK(oc.markers.size() == 2 && oc.markers[0].color == 3 && oc.markers[1].name == "Drop" && near(static_cast<float>(oc.markers[1].time), 2.0f));
    CHECK(r.results.size() == 4 && r.results[3]["markers"].size() == 2);
    CHECK(applyOps(ops, nlohmann::json::parse(R"([{"op": "layer.ripple_delete", "layers": ["One"]}, {"op": "marker.remove", "index": 0}])")).ok);
    CHECK(oc.markers.size() == 1 && oc.layers.size() == 1 && near(static_cast<float>(oc.layers[0].inPoint), 0.0f));
    CHECK(!applyOps(ops, nlohmann::json::parse(R"({"op": "layer.speed", "layer": "Two", "speed": 50})")).ok); // not footage
    // Markers: one per moment, in order, stepping between them.
    Composition m;
    m.duration = 10.0;
    addMarker(m, 3.0, "C");
    addMarker(m, 1.0, "A");
    CHECK(addMarker(m, 1.01, "", 2) == 0 && m.markers.size() == 2 && m.markers[0].name == "A" && m.markers[0].color == 2);
    double next = 0;
    CHECK(nextMarker(m, 1.0, true, next) && near(static_cast<float>(next), 3.0f));
    CHECK(nextMarker(m, 3.0, false, next) && near(static_cast<float>(next), 1.0f) && !nextMarker(m, 3.0, true, next));
    CHECK(moveMarker(m, 1, 0.5) == 0 && m.markers[0].name == "C");
    Project mp;
    mp.comp = m;
    mp.comp.markers.push_back({0.2, "late", 9});
    CHECK(!validate(mp).empty());
    repair(mp);
    CHECK(validate(mp).empty() && mp.comp.markers.front().name == "late" && mp.comp.markers.front().color == kLabelColorCount - 1);
    Project mback;
    CHECK(deserializeProject(serializeProject(mp), mback) && mback.comp.markers == mp.comp.markers);
}

} // namespace

void testColorEffects() {
    // Curves: straight without points, through its points, never beyond them, flat past the ends.
    CHECK(near(curveValue({}, 0.3f), 0.3f));
    const std::vector<Vec2> s = {{0, 0}, {0.25f, 0.1f}, {0.75f, 0.9f}, {1, 1}};
    CHECK(near(curveValue(s, 0.25f), 0.1f) && near(curveValue(s, 0.75f), 0.9f));
    float last = -1.0f;
    bool monotone = true;
    for (int i = 0; i <= 100; ++i) {
        const float y = curveValue(s, i / 100.0f);
        monotone = monotone && y >= last - 1e-5f;
        last = y;
    }
    CHECK(monotone);
    CHECK(near(curveValue({{0.2f, 0.3f}, {0.8f, 0.6f}}, 0.0f), 0.3f) && near(curveValue({{0.2f, 0.3f}, {0.8f, 0.6f}}, 1.0f), 0.6f));
    CHECK(curveIsIdentity({}) && curveIsIdentity({{0, 0}, {1, 1}}) && !curveIsIdentity(s));
    std::vector<Vec2> messy = {{0.9f, 2.0f}, {0.1f, -1.0f}, {0.1f, 0.5f}};
    cleanCurve(messy);
    CHECK(messy.size() == 2 && messy[0].x < messy[1].x && messy[1].y == 1.0f && messy[0].y == 0.0f);

    // .cube files: a 3D table, a 1D one (made 3D), and the mistakes people make.
    Lut3D lut;
    std::string err;
    CHECK(parseCubeLut("TITLE \"Swap\"\n# a comment\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n", lut, &err));
    CHECK(lut.size == 2 && lut.title == "Swap" && lut.rgb.size() == 24 && lut.rgb[3] == 1.0f);
    CHECK(parseCubeLut("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n", lut) && lut.size == 33);
    CHECK(!parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n", lut, &err) && err.find("8 rows") != std::string::npos);
    CHECK(!parseCubeLut("hello", lut, &err));

    // File and curves travel with the project, and through presets.
    Project p;
    Layer& l = p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {960, 540}, 5.0));
    l.effects.push_back(makeEffect(EffectType::Lut));
    l.effects.back().file = "bundled:Warm.cube";
    l.effects.push_back(makeEffect(EffectType::Curves));
    l.effects.back().curves[2] = {{0, 0.1f}, {1, 0.9f}};
    l.effects.push_back(makeEffect(EffectType::ChromaKey));
    Project q;
    CHECK(deserializeProject(serializeProject(p), q, &err));
    CHECK(serializeProject(p) == serializeProject(q));
    const Layer& back = q.comp.layers.front();
    CHECK(back.effects.size() == 3 && back.effects[0].file == "bundled:Warm.cube" && back.effects[1].curves[2].size() == 2);
    CHECK(near(back.effects[2].value1.base, 30.0f)); // the key's tolerance default
    EffectPreset preset;
    CHECK(parseEffectPreset(effectPresetToJson({"Look", back.effects}), preset) && preset.effects[1].curves[2] == back.effects[1].curves[2] &&
          preset.effects[0].file == "bundled:Warm.cube");

    // Operations: set a LUT file and curves; wrong ones are refused with a reason.
    OpsResult r = run(p, R"([{"op": "layer.add", "type": "rectangle", "name": "Card"},
        {"op": "effect.add", "layer": "Card", "effect": "curves"},
        {"op": "effect.set", "layer": "Card", "index": 0, "curves": {"rgb": [[0, 0], [0.5, 0.7], [1, 1]], "blue": null}},
        {"op": "effect.add", "layer": "Card", "effect": "lut"},
        {"op": "effect.set", "layer": "Card", "index": 1, "file": "C:/looks/film.cube"},
        {"op": "query.layer", "layer": "Card"}])");
    CHECK(r.ok);
    if (r.ok) {
        const auto& fx = r.results[5]["effect_list"];
        CHECK(fx[0]["curves"]["rgb"].size() == 3 && fx[1]["file"] == "C:/looks/film.cube");
    }
    CHECK(!run(p, R"([{"op": "effect.set", "layer": "Card", "index": 0, "file": "x.cube"}])").ok);
    r = run(p, R"([{"op": "effect.set", "layer": "Card", "index": 0, "curves": {"rgbb": []}}])");
    CHECK(!r.ok && r.error.find("rgb") != std::string::npos);
    CHECK(!run(p, R"([{"op": "effect.set", "layer": "Card", "index": 0, "curves": {"red": [[0, 2]]}}])").ok);
}

// Paint, Puppet pins and the Mesh Warp: operations, the math, the file.
void testPaintPuppetWarp() {
    // One pin moves everything; two turn it rigidly; a pin's own point lands on its target.
    CHECK(near(puppetPoint({{{0, 0}, {10, 5}}}, {3, 4}).x, 13.0f));
    const std::vector<PinMove> two = {{{-100, 0}, {0, -100}}, {{100, 0}, {0, 100}}}; // a quarter turn
    const Vec2 turned = puppetPoint(two, {50, 0});
    CHECK(near(turned.x, 0.0f) && near(turned.y, 50.0f));
    CHECK(near(puppetPoint(two, {100, 0}).y, 100.0f));
    Vec2 offsets[kWarpPoints] = {};
    offsets[5] = {0, -40};
    CHECK(near(warpPoint(offsets, {0, 0}, {300, 300}, {100, 100}).y, 60.0f)); // on the moved grid point
    CHECK(near(warpPoint(offsets, {0, 0}, {300, 300}, {0, 0}).y, 0.0f));      // a corner stays

    Project p;
    p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 5.0));
    const std::string id = std::to_string(p.comp.layers[0].id);
    OpsResult r = run(p, (R"j([{"op": "paint.stroke", "layer": )j" + id + R"j(, "points": [[0, 0], [50, 50]], "width": 8},
        {"op": "paint.stroke", "layer": )j" + id + R"j(, "points": [[10, 10]], "erase": true},
        {"op": "puppet.pin.add", "layer": )j" + id + R"j(, "point": [20, 30]},
        {"op": "property.set", "layer": )j" + id + R"j(, "prop": "pin0", "value": [25, 30]},
        {"op": "layer.set", "layer": )j" + id + R"j(, "warp": true},
        {"op": "property.set", "layer": )j" + id + R"j(, "prop": "warp3", "value": [4, 0]},
        {"op": "property.set", "layer": )j" + id + R"j(, "prop": "paint_reveal", "value": 50}])j").c_str());
    CHECK(r.ok);
    const Layer& l = p.comp.layers[0];
    CHECK(l.paint.size() == 2 && l.paint[1].erase && l.pins.size() == 1 && l.warp && deforms(l, 0.0));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer& b = back.comp.layers[0];
    CHECK(b.paint == l.paint && b.pins.size() == 1 && near(b.pins[0].rest.x, 20.0f) && near(b.pins[0].position.base.x, 25.0f));
    CHECK(b.warp && near(b.warpPoints[3].base.x, 4.0f) && near(b.paintReveal.base, 50.0f));
    CHECK(run(p, (R"j([{"op": "puppet.pin.remove", "layer": )j" + id + R"j(, "index": 0}, {"op": "paint.clear", "layer": )j" + id + R"j(}])j").c_str()).ok);
    CHECK(p.comp.layers[0].pins.empty() && p.comp.layers[0].paint.empty());
    CHECK(!run(p, (R"j({"op": "paint.stroke", "layer": )j" + id + R"j(, "points": [], "width": 8})j").c_str()).ok);

    // The Puppet mesh: a pin's point follows it exactly; two pins turned together turn what is
    // between them; a piece of the picture apart from the pinned one stays put.
    CHECK(run(p, (R"j([{"op": "layer.set", "layer": )j" + id + R"j(, "warp": false},
        {"op": "puppet.pin.add", "layer": )j" + id + R"j(, "point": [-40, 0]},
        {"op": "puppet.pin.add", "layer": )j" + id + R"j(, "point": [40, 0]},
        {"op": "puppet.pin.add", "layer": )j" + id + R"j(, "point": [0, 0], "starch": true},
        {"op": "property.set", "layer": )j" + id + R"j(, "prop": "pin0", "value": [0, -40]},
        {"op": "property.set", "layer": )j" + id + R"j(, "prop": "pin1", "value": [0, 40]}])j").c_str()).ok);
    Layer& bent = p.comp.layers[0];
    CHECK(bent.pins.size() == 3 && bent.pins[2].starch && deforms(bent, 0.0));
    const Vec2 lo{-50, -50}, hi{50, 50};
    const Vec2 onPin = deformPoint(bent, 0.0, {-40, 0}, lo, hi), between = deformPoint(bent, 0.0, {20, 0}, lo, hi);
    CHECK(std::fabs(onPin.x) < 6.0f && std::fabs(onPin.y + 40.0f) < 6.0f);
    CHECK(std::fabs(between.x) < 6.0f && std::fabs(between.y - 20.0f) < 6.0f); // a quarter turn
    PuppetShape apart; // two blocks with a gap: the left one pinned, the right one free
    apart.key = "test";
    apart.lo = lo, apart.hi = hi;
    puppetGrid(lo, hi, apart.cols, apart.rows);
    apart.inside.assign(static_cast<size_t>(apart.cols * apart.rows), 0);
    for (int y = 0; y < apart.rows; ++y)
        for (int x = 0; x < apart.cols; ++x)
            if (x < apart.cols / 2 - 2 || x > apart.cols / 2 + 2) apart.inside[static_cast<size_t>(y * apart.cols + x)] = 1;
    setPuppetShape(bent.id, apart);
    bent.pins = {bent.pins[0]};
    bent.pins[0].rest = {-40, 0};
    bent.pins[0].position = AnimVec2{Vec2{-40, 30}};
    const Vec2 left = deformPoint(bent, 0.0, {-30, -10}, lo, hi), right = deformPoint(bent, 0.0, {40, 10}, lo, hi);
    CHECK(near(left.y, 20.0f) && near(right.x, 40.0f) && near(right.y, 10.0f));
    CHECK(puppetOnMesh(bent, {-40, 0}, lo, hi) && !puppetOnMesh(bent, {0, 0}, lo, hi));
    Project starchy;
    CHECK(deserializeProject(serializeProject(p), starchy) && starchy.comp.layers[0].pins.size() == 1);
    CHECK(run(p, (R"j([{"op": "puppet.pin.add", "layer": )j" + id + R"j(, "point": [1, 1], "starch": true}])j").c_str()).ok);
    CHECK(deserializeProject(serializeProject(p), starchy) && starchy.comp.layers[0].pins.back().starch);
}

// Model files: OBJ (with normals and a quad), STL (binary, welded), glTF (embedded), simplified.
void testMeshFiles() {
    MeshData m;
    std::string error;
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";
    CHECK(loadMeshFile("obj", obj, {}, m, error) && m.triangleCount() == 2 && m.vertexCount() == 4 && m.normals.size() == 12);
    std::string stl(84, ' ');
    const uint32_t one = 1;
    std::memcpy(&stl[80], &one, 4);
    const float tri[12] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0};
    stl.append(reinterpret_cast<const char*>(tri), 48);
    stl.append(2, ' ');
    CHECK(loadMeshFile("stl", stl, {}, m, error) && m.triangleCount() == 1 && m.vertexCount() == 3);
    zUpToYUp(m);
    CHECK(m.positions[3] == 1.0f && m.positions[7] == 0.0f && m.positions[8] == -1.0f); // (1, 0, 0) stays; (0, 1, 0) goes to (0, 0, -1)
    // A glTF triangle in a data URI: 3 positions (36 bytes of floats).
    const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<const unsigned char*>(pos);
    std::string enc;
    for (size_t i = 0; i < 36; i += 3) {
        const uint32_t v = bytes[i] << 16 | bytes[i + 1] << 8 | bytes[i + 2];
        for (int k = 3; k >= 0; --k) enc += b64[(v >> (6 * k)) & 63];
    }
    const std::string gltf = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" + enc +
                             R"("}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"translation":[5,0,0]}],"scenes":[{"nodes":[0]}]})";
    CHECK(loadMeshFile("gltf", gltf, {}, m, error) && m.triangleCount() == 1 && m.positions[3] == 6.0f);
    CHECK(!loadMeshFile("gltf", R"({"buffers":[{"uri":"model.bin"}]})", [](const std::string&, std::string&) { return false; }, m, error));
    CHECK(!loadMeshFile("obj", "garbage", {}, m, error) && !error.empty());
    // A fine grid of triangles, simplified to a few.
    MeshData grid;
    const int side = 60;
    for (int y = 0; y <= side; ++y)
        for (int x = 0; x <= side; ++x) grid.positions.insert(grid.positions.end(), {static_cast<float>(x), static_cast<float>(y), 0.0f});
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * (side + 1) + x), b = a + 1, c = a + side + 1, d = c + 1;
            grid.indices.insert(grid.indices.end(), {a, b, d, a, d, c});
        }
    simplifyMesh(grid, 500);
    CHECK(grid.triangleCount() > 50 && grid.triangleCount() <= 500);
}

// The tracker finds a moved patch again, to a fraction of a pixel.
void testTracking() {
    auto frame = [](float cx, float cy) {
        GrayImage g;
        g.width = g.height = 96;
        g.pixels.resize(96 * 96);
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 96; ++x) {
                const float dx = x - cx, dy = y - cy;
                g.pixels[static_cast<size_t>(y * 96 + x)] = static_cast<uint8_t>(40 + 200 * std::exp(-(dx * dx + dy * dy) / 40.0f) +
                                                                                 10 * ((x / 7 + y / 5) % 2));
            }
        return g;
    };
    const GrayImage a = frame(40, 40), b = frame(46, 37);
    const TrackResult r = trackPatch(a, b, {40, 40}, {40, 40}, 10, 16);
    CHECK(r.confidence > 0.8f && std::fabs(r.point.x - 46) < 1.0f && std::fabs(r.point.y - 37) < 1.0f);
    const uint8_t rgba[8] = {255, 255, 255, 255, 0, 0, 0, 255};
    const GrayImage g = grayFromRgba(rgba, 2, 1);
    CHECK(g.pixels.size() == 2 && g.pixels[0] > 250 && g.pixels[1] == 0);
}

// Mask Path keyframes: the outline moves between keys point by point, saved, moved with the clip.
void testMaskPathKeys() {
    Project p;
    p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 5.0));
    const uint64_t id = p.comp.layers[0].id;
    const std::string ops = R"j([{"op": "mask.add", "layer": )j" + std::to_string(id) + R"j(, "box": [0, 0, 100, 100]},
        {"op": "mask.set", "layer": )j" + std::to_string(id) + R"j(, "index": 0, "time": 0, "path": [[0, 0], [100, 0], [100, 100], [0, 100]]},
        {"op": "mask.set", "layer": )j" + std::to_string(id) + R"j(, "index": 0, "time": 2, "path": [[100, 0], [200, 0], [200, 100], [100, 100]]},
        {"op": "mask.duplicate", "layer": )j" + std::to_string(id) + R"j(, "index": 0}])j";
    OpsResult r = run(p, ops.c_str());
    CHECK(r.ok);
    if (!r.ok) return;
    const Mask& m = p.comp.layers[0].masks[0];
    CHECK(m.pathKeys.size() == 2 && p.comp.layers[0].masks.size() == 2 && p.comp.layers[0].masks[1].pathKeys.size() == 2);
    CHECK(near(m.pathAt(1.0).vertices[0].point.x, 50.0f) && near(m.pathAt(3.0).vertices[0].point.x, 100.0f));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && back.comp.layers[0].masks[0].pathKeys == m.pathKeys);
    p.comp.layers[0].shiftTime(1.0);
    CHECK(near(static_cast<float>(p.comp.layers[0].masks[0].pathKeys[1].time), 3.0f));
    CHECK(run(p, (R"j({"op": "mask.set", "layer": )j" + std::to_string(id) + R"j(, "index": 0, "animated": false, "time": 2})j").c_str()).ok);
    CHECK(p.comp.layers[0].masks[0].pathKeys.empty() && near(p.comp.layers[0].masks[0].path.vertices[0].point.x, 50.0f));
}

// Auto Level brings clips to one loudness; silence is found and cut out of a clip.
void testAutoLevelAndSilence() {
    auto tone = [](float amp, double seconds) {
        std::vector<float> s(static_cast<size_t>(seconds * 48000) * 2);
        for (size_t i = 0; i < s.size() / 2; ++i) s[i * 2] = s[i * 2 + 1] = amp * static_cast<float>(std::sin(2 * 3.14159265 * 220 * i / 48000.0));
        return s;
    };
    auto levelDb = [](const std::vector<float>& s) {
        double sum = 0;
        for (float v : s) sum += v * v;
        return 20 * std::log10(std::sqrt(sum / static_cast<double>(s.size())));
    };
    std::vector<float> quiet = tone(0.02f, 2.0), loud = tone(0.5f, 2.0);
    dsp::autoLevel(quiet, 48000, -18.0f, 0.0f);
    dsp::autoLevel(loud, 48000, -18.0f, 0.0f);
    CHECK(std::fabs(levelDb(quiet) - -18.0) < 0.5 && std::fabs(levelDb(loud) - -18.0) < 0.5);
    for (AudioEffectType t : {AudioEffectType::Reverb, AudioEffectType::Echo, AudioEffectType::Compressor, AudioEffectType::Filter}) {
        std::vector<float> s = tone(0.3f, 0.5);
        dsp::process(s, 48000, {makeAudioEffect(t)});
        CHECK(std::all_of(s.begin(), s.end(), [](float v) { return std::isfinite(v) && std::fabs(v) < 4.0f; }));
        CHECK(!dsp::processKey({makeAudioEffect(t)}).empty() || t == AudioEffectType::Filter);
    }

    // 1 s of sound, 1 s of silence, 1 s of sound: the silence is found (padded) ...
    std::vector<float> loudness(300, 0.2f);
    for (int i = 100; i < 200; ++i) loudness[i] = 0.0005f;
    const auto stretches = dsp::quietStretches(loudness, 0.01, -40.0f, 0.5, 0.1);
    CHECK(stretches.size() == 1 && near(static_cast<float>(stretches[0].first), 1.1f) && near(static_cast<float>(stretches[0].second), 1.9f));
    CHECK(dsp::quietStretches(loudness, 0.01, -40.0f, 1.5, 0.1).empty()); // shorter than the minimum
    CHECK(dsp::thresholdFromSample(loudness, 0.01, 1.2, 1.8) < -40.0f);
    // ... and cut out of a 3 s clip, the rest moving back.
    Project p;
    MediaItem voice;
    voice.name = "voice.wav", voice.kind = MediaKind::Audio, voice.duration = 3.0, voice.hasAudio = true;
    const uint64_t mid = p.comp.addMedia(voice);
    const uint64_t clip = p.comp.addLayer(makeMediaLayer(*p.comp.findMedia(mid), {0, 0}, 0.0, 3.0)).id;
    const double removed = removeQuietParts(p.comp, clip, stretches);
    CHECK(near(static_cast<float>(removed), 0.8f));
    double end = 0.0;
    int pieces = 0;
    for (const Layer& l : p.comp.layers) end = std::max(end, l.outPoint), pieces += l.type == LayerType::Media;
    CHECK(pieces == 2 && near(static_cast<float>(end), 2.2f));
    CHECK(validate(p).empty());
    // The operation needs the sound (an environment); audio.level sets one target on all clips.
    CHECK(!run(p, R"({"op": "audio.remove_silence", "layer": 1})").ok);
    CHECK(run(p, R"({"op": "audio.level", "target": -20})").ok);
    for (const Layer& l : p.comp.layers)
        CHECK(l.audioEffects.size() == 1 && l.audioEffects[0].type == AudioEffectType::AutoLevel && l.audioEffects[0].value == -20.0f);
}

void testAudioEffects() {
    // Defaults and ranges.
    CHECK(audioEffectFromId("noise_reduction") == AudioEffectType::NoiseReduction && audioEffectFromId("nope") == AudioEffectType::Count);
    AudioEffect duck = makeAudioEffect(AudioEffectType::Ducking);
    CHECK(duck.value == 15.0f && duck.value2 == -40.0f && near(duck.value3, 0.4f));
    duck.value = 90.0f;
    duck.value3 = -1.0f;
    clampAudioEffect(duck);
    CHECK(duck.value == 40.0f && near(duck.value3, 0.05f));

    // EQ: +6 dB under the low shelf is about 6 dB louder; a tone in the middle is left alone.
    auto sine = [](double freq, double seconds, float amp) {
        std::vector<float> s(static_cast<size_t>(seconds * 48000) * 2);
        for (size_t i = 0; i < s.size() / 2; ++i) s[i * 2] = s[i * 2 + 1] = amp * static_cast<float>(std::sin(2 * 3.14159265 * freq * i / 48000.0));
        return s;
    };
    auto rms = [](const std::vector<float>& s, size_t from, size_t to) {
        double sum = 0;
        for (size_t i = from * 2; i < to * 2; ++i) sum += s[i] * s[i];
        return std::sqrt(sum / static_cast<double>((to - from) * 2));
    };
    std::vector<float> low = sine(30, 1.0, 0.25f);
    const double before = rms(low, 24000, 48000);
    dsp::equalize(low, 48000, 6.0f, 0.0f, 0.0f);
    const double gainDb = 20 * std::log10(rms(low, 24000, 48000) / before);
    CHECK(gainDb > 5.0 && gainDb < 6.5);
    std::vector<float> mid = sine(1000, 1.0, 0.25f);
    const double midBefore = rms(mid, 24000, 48000);
    dsp::equalize(mid, 48000, 6.0f, 0.0f, 6.0f);
    CHECK(std::fabs(20 * std::log10(rms(mid, 24000, 48000) / midBefore)) < 1.0);

    // Noise reduction: steady hiss between the bursts gets much quieter, the bursts stay.
    std::vector<float> noisy = sine(440, 4.0, 0.3f);
    uint32_t seed = 1;
    for (size_t i = 0; i < noisy.size() / 2; ++i) {
        const bool burst = (i / 48000) % 2 == 0;
        seed = seed * 1664525u + 1013904223u;
        const float n = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.02f;
        noisy[i * 2] = noisy[i * 2 + 1] = (burst ? noisy[i * 2] : 0.0f) + n;
    }
    const double hiss = rms(noisy, 60000, 90000), tone = rms(noisy, 110000, 140000);
    dsp::reduceNoise(noisy, 48000, 20.0f, 50.0f);
    CHECK(rms(noisy, 60000, 90000) < hiss * 0.4);
    CHECK(rms(noisy, 110000, 140000) > tone * 0.9);

    // Loudness, and the key that names the processing (off or flat: none).
    const std::vector<float> loud = dsp::loudness(sine(440, 0.1, 0.5f), 480);
    CHECK(loud.size() == 10 && std::fabs(loud[5] - 0.5f / std::sqrt(2.0f)) < 0.02f);
    std::vector<AudioEffect> fx = {makeAudioEffect(AudioEffectType::Eq), makeAudioEffect(AudioEffectType::Gain)};
    CHECK(dsp::processKey(fx).empty() && !dsp::changesSamples(fx));
    fx[0].value = 3.0f;
    CHECK(!dsp::processKey(fx).empty());
    fx[0].enabled = false;
    CHECK(dsp::processKey(fx).empty());

    // Through operations and files.
    Project p;
    MediaItem song;
    song.name = "song.wav";
    song.path = "C:/audio/song.wav";
    song.kind = MediaKind::Audio;
    song.duration = 30.0;
    song.hasAudio = true;
    const uint64_t media = p.comp.addMedia(song);
    p.comp.addLayer(makeMediaLayer(*p.comp.findMedia(media), {0, 0}, 0.0, 5.0)).name = "Song";
    p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {960, 540}, 5.0)).name = "Card";
    OpsResult r = run(p, R"([{"op": "audio_effect.add", "layer": "Song", "effect": "eq", "values": {"low": 4, "high": -3}},
        {"op": "audio_effect.add", "layer": "Song", "effect": "ducking"},
        {"op": "audio_effect.set", "layer": "Song", "index": 1, "enabled": false, "values": {"amount": 20}},
        {"op": "query.layer", "layer": "Song"}])");
    CHECK(r.ok);
    if (r.ok) {
        CHECK(r.results[0]["values"]["low"] == 4.0 && r.results[0]["values"]["high"] == -3.0);
        const auto& list = r.results[3]["audio_effects"];
        CHECK(list.size() == 2 && list[1]["effect"] == "ducking" && list[1]["enabled"] == false && list[1]["values"]["amount"] == 20.0);
    }
    r = run(p, R"([{"op": "audio_effect.add", "layer": "Card", "effect": "eq"}])");
    CHECK(!r.ok && r.error.find("no sound") != std::string::npos);
    r = run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 0, "values": {"lows": 3}}])");
    CHECK(!r.ok && r.error.find("low") != std::string::npos);
    CHECK(!run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 0, "values": {"low": 99}}])").ok);
    CHECK(!run(p, R"([{"op": "audio_effect.add", "layer": "Song", "effect": "flanger"}])").ok);
    // Noise Reduction's sample of only the noise: kept in the file, dropped with null; only it takes one.
    r = run(p, R"([{"op": "audio_effect.add", "layer": "Song", "effect": "noise_reduction", "sample": [1.5, 2.25]}])");
    CHECK(r.ok);
    auto songOf = [](Project& in) -> Layer& {
        return *std::find_if(in.comp.layers.begin(), in.comp.layers.end(), [](const Layer& l) { return l.name == "Song"; });
    };
    if (r.ok) {
        const AudioEffect& nr = songOf(p).audioEffects.back();
        CHECK(nr.hasSample() && nr.sampleFrom == 1.5 && nr.sampleTo == 2.25 && dsp::processKey({nr}).find("1.500") != std::string::npos);
        Project withSample;
        CHECK(deserializeProject(serializeProject(p), withSample) && songOf(withSample).audioEffects.back().sampleTo == 2.25);
        CHECK(run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 2, "sample": null}])").ok && !songOf(p).audioEffects.back().hasSample());
        CHECK(!run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 2, "sample": [3, 1]}])").ok);
        CHECK(!run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 0, "sample": [1, 2]}])").ok);
        CHECK(run(p, R"([{"op": "audio_effect.remove", "layer": "Song", "index": 2}])").ok);
    }
    // Learned from a sample, a steady hum is turned down where only the hum is.
    {
        std::vector<float> hum(48000 * 3 * 2);
        for (size_t i = 0; i < hum.size() / 2; ++i) hum[i * 2] = hum[i * 2 + 1] = 0.05f * static_cast<float>(std::sin(2 * 3.14159265 * 120.0 * i / 48000.0));
        std::vector<float> cleaned = hum;
        dsp::reduceNoise(cleaned, 48000.0, 24.0f, 50.0f, 0.5, 1.5);
        double before = 0, after = 0;
        for (size_t i = 48000 * 2; i < 48000 * 4; ++i) before += hum[i] * hum[i], after += cleaned[i] * cleaned[i];
        CHECK(after < before * 0.25);
    }
    CHECK(run(p, R"([{"op": "audio_effect.remove", "layer": "Song", "index": 1}])").ok);
    Project q;
    std::string err;
    CHECK(deserializeProject(serializeProject(p), q, &err));
    const Layer* song2 = nullptr;
    for (const Layer& l : q.comp.layers)
        if (l.name == "Song") song2 = &l;
    CHECK(song2 && song2->audioEffects.size() == 1 && song2->audioEffects[0].value == 4.0f && song2->audioEffects[0].value3 == -3.0f);
}

void testLanguageFiles() {
    LanguageFile file;
    std::string err;
    CHECK(parseLanguage(R"({"format": "bemotion-language", "code": "pt-BR", "name": "Português", "strings": {"File": "Arquivo", "Empty": ""}})", file, &err));
    CHECK(file.code == "pt-BR" && file.strings.size() == 1 && file.strings["File"] == "Arquivo");
    CHECK(!parseLanguage(R"({"code": "pt"})", file, &err));
    CHECK(placeholdersOf("%2 of %1, {name} %") == std::vector<std::string>({"%1", "%2", "{name}"}));
    CHECK(matchLanguage({"es", "pt-BR"}, "pt_BR") == 1 && matchLanguage({"es", "pt-BR"}, "pt_PT") == 1 && matchLanguage({"es"}, "de_DE") == -1);
    // The bundled translations parse and keep their texts' placeholders.
    std::ifstream in(std::filesystem::path(BEMOTION_SOURCE_DIR) / "assets" / "languages" / "pt-BR.json", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    CHECK(parseLanguage(text.str(), file, &err) && file.strings.size() > 1000);
    for (const auto& [english, translation] : file.strings)
        if (placeholdersOf(english) != placeholdersOf(translation)) {
            std::printf("  placeholders differ: %s\n", english.c_str());
            CHECK(false);
        }
}

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
        {"history limits", testHistoryLimits},
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
        {"separate audio", testSeparateAudio},
        {"quick animation marks", testQuickAnimationMarks},
        {"place motion", testPlaceMotion},
        {"svg import", testSvgImport},
        {"lottie export", testLottieExport},
        {"subtitles", testSubtitles},
        {"counter", testCounter},
        {"letters and fit", testLetters},
        {"crop", testCrop},
        {"lens", testLens},
        {"expressions", testExpressions},
        {"expressions across projects", testExpressionsAcrossProjects},
        {"expressions through operations", testExpressionsThroughOperations},
        {"media folders", testMediaFolders},
        {"gradients and picture frames", testGradientsAndFrames},
        {"references complete", testReferencesComplete},
        {"plugin catalog", testPluginCatalog},
        {"updates", testUpdates},
        {"paths, masks, blending and mattes", testPathsAndMasks},
        {"clip speed, ripple, roll, slip, markers", testClipTiming},
        {"lut, curves and chroma key", testColorEffects},
        {"audio effects: eq, noise reduction, ducking", testAudioEffects},
        {"auto level and silence removal", testAutoLevelAndSilence},
        {"mask path keyframes", testMaskPathKeys},
        {"paint, puppet and warp", testPaintPuppetWarp},
        {"point tracking", testTracking},
        {"3D model files", testMeshFiles},
        {"language files", testLanguageFiles},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::printf("[%s] %s\n", g_failures == before ? " ok " : "FAIL", name);
    }
    std::printf("%s\n", g_failures == 0 ? "All tests passed." : "Some tests failed.");
    return g_failures == 0 ? 0 : 1;
}
