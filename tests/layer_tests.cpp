// Kinds of layers and effects: text, crop, lens, masks, color, paint, tracking, audio.

#include "Check.h"

#include "core/AudioDsp.h"
#include "core/Clipboard.h"
#include "core/Data.h"
#include "core/Deform.h"
#include "core/Editing.h"
#include "core/Geometry.h"
#include "core/Ops.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Tracking.h"
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

void testBackground() {
    Project p;
    CHECK(run(p, R"([{"op": "layer.add", "type": "text", "text": "Hi", "name": "T"},
                     {"op": "layer.add", "type": "background", "name": "BG", "fill": "radial"},
                     {"op": "property.set", "layer": "BG", "prop": "bg_darken", "value": 30}])")
              .ok);
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
    CHECK(back.comp.layers.back().type == LayerType::Background &&
          back.comp.layers.back().background.fill == BackgroundFill::Radial);
    CHECK(near(back.comp.layers.back().background.darken.valueAt(0), 30));
    CHECK(validate(back).empty());
}

void testSubtitles() {
    const std::string srt = "\xEF\xBB\xBF"
                            "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello there\r\n\r\n2\n00:00:03.000 --> 00:00:04.000 "
                            "align:start\nSecond\nline\n";
    const std::vector<Caption> caps = parseSrt(srt);
    CHECK(caps.size() == 2 && near(static_cast<float>(caps[0].start), 1.0f) &&
          near(static_cast<float>(caps[0].end), 2.5f));
    CHECK(caps.size() == 2 && caps[0].text == "Hello there" && caps[1].text == "Second\nline");
    CHECK(parseSrt(toSrt(caps)) == caps);
    std::vector<Caption> longOne = {{0, 10, "one two three four five six seven eight nine ten eleven twelve"}};
    splitLongCaptions(longOne, 20);
    CHECK(longOne.size() >= 3 && near(static_cast<float>(longOne.front().start), 0) &&
          near(static_cast<float>(longOne.back().end), 10));
    for (size_t i = 1; i < longOne.size(); ++i)
        CHECK(longOne[i].text.size() <= 20 &&
              near(static_cast<float>(longOne[i].start), static_cast<float>(longOne[i - 1].end)));
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
    CHECK(
        run(p,
            R"({"op": "layer.set", "layer": "Subtitles", "captions": [{"start": 5, "end": 6, "text": "Bye"}, {"start": 0, "end": 1, "text": "Hi"}]})")
            .ok);
    CHECK(p.comp.layers.front().text.captions.front().text == "Hi"); // sorted
    CHECK(
        !run(p, R"({"op": "layer.set", "layer": "Subtitles", "captions": [{"start": 2, "end": 1, "text": "x"}]})").ok);
    CHECK(
        run(p,
            R"([{"op": "layer.add", "type": "text", "name": "T"}, {"op": "layer.set", "layer": "T", "outline": true, "box": true}])")
            .ok);
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
        if (x.text.subtitle)
            CHECK(x.text.captions.size() == 2 && x.text.captions[0].text == "a" &&
                  near(static_cast<float>(x.text.captions[0].end), 3.0f));
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
    CHECK(run(p, R"({"op":"layer.set","layer":"Counter","from":10,"to":20,"count_duration":0,"count_easing":"linear"})")
              .ok);
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
    CHECK(formatCounter(-1234, 0, " ") == "-1 234" && formatCounter(999, 0, ",") == "999" &&
          formatCounter(-0.001, 1, "") == "0.0");
    CHECK(run(p, R"({"op":"layer.set","layer":"Counter","decimals":1,"prefix":"R$ ","suffix":" mil","separator":"."})")
              .ok);
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
    CHECK(
        run(p, R"({"op":"layer.add","type":"text","name":"T","text":"Hello world","letter_in":"rise","letter_by":"word",
                     "letter_order":"center","letter_delay":0.1,"letter_duration":0.4,"letter_loop":"wave","fit_lines":2})")
            .ok);
    const TextProps& t = p.comp.layers.front().text;
    CHECK(t.letterIn == LetterAnim::Rise && t.letterUnit == LetterUnit::Word && t.letterOrder == LetterOrder::Center);
    CHECK(t.letterLoop == LetterLoop::Wave && near(t.letterDelay, 0.1f) && t.fitLines == 2 && t.lettersAnimated());
    CHECK(!run(p, R"({"op":"layer.set","layer":"T","letter_in":"explode"})").ok);
    CHECK(!run(p, R"({"op":"layer.set","layer":"T","letter_delay":-1})").ok);
    CHECK(run(p, R"({"op":"layer.set","layer":"T","letter_out":"fade","letter_in":"none"})").ok);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && validate(back).empty());
    const TextProps& b = back.comp.layers.front().text;
    CHECK(b.letterIn == LetterAnim::None && b.letterOut == LetterAnim::Fade && b.letterUnit == LetterUnit::Word &&
          b.fitLines == 2);
    CHECK(run(p, R"({"op":"layer.add","type":"rectangle","name":"R"})").ok);
    CHECK(!run(p, R"({"op":"layer.set","layer":"R","letter_in":"fade"})").ok);

    // Subtitles fit by themselves: two lines at most, and time to read each caption.
    CHECK(run(p, R"({"op":"layer.add","type":"subtitle","captions":[{"start":0,"end":0.2,"text":"Hi"},
                     {"start":0.5,"end":3,"text":"A much longer caption that keeps on going well past what two lines of subtitles can hold, really"}]})")
              .ok);
    for (Layer& l : p.comp.layers)
        if (l.text.subtitle) CHECK(l.text.fitLines == 2);
    CHECK(run(p, R"({"op":"layer.set","layer":"Subtitles","fit_captions":true})").ok);
    for (const Layer& l : p.comp.layers)
        if (l.text.subtitle) {
            const auto& c = l.text.captions;
            CHECK(c.size() == 3 && near(static_cast<float>(c[0].end), 0.5f)); // lengthened up to the next one
            for (size_t i = 0; i + 1 < c.size(); ++i)
                CHECK(c[i].end <= c[i + 1].start + 1e-9 && c[i].text.size() <= 84);
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
                     {"op": "property.set", "layer": "Camera", "prop": "rotation_y", "value": 10}])")
              .ok);
    const Composition& c = p.comp;
    const Lens lens = c.lensAt(0);
    CHECK(near(lens.z, 200.0f - c.focalLength()) && near(lens.ry, 10.0f));
    // The camera's view of a 3D layer is the lens's.
    for (const Layer& l : c.layers)
        if (l.name == "Deep") {
            Projective a, b;
            CHECK(c.viewFor(l, 0, a) && c.viewThrough(lens, l, 0, b));
            CHECK(near(a.apply({1000, 500}).x, b.apply({1000, 500}).x) &&
                  near(a.apply({1000, 500}).y, b.apply({1000, 500}).y));
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
    for (Layer& l : p.comp.layers)
        (l.name == "Wave" ? wave : l.name == "Card" ? card : spot) = &l;
    CHECK(wave && wave->shape.kind == ShapeKind::Path && wave->shape.path.vertices.size() == 3 &&
          !wave->shape.path.closed);
    CHECK(wave && !wave->shape.fillEnabled && wave->shape.strokeEnabled); // an open path is a line
    CHECK(card && card->matte == MatteMode::Alpha && card->matteLayer == spot->id &&
          card->blend == BlendMode::Multiply);
    CHECK(p.comp.isMatte(spot->id) && !p.comp.isMatte(card->id));
    CHECK(card->masks.size() == 1 && card->masks[0].mode == MaskMode::Subtract &&
          near(card->masks[0].feather.base, 12.0f));
    CHECK(card->masks[0].path.vertices.size() == 4);
    CHECK(spot && spot->shape.kind == ShapeKind::Path &&
          spot->shape.path.vertices.size() == 4); // the ellipse, as a path
    const BezierPath wavePath = wave->shape.path;
    const uint64_t spotId = spot->id; // (the layers move when the next batch replaces the project)
    // Mask values are properties; the repeater's and the gradient's too.
    CHECK(applyOps(p, nlohmann::json::parse(
                          R"([{"op": "property.set", "layer": "Card", "prop": "mask0.opacity", "value": 50},
                                     {"op": "property.set", "layer": "Card", "prop": "repeat_copies", "value": 5},
                                     {"op": "property.set", "layer": "Card", "prop": "fill_color2", "value": "#336699"},
                                     {"op": "property.set", "layer": "Card", "prop": "stroke_dash", "value": 8}])"))
              .ok);
    card = nullptr;
    for (Layer& l : p.comp.layers)
        if (l.name == "Card") card = &l;
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
    CHECK(c2 && c2->matte == MatteMode::Alpha && c2->blend == BlendMode::Multiply &&
          c2->shape.fill == ShapeFill::Linear);
    CHECK(c2 && c2->masks.size() == 1 && c2->masks[0].path == card->masks[0].path &&
          near(c2->masks[0].opacity.base, 50.0f));
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
    for (const Layer& l : bad.comp.layers)
        matted += l.matte != MatteMode::None;
    CHECK(matted == 1);
    CHECK(!applyOps(back, nlohmann::json::parse(
                              R"({"op": "layer.set", "layer": "Card", "matte": "alpha", "matte_layer": "Card"})"))
               .ok);
    back.comp.removeLayer(spotId);
    for (const Layer& l : back.comp.layers)
        if (l.name == "Card") CHECK(l.matte == MatteMode::None && l.matteLayer == 0);

    // Copied together, a layer keeps its matte (the copy's); grouped apart, it loses it.
    Project two;
    CHECK(applyOps(two, nlohmann::json::parse(R"([{"op": "layer.add", "type": "ellipse", "name": "M"},
        {"op": "layer.add", "type": "rectangle", "name": "R", "below": "M"}, {"op": "layer.set", "layer": "R", "matte": "luma"}])"))
              .ok);
    std::vector<uint64_t> both;
    for (const Layer& l : two.comp.layers)
        both.push_back(l.id);
    const std::vector<uint64_t> pasted = pasteLayers(two.comp, copyLayers(two.comp, both), 0.0, 0, 0);
    int pastedMattes = 0;
    for (uint64_t id : pasted) {
        const Layer* l = two.comp.findLayer(id);
        if (l && l->matte == MatteMode::Luma)
            pastedMattes += std::find(pasted.begin(), pasted.end(), l->matteLayer) != pasted.end();
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

// Mask Path keyframes: the outline moves between keys point by point, saved, moved with the clip.
void testMaskPathKeys() {
    Project p;
    p.comp.addLayer(makeShapeLayer(ShapeKind::Rectangle, {0, 0}, 5.0));
    const uint64_t id = p.comp.layers[0].id;
    const std::string ops =
        R"j([{"op": "mask.add", "layer": )j" + std::to_string(id) + R"j(, "box": [0, 0, 100, 100]},
        {"op": "mask.set", "layer": )j" +
        std::to_string(id) + R"j(, "index": 0, "time": 0, "path": [[0, 0], [100, 0], [100, 100], [0, 100]]},
        {"op": "mask.set", "layer": )j" +
        std::to_string(id) + R"j(, "index": 0, "time": 2, "path": [[100, 0], [200, 0], [200, 100], [100, 100]]},
        {"op": "mask.duplicate", "layer": )j" +
        std::to_string(id) + R"j(, "index": 0}])j";
    OpsResult r = run(p, ops.c_str());
    CHECK(r.ok);
    if (!r.ok) return;
    const Mask& m = p.comp.layers[0].masks[0];
    CHECK(m.pathKeys.size() == 2 && p.comp.layers[0].masks.size() == 2 &&
          p.comp.layers[0].masks[1].pathKeys.size() == 2);
    CHECK(near(m.pathAt(1.0).vertices[0].point.x, 50.0f) && near(m.pathAt(3.0).vertices[0].point.x, 100.0f));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back) && back.comp.layers[0].masks[0].pathKeys == m.pathKeys);
    p.comp.layers[0].shiftTime(1.0);
    CHECK(near(static_cast<float>(p.comp.layers[0].masks[0].pathKeys[1].time), 3.0f));
    CHECK(run(p, (R"j({"op": "mask.set", "layer": )j" + std::to_string(id) +
                  R"j(, "index": 0, "animated": false, "time": 2})j")
                     .c_str())
              .ok);
    CHECK(p.comp.layers[0].masks[0].pathKeys.empty() &&
          near(p.comp.layers[0].masks[0].path.vertices[0].point.x, 50.0f));
}

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
    CHECK(near(curveValue({{0.2f, 0.3f}, {0.8f, 0.6f}}, 0.0f), 0.3f) &&
          near(curveValue({{0.2f, 0.3f}, {0.8f, 0.6f}}, 1.0f), 0.6f));
    CHECK(curveIsIdentity({}) && curveIsIdentity({{0, 0}, {1, 1}}) && !curveIsIdentity(s));
    std::vector<Vec2> messy = {{0.9f, 2.0f}, {0.1f, -1.0f}, {0.1f, 0.5f}};
    cleanCurve(messy);
    CHECK(messy.size() == 2 && messy[0].x < messy[1].x && messy[1].y == 1.0f && messy[0].y == 0.0f);

    // .cube files: a 3D table, a 1D one (made 3D), and the mistakes people make.
    Lut3D lut;
    std::string err;
    CHECK(parseCubeLut(
        "TITLE \"Swap\"\n# a comment\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n", lut,
        &err));
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
    CHECK(back.effects.size() == 3 && back.effects[0].file == "bundled:Warm.cube" &&
          back.effects[1].curves[2].size() == 2);
    CHECK(near(back.effects[2].value1.base, 30.0f)); // the key's tolerance default
    EffectPreset preset;
    CHECK(parseEffectPreset(effectPresetToJson({"Look", back.effects}), preset) &&
          preset.effects[1].curves[2] == back.effects[1].curves[2] && preset.effects[0].file == "bundled:Warm.cube");

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
    OpsResult r =
        run(p, (R"j([{"op": "paint.stroke", "layer": )j" + id + R"j(, "points": [[0, 0], [50, 50]], "width": 8},
        {"op": "paint.stroke", "layer": )j" +
                id + R"j(, "points": [[10, 10]], "erase": true},
        {"op": "puppet.pin.add", "layer": )j" +
                id + R"j(, "point": [20, 30]},
        {"op": "property.set", "layer": )j" +
                id + R"j(, "prop": "pin0", "value": [25, 30]},
        {"op": "layer.set", "layer": )j" +
                id + R"j(, "warp": true},
        {"op": "property.set", "layer": )j" +
                id + R"j(, "prop": "warp3", "value": [4, 0]},
        {"op": "property.set", "layer": )j" +
                id + R"j(, "prop": "paint_reveal", "value": 50}])j")
                   .c_str());
    CHECK(r.ok);
    const Layer& l = p.comp.layers[0];
    CHECK(l.paint.size() == 2 && l.paint[1].erase && l.pins.size() == 1 && l.warp && deforms(l, 0.0));
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer& b = back.comp.layers[0];
    CHECK(b.paint == l.paint && b.pins.size() == 1 && near(b.pins[0].rest.x, 20.0f) &&
          near(b.pins[0].position.base.x, 25.0f));
    CHECK(b.warp && near(b.warpPoints[3].base.x, 4.0f) && near(b.paintReveal.base, 50.0f));
    CHECK(run(p, (R"j([{"op": "puppet.pin.remove", "layer": )j" + id +
                  R"j(, "index": 0}, {"op": "paint.clear", "layer": )j" + id + R"j(}])j")
                     .c_str())
              .ok);
    CHECK(p.comp.layers[0].pins.empty() && p.comp.layers[0].paint.empty());
    CHECK(!run(p, (R"j({"op": "paint.stroke", "layer": )j" + id + R"j(, "points": [], "width": 8})j").c_str()).ok);

    // The Puppet mesh: a pin's point follows it exactly; two pins turned together turn what is
    // between them; a piece of the picture apart from the pinned one stays put.
    CHECK(run(p, (R"j([{"op": "layer.set", "layer": )j" + id + R"j(, "warp": false},
        {"op": "puppet.pin.add", "layer": )j" +
                  id + R"j(, "point": [-40, 0]},
        {"op": "puppet.pin.add", "layer": )j" +
                  id + R"j(, "point": [40, 0]},
        {"op": "puppet.pin.add", "layer": )j" +
                  id + R"j(, "point": [0, 0], "starch": true},
        {"op": "property.set", "layer": )j" +
                  id + R"j(, "prop": "pin0", "value": [0, -40]},
        {"op": "property.set", "layer": )j" +
                  id + R"j(, "prop": "pin1", "value": [0, 40]}])j")
                     .c_str())
              .ok);
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
            if (x < apart.cols / 2 - 2 || x > apart.cols / 2 + 2)
                apart.inside[static_cast<size_t>(y * apart.cols + x)] = 1;
    setPuppetShape(bent.id, apart);
    bent.pins = {bent.pins[0]};
    bent.pins[0].rest = {-40, 0};
    bent.pins[0].position = AnimVec2{Vec2{-40, 30}};
    const Vec2 left = deformPoint(bent, 0.0, {-30, -10}, lo, hi), right = deformPoint(bent, 0.0, {40, 10}, lo, hi);
    CHECK(near(left.y, 20.0f) && near(right.x, 40.0f) && near(right.y, 10.0f));
    CHECK(puppetOnMesh(bent, {-40, 0}, lo, hi) && !puppetOnMesh(bent, {0, 0}, lo, hi));
    Project starchy;
    CHECK(deserializeProject(serializeProject(p), starchy) && starchy.comp.layers[0].pins.size() == 1);
    CHECK(run(p, (R"j([{"op": "puppet.pin.add", "layer": )j" + id + R"j(, "point": [1, 1], "starch": true}])j").c_str())
              .ok);
    CHECK(deserializeProject(serializeProject(p), starchy) && starchy.comp.layers[0].pins.back().starch);
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
                g.pixels[static_cast<size_t>(y * 96 + x)] = static_cast<uint8_t>(
                    40 + 200 * std::exp(-(dx * dx + dy * dy) / 40.0f) + 10 * ((x / 7 + y / 5) % 2));
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

// Auto Level brings clips to one loudness; silence is found and cut out of a clip.
void testAutoLevelAndSilence() {
    auto tone = [](float amp, double seconds) {
        std::vector<float> s(static_cast<size_t>(seconds * 48000) * 2);
        for (size_t i = 0; i < s.size() / 2; ++i)
            s[i * 2] = s[i * 2 + 1] = amp * static_cast<float>(std::sin(2 * 3.14159265 * 220 * i / 48000.0));
        return s;
    };
    auto levelDb = [](const std::vector<float>& s) {
        double sum = 0;
        for (float v : s)
            sum += v * v;
        return 20 * std::log10(std::sqrt(sum / static_cast<double>(s.size())));
    };
    std::vector<float> quiet = tone(0.02f, 2.0), loud = tone(0.5f, 2.0);
    dsp::autoLevel(quiet, 48000, -18.0f, 0.0f);
    dsp::autoLevel(loud, 48000, -18.0f, 0.0f);
    CHECK(std::fabs(levelDb(quiet) - -18.0) < 0.5 && std::fabs(levelDb(loud) - -18.0) < 0.5);
    for (AudioEffectType t :
         {AudioEffectType::Reverb, AudioEffectType::Echo, AudioEffectType::Compressor, AudioEffectType::Filter}) {
        std::vector<float> s = tone(0.3f, 0.5);
        dsp::process(s, 48000, {makeAudioEffect(t)});
        CHECK(std::all_of(s.begin(), s.end(), [](float v) { return std::isfinite(v) && std::fabs(v) < 4.0f; }));
        CHECK(!dsp::processKey({makeAudioEffect(t)}).empty() || t == AudioEffectType::Filter);
    }

    // 1 s of sound, 1 s of silence, 1 s of sound: the silence is found (padded) ...
    std::vector<float> loudness(300, 0.2f);
    for (int i = 100; i < 200; ++i)
        loudness[i] = 0.0005f;
    const auto stretches = dsp::quietStretches(loudness, 0.01, -40.0f, 0.5, 0.1);
    CHECK(stretches.size() == 1 && near(static_cast<float>(stretches[0].first), 1.1f) &&
          near(static_cast<float>(stretches[0].second), 1.9f));
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
    for (const Layer& l : p.comp.layers)
        end = std::max(end, l.outPoint), pieces += l.type == LayerType::Media;
    CHECK(pieces == 2 && near(static_cast<float>(end), 2.2f));
    CHECK(validate(p).empty());
    // The operation needs the sound (an environment); audio.level sets one target on all clips.
    CHECK(!run(p, R"({"op": "audio.remove_silence", "layer": 1})").ok);
    CHECK(run(p, R"({"op": "audio.level", "target": -20})").ok);
    for (const Layer& l : p.comp.layers)
        CHECK(l.audioEffects.size() == 1 && l.audioEffects[0].type == AudioEffectType::AutoLevel &&
              l.audioEffects[0].value == -20.0f);
}

void testAudioEffects() {
    // Defaults and ranges.
    CHECK(audioEffectFromId("noise_reduction") == AudioEffectType::NoiseReduction &&
          audioEffectFromId("nope") == AudioEffectType::Count);
    AudioEffect duck = makeAudioEffect(AudioEffectType::Ducking);
    CHECK(duck.value == 15.0f && duck.value2 == -40.0f && near(duck.value3, 0.4f));
    duck.value = 90.0f;
    duck.value3 = -1.0f;
    clampAudioEffect(duck);
    CHECK(duck.value == 40.0f && near(duck.value3, 0.05f));

    // EQ: +6 dB under the low shelf is about 6 dB louder; a tone in the middle is left alone.
    auto sine = [](double freq, double seconds, float amp) {
        std::vector<float> s(static_cast<size_t>(seconds * 48000) * 2);
        for (size_t i = 0; i < s.size() / 2; ++i)
            s[i * 2] = s[i * 2 + 1] = amp * static_cast<float>(std::sin(2 * 3.14159265 * freq * i / 48000.0));
        return s;
    };
    auto rms = [](const std::vector<float>& s, size_t from, size_t to) {
        double sum = 0;
        for (size_t i = from * 2; i < to * 2; ++i)
            sum += s[i] * s[i];
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
    OpsResult r =
        run(p, R"([{"op": "audio_effect.add", "layer": "Song", "effect": "eq", "values": {"low": 4, "high": -3}},
        {"op": "audio_effect.add", "layer": "Song", "effect": "ducking"},
        {"op": "audio_effect.set", "layer": "Song", "index": 1, "enabled": false, "values": {"amount": 20}},
        {"op": "query.layer", "layer": "Song"}])");
    CHECK(r.ok);
    if (r.ok) {
        CHECK(r.results[0]["values"]["low"] == 4.0 && r.results[0]["values"]["high"] == -3.0);
        const auto& list = r.results[3]["audio_effects"];
        CHECK(list.size() == 2 && list[1]["effect"] == "ducking" && list[1]["enabled"] == false &&
              list[1]["values"]["amount"] == 20.0);
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
        return *std::find_if(in.comp.layers.begin(), in.comp.layers.end(),
                             [](const Layer& l) { return l.name == "Song"; });
    };
    if (r.ok) {
        const AudioEffect& nr = songOf(p).audioEffects.back();
        CHECK(nr.hasSample() && nr.sampleFrom == 1.5 && nr.sampleTo == 2.25 &&
              dsp::processKey({nr}).find("1.500") != std::string::npos);
        Project withSample;
        CHECK(deserializeProject(serializeProject(p), withSample) &&
              songOf(withSample).audioEffects.back().sampleTo == 2.25);
        CHECK(run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 2, "sample": null}])").ok &&
              !songOf(p).audioEffects.back().hasSample());
        CHECK(!run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 2, "sample": [3, 1]}])").ok);
        CHECK(!run(p, R"([{"op": "audio_effect.set", "layer": "Song", "index": 0, "sample": [1, 2]}])").ok);
        CHECK(run(p, R"([{"op": "audio_effect.remove", "layer": "Song", "index": 2}])").ok);
    }
    // Learned from a sample, a steady hum is turned down where only the hum is.
    {
        std::vector<float> hum(48000 * 3 * 2);
        for (size_t i = 0; i < hum.size() / 2; ++i)
            hum[i * 2] = hum[i * 2 + 1] = 0.05f * static_cast<float>(std::sin(2 * 3.14159265 * 120.0 * i / 48000.0));
        std::vector<float> cleaned = hum;
        dsp::reduceNoise(cleaned, 48000.0, 24.0f, 50.0f, 0.5, 1.5);
        double before = 0, after = 0;
        for (size_t i = 48000 * 2; i < 48000 * 4; ++i)
            before += hum[i] * hum[i], after += cleaned[i] * cleaned[i];
        CHECK(after < before * 0.25);
    }
    CHECK(run(p, R"([{"op": "audio_effect.remove", "layer": "Song", "index": 1}])").ok);
    Project q;
    std::string err;
    CHECK(deserializeProject(serializeProject(p), q, &err));
    const Layer* song2 = nullptr;
    for (const Layer& l : q.comp.layers)
        if (l.name == "Song") song2 = &l;
    CHECK(song2 && song2->audioEffects.size() == 1 && song2->audioEffects[0].value == 4.0f &&
          song2->audioEffects[0].value3 == -3.0f);
}

} // namespace

Tests layerTests() {
    return {
        {"background", testBackground},
        {"subtitles", testSubtitles},
        {"counter", testCounter},
        {"letters and fit", testLetters},
        {"crop", testCrop},
        {"lens", testLens},
        {"paths, masks, blending and mattes", testPathsAndMasks},
        {"mask path keyframes", testMaskPathKeys},
        {"lut, curves and chroma key", testColorEffects},
        {"paint, puppet and warp", testPaintPuppetWarp},
        {"point tracking", testTracking},
        {"auto level and silence removal", testAutoLevelAndSilence},
        {"audio effects: eq, noise reduction, ducking", testAudioEffects},
    };
}
