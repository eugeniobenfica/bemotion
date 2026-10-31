// Files: the project format, media, imports and exports, catalogs, languages, the docs.

#include "Check.h"

#include "core/Expressions.h"
#include "core/Geometry.h"
#include "core/Language.h"
#include "core/Lottie.h"
#include "core/MeshFile.h"
#include "core/Ops.h"
#include "core/PluginCatalog.h"
#include "core/Project.h"
#include "core/Serialization.h"
#include "core/Svg.h"
#include "core/Updates.h"
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
    CHECK(v && v->kind == MediaKind::Video && v->width == 1280 && near(static_cast<float>(v->fps), 25.0f) &&
          v->hasAudio);
    const Layer* ql = q.comp.findLayer(layerId);
    CHECK(ql && ql->type == LayerType::Media && ql->media.mediaId == id);
    CHECK(ql && near(static_cast<float>(ql->media.start), 3.0f) && ql->media.volume.keyCount() == 1);
    CHECK(q.comp.nextMediaId == p.comp.nextMediaId);
    CHECK(serializeProject(p) == serializeProject(q));
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
    CHECK(m.effects.size() == 2 && m.effects[0].type == EffectType::DropShadow &&
          m.effects[1].type == EffectType::Glow);
    CHECK(near(m.effects[1].size.valueAt(1.5), 25.0f));
    CHECK(serializeProject(p) == serializeProject(q));
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

void testLegacyFormatId() {
    Project p;
    std::string text = serializeProject(p);
    const auto pos = text.find("\"bemotion\"");
    CHECK(pos != std::string::npos);
    text.replace(pos, 10, "\"mirage\"");
    Project q;
    CHECK(deserializeProject(text, q));
}

// Files saved by earlier versions (tests/files/) open as they were: nothing repaired unless the
// file's .repairs.txt names it (one "where: what" a line), and saving them changes nothing more.
void testOldFiles() {
    const auto folder = std::filesystem::path(BEMOTION_SOURCE_DIR) / "tests" / "files";
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != kProjectExtension) continue;
        ++count;
        const std::string name = entry.path().filename().generic_string();
        Project p;
        std::string error;
        std::vector<Issue> repairs;
        if (!loadProject(entry.path(), p, &error, &repairs)) {
            std::printf("  %s: %s\n", name.c_str(), error.c_str());
            CHECK(false);
            continue;
        }
        std::vector<std::string> expected;
        std::ifstream list(std::filesystem::path(entry.path()).replace_extension(".repairs.txt"));
        for (std::string line; std::getline(list, line);)
            if (!line.empty()) expected.push_back(line);
        for (const Issue& issue : repairs)
            if (std::find(expected.begin(), expected.end(), issue.where + ": " + issue.what) == expected.end()) {
                std::printf("  %s: unexpected repair %s: %s\n", name.c_str(), issue.where.c_str(), issue.what.c_str());
                CHECK(false);
            }
        CHECK(!p.comp.layers.empty() && validate(p).empty());
        Project again;
        CHECK(deserializeProject(serializeProject(p), again) && serializeProject(again) == serializeProject(p));
    }
    CHECK(count >= 3);
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
    CHECK(!run(p, (R"({"op": "param.set", "layer": "Model", "param": "model", "value": ")" +
                   (dir / "a.txt").generic_string() + R"("})")
                      .c_str())
               .ok);
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
    CHECK(m && m->kind == PluginParamKind::File &&
          fs::exists(fs::path(std::u8string(m->text.begin(), m->text.end()))) && m->accept.size() == 2);
    CHECK(back.comp.layers[0].plugin.find("shading")->choice == 1);
    fs::remove_all(dir);
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
        CHECK(d.shapes[0].fillKind == ShapeFill::Linear && near(d.shapes[0].fillAngle, 90.0f) &&
              near(d.shapes[0].fillColor.r, 1.0f));
        // The circle went through its group's translate; its class colored it.
        Vec2 mn, mx;
        CHECK(bezierBounds(d.shapes[1].outline, mn, mx) && near(mn.x, 30.0f, 0.1f) && near(mx.y, 70.0f, 0.1f));
        CHECK(near(d.shapes[1].fillColor.r, 1.0f) && d.shapes[1].stroke && near(d.shapes[1].strokeColor.b, 1.0f) &&
              near(d.shapes[1].strokeWidth, 2.0f));
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
    CHECK(group && group->type == LayerType::Compound && group->name == "Logo" &&
          near(static_cast<float>(group->inPoint), 1.0f));
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
        CHECK(pos["k"][1]["h"] == 1);                                   // held
        // An expression: baked every frame of the clip.
        CHECK((*boxJson)["ks"]["r"]["a"] == 1 && (*boxJson)["ks"]["r"]["k"].size() >= 90);
        CHECK((*boxJson)["shapes"][0]["it"][0]["ty"] == "rc");
    }
}

// Model files: OBJ (with normals and a quad), STL (binary, welded), glTF (embedded), simplified.
void testMeshFiles() {
    MeshData m;
    std::string error;
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";
    CHECK(loadMeshFile("obj", obj, {}, m, error) && m.triangleCount() == 2 && m.vertexCount() == 4 &&
          m.normals.size() == 12);
    std::string stl(84, ' ');
    const uint32_t one = 1;
    std::memcpy(&stl[80], &one, 4);
    const float tri[12] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0};
    stl.append(reinterpret_cast<const char*>(tri), 48);
    stl.append(2, ' ');
    CHECK(loadMeshFile("stl", stl, {}, m, error) && m.triangleCount() == 1 && m.vertexCount() == 3);
    zUpToYUp(m);
    CHECK(m.positions[3] == 1.0f && m.positions[7] == 0.0f &&
          m.positions[8] == -1.0f); // (1, 0, 0) stays; (0, 1, 0) goes to (0, 0, -1)
    // A glTF triangle in a data URI: 3 positions (36 bytes of floats).
    const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<const unsigned char*>(pos);
    std::string enc;
    for (size_t i = 0; i < 36; i += 3) {
        const uint32_t v = bytes[i] << 16 | bytes[i + 1] << 8 | bytes[i + 2];
        for (int k = 3; k >= 0; --k)
            enc += b64[(v >> (6 * k)) & 63];
    }
    const std::string gltf =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" + enc +
        R"("}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"translation":[5,0,0]}],"scenes":[{"nodes":[0]}]})";
    CHECK(loadMeshFile("gltf", gltf, {}, m, error) && m.triangleCount() == 1 && m.positions[3] == 6.0f);
    CHECK(!loadMeshFile(
        "gltf", R"({"buffers":[{"uri":"model.bin"}]})", [](const std::string&, std::string&) { return false; }, m,
        error));
    CHECK(!loadMeshFile("obj", "garbage", {}, m, error) && !error.empty());
    // A fine grid of triangles, simplified to a few.
    MeshData grid;
    const int side = 60;
    for (int y = 0; y <= side; ++y)
        for (int x = 0; x <= side; ++x)
            grid.positions.insert(grid.positions.end(), {static_cast<float>(x), static_cast<float>(y), 0.0f});
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * (side + 1) + x), b = a + 1, c = a + side + 1, d = c + 1;
            grid.indices.insert(grid.indices.end(), {a, b, d, a, d, c});
        }
    simplifyMesh(grid, 500);
    CHECK(grid.triangleCount() > 50 && grid.triangleCount() <= 500);
}

void testLanguageFiles() {
    LanguageFile file;
    std::string err;
    CHECK(parseLanguage(
        R"({"format": "bemotion-language", "code": "pt-BR", "name": "Português", "strings": {"File": "Arquivo", "Empty": ""}})",
        file, &err));
    CHECK(file.code == "pt-BR" && file.strings.size() == 1 && file.strings["File"] == "Arquivo");
    CHECK(!parseLanguage(R"({"code": "pt"})", file, &err));
    CHECK(placeholdersOf("%2 of %1, {name} %") == std::vector<std::string>({"%1", "%2", "{name}"}));
    CHECK(matchLanguage({"es", "pt-BR"}, "pt_BR") == 1 && matchLanguage({"es", "pt-BR"}, "pt_PT") == 1 &&
          matchLanguage({"es"}, "de_DE") == -1);
    // The bundled translations parse and keep their texts' placeholders.
    std::ifstream in(std::filesystem::path(BEMOTION_SOURCE_DIR) / "assets" / "languages" / "pt-BR.json",
                     std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    CHECK(parseLanguage(text.str(), file, &err) && file.strings.size() > 1000);
    for (const auto& [english, translation] : file.strings)
        if (placeholdersOf(english) != placeholdersOf(translation)) {
            std::printf("  placeholders differ: %s\n", english.c_str());
            CHECK(false);
        }
}

// The official plugin catalog: only plugins whose every file has a safe path and a hash.
void testPluginCatalog() {
    const std::string hash(64, 'a');
    const std::string text = R"({"catalog": 1, "plugins": [
        {"id": "good", "name": "Good", "version": "1.2.0", "api": 7, "permissions": ["edit"],
         "files": [{"path": "plugin.json", "size": 10, "sha256": ")" +
                             hash + R"("},
                   {"path": "maps/world.lua", "size": 5, "sha256": ")" +
                             hash + R"("}]},
        {"id": "escape", "files": [{"path": "plugin.json", "size": 1, "sha256": ")" +
                             hash + R"("},
                                   {"path": "../../evil.lua", "size": 1, "sha256": ")" +
                             hash + R"("}]},
        {"id": "nohash", "files": [{"path": "plugin.json", "size": 1, "sha256": "xyz"}]},
        {"id": "no/slash", "files": [{"path": "plugin.json", "size": 1, "sha256": ")" +
                             hash + R"("}]},
        {"id": "nomanifest", "files": [{"path": "main.lua", "size": 1, "sha256": ")" +
                             hash + R"("}]}]})";
    std::vector<CatalogPlugin> plugins;
    std::string error;
    CHECK(parsePluginCatalog(text, plugins, &error));
    CHECK(plugins.size() == 1 && plugins[0].id == "good" && plugins[0].size() == 15 && plugins[0].api == 7 &&
          !error.empty());
    CHECK(!parsePluginCatalog("[1, 2]", plugins) && !parsePluginCatalog("not json", plugins));
    CHECK(safeCatalogPath("main.lua") && safeCatalogPath("maps/world.lua"));
    CHECK(!safeCatalogPath("/etc/x") && !safeCatalogPath("a/../b") && !safeCatalogPath("C:/x") &&
          !safeCatalogPath("a\\b") && !safeCatalogPath("a//b"));
    CHECK(compareVersions("1.10.0", "1.9") > 0 && compareVersions("1.0", "1.0.0") == 0 &&
          compareVersions("0.9.1", "1.0") < 0);
}

void testUpdates() {
    CHECK(compareReleaseVersions("0.2.0", "0.2.0-beta.3") > 0 &&
          compareReleaseVersions("0.2.0-beta.3", "0.2.0-beta.2") > 0);
    CHECK(compareReleaseVersions("0.2.0-beta.10", "0.2.0-beta.9") > 0 &&
          compareReleaseVersions("0.2.0-beta.2", "0.1.9") > 0);
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
    CHECK(!parseReleases(R"({"message": "API rate limit exceeded"})", releases, &error) &&
          error.find("rate limit") != std::string::npos);
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
    const auto sums =
        parseSha256Sums(a + " *bemotion-0.2.0-setup.exe\r\n" + b + "  bemotion-x86_64.AppImage\nnot a line\n");
    CHECK(sums.size() == 2 && sums.at("bemotion-0.2.0-setup.exe") == std::string(64, 'a') &&
          sums.at("bemotion-x86_64.AppImage") == b);
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

// Media folders: names that keep the Media panel tidy, saved with the project.
void testMediaFolders() {
    Project p;
    MediaItem a, b;
    a.name = "intro.mp4", b.name = "logo.png";
    const uint64_t ida = p.comp.addMedia(a), idb = p.comp.addMedia(b);
    const std::string ops = R"j([{"op": "folder.add", "name": "Footage"}, {"op": "folder.add", "name": " Logos "},
                                 {"op": "media.move", "media": )j" +
                            std::to_string(ida) + R"j(, "folder": "Footage"},
                                 {"op": "media.move", "media": )j" +
                            std::to_string(idb) + R"j(, "folder": "Logos"},
                                 {"op": "folder.rename", "folder": "Footage", "name": "Video"},
                                 {"op": "query.composition"}])j";
    OpsResult r = run(p, ops.c_str());
    CHECK(r.ok);
    if (r.ok)
        CHECK(r.results[5]["folders"] == nlohmann::json::array({"Video", "Logos"}) &&
              r.results[5]["media"][0]["folder"] == "Video");
    CHECK(!run(p, R"j({"op": "folder.add", "name": "Video"})j").ok);                  // each name once
    CHECK(!run(p, R"j({"op": "folder.rename", "folder": "Vdeo", "name": "X"})j").ok); // a folder that is not there
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

} // namespace

Tests fileTests() {
    return {
        {"serialization round trip", testSerializationRoundTrip},
        {"media", testMedia},
        {"guides round trip", testGuidesRoundTrip},
        {"effects round trip", testEffectsRoundTrip},
        {"plugin layer round trip", testPluginLayerRoundTrip},
        {"legacy format id", testLegacyFormatId},
        {"files from earlier versions", testOldFiles},
        {"file parameters", testFileParams},
        {"svg import", testSvgImport},
        {"lottie export", testLottieExport},
        {"3D model files", testMeshFiles},
        {"language files", testLanguageFiles},
        {"plugin catalog", testPluginCatalog},
        {"updates", testUpdates},
        {"references complete", testReferencesComplete},
        {"media folders", testMediaFolders},
    };
}
