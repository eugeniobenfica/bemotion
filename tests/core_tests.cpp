// Minimal, dependency-free unit tests for the document model.

#include "core/Animated.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/Project.h"
#include "core/Serialization.h"

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
    text.text.weight = FontWeight::Bold;

    Project q;
    std::string err;
    CHECK(deserializeProject(serializeProject(p), q, &err));
    CHECK(q.name == "Round Trip");
    CHECK(q.comp.width == 1080 && q.comp.height == 1920);
    CHECK(q.comp.layers.size() == 2);
    CHECK(q.comp.layers[0].text.text == "Olá, bemotion");
    CHECK(q.comp.layers[0].text.weight == FontWeight::Bold);
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

void testMoveLayers() {
    Composition c = makeStack();
    CHECK(order(c) == "ABCDE");
    c.moveLayers({idOf(c, "B"), idOf(c, "D")}, 0);
    CHECK(order(c) == "BDACE");
    c.moveLayers({idOf(c, "B")}, 99);
    CHECK(order(c) == "DACEB");
}

void testShiftLayers() {
    Composition c = makeStack();
    c.shiftLayers({idOf(c, "C"), idOf(c, "D")}, -1);
    CHECK(order(c) == "ACDBE");
    c.shiftLayers({idOf(c, "A")}, -1); // already on top
    CHECK(order(c) == "ACDBE");
    c.shiftLayers({idOf(c, "A"), idOf(c, "C")}, 99); // to the bottom
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
    CHECK(c.indexOf(tail) == 0); // placed above the original
    CHECK(c.findLayer(original)->outPoint == 1.5);
    CHECK(c.findLayer(tail)->inPoint == 1.5 && c.findLayer(tail)->outPoint == 4.0);
    // Animation continues seamlessly across the cut.
    CHECK(near(c.findLayer(tail)->transform.opacity.valueAt(2.0), 50.0f));
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
        {"move layers", testMoveLayers},
        {"shift layers", testShiftLayers},
        {"split layer", testSplitLayer},
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
