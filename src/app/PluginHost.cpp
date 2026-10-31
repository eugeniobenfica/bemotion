#include "app/PluginHost.h"
#include "app/PluginLua.h"

#include "core/Data.h"
#include "app/Platform.h"
#include "app/Settings.h"
#include "core/Easing.h"
#include "core/MeshFile.h"
#include "media/Recorder.h"
#include "render/PluginCanvas.h"
#include "ui/AppSignals.h"

// Lua is built as C++ (see cmake/Dependencies.cmake): no extern "C".
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <sstream>

namespace bemotion {

using namespace pluginlua;

namespace {

void copyDirectory(const fs::path& from, const fs::path& to) {
    fs::create_directories(to);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

} // namespace

PluginHost& PluginHost::get() {
    static PluginHost host;
    return host;
}

fs::path PluginHost::userDir() {
    const auto dir = platform::appDataDir() / "plugins";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

void PluginHost::shutdown() {
    for (Plugin& p : plugins_)
        if (p.L) closeSandbox(p.L);
    plugins_.clear();
    types_.clear();
    commands_.clear();
    render::setPluginDrawer({});
}

void PluginHost::load() {
    shutdown();
    std::error_code ec;
    // Essentials only: the core alone, no plugin at all (their layers draw nothing meanwhile).
    const std::vector<fs::path> roots = settings().essentialsOnly ? std::vector<fs::path>{} : std::vector<fs::path>{platform::assetsDir() / "plugins", userDir()};
    for (const fs::path& root : roots) {
        const bool builtin = root != userDir();
        std::vector<fs::path> dirs;
        for (const auto& entry : fs::directory_iterator(root, ec))
            if (entry.is_directory() && fs::exists(entry.path() / "plugin.json")) dirs.push_back(entry.path());
        std::sort(dirs.begin(), dirs.end());
        for (const auto& dir : dirs) loadPlugin(dir, builtin);
    }
    render::setPluginDrawer([this](const Layer& layer, double t, render::PluginCanvas& c) { draw(layer, t, c); });
}

void PluginHost::loadPlugin(const fs::path& dir, bool builtin) {
    Plugin p;
    p.dir = dir;
    p.builtin = builtin;
    std::string mainFile = "main.lua";
    try {
        const json m = json::parse(readFile(dir / "plugin.json"));
        p.id = m.value("id", std::string());
        p.name = m.value("name", p.id);
        p.version = m.value("version", std::string("1.0"));
        p.author = m.value("author", std::string());
        p.description = m.value("description", std::string());
        mainFile = m.value("main", mainFile);
        for (const auto& perm : m.value("permissions", json::array()))
            if (perm.is_string()) p.permissions.push_back(perm.get<std::string>());
        for (const auto& sj : m.value("settings", json::array())) {
            if (!sj.is_object() || !sj.contains("id")) continue;
            PluginSetting s;
            s.id = sj.value("id", std::string());
            s.label = sj.value("label", s.id);
            s.description = sj.value("description", std::string());
            const std::string kind = sj.value("kind", std::string("toggle"));
            s.kind = kind == "number" ? PluginSetting::Kind::Number : kind == "choice" ? PluginSetting::Kind::Choice
                     : kind == "text"  ? PluginSetting::Kind::Text : PluginSetting::Kind::Toggle;
            const json def = sj.value("default", json());
            s.number = def.is_number() ? def.get<double>() : 0.0;
            s.toggle = def.is_boolean() && def.get<bool>();
            s.choices = sj.value("choices", std::vector<std::string>{});
            s.text = def.is_string() ? def.get<std::string>() : (s.choices.empty() ? std::string() : s.choices.front());
            s.min = sj.value("min", 0.0);
            s.max = sj.value("max", 0.0);
            s.step = sj.value("step", 1.0);
            s.format = sj.value("format", std::string(s.step < 1.0 ? "%.2f" : "%.0f"));
            if (!s.id.empty()) p.settings.push_back(std::move(s));
        }
    } catch (const std::exception& e) {
        p.id = platform::toUtf8(dir.filename());
        p.name = p.id;
        p.error = std::string("plugin.json: ") + e.what();
    }
    if (p.id.empty()) p.error = "plugin.json: missing \"id\"";
    // A user plugin with a bundled plugin's id replaces it.
    if (Plugin* existing = findPlugin(p.id)) {
        if (existing->L) closeSandbox(existing->L);
        std::erase_if(types_, [&](const PluginObjectType& t) { return t.pluginId == p.id; });
        std::erase_if(commands_, [&](const PluginCommand& c) { return c.pluginId == p.id; });
        std::erase_if(plugins_, [&](const Plugin& q) { return q.id == p.id; });
    }
    const auto& disabled = settings().disabledPlugins;
    p.enabled = std::find(disabled.begin(), disabled.end(), p.id) == disabled.end();
    plugins_.push_back(std::move(p));
    Plugin& plugin = plugins_.back();
    if (!plugin.error.empty()) return;

    plugin.L = newSandbox(plugin.id);
    lua_State* L = plugin.L;
    lua_pushstring(L, platform::toUtf8(dir).c_str()); // where bemotion.include reads
    lua_setfield(L, LUA_REGISTRYINDEX, kPluginDirKey);
    const std::string code = withoutBom(readFile(dir / platform::fromUtf8(mainFile)));
    std::vector<PluginObjectType> registered;
    std::vector<PluginCommand> registeredCommands;
    g_registering = &registered;
    g_registeringCommands = &registeredCommands;
    g_budget = kLoadBudget;
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kHookEvery);
    const std::string chunk = "@" + plugin.id + "/" + mainFile;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK || sandboxCall(L, 0, 0) != LUA_OK) {
        plugin.error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error loading the script";
        lua_pop(L, 1);
    }
    g_registering = nullptr;
    g_registeringCommands = nullptr;
    if (!plugin.error.empty()) {
        closeSandbox(L);
        plugin.L = nullptr;
        return;
    }
    for (auto& t : registered) types_.push_back(std::move(t));
    for (auto& c : registeredCommands) commands_.push_back(std::move(c));
}

bool Plugin::may(const std::string& permission) const {
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}

std::vector<const PluginCommand*> PluginHost::commands() const {
    std::vector<const PluginCommand*> out;
    for (const auto& c : commands_)
        for (const auto& p : plugins_)
            if (p.id == c.pluginId && p.enabled && p.L) out.push_back(&c);
    return out;
}

namespace {

// Calls the function on top of the stack with `doc`, on a copy of `project`.
bool runWithDoc(lua_State* L, Project& project, double time, const std::vector<uint64_t>& selection,
                const OpEnvironment& env, bool mayImport, std::string& error, bool pressed = false,
                CommandOutcome* outcome = nullptr) {
    CommandRun run;
    run.work = project;
    run.env = &env;
    run.mayImport = mayImport;
    run.time = time;
    g_run = &run;
    pushDoc(L, time, selection, pressed);
    g_budget = kCommandBudget;
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kHookEvery);
    const bool ok = sandboxCall(L, 1, 0) == LUA_OK;
    g_run = nullptr;
    if (!ok) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "the command failed";
        lua_pop(L, 1);
        return false;
    }
    if (outcome) {
        outcome->changed = run.ops > 0;
        outcome->play = run.play;
    }
    if (run.ops > 0) project = std::move(run.work);
    return true;
}

} // namespace

bool PluginHost::runCommand(const std::string& commandId, Project& project, double time, const std::vector<uint64_t>& selection,
                            const OpEnvironment& env, std::string& error, bool pressed, CommandOutcome* outcome) {
    const auto it = std::find_if(commands_.begin(), commands_.end(), [&](const PluginCommand& c) { return c.id == commandId; });
    Plugin* plugin = it != commands_.end() ? findPlugin(it->pluginId) : nullptr;
    if (!plugin || !plugin->L || !plugin->enabled) {
        error = "the command " + commandId + " is not available";
        return false;
    }
    lua_State* L = plugin->L;
    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->runRef);
    const bool ok = runWithDoc(L, project, time, selection, env, plugin->may("media"), error, pressed, outcome);
    if (!ok) {
        plugin->log.push_back(it->name + ": " + error);
        if (plugin->log.size() > kMaxLogLines) plugin->log.erase(plugin->log.begin());
    }
    lua_settop(L, top);
    return ok;
}

bool PluginHost::runScript(const std::string& source, const std::string& chunkName, Project& project, double time,
                           const OpEnvironment& env, std::vector<std::string>& log, std::string& error,
                           const std::vector<uint64_t>& selection) {
    const std::string code = withoutBom(source);
    // A throwaway sandbox with its own (fake) plugin entry, so print() has somewhere to go.
    const std::string id = "script";
    Plugin p;
    p.id = id;
    p.name = chunkName;
    p.permissions = {"edit", "media"};
    PluginHost& host = get();
    host.plugins_.push_back(std::move(p));
    lua_State* L = newSandbox(id);
    host.plugins_.back().L = L;
    bool ok = false;
    g_budget = kCommandBudget;
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kHookEvery);
    // The script's body is the command: `doc` comes as its argument (local doc = ...).
    if (luaL_loadbufferx(L, code.data(), code.size(), ("@" + chunkName).c_str(), "t") != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
    } else {
        ok = runWithDoc(L, project, time, selection, env, true, error);
    }
    log = host.plugins_.back().log;
    closeSandbox(L);
    host.plugins_.pop_back();
    return ok;
}

std::vector<const PluginObjectType*> PluginHost::objectTypes() const {
    std::vector<const PluginObjectType*> out;
    for (const auto& t : types_)
        for (const auto& p : plugins_)
            if (p.id == t.pluginId && p.enabled && p.L) out.push_back(&t);
    return out;
}

const PluginObjectType* PluginHost::findType(const std::string& id) const {
    for (const auto& t : types_)
        if (t.id == id) return &t;
    return nullptr;
}

Plugin* PluginHost::findPlugin(const std::string& id) {
    for (auto& p : plugins_)
        if (p.id == id) return &p;
    return nullptr;
}

void PluginHost::processPendingReload() {
    // A running command's Lua state owns the take's start time and stop action.
    if (!reloadRequested_ || media::Recorder::get().recording()) return;
    reloadRequested_ = false;
    load();
    emit ui::AppSignals::get().pluginsChanged();
}

void PluginHost::setEnabled(const std::string& pluginId, bool enabled) {
    if (Plugin* p = findPlugin(pluginId)) {
        if (!enabled && p->may("microphone") && media::Recorder::get().recording()) return;
        p->enabled = enabled;
    }
    auto& disabled = settings().disabledPlugins;
    std::erase(disabled, pluginId);
    if (!enabled) disabled.push_back(pluginId);
    saveSettings();
}

bool PluginHost::install(const fs::path& manifest, std::string* error) {
    try {
        const json m = json::parse(readFile(manifest));
        const std::string id = m.value("id", std::string());
        if (id.empty() || id.find_first_of("/\\:.") != std::string::npos) throw std::runtime_error("plugin.json needs a simple \"id\"");
        copyDirectory(manifest.parent_path(), userDir() / platform::fromUtf8(id));
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    requestReload();
    return true;
}

bool PluginHost::uninstall(const std::string& pluginId, std::string* error) {
    const Plugin* plugin = findPlugin(pluginId);
    if (!plugin || plugin->builtin) {
        if (error) *error = "only installed plugins can be removed";
        return false;
    }
    if (media::Recorder::get().recording()) {
        if (error) *error = "stop recording before removing this plugin";
        return false;
    }
    std::error_code ec;
    const auto status = fs::symlink_status(plugin->dir, ec);
    if (ec || fs::is_symlink(status)) {
        if (error) *error = "linked plugin folders cannot be removed here";
        return false;
    }
    const fs::path folder = fs::canonical(plugin->dir, ec);
    if (ec) {
        if (error) *error = "the plugin folder could not be found";
        return false;
    }
    const fs::path root = fs::canonical(userDir(), ec);
    if (ec || folder.parent_path() != root || folder != root / plugin->dir.filename()) {
        if (error) *error = "the plugin is outside the installed plugins folder";
        return false;
    }
    if (!platform::moveToTrash(folder, error)) return false;
    std::erase(settings().disabledPlugins, pluginId);
    saveSettings();
    requestReload();
    return true;
}

Layer PluginHost::createLayer(const PluginObjectType& type, Vec2 position, double duration) const {
    Layer l = makePluginLayer(type.name, type.prototype, position, duration);
    for (const PluginIntro& intro : type.intro)
        if (PluginParam* p = l.plugin.find(intro.param)) {
            p->number.setKey(0.0, intro.from, Easing::EaseOut);
            p->number.setKey(std::min(intro.duration, duration), p->defaultNumber);
        }
    return l;
}

void PluginHost::reconcile(Layer& layer) const {
    if (layer.type != LayerType::Plugin) return;
    const PluginObjectType* type = findType(layer.plugin.objectType);
    if (!type) return;
    PluginProps& props = layer.plugin;
    std::vector<PluginParam> params;
    for (const PluginParam& schema : type->prototype.params) {
        PluginParam* have = props.find(schema.id);
        if (!have || have->kind != schema.kind) {
            params.push_back(schema);
            continue;
        }
        PluginParam p = std::move(*have);
        p.label = schema.label;
        p.group = schema.group;
        p.min = schema.min;
        p.max = schema.max;
        p.speed = schema.speed;
        p.format = schema.format;
        p.defaultNumber = schema.defaultNumber;
        p.defaultColor = schema.defaultColor;
        p.accept = schema.accept;
        if (p.kind == PluginParamKind::Choice) {
            const std::string current = p.choice < static_cast<int>(p.choices.size()) ? p.choices[p.choice] : "";
            p.choices = schema.choices;
            const auto it = std::find(p.choices.begin(), p.choices.end(), current);
            p.choice = it == p.choices.end() ? schema.choice : static_cast<int>(it - p.choices.begin());
        }
        params.push_back(std::move(p));
    }
    props.params = std::move(params);
    props.defaultSize = type->prototype.defaultSize;
    if (type->prototype.hasRows && !props.hasRows) props.rows = type->prototype.rows;
    props.hasRows = type->prototype.hasRows;
    if (type->prototype.hasTable && !props.hasTable) props.table = type->prototype.table;
    props.hasTable = type->prototype.hasTable;
}

void PluginHost::reconcile(Composition& comp) const {
    for (Layer& l : comp.layers) reconcile(l);
}

std::vector<std::string> PluginHost::rowLabels(const Layer& layer, std::vector<int>* unknownRows) {
    std::vector<std::string> names;
    if (unknownRows) unknownRows->clear();
    const PluginObjectType* type = findType(layer.plugin.objectType);
    Plugin* plugin = type ? findPlugin(type->pluginId) : nullptr;
    if (!type || type->labelsRef < 0 || !plugin || !plugin->L || !plugin->enabled) return names;
    // row_labels(o) returns { "name", ... } and, optionally, { row index, ... } (1-based) not found.
    lua_State* L = plugin->L;
    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, type->labelsRef);
    pushParams(L, layer, layer.inPoint);
    g_budget = kCommandBudget;
    if (sandboxCall(L, 1, 2) == LUA_OK) {
        if (lua_istable(L, -2)) {
            const lua_Integer n = luaL_len(L, -2);
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, -2, i);
                if (lua_type(L, -1) == LUA_TSTRING) names.emplace_back(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        if (unknownRows && lua_istable(L, -1)) {
            const lua_Integer n = luaL_len(L, -1);
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, i);
                if (lua_isinteger(L, -1)) unknownRows->push_back(static_cast<int>(lua_tointeger(L, -1)) - 1);
                lua_pop(L, 1);
            }
        }
    } else {
        platform::log(std::string("plugin row_labels failed: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
    }
    lua_settop(L, top);
    return names;
}

bool PluginHost::importCsv(Layer& layer, const std::string& csv, std::string& error) {
    PluginProps& props = layer.plugin;
    if (!props.hasRows && !props.hasTable) {
        error = "this object has no data";
        return false;
    }
    const Cells cells = parseCsv(csv);
    if (cells.empty()) {
        error = "the file has no data";
        return false;
    }
    const PluginObjectType* type = findType(props.objectType);
    Plugin* plugin = type ? findPlugin(type->pluginId) : nullptr;
    if (!type || type->importRef < 0 || !plugin || !plugin->L || !plugin->enabled) { // the standard reading
        if (props.hasRows) {
            std::vector<DataRow> rows = rowsFromCells(cells);
            if (rows.empty()) {
                error = "no line with a label and a number was found";
                return false;
            }
            props.rows = std::move(rows);
        } else {
            props.table = tableFromCells(cells);
        }
        return true;
    }
    // import_csv(cells, o) returns { rows = ..., table = ..., params = { id = value } } (each optional).
    lua_State* L = plugin->L;
    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, type->importRef);
    pushCells(L, cells);
    pushParams(L, layer, layer.inPoint);
    g_budget = kCommandBudget;
    bool ok = sandboxCall(L, 2, 1) == LUA_OK;
    if (!ok) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "import_csv failed";
    } else if (!lua_istable(L, -1)) {
        error = "import_csv must return a table";
        ok = false;
    } else {
        const int result = lua_gettop(L);
        lua_getfield(L, result, "rows");
        if (lua_istable(L, -1) && props.hasRows) props.rows = readRows(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, result, "table");
        if (lua_istable(L, -1) && props.hasTable) props.table = readTable(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, result, "params");
        if (lua_istable(L, -1))
            for (PluginParam& p : props.params) {
                lua_getfield(L, -1, p.id.c_str());
                switch (p.kind) {
                case PluginParamKind::Number:
                    if (lua_isnumber(L, -1)) p.number.set(layer.inPoint, static_cast<float>(lua_tonumber(L, -1)));
                    break;
                case PluginParamKind::Text:
                    if (lua_type(L, -1) == LUA_TSTRING) p.text = lua_tostring(L, -1);
                    break;
                case PluginParamKind::Toggle:
                    if (lua_isboolean(L, -1)) p.toggle = lua_toboolean(L, -1);
                    break;
                case PluginParamKind::Choice:
                    if (lua_type(L, -1) == LUA_TSTRING) {
                        const auto it = std::find(p.choices.begin(), p.choices.end(), lua_tostring(L, -1));
                        if (it != p.choices.end()) p.choice = static_cast<int>(it - p.choices.begin());
                    }
                    break;
                case PluginParamKind::Color:
                    if (!lua_isnil(L, -1)) p.color.set(layer.inPoint, readColor(L, -1, p.color.valueAt(layer.inPoint)));
                    break;
                case PluginParamKind::File: break; // only the user picks files
                }
                lua_pop(L, 1);
            }
        lua_pop(L, 1);
    }
    if (!ok) plugin->drawError = error;
    lua_settop(L, top);
    return ok;
}

void PluginHost::draw(const Layer& layer, double t, render::PluginCanvas& c) {
    const PluginObjectType* type = findType(layer.plugin.objectType);
    Plugin* plugin = type ? findPlugin(type->pluginId) : nullptr;
    if (!type || !plugin || !plugin->L) {
        drawPlaceholder(c, layer, t, "Missing plugin: " + layer.plugin.objectType);
        return;
    }
    if (!plugin->enabled) {
        drawPlaceholder(c, layer, t, "Plugin \"" + plugin->name + "\" is disabled");
        return;
    }
    lua_State* L = plugin->L;
    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, type->drawRef);
    pushParams(L, layer, t);
    lua_getfield(L, LUA_REGISTRYINDEX, "bemotion.gfx");
    g_canvas = &c;
    g_budget = kDrawBudget;
    if (sandboxCall(L, 2, 0) != LUA_OK) {
        plugin->drawError = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
        drawPlaceholder(c, layer, t, plugin->drawError);
    } else {
        plugin->drawError.clear();
    }
    g_canvas = nullptr;
    lua_settop(L, top);
}

} // namespace bemotion
