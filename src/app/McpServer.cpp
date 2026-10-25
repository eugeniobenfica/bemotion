#include "app/McpServer.h"

#include "app/Automation.h"
#include "app/Platform.h"
#include "app/PluginHost.h"
#include "core/Describe.h"
#include "core/Ops.h"
#include "core/Serialization.h"
#include "core/Validate.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace bemotion {

extern const char* const kOperationsReference;  // docs/OPERATIONS.md, built in (OperationsReference.cpp)
extern const char* const kExpressionsReference; // docs/EXPRESSIONS.md, the same way

namespace {

using json = nlohmann::json;

constexpr const char* kProtocolVersions[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

constexpr const char* kInstructions =
    "bemotion is a video editor with a focus on 2D animation. Projects are .bemotion files; pass their full path in "
    "`project`.\n"
    "Workflow: describe_project (ids, names, timing, what is animated) -> apply_operations (JSON operations; "
    "get_operations_reference lists them all) -> render_frames to LOOK at the result (always check visually) -> "
    "repeat -> export_video. For loops and layouts, run_lua is shorter. Refer to layers by id or unique name. "
    "Every batch is atomic and validated: if one operation fails nothing changes and the error says why, often "
    "with the right name. Times are seconds, positions composition pixels from the top-left, colors \"#RRGGBB\".";

// A tool's failure: reported to the assistant as an error result (not a protocol error).
struct ToolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

json textContent(const std::string& text) { return {{"type", "text"}, {"text", text}}; }

json toolResult(json content, bool error = false) {
    json r = {{"content", std::move(content)}};
    if (error) r["isError"] = true;
    return r;
}

std::string base64(const std::vector<uint8_t>& data) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t n = (data[i] << 16) | (i + 1 < data.size() ? data[i + 1] << 8 : 0) | (i + 2 < data.size() ? data[i + 2] : 0);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < data.size() ? k[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? k[n & 63] : '=';
    }
    return out;
}

std::string stringArg(const json& args, const char* name) {
    if (!args.contains(name) || !args[name].is_string() || args[name].get<std::string>().empty())
        throw ToolError(std::string("missing argument \"") + name + "\"");
    return args[name].get<std::string>();
}

Project loadArg(const json& args) {
    const std::string file = stringArg(args, "project");
    Project p;
    std::string error;
    if (!loadProject(platform::fromUtf8(file), p, &error)) throw ToolError("could not open " + file + ": " + error);
    PluginHost::get().reconcile(p.comp);
    return p;
}

void saveArg(const json& args, const Project& p) {
    const std::string file = stringArg(args, "project");
    std::string error;
    if (!saveProject(p, platform::fromUtf8(file), &error)) throw ToolError("could not save " + file + ": " + error);
}

bool flag(const json& args, const char* name) { return args.contains(name) && args[name].is_boolean() && args[name].get<bool>(); }

// --- the tools -----------------------------------------------------------------------------------

json projectProp() {
    return {{"type", "string"}, {"description", "Full path of the .bemotion project file"}};
}

json toolList() {
    auto tool = [](const char* name, const char* description, json properties, std::vector<std::string> required) {
        return json{{"name", name},
                    {"description", description},
                    {"inputSchema", {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}}}};
    };
    json ops = {{"description", "One operation object, or a list of them (run in order, atomically). "
                                "Example: {\"op\": \"layer.add\", \"type\": \"text\", \"text\": \"Hi\"}"}};
    // Every operation, from the table that runs them (core/Ops.cpp): never out of date.
    static const std::string applyDescription = [] {
        std::string list;
        for (const std::string& n : operationNames()) list += (list.empty() ? "" : ", ") + n;
        return "Edits a project with operations (see get_operations_reference): " + list +
               ". Atomic: on failure nothing changes and the error explains it. Saves the project.";
    }();
    return json::array({
        tool("describe_project", "Summary of a project: size, frame rate, duration and every layer (id, name, type, "
                                 "time range, position, animated properties, effects). Start here.",
             {{"project", projectProp()}}, {"project"}),
        tool("create_project", "Creates an empty project file (default 1920x1080, 30 fps).",
             {{"project", projectProp()},
              {"width", {{"type", "integer"}}},
              {"height", {{"type", "integer"}}},
              {"fps", {{"type", "number"}}}},
             {"project"}),
        tool("query_layer", "Everything about one layer: each property's value at `time` and its keyframes, effects, text.",
             {{"project", projectProp()},
              {"layer", {{"description", "Layer id (number) or unique name"}}},
              {"time", {{"type", "number"}, {"description", "Seconds (default 0)"}}}},
             {"project", "layer"}),
        tool("apply_operations", applyDescription.c_str(),
             {{"project", projectProp()},
              {"operations", ops},
              {"dry_run", {{"type", "boolean"}, {"description", "Check without saving"}}}},
             {"project", "operations"}),
        tool("run_lua",
             "Runs Lua with the bemotion `doc` API (shorter than operations for loops and layouts): the code receives doc "
             "as `...` (local doc = ...). doc.apply(op or {ops}) returns results; doc.layers(), doc.layer(id), "
             "doc.composition() read; doc.expression(layer, prop, expression, time) tries an expression. Atomic; "
             "print() output is returned. Saves the project.",
             {{"project", projectProp()},
              {"code", {{"type", "string"}}},
              {"time", {{"type", "number"}, {"description", "doc.time (seconds)"}}},
              {"dry_run", {{"type", "boolean"}}}},
             {"project", "code"}),
        tool("render_frames",
             "Renders moments of the project and returns them as an image, so you can see the result: one frame, or a "
             "contact sheet (left to right, top to bottom) for several. Use it after every edit.",
             {{"project", projectProp()},
              {"times", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "Seconds; default: evenly spread"}}},
              {"count", {{"type", "integer"}, {"description", "How many moments when `times` is not given (default 6)"}}},
              {"width", {{"type", "integer"}, {"description", "Width of each frame in pixels (default 480, one frame 960)"}}}},
             {"project"}),
        tool("validate_project", "Checks the project against bemotion's rules; with fix, saves the repaired file.",
             {{"project", projectProp()}, {"fix", {{"type", "boolean"}}}}, {"project"}),
        tool("export_video",
             "Exports the project: the format follows the output extension (.mp4, .mov ProRes, .webm, .gif, .png "
             "sequence). Takes a while for long projects.",
             {{"project", projectProp()},
              {"output", {{"type", "string"}, {"description", "Full path of the file to write"}}},
              {"scale", {{"type", "number"}, {"description", "Output size relative to the composition (default 1)"}}},
              {"transparent", {{"type", "boolean"}, {"description", "No background (MOV, WebM, PNG)"}}}},
             {"project", "output"}),
        tool("test_expression",
             "Works an expression out on a layer's property without setting it (see get_expressions_reference): its "
             "value at each time, or what is wrong. Try one here, then set it with the expression.set operation.",
             {{"project", projectProp()},
              {"layer", {{"description", "Layer id (number) or unique name"}}},
              {"prop", {{"type", "string"}, {"description", "Property id: position, rotation, opacity, fx0.size, p.progress..."}}},
              {"expression", {{"type", "string"}, {"description", "Default: the property's own expression"}}},
              {"times", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "Seconds (default 0)"}}}},
             {"project", "layer", "prop"}),
        tool("get_operations_reference", "The full reference of operations: every field, property ids, units, easings.", json::object(), {}),
        tool("get_expressions_reference",
             "The expression language (expression.set, test_expression): names, functions, other layers, examples.", json::object(), {}),
    });
}

json callTool(const std::string& name, const json& args) {
    platform::log("mcp tool " + name);
    if (name == "get_operations_reference") return toolResult({textContent(kOperationsReference)});
    if (name == "get_expressions_reference") return toolResult({textContent(kExpressionsReference)});

    if (name == "create_project") {
        const std::string file = stringArg(args, "project");
        Project p;
        p.name = platform::toUtf8(platform::fromUtf8(file).stem());
        json set = {{"op", "composition.set"}};
        for (const char* k : {"width", "height", "fps"})
            if (args.contains(k)) set[k] = args[k];
        const OpsResult r = applyOps(p, set);
        if (!r.ok) throw ToolError(r.error);
        p.comp.fitDuration();
        saveArg(args, p);
        return toolResult({textContent("created " + file + "\n" + describeProject(p))});
    }

    bool known = false;
    for (const json& t : toolList()) known |= t["name"] == name;
    if (!known) throw ToolError("unknown tool \"" + name + "\"");
    Project p = loadArg(args);
    if (name == "describe_project") return toolResult({textContent(describeProject(p))});

    if (name == "query_layer") {
        if (!args.contains("layer")) throw ToolError("missing argument \"layer\"");
        json op = {{"op", "query.layer"}, {"layer", args["layer"]}};
        if (args.contains("time")) op["time"] = args["time"];
        const OpsResult r = applyOps(p, op);
        if (!r.ok) throw ToolError(r.error);
        return toolResult({textContent(r.results[0].dump(2))});
    }

    if (name == "test_expression") {
        if (!args.contains("layer")) throw ToolError("missing argument \"layer\"");
        json op = {{"op", "query.expression"}, {"layer", args["layer"]}, {"prop", stringArg(args, "prop")}};
        if (args.contains("expression")) op["expression"] = args["expression"];
        op["times"] = args.contains("times") && args["times"].is_array() && !args["times"].empty() ? args["times"] : json::array({0});
        const OpsResult r = applyOps(p, op);
        if (!r.ok) throw ToolError(r.error);
        const json& out = r.results[0];
        if (!out.value("ok", false)) throw ToolError("the expression does not work: " + out.value("error", std::string()));
        return toolResult({textContent(out.dump(2))});
    }

    if (name == "apply_operations") {
        if (!args.contains("operations")) throw ToolError("missing argument \"operations\"");
        json ops = args["operations"];
        if (ops.is_string()) { // some clients send JSON as a string
            try {
                ops = json::parse(ops.get<std::string>());
            } catch (const std::exception& e) {
                throw ToolError(std::string("\"operations\" is not valid JSON: ") + e.what());
            }
        }
        const OpsResult r = applyOps(p, ops, automationEnvironment());
        if (!r.ok) throw ToolError(r.error);
        const bool save = r.changed && !flag(args, "dry_run");
        if (save) saveArg(args, p);
        json out = {{"results", r.results}, {"saved", save}};
        return toolResult({textContent(out.dump(2))});
    }

    if (name == "run_lua") {
        const std::string code = stringArg(args, "code");
        const std::string before = serializeProject(p);
        std::vector<std::string> log;
        std::string error;
        const double time = args.contains("time") && args["time"].is_number() ? args["time"].get<double>() : 0.0;
        if (!PluginHost::runScript(code, "run_lua", p, time, automationEnvironment(), log, error)) {
            std::string text = error;
            for (const auto& l : log) text += "\n" + l;
            throw ToolError(text);
        }
        const bool changed = serializeProject(p) != before;
        if (changed && !flag(args, "dry_run")) saveArg(args, p);
        json out = {{"changed", changed}, {"log", log}};
        return toolResult({textContent(out.dump(2))});
    }

    if (name == "render_frames") {
        std::vector<double> times;
        if (args.contains("times") && args["times"].is_array())
            for (const json& t : args["times"])
                if (t.is_number()) times.push_back(t.get<double>());
        if (times.empty()) times = spreadTimes(p.comp, args.value("count", 6));
        const int width = args.value("width", times.size() == 1 ? 960 : 480);
        std::vector<uint8_t> png;
        std::string error;
        if (!renderFramesPng(p.comp, times, width, png, error)) throw ToolError(error);
        std::string caption = "Frames at";
        for (double t : times) {
            char buf[24];
            std::snprintf(buf, sizeof buf, " %.2f s", t);
            caption += buf;
        }
        if (times.size() > 1) caption += " (left to right, top to bottom)";
        return toolResult({textContent(caption), {{"type", "image"}, {"data", base64(png)}, {"mimeType", "image/png"}}});
    }

    if (name == "validate_project") {
        // Loading already repaired the copy in memory: report what loading fixed.
        std::vector<Issue> repairs;
        Project raw;
        std::string error;
        loadProject(platform::fromUtf8(stringArg(args, "project")), raw, &error, &repairs);
        if (repairs.empty()) return toolResult({textContent("valid")});
        std::string text;
        for (const Issue& i : repairs) text += i.where + ": " + i.what + "\n";
        if (flag(args, "fix")) {
            saveArg(args, raw);
            text += "repaired and saved";
        }
        return toolResult({textContent(text)});
    }

    if (name == "export_video") {
        exporting::Settings s;
        s.output = platform::fromUtf8(stringArg(args, "output"));
        if (args.contains("scale") && args["scale"].is_number()) s.scale = args["scale"].get<float>();
        s.transparent = flag(args, "transparent");
        std::string error;
        if (!exportProject(p, s, {}, error)) throw ToolError("export failed: " + error);
        return toolResult({textContent("exported " + stringArg(args, "output"))});
    }

    throw ToolError("unknown tool \"" + name + "\"");
}

// --- JSON-RPC ------------------------------------------------------------------------------------

void send(const json& message) {
    const std::string line = message.dump() + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

json errorResponse(const json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

void handle(const json& msg) {
    if (!msg.is_object() || !msg.contains("method")) {
        if (msg.is_object() && msg.contains("id")) send(errorResponse(msg["id"], -32600, "invalid request"));
        return;
    }
    const std::string method = msg["method"].is_string() ? msg["method"].get<std::string>() : "";
    if (!msg.contains("id")) return; // a notification (initialized, cancelled...): nothing to answer
    const json id = msg["id"];
    const json params = msg.value("params", json::object());
    json result;
    if (method == "initialize") {
        std::string version = kProtocolVersions[0];
        const std::string asked = params.value("protocolVersion", std::string());
        for (const char* v : kProtocolVersions)
            if (asked == v) version = v;
        result = {{"protocolVersion", version},
                  {"capabilities", {{"tools", {{"listChanged", false}}}}},
                  {"serverInfo", {{"name", "bemotion"}, {"version", BEMOTION_VERSION}}},
                  {"instructions", kInstructions}};
    } else if (method == "ping") {
        result = json::object();
    } else if (method == "tools/list") {
        result = {{"tools", toolList()}};
    } else if (method == "tools/call") {
        const std::string name = params.value("name", std::string());
        const json args = params.value("arguments", json::object());
        try {
            result = callTool(name, args);
        } catch (const std::exception& e) {
            platform::log(std::string("mcp tool error: ") + e.what());
            result = toolResult({textContent(e.what())}, true);
        }
    } else {
        send(errorResponse(id, -32601, "method not found: " + method));
        return;
    }
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

} // namespace

int runMcpServer() {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // "\n" stays "\n" (one message per line)
#endif
    platform::log("mcp server started");
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        json msg;
        try {
            msg = json::parse(line);
        } catch (const std::exception& e) {
            send(errorResponse(nullptr, -32700, std::string("parse error: ") + e.what()));
            continue;
        }
        if (msg.is_array()) {
            for (const json& m : msg) handle(m);
        } else {
            handle(msg);
        }
    }
    platform::log("mcp server stopped");
    return 0;
}

} // namespace bemotion
