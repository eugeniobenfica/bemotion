// Expressions: the language, and reaching it through operations.

#include "Check.h"

#include "core/Describe.h"
#include "core/Expressions.h"
#include "core/Ops.h"
#include "core/Project.h"
#include "core/Serialization.h"

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

void testExpressions() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "ellipse", "name": "Ball", "position": [100, 200]},
                     {"op": "layer.add", "type": "rectangle", "name": "Box", "start": 1, "duration": 4},
                     {"op": "expression.set", "layer": "Box", "prop": "rotation", "expression": "time * 90"},
                     {"op": "expression.set", "layer": "Box", "prop": "position", "expression": "layer(\"Ball\").position + [0, -50]"},
                     {"op": "expression.set", "layer": "Box", "prop": "opacity", "expression": "progress < 0.5 ? 20 : 100"}])")
              .ok);
    CHECK(!run(p, R"({"op": "expression.set", "layer": "Box", "prop": "scale", "expression": "time * (2"})")
               .ok); // does not read
    Composition& c = p.comp;
    const ExpressionScope scope(c);
    Layer* box = nullptr;
    for (Layer& l : c.layers)
        if (l.name == "Box") box = &l;
    CHECK(box && near(box->transform.rotation.valueAt(2.0), 180.0f));
    CHECK(box && near(box->transform.position.valueAt(2.0).x, 100.0f) &&
          near(box->transform.position.valueAt(2.0).y, 150.0f));
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
    r = run(
        p,
        R"j([{"op": "query.expression", "layer": "Box", "prop": "rotation", "expression": "time * 90", "times": [0, 1, 2]},
                   {"op": "query.expression", "layer": "Box", "prop": "rotation", "expression": "wigle(2, 3)"},
                   {"op": "query.expression", "layer": "Box", "prop": "opacity"},
                   {"op": "query.expression", "layer": "Box", "prop": "position", "expression": "layer(\"Nobody\").position"}])j");
    CHECK(r.ok && !r.changed);
    if (r.ok) {
        CHECK(r.results[0]["values"] == nlohmann::json::array({0.0, 90.0, 180.0}));
        CHECK(r.results[1]["ok"] == false &&
              r.results[1]["error"].get<std::string>().find("did you mean \"wiggle\"") != std::string::npos);
        CHECK(r.results[2]["value"] == 50.0);
        CHECK(r.results[3]["ok"] == false);
    }
    CHECK(!run(p, R"j({"op": "expression.set", "layer": "Box", "prop": "scale", "expression": "wigle(2, 3)"})j")
               .ok); // checked when set
    // velocity, speed and valueAt read the property's own keyframes.
    CHECK(
        run(p,
            R"j([{"op": "key.add", "layer": "Box", "prop": "position", "time": 0, "value": [0, 0], "easing": "linear"},
                     {"op": "key.add", "layer": "Box", "prop": "position", "time": 2, "value": [200, 0]}])j")
            .ok);
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

// A project closed and another opened: expressions read the new one (the closed one is gone,
// reading it crashed when a project was opened again).
void testExpressionsAcrossProjects() {
    auto make = [](float x) {
        auto p = std::make_unique<Project>();
        CHECK(run(*p, R"([{"op": "layer.add", "type": "ellipse", "name": "Ball"},
                          {"op": "layer.add", "type": "rectangle", "name": "Box"},
                          {"op": "expression.set", "layer": "Box", "prop": "position", "expression": "layer(\"Ball\").position"}])")
                  .ok);
        for (Layer& l : p->comp.layers)
            if (l.name == "Ball") l.transform.position.base = Vec2{x, 0};
        return p;
    };
    auto first = make(10.0f);
    setExpressionComposition(&first->comp);
    { const ExpressionScope s(first->comp); }
    setExpressionComposition(nullptr); // closed, as the editor's window does when it goes
    first.reset();
    auto second = make(20.0f);
    setExpressionComposition(&second->comp);
    { const ExpressionScope s(second->comp); } // ends back on the editor's composition: the second
    const Layer* box = nullptr;
    for (const Layer& l : second->comp.layers)
        if (l.name == "Box") box = &l;
    CHECK(box && near(box->transform.position.valueAt(0).x, 20.0f));
    setExpressionComposition(nullptr);
}

} // namespace

Tests expressionTests() {
    return {
        {"expressions", testExpressions},
        {"expressions through operations", testExpressionsThroughOperations},
        {"expressions across projects", testExpressionsAcrossProjects},
    };
}
