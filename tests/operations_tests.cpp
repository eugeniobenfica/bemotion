// Operations and the rules: batches, errors, validation and repair.

#include "Check.h"

#include "core/Data.h"
#include "core/Describe.h"
#include "core/Ops.h"
#include "core/Project.h"
#include "core/Serialization.h"
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
    CHECK(run(p, R"({"op": "layer.set", "layer": "Dott", "visible": false})").error.find("\"Dot\"") !=
          std::string::npos);
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
    L[0].id = L[1].id;                                          // duplicate id
    L[1].transform.opacity.base = std::nanf("");                // not a number
    L[1].track = L[2].track;                                    // overlaps T on its track
    L[2].parent = 12345;                                        // missing compound
    L[2].inPoint = 3.0, L[2].outPoint = 1.0;                    // out before in
    L[0].transform.rotation.keys = {{2.0, 10.0f}, {1.0, 0.0f}}; // keys out of order
    L[0].type = LayerType::Media, L[0].media.mediaId = 77;      // media that is not there
    CHECK(validate(p).size() >= 6);
    const auto fixed = repair(p);
    CHECK(!fixed.empty());
    CHECK(validate(p).empty());
    CHECK(p.comp.layers.size() == 3); // nothing lost
    CHECK(std::isfinite(p.comp.layers[1].transform.opacity.base) ||
          std::isfinite(p.comp.layers[0].transform.opacity.base));
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
    const std::string ops = R"j([{"op": "layer.set", "layer": )j" + std::to_string(textId) +
                            R"j(, "gradient": "linear"},
        {"op": "property.set", "layer": )j" +
                            std::to_string(textId) + R"j(, "prop": "text_color2", "value": [0, 0, 1, 1]},
        {"op": "layer.set", "layer": )j" +
                            std::to_string(mediaId) + R"j(, "flip_h": true},
        {"op": "property.set", "layer": )j" +
                            std::to_string(mediaId) + R"j(, "prop": "corner_radius", "value": 24},
        {"op": "property.set", "layer": )j" +
                            std::to_string(mediaId) + R"j(, "prop": "border_width", "value": 3}])j";
    OpsResult r = run(p, ops.c_str());
    CHECK(r.ok);
    Project back;
    CHECK(deserializeProject(serializeProject(p), back));
    const Layer* t = back.comp.findLayer(textId);
    const Layer* m = back.comp.findLayer(mediaId);
    CHECK(t && t->text.fill == ShapeFill::Linear && t->text.color2.base.b == 1.0f);
    CHECK(m && m->media.flipH && !m->media.flipV && m->media.cornerRadius.base == 24.0f &&
          m->media.borderWidth.base == 3.0f);
    CHECK(!run(p, (R"j({"op": "layer.set", "layer": )j" + std::to_string(textId) + R"j(, "flip_h": true})j").c_str())
               .ok); // text has no picture
}

// Layers: removing, duplicating and listing them.
void testOpsLayers() {
    Project p;
    OpsResult r = run(p, R"([
        {"op": "layer.add", "type": "rectangle", "name": "Box", "duration": 4},
        {"op": "layer.add", "type": "ellipse", "name": "Dot"},
        {"op": "layer.duplicate", "layer": "Box"},
        {"op": "query.layers"}
    ])");
    CHECK(r.ok);
    CHECK(p.comp.layers.size() == 3);
    const Layer* dup = p.comp.findLayer(r.results[2]["layer"].get<uint64_t>());
    CHECK(dup && dup->type == LayerType::Shape && near(static_cast<float>(dup->outPoint - dup->inPoint), 4.0f));
    const nlohmann::json& list = r.results[3];
    CHECK(list.is_array() && list.size() == 3);
    CHECK(list[0]["layer"] == p.comp.layers[0].id); // top first
    bool sawDot = false;
    for (const nlohmann::json& l : list) sawDot |= l["name"] == "Dot" && l["type"] == "ellipse";
    CHECK(sawDot);
    CHECK(run(p, R"({"op": "layer.remove", "layer": "Dot"})").ok);
    CHECK(p.comp.layers.size() == 2);
    CHECK(!run(p, R"({"op": "layer.remove", "layer": "Dot"})").ok); // gone
    CHECK(validate(p).empty());
}

// Keyframes: removing one, clearing them all, changing their easing.
void testOpsKeys() {
    Project p;
    CHECK(run(p, R"([
        {"op": "layer.add", "type": "text", "text": "Hi", "name": "T"},
        {"op": "key.add", "layer": "T", "prop": "opacity", "time": 0, "value": 0},
        {"op": "key.add", "layer": "T", "prop": "opacity", "time": 1, "value": 50},
        {"op": "key.add", "layer": "T", "prop": "opacity", "time": 2, "value": 100}
    ])").ok);
    auto opacity = [&]() -> const AnimFloat& { return p.comp.layers[0].transform.opacity; };
    CHECK(opacity().keyCount() == 3);
    CHECK(run(p, R"({"op": "key.easing", "layer": "T", "prop": "opacity", "time": 1, "easing": "hold"})").ok);
    CHECK(opacity().keys[1].easing == Easing::Hold && opacity().keys[0].easing != Easing::Hold);
    CHECK(run(p, R"({"op": "key.easing", "layer": "T", "prop": "opacity", "all": true, "easing": "linear"})").ok);
    CHECK(std::all_of(opacity().keys.begin(), opacity().keys.end(), [](const auto& k) { return k.easing == Easing::Linear; }));
    CHECK(!run(p, R"({"op": "key.easing", "layer": "T", "prop": "opacity", "all": true, "easing": "wobbly"})").ok);
    CHECK(run(p, R"({"op": "key.remove", "layer": "T", "prop": "opacity", "time": 1})").ok);
    CHECK(opacity().keyCount() == 2 && near(static_cast<float>(opacity().keys[1].time), 2.0f));
    CHECK(!run(p, R"({"op": "key.remove", "layer": "T", "prop": "opacity", "time": 1})").ok); // no key there now
    CHECK(run(p, R"({"op": "key.clear", "layer": "T", "prop": "opacity", "value": 70})").ok);
    CHECK(opacity().keyCount() == 0 && near(opacity().base, 70.0f));
    CHECK(validate(p).empty());
}

// Masks and effects come off by index.
void testOpsRemoveByIndex() {
    Project p;
    CHECK(run(p, R"([
        {"op": "layer.add", "type": "rectangle", "name": "R"},
        {"op": "mask.add", "layer": "R", "shape": "ellipse", "name": "Round"},
        {"op": "mask.add", "layer": "R", "name": "Square"},
        {"op": "effect.add", "layer": "R", "effect": "glow"},
        {"op": "effect.add", "layer": "R", "effect": "drop_shadow"}
    ])").ok);
    auto r = [&]() -> const Layer& { return p.comp.layers[0]; };
    CHECK(r().masks.size() == 2 && r().effects.size() == 2);
    CHECK(run(p, R"({"op": "mask.remove", "layer": "R", "index": 0})").ok);
    CHECK(r().masks.size() == 1 && r().masks[0].name == "Square");
    CHECK(!run(p, R"({"op": "mask.remove", "layer": "R", "index": 5})").ok);
    const EffectType second = r().effects[1].type;
    CHECK(run(p, R"({"op": "effect.remove", "layer": "R", "index": 0})").ok);
    CHECK(r().effects.size() == 1 && r().effects[0].type == second);
    CHECK(!run(p, R"({"op": "effect.remove", "layer": "R", "index": 1})").ok);
    CHECK(validate(p).empty());
}

// Footage timing through operations: slip, close a gap, time remap, freeze.
void testOpsClipTiming() {
    Project p;
    Composition& c = p.comp;
    c.duration = 30.0;
    MediaItem video;
    video.name = "take.mp4", video.path = "C:/footage/take.mp4", video.kind = MediaKind::Video;
    video.width = 640, video.height = 360, video.duration = 10.0, video.fps = 30.0;
    video.hasVideo = video.hasAudio = true;
    Layer clip = makeMediaLayer(*c.findMedia(c.addMedia(video)), {0, 0}, 0.0, 5.0);
    clip.name = "Take";
    const uint64_t take = c.addLayer(clip).id;
    Layer after = makeShapeLayer(ShapeKind::Rectangle, {}, 2.0);
    after.name = "After";
    after.track = c.findLayer(take)->track;
    after.shiftTime(13.0); // a gap after the clip
    const uint64_t later = c.addLayer(after).id;
    auto in = [&](uint64_t id) { return static_cast<float>(c.findLayer(id)->inPoint); };
    auto out = [&](uint64_t id) { return static_cast<float>(c.findLayer(id)->outPoint); };
    c.setInPoint(take, 2.0); // shows source 2..10

    OpsResult r = run(p, R"({"op": "layer.slip", "layer": "Take", "delta": -1})");
    CHECK(r.ok && r.results[0].contains("delta"));
    CHECK(near(in(take), 2.0f)); // the clip stays put, its content moves
    CHECK(run(p, R"({"op": "track.close_gap", "layer": "Take", "time": 11.5})").ok);
    CHECK(near(in(later), out(take)));
    CHECK(!run(p, R"({"op": "track.close_gap", "layer": "Take", "time": 11.5})").ok); // no gap left

    CHECK(run(p, R"({"op": "layer.time_remap", "layer": "Take", "enabled": true})").ok);
    CHECK(c.findLayer(take)->media.timeRemap && c.findLayer(take)->media.remap.keyCount() == 2);
    r = run(p, R"({"op": "layer.freeze", "layer": "Take", "time": 4})");
    CHECK(r.ok && r.results[0].contains("source_time"));
    const Layer& frozen = *c.findLayer(take);
    CHECK(near(static_cast<float>(frozen.media.sourceTime(3.0)), static_cast<float>(frozen.media.sourceTime(6.0))));
    CHECK(run(p, R"({"op": "layer.time_remap", "layer": "Take", "enabled": false})").ok);
    CHECK(!c.findLayer(take)->media.timeRemap);
    CHECK(!run(p, R"({"op": "layer.freeze", "layer": "After", "time": 14})").ok); // not footage
    CHECK(validate(p).empty());
}

// Data for plugin objects: rows for charts, a table for tables, CSV for either.
void testOpsDataSet() {
    Project p;
    PluginProps chart;
    chart.objectType = "charts.bar";
    chart.hasRows = true;
    p.comp.addLayer(makePluginLayer("Bars", chart, {960, 540}, 5.0));
    PluginProps grid;
    grid.objectType = "charts.table";
    grid.hasTable = true;
    p.comp.addLayer(makePluginLayer("Grid", grid, {960, 540}, 5.0));
    Layer plain = makeTextLayer("Plain", {0, 0}, 5.0);
    plain.name = "Plain";
    p.comp.addLayer(plain);
    auto rows = [&]() -> const std::vector<DataRow>& {
        return std::find_if(p.comp.layers.begin(), p.comp.layers.end(), [](const Layer& l) { return l.name == "Bars"; })->plugin.rows;
    };
    CHECK(run(p, R"({"op": "data.set", "layer": "Bars", "rows": [{"label": "Jan", "value": 42, "color": "#E8B931"}, ["Feb", 7]]})").ok);
    CHECK(rows().size() == 2 && rows()[1].label == "Feb" && near(rows()[0].value, 42.0f));
    CHECK(run(p, R"({"op": "data.set", "layer": "Bars", "csv": "Month;Sales\nMar;1.234,5\nApr;10"})").ok);
    CHECK(rows().size() == 2 && rows()[0].label == "Mar" && near(rows()[0].value, 1234.5f));
    CHECK(run(p, R"({"op": "data.set", "layer": "Grid", "table": {"header": ["Name", "Score"], "rows": [["Ana", 12], ["Bia", "9"]]}})").ok);
    CHECK(!run(p, R"({"op": "data.set", "layer": "Grid", "rows": [["x", 1]]})").ok);  // a table takes no rows
    CHECK(!run(p, R"({"op": "data.set", "layer": "Plain", "rows": [["x", 1]]})").ok); // no data at all
    CHECK(!run(p, R"({"op": "data.set", "layer": "Bars"})").ok);                       // nothing given
}

// Importing goes through the host's probe: without one (a plain core) it fails cleanly.
void testOpsMediaImport() {
    Project p;
    CHECK(!run(p, R"({"op": "media.import", "path": "C:/footage/take.mp4"})").ok);
    OpEnvironment env;
    env.probeMedia = [](const std::string& path, MediaItem& out, std::string& error) {
        if (path.find("missing") != std::string::npos) {
            error = "no such file";
            return false;
        }
        out.name = "take.mp4", out.path = path, out.kind = MediaKind::Video;
        out.width = 1280, out.height = 720, out.duration = 3.0, out.fps = 25.0, out.hasVideo = true;
        return true;
    };
    OpsResult r = applyOps(p, nlohmann::json::parse(R"({"op": "media.import", "path": "C:/footage/take.mp4"})"), env);
    CHECK(r.ok && r.results[0]["width"] == 1280 && r.results[0]["name"] == "take.mp4");
    const MediaItem* m = r.ok ? p.comp.findMedia(r.results[0]["media"].get<uint64_t>()) : nullptr;
    CHECK(m && m->kind == MediaKind::Video);
    r = applyOps(p, nlohmann::json::parse(R"({"op": "media.import", "path": "C:/footage/missing.mp4"})"), env);
    CHECK(!r.ok && r.error.find("no such file") != std::string::npos);
    CHECK(p.comp.media.size() == 1);
}

} // namespace

Tests operationsTests() {
    return {
        {"operations: build and query", testOpsBuildAndQuery},
        {"operations: atomic batches and errors", testOpsAtomic},
        {"operations: compounds, effects, split, order", testOpsStructure},
        {"operations: file round trip and describe", testOpsRoundTrip},
        {"validate and repair", testValidateAndRepair},
        {"csv and numbers", testCsvAndNumbers},
        {"length limit", testLengthLimit},
        {"gradients and picture frames", testGradientsAndFrames},
        {"operations: remove, duplicate, list layers", testOpsLayers},
        {"operations: remove, clear and ease keys", testOpsKeys},
        {"operations: remove masks and effects", testOpsRemoveByIndex},
        {"operations: slip, close gap, remap, freeze", testOpsClipTiming},
        {"operations: data for plugin objects", testOpsDataSet},
        {"operations: import media", testOpsMediaImport},
    };
}
