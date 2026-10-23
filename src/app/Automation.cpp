#include "app/Automation.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "core/Describe.h"
#include "core/Serialization.h"
#include "core/Validate.h"
#include "media/MediaIO.h"
#include "render/OffscreenRenderer.h"

#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bemotion {

using json = nlohmann::json;

namespace {

constexpr const char* kUsage =
    "bemotion command-line tools (docs/AUTOMATION.md)\n"
    "  bemotion describe <project> [--json]\n"
    "  bemotion validate <project> [--fix]\n"
    "  bemotion apply <project> <ops.json | -> [--out <file>] [--dry-run] [--describe]\n"
    "  bemotion apply <project> --ops '<json>' [...]\n"
    "  bemotion render <project> [--at t1,t2,...] [--count N] [--width W] [--each] --out <file.png>\n"
    "  bemotion script <project> <file.lua | -> [--time T] [--out <file>] [--dry-run] [--describe]\n"
    "  bemotion new <project> [--width W] [--height H] [--fps F]\n"
    "  bemotion <project> --export <file> [--format mp4|png|mov|webm|gif] [--scale N] [--transparent]\n";

// Options after the positional arguments: "--name value" or "--flag".
struct Args {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> options;
    bool flag(const std::string& name) const {
        return std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.first == name; });
    }
    std::string value(const std::string& name, const std::string& fallback = "") const {
        for (const auto& [k, v] : options)
            if (k == name) return v;
        return fallback;
    }
};

Args parse(const std::vector<std::string>& args, std::initializer_list<const char*> flags) {
    Args a;
    for (size_t i = 2; i < args.size(); ++i) {
        const std::string& s = args[i];
        if (s.rfind("--", 0) == 0) {
            const std::string name = s.substr(2);
            const bool isFlag = std::any_of(flags.begin(), flags.end(), [&](const char* f) { return name == f; });
            a.options.emplace_back(name, !isFlag && i + 1 < args.size() ? args[++i] : "");
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

int usageError(const std::string& message) {
    std::fprintf(stderr, "error: %s\n\n%s", message.c_str(), kUsage);
    return 2;
}

bool load(const std::string& file, Project& p, std::vector<Issue>* repairs = nullptr) {
    std::string error;
    if (!loadProject(platform::fromUtf8(file), p, &error, repairs)) {
        std::fprintf(stderr, "error: could not open %s: %s\n", file.c_str(), error.c_str());
        return false;
    }
    PluginHost::get().reconcile(p.comp); // plugin objects as the editor shows them
    return true;
}

bool save(const Project& p, const std::string& file) {
    std::string error;
    if (!saveProject(p, platform::fromUtf8(file), &error)) {
        std::fprintf(stderr, "error: could not save %s: %s\n", file.c_str(), error.c_str());
        return false;
    }
    return true;
}

void print(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

int describe(const Args& a) {
    if (a.positional.size() != 1) return usageError("describe takes one project");
    Project p;
    if (!load(a.positional[0], p)) return 1;
    if (a.flag("json")) {
        const OpsResult r = applyOps(p, json::array({{{"op", "query.composition"}}, {{"op", "query.layers"}}}));
        json out = r.results[0];
        out["layers"] = r.results[1];
        print(out.dump(2) + "\n");
    } else {
        print(describeProject(p));
    }
    return 0;
}

int validateCmd(const Args& a) {
    if (a.positional.size() != 1) return usageError("validate takes one project");
    Project p;
    std::vector<Issue> repairs;
    if (!load(a.positional[0], p, &repairs)) return 1;
    if (repairs.empty()) {
        print("valid\n");
        return 0;
    }
    std::string out;
    for (const Issue& i : repairs) out += i.where + ": " + i.what + "\n";
    print(out);
    if (a.flag("fix")) {
        if (!save(p, a.positional[0])) return 1;
        print("repaired and saved\n");
        return 0;
    }
    return 1;
}

int apply(const Args& a) {
    if (a.positional.empty()) return usageError("apply needs a project");
    const std::string file = a.positional[0];
    std::string text = a.value("ops");
    if (text.empty()) {
        if (a.positional.size() < 2) return usageError("apply needs operations: a .json file, - for stdin, or --ops '<json>'");
        if (a.positional[1] == "-") {
            text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        } else {
            std::ifstream in(platform::fromUtf8(a.positional[1]), std::ios::binary);
            if (!in) return usageError("cannot read " + a.positional[1]);
            std::stringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
    }
    json ops;
    try {
        ops = json::parse(text);
    } catch (const std::exception& e) {
        print(json{{"ok", false}, {"error", std::string("the operations are not valid JSON: ") + e.what()}}.dump(2) + "\n");
        return 1;
    }
    Project p;
    if (!load(file, p)) return 1;
    const OpsResult r = applyOps(p, ops, automationEnvironment());
    json out = {{"ok", r.ok}};
    if (!r.ok) {
        out["error"] = r.error;
        out["failed_index"] = r.failedIndex;
    } else {
        out["results"] = r.results;
        out["changed"] = r.changed;
        if (r.changed && !a.flag("dry-run")) {
            const std::string target = a.value("out", file);
            if (!save(p, target)) return 1;
            out["saved"] = target;
        }
        if (a.flag("describe")) out["project"] = describeProject(p);
    }
    print(out.dump(2) + "\n");
    return r.ok ? 0 : 1;
}

int renderCmd(const Args& a) {
    if (a.positional.size() != 1) return usageError("render takes one project");
    const std::string outFile = a.value("out");
    if (outFile.empty()) return usageError("render needs --out <file.png>");
    Project p;
    if (!load(a.positional[0], p)) return 1;
    const Composition& c = p.comp;

    std::vector<double> times;
    if (const std::string at = a.value("at"); !at.empty()) {
        std::stringstream ss(at);
        for (std::string item; std::getline(ss, item, ',');) times.push_back(std::clamp(std::strtod(item.c_str(), nullptr), 0.0, c.duration));
    } else { // evenly spread over the composition, the last frame included
        const int n = std::clamp(std::atoi(a.value("count", "6").c_str()), 1, 64);
        for (int i = 0; i < n; ++i) times.push_back(n == 1 ? 0.0 : c.snapToFrame((c.duration - c.frameDuration()) * i / (n - 1)));
    }
    const int width = std::clamp(std::atoi(a.value("width", times.size() > 1 && !a.flag("each") ? "480" : "960").c_str()), 16, 8192);
    const float scale = static_cast<float>(width) / c.width;
    const int fw = std::max(1, static_cast<int>(std::lround(c.width * scale))), fh = std::max(1, static_cast<int>(std::lround(c.height * scale)));
    render::OffscreenRenderer renderer;
    std::string error;
    if (!renderer.init(fw, fh, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    auto writePng = [](const std::string& file, int w, int h, const std::vector<uint8_t>& rgba) {
        std::ofstream out(platform::fromUtf8(file), std::ios::binary);
        if (!out) return false;
        stbi_write_png_to_func([](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write(static_cast<char*>(data), size); },
                               &out, w, h, 4, rgba.data(), w * 4);
        return static_cast<bool>(out);
    };
    json files = json::array();
    std::vector<uint8_t> frame;
    if (a.flag("each") || times.size() == 1) {
        const std::string base = outFile.size() > 4 && outFile.substr(outFile.size() - 4) == ".png" ? outFile.substr(0, outFile.size() - 4) : outFile;
        for (double t : times) {
            renderer.render(c, t, scale, true, frame);
            char suffix[32];
            std::snprintf(suffix, sizeof suffix, "_%06.2fs.png", t);
            const std::string file = times.size() == 1 ? base + ".png" : base + suffix;
            if (!writePng(file, fw, fh, frame)) return usageError("cannot write " + file);
            files.push_back({{"time", t}, {"file", file}});
        }
    } else { // a contact sheet: left to right, top to bottom, 4 px apart
        const int cols = std::min<int>(static_cast<int>(times.size()), times.size() <= 4 ? 2 : 3), rows = static_cast<int>((times.size() + cols - 1) / cols);
        const int gap = 4, sw = cols * fw + (cols + 1) * gap, sh = rows * fh + (rows + 1) * gap;
        std::vector<uint8_t> sheet(static_cast<size_t>(sw) * sh * 4, 0);
        for (size_t i = 0; i < sheet.size(); i += 4) sheet[i] = sheet[i + 1] = sheet[i + 2] = 40, sheet[i + 3] = 255;
        for (size_t k = 0; k < times.size(); ++k) {
            renderer.render(c, times[k], scale, true, frame);
            const int x0 = gap + static_cast<int>(k % cols) * (fw + gap), y0 = gap + static_cast<int>(k / cols) * (fh + gap);
            for (int y = 0; y < fh; ++y)
                std::copy_n(frame.data() + static_cast<size_t>(y) * fw * 4, static_cast<size_t>(fw) * 4,
                            sheet.data() + (static_cast<size_t>(y0 + y) * sw + x0) * 4);
            files.push_back({{"time", times[k]}, {"row", k / cols}, {"column", k % cols}});
        }
        if (!writePng(outFile, sw, sh, sheet)) return usageError("cannot write " + outFile);
        print(json{{"ok", true}, {"sheet", outFile}, {"frames", files}}.dump(2) + "\n");
        return 0;
    }
    print(json{{"ok", true}, {"files", files}}.dump(2) + "\n");
    return 0;
}

int script(const Args& a) {
    if (a.positional.size() != 2) return usageError("script takes a project and a .lua file (or - for stdin)");
    std::string code;
    if (a.positional[1] == "-") {
        code.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    } else {
        std::ifstream in(platform::fromUtf8(a.positional[1]), std::ios::binary);
        if (!in) return usageError("cannot read " + a.positional[1]);
        std::stringstream ss;
        ss << in.rdbuf();
        code = ss.str();
    }
    Project p;
    if (!load(a.positional[0], p)) return 1;
    const std::string before = serializeProject(p);
    std::vector<std::string> log;
    std::string error;
    const double time = std::atof(a.value("time", "0").c_str());
    const bool ok = PluginHost::runScript(code, platform::toUtf8(platform::fromUtf8(a.positional[1]).filename()), p, time,
                                          automationEnvironment(), log, error);
    json out = {{"ok", ok}, {"log", log}};
    if (!ok) {
        out["error"] = error;
    } else {
        const bool changed = serializeProject(p) != before;
        out["changed"] = changed;
        if (changed && !a.flag("dry-run")) {
            const std::string target = a.value("out", a.positional[0]);
            if (!save(p, target)) return 1;
            out["saved"] = target;
        }
        if (a.flag("describe")) out["project"] = describeProject(p);
    }
    print(out.dump(2) + "\n");
    return ok ? 0 : 1;
}

int newProject(const Args& a) {
    if (a.positional.size() != 1) return usageError("new takes the project file to create");
    Project p;
    p.name = platform::fromUtf8(a.positional[0]).stem().u8string().empty() ? "Untitled"
                                                                           : platform::toUtf8(platform::fromUtf8(a.positional[0]).stem());
    json set = {{"op", "composition.set"}};
    if (a.flag("width")) set["width"] = std::atof(a.value("width").c_str());
    if (a.flag("height")) set["height"] = std::atof(a.value("height").c_str());
    if (a.flag("fps")) set["fps"] = std::atof(a.value("fps").c_str());
    const OpsResult r = applyOps(p, set);
    if (!r.ok) return usageError(r.error);
    p.comp.fitDuration();
    if (!save(p, a.positional[0])) return 1;
    print(json{{"ok", true}, {"saved", a.positional[0]}}.dump(2) + "\n");
    return 0;
}

} // namespace

OpEnvironment automationEnvironment() {
    OpEnvironment env;
    env.probeMedia = [](const std::string& path, MediaItem& out, std::string& error) {
        const media::ProbeResult r = media::probe(platform::fromUtf8(path));
        if (!r.ok) {
            error = r.error;
            return false;
        }
        out = r.item;
        return true;
    };
    env.makePluginLayer = [](const std::string& objectType, Layer& out, std::string& error) {
        const PluginObjectType* type = PluginHost::get().findType(objectType);
        if (!type) {
            std::string known;
            for (const PluginObjectType* t : PluginHost::get().objectTypes()) known += (known.empty() ? "" : ", ") + t->id;
            error = "unknown object type \"" + objectType + "\" (installed: " + (known.empty() ? "none" : known) + ")";
            return false;
        }
        out = PluginHost::get().createLayer(*type, {0, 0}, kDefaultLayerLength);
        return true;
    };
    return env;
}

bool isAutomationCommand(const std::string& arg) {
    return arg == "describe" || arg == "validate" || arg == "apply" || arg == "render" || arg == "new" || arg == "script" || arg == "help" ||
           arg == "--help" || arg == "-h";
}

int runAutomation(const std::vector<std::string>& args) {
    const std::string cmd = args.size() > 1 ? args[1] : "help";
    if (cmd == "describe") return describe(parse(args, {"json"}));
    if (cmd == "validate") return validateCmd(parse(args, {"fix"}));
    if (cmd == "apply") return apply(parse(args, {"dry-run", "describe"}));
    if (cmd == "render") return renderCmd(parse(args, {"each"}));
    if (cmd == "new") return newProject(parse(args, {}));
    if (cmd == "script") return script(parse(args, {"dry-run", "describe"}));
    print(kUsage);
    return 0;
}

void attachParentConsole() {
#ifdef _WIN32
    if (!GetStdHandle(STD_OUTPUT_HANDLE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
    SetConsoleOutputCP(CP_UTF8); // names with accents print as they are
#endif
}

} // namespace bemotion
