// What plugins call: the g drawing API, the bemotion table, doc for commands, data, and the
// sandbox they get (app/PluginLua.h).

#include "app/PluginLua.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "core/Data.h"
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

namespace bemotion::pluginlua {

// --- values ------------------------------------------------------------------

bool parseHex(const char* s, Color& out) {
    if (!s || s[0] != '#') return false;
    const size_t n = std::strlen(s);
    if (n != 7 && n != 9) return false;
    unsigned v = 0;
    for (size_t i = 1; i < n; ++i) {
        const char c = s[i];
        const int d = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
        if (d < 0) return false;
        v = v * 16 + static_cast<unsigned>(d);
    }
    if (n == 7)
        out = Color::fromHex(v);
    else
        out = Color::fromHex(v >> 8, static_cast<float>(v & 0xFF) / 255.0f);
    return true;
}

float fieldNumber(lua_State* L, int idx, const char* key, int arrayIndex, float fallback) {
    lua_getfield(L, idx, key);
    if (lua_isnil(L, -1) && arrayIndex > 0) {
        lua_pop(L, 1);
        lua_rawgeti(L, idx, arrayIndex);
    }
    const float v = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : fallback;
    lua_pop(L, 1);
    return v;
}

// A color is "#RRGGBB[AA]" or a table {r, g, b[, a]} (named or positional, 0..1).
Color readColor(lua_State* L, int idx, Color fallback) {
    idx = lua_absindex(L, idx);
    if (lua_type(L, idx) == LUA_TSTRING) {
        Color c = fallback;
        parseHex(lua_tostring(L, idx), c);
        return c;
    }
    if (!lua_istable(L, idx)) return fallback;
    return {fieldNumber(L, idx, "r", 1, fallback.r), fieldNumber(L, idx, "g", 2, fallback.g),
            fieldNumber(L, idx, "b", 3, fallback.b), fieldNumber(L, idx, "a", 4, 1.0f)};
}

void pushColor(lua_State* L, Color c) {
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, c.r);
    lua_setfield(L, -2, "r");
    lua_pushnumber(L, c.g);
    lua_setfield(L, -2, "g");
    lua_pushnumber(L, c.b);
    lua_setfield(L, -2, "b");
    lua_pushnumber(L, c.a);
    lua_setfield(L, -2, "a");
}

std::string fieldString(lua_State* L, int idx, const char* key, const std::string& fallback) {
    lua_getfield(L, idx, key);
    std::string v = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : fallback;
    lua_pop(L, 1);
    return v;
}

Vec2 vec(lua_State* L, int x, int y) {
    return {static_cast<float>(luaL_checknumber(L, x)), static_cast<float>(luaL_checknumber(L, y))};
}

// Flat {x1, y1, x2, y2, ...} array.
std::vector<Vec2> readPoints(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    std::vector<Vec2> pts;
    const lua_Integer n = luaL_len(L, idx);
    for (lua_Integer i = 1; i + 1 <= n; i += 2) {
        lua_rawgeti(L, idx, i);
        lua_rawgeti(L, idx, i + 1);
        pts.push_back({static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1))});
        lua_pop(L, 2);
    }
    return pts;
}

render::PluginCanvas& canvas(lua_State* L) {
    if (!g_canvas) luaL_error(L, "drawing is only possible inside draw()");
    return *g_canvas;
}

// --- graphics API (the `g` argument of draw) ------------------------------------

int gRect(lua_State* L) {
    const Vec2 p = vec(L, 1, 2), s = vec(L, 3, 4);
    canvas(L).rect(p, p + s, readColor(L, 5, {}), static_cast<float>(luaL_optnumber(L, 6, 0)));
    return 0;
}
int gRectOutline(lua_State* L) {
    const Vec2 p = vec(L, 1, 2), s = vec(L, 3, 4);
    canvas(L).rectOutline(p, p + s, readColor(L, 5, {}), static_cast<float>(luaL_optnumber(L, 6, 1)),
                          static_cast<float>(luaL_optnumber(L, 7, 0)));
    return 0;
}
int gCircle(lua_State* L) {
    canvas(L).circle(vec(L, 1, 2), static_cast<float>(luaL_checknumber(L, 3)), readColor(L, 4, {}));
    return 0;
}
int gLine(lua_State* L) {
    canvas(L).line(vec(L, 1, 2), vec(L, 3, 4), static_cast<float>(luaL_checknumber(L, 5)), readColor(L, 6, {}));
    return 0;
}
int gPolyline(lua_State* L) {
    canvas(L).polyline(readPoints(L, 1), static_cast<float>(luaL_checknumber(L, 2)), readColor(L, 3, {}),
                       lua_toboolean(L, 4));
    return 0;
}
int gClip(lua_State* L) {
    const Vec2 a = vec(L, 1, 2);
    canvas(L).clip(a, a + Vec2{static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4))});
    return 0;
}
int gUnclip(lua_State* L) {
    canvas(L).unclip();
    return 0;
}
int gPolygon(lua_State* L) {
    canvas(L).polygon(readPoints(L, 1), readColor(L, 2, {}));
    return 0;
}
int gArc(lua_State* L) {
    canvas(L).arc(vec(L, 1, 2), static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4)),
                  static_cast<float>(luaL_checknumber(L, 5)), static_cast<float>(luaL_checknumber(L, 6)),
                  readColor(L, 7, {}));
    return 0;
}
render::TextAnchor anchorArg(lua_State* L, int idx) {
    const char* a = luaL_optstring(L, idx, "left");
    return std::strcmp(a, "center") == 0  ? render::TextAnchor::Center
           : std::strcmp(a, "right") == 0 ? render::TextAnchor::Right
                                          : render::TextAnchor::Left;
}
int gText(lua_State* L) {
    canvas(L).text(luaL_checkstring(L, 1), vec(L, 2, 3), static_cast<float>(luaL_checknumber(L, 4)),
                   readColor(L, 5, {}), anchorArg(L, 6), lua_toboolean(L, 7));
    return 0;
}
int gMeasure(lua_State* L) {
    lua_pushnumber(
        L, canvas(L).measure(luaL_checkstring(L, 1), static_cast<float>(luaL_checknumber(L, 2)), lua_toboolean(L, 3)));
    return 1;
}

// --- helpers (the `bemotion` table) --------------------------------------------

int bmEase(lua_State* L) {
    const Easing e = easingFromId(luaL_checkstring(L, 1));
    lua_pushnumber(L, applyEasing(e, static_cast<float>(luaL_checknumber(L, 2))));
    return 1;
}
int bmColor(lua_State* L) {
    pushColor(L, readColor(L, 1, {}));
    return 1;
}
int bmMix(lua_State* L) {
    const Color a = readColor(L, 1, {}), b = readColor(L, 2, {});
    pushColor(L, lerp(a, b, static_cast<float>(luaL_checknumber(L, 3))));
    return 1;
}
int bmWithAlpha(lua_State* L) {
    Color c = readColor(L, 1, {});
    c.a *= static_cast<float>(luaL_checknumber(L, 2));
    pushColor(L, c);
    return 1;
}
// format(value, decimals = 0): thousands separators, e.g. 12,345.6
int bmFormat(lua_State* L) {
    const double v = luaL_checknumber(L, 1);
    const int decimals = std::clamp(static_cast<int>(luaL_optinteger(L, 2, 0)), 0, 6);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, std::fabs(v));
    std::string s = buf;
    const size_t dot = s.find('.');
    for (int i = static_cast<int>(dot == std::string::npos ? s.size() : dot) - 3; i > 0; i -= 3)
        s.insert(i, ",");
    if (v < 0 && std::fabs(v) >= std::pow(10.0, -decimals) * 0.5) s = "-" + s;
    lua_pushstring(L, s.c_str());
    return 1;
}

Plugin* pluginOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "bemotion.plugin");
    const std::string id = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return PluginHost::get().findPlugin(id);
}

int bmPrint(lua_State* L) {
    std::string line;
    for (int i = 1; i <= lua_gettop(L); ++i) {
        if (i > 1) line += "  ";
        line += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    if (Plugin* p = pluginOf(L)) {
        p->log.push_back(line);
        if (p->log.size() > kMaxLogLines) p->log.erase(p->log.begin());
    }
    return 0;
}

// Object types and commands registered while main.lua runs, appended by the host afterwards.
std::vector<PluginObjectType>* g_registering = nullptr;
std::vector<PluginCommand>* g_registeringCommands = nullptr;

// --- Lua <-> JSON (operations travel as JSON) ----------------------------------------------

json toJson(lua_State* L, int idx, int depth) {
    idx = lua_absindex(L, idx);
    if (depth > 32) luaL_error(L, "table nested too deeply");
    switch (lua_type(L, idx)) {
    case LUA_TNIL: return nullptr;
    case LUA_TBOOLEAN: return static_cast<bool>(lua_toboolean(L, idx));
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) return static_cast<int64_t>(lua_tointeger(L, idx));
        return lua_tonumber(L, idx);
    case LUA_TSTRING: return std::string(lua_tostring(L, idx));
    case LUA_TTABLE: {
        // A sequence 1..n is an array; anything else an object (an empty table is an object).
        const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, idx));
        bool array = n > 0;
        if (array) {
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                lua_pop(L, 1);
                if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1 || lua_tointeger(L, -1) > n) array = false;
            }
        }
        if (array) {
            json a = json::array();
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, idx, i);
                a.push_back(toJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return a;
        }
        json o = json::object();
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            if (lua_type(L, -2) == LUA_TSTRING) o[lua_tostring(L, -2)] = toJson(L, -1, depth + 1);
            lua_pop(L, 1);
        }
        return o;
    }
    default: luaL_error(L, "operations can only hold numbers, strings, booleans and tables"); return nullptr;
    }
}

void pushJson(lua_State* L, const json& j) {
    switch (j.type()) {
    case json::value_t::null: lua_pushnil(L); break;
    case json::value_t::boolean: lua_pushboolean(L, j.get<bool>()); break;
    case json::value_t::number_integer:
    case json::value_t::number_unsigned: lua_pushinteger(L, j.get<lua_Integer>()); break;
    case json::value_t::number_float: lua_pushnumber(L, j.get<double>()); break;
    case json::value_t::string: lua_pushstring(L, j.get<std::string>().c_str()); break;
    case json::value_t::array:
        lua_createtable(L, static_cast<int>(j.size()), 0);
        for (size_t i = 0; i < j.size(); ++i) {
            pushJson(L, j[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        break;
    default:
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (const auto& [k, v] : j.items()) {
            pushJson(L, v);
            lua_setfield(L, -2, k.c_str());
        }
        break;
    }
}

// --- the document a command edits ---------------------------------------------------------

CommandRun* g_run = nullptr;

// doc.apply(op | {op, op, ...}): runs operations now (later calls see their effect) and
// returns their results; a failing operation raises an error that ends the command.
int docApply(lua_State* L) {
    if (!g_run) return luaL_error(L, "doc can only be used while the command runs");
    luaL_checktype(L, 1, LUA_TTABLE);
    const json ops = toJson(L, 1);
    const bool single = ops.is_object();
    for (const json& op : single ? json::array({ops}) : ops)
        if (op.is_object() && op.value("op", std::string()) == "media.import" && !g_run->mayImport)
            return luaL_error(L, "media.import needs the \"media\" permission in plugin.json");
    const OpsResult r = applyOps(g_run->work, ops, *g_run->env);
    if (!r.ok) return luaL_error(L, "%s", r.error.c_str());
    g_run->ops += single ? 1 : static_cast<int>(ops.size());
    pushJson(L, single ? r.results[0] : r.results);
    return 1;
}

// Shortcuts over doc.apply for the most common reads and writes.
int docQuery(lua_State* L, json op) {
    const OpsResult r = applyOps(g_run->work, op, *g_run->env);
    if (!r.ok) return luaL_error(L, "%s", r.error.c_str());
    pushJson(L, r.results[0]);
    return 1;
}
json layerRef(lua_State* L, int idx) {
    if (lua_isinteger(L, idx)) return static_cast<int64_t>(lua_tointeger(L, idx));
    return std::string(luaL_checkstring(L, idx));
}
int docLayers(lua_State* L) { return g_run ? docQuery(L, {{"op", "query.layers"}}) : 0; }
int docComposition(lua_State* L) { return g_run ? docQuery(L, {{"op", "query.composition"}}) : 0; }
int docLayer(lua_State* L) {
    if (!g_run) return 0;
    json op = {{"op", "query.layer"}, {"layer", layerRef(L, 1)}};
    if (lua_isnumber(L, 2)) op["time"] = lua_tonumber(L, 2);
    return docQuery(L, op);
}

// doc.expression(layer, prop[, expression[, time]]): works an expression out without setting
// it (query.expression; default: the property's own, at doc.time). The value, or nil and why not.
int docExpression(lua_State* L) {
    if (!g_run) return 0;
    json op = {{"op", "query.expression"}, {"layer", layerRef(L, 1)}, {"prop", luaL_checkstring(L, 2)}};
    if (lua_type(L, 3) == LUA_TSTRING) op["expression"] = lua_tostring(L, 3);
    op["time"] = lua_isnumber(L, 4) ? lua_tonumber(L, 4) : g_run->time;
    const OpsResult r = applyOps(g_run->work, op, *g_run->env);
    if (!r.ok) return luaL_error(L, "%s", r.error.c_str());
    if (!r.results[0].value("ok", false)) {
        lua_pushnil(L);
        lua_pushstring(L, r.results[0].value("error", std::string()).c_str());
        return 2;
    }
    pushJson(L, r.results[0]["value"]);
    return 1;
}

// doc.play(true | false): start or stop playback when the command ends (e.g. while recording).
int docPlay(lua_State* L) {
    if (!g_run) return luaL_error(L, "doc can only be used while the command runs");
    g_run->play = lua_toboolean(L, 1) ? 1 : 0;
    return 0;
}

void pushDoc(lua_State* L, double time, const std::vector<uint64_t>& selection, bool pressed) {
    const luaL_Reg fns[] = {
        {"apply", docApply},           {"layers", docLayers}, {"layer", docLayer}, {"composition", docComposition},
        {"expression", docExpression}, {"play", docPlay},     {nullptr, nullptr}};
    luaL_newlib(L, fns);
    lua_pushboolean(L, pressed);
    lua_setfield(L, -2, "pressed");
    lua_pushnumber(L, time);
    lua_setfield(L, -2, "time");
    lua_createtable(L, static_cast<int>(selection.size()), 0);
    for (size_t i = 0; i < selection.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(selection[i]));
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "selection");
}

int bmRegisterCommand(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!g_registeringCommands) return luaL_error(L, "register_command can only be called while the plugin loads");
    Plugin* plugin = pluginOf(L);
    if (!plugin || !plugin->may("edit"))
        return luaL_error(L, "register_command: add \"permissions\": [\"edit\"] to plugin.json to change projects");
    PluginCommand c;
    const std::string id = fieldString(L, 1, "id", "");
    if (id.empty()) return luaL_error(L, "register_command: missing id");
    c.pluginId = plugin->id;
    c.id = c.pluginId + "." + id;
    c.name = fieldString(L, 1, "name", id);
    c.description = fieldString(L, 1, "description", "");
    lua_getfield(L, 1, "button"); // { place = "timeline", icon = "mic", toggle = true }
    if (lua_istable(L, -1)) {
        c.place = fieldString(L, -1, "place", "timeline");
        c.icon = fieldString(L, -1, "icon", "plugin");
        lua_getfield(L, -1, "toggle");
        c.toggle = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "run");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "register_command '%s': run must be a function", id.c_str());
    c.runRef = luaL_ref(L, LUA_REGISTRYINDEX);
    g_registeringCommands->push_back(std::move(c));
    return 0;
}

PluginParamKind kindFromName(const std::string& s) {
    if (s == "color") return PluginParamKind::Color;
    if (s == "toggle" || s == "bool") return PluginParamKind::Toggle;
    if (s == "choice") return PluginParamKind::Choice;
    if (s == "text" || s == "string") return PluginParamKind::Text;
    if (s == "file") return PluginParamKind::File;
    return PluginParamKind::Number;
}

// Chart rows: { {label, value, color} | {label=, value=, color=}, ... } at `idx`.
std::vector<DataRow> readRows(lua_State* L, int idx) {
    std::vector<DataRow> rows;
    idx = lua_absindex(L, idx);
    for (lua_Integer i = 1; i <= luaL_len(L, idx); ++i) {
        lua_rawgeti(L, idx, i);
        const int e = lua_gettop(L);
        if (lua_istable(L, e)) {
            DataRow r;
            lua_getfield(L, e, "label");
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_rawgeti(L, e, 1);
            }
            r.label = lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
            r.value = fieldNumber(L, e, "value", 2, 0);
            lua_getfield(L, e, "color");
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_rawgeti(L, e, 3);
            }
            r.color = readColor(L, -1, labelColor(static_cast<int>(rows.size())));
            lua_pop(L, 1);
            rows.push_back(std::move(r));
        }
        lua_pop(L, 1);
    }
    return rows;
}

// A table: { header = {...}, rows = { {...}, ... } } at `idx` (cells as text).
DataTable readTable(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    Cells cells;
    auto readLine = [&](int at) {
        std::vector<std::string> line;
        for (lua_Integer c = 1; c <= luaL_len(L, at); ++c) {
            lua_rawgeti(L, at, c);
            line.emplace_back(lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER ? lua_tostring(L, -1)
                                                                                               : "");
            lua_pop(L, 1);
        }
        return line;
    };
    lua_getfield(L, idx, "header");
    cells.push_back(lua_istable(L, -1) ? readLine(lua_gettop(L)) : std::vector<std::string>{});
    lua_pop(L, 1);
    lua_getfield(L, idx, "rows");
    if (lua_istable(L, -1)) {
        const int list = lua_gettop(L);
        for (lua_Integer i = 1; i <= luaL_len(L, list); ++i) {
            lua_rawgeti(L, list, i);
            if (lua_istable(L, -1)) cells.push_back(readLine(lua_gettop(L)));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return tableFromCells(cells);
}

void pushCells(lua_State* L, const Cells& cells) {
    lua_createtable(L, static_cast<int>(cells.size()), 0);
    for (size_t i = 0; i < cells.size(); ++i) {
        lua_createtable(L, static_cast<int>(cells[i].size()), 0);
        for (size_t c = 0; c < cells[i].size(); ++c) {
            lua_pushstring(L, cells[i][c].c_str());
            lua_rawseti(L, -2, static_cast<lua_Integer>(c + 1));
        }
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
}

void pushTable(lua_State* L, const DataTable& t) {
    lua_createtable(L, 0, 2);
    pushCells(L, {t.header});
    lua_rawgeti(L, -1, 1);
    lua_setfield(L, -3, "header");
    lua_pop(L, 1);
    pushCells(L, t.rows);
    lua_setfield(L, -2, "rows");
}

// bemotion.cached(key, fn): fn() once per key, with the command budget (parsing a file, building
// a mesh); later calls return the same value. The plugin's cache empties when it grows large.
// bemotion.include("maps/world.lua"): runs a Lua file of the plugin's own folder and returns
// what it returns (once: later calls get the same value). Only files inside that folder.

int bmInclude(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    if (name.empty() || name.find("..") != std::string::npos || name.find(':') != std::string::npos ||
        name.front() == '/' || name.front() == '\\')
        return luaL_error(L, "include: \"%s\" is not a file of this plugin", name.c_str());
    lua_getfield(L, LUA_REGISTRYINDEX, kIncludedKey);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, name.c_str());
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 2);
    } else {
        lua_pop(L, 1);
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kPluginDirKey);
    const char* dir = lua_tostring(L, -1);
    if (!dir) return luaL_error(L, "include: only plugins can include files");
    const std::filesystem::path file = platform::fromUtf8(dir) / platform::fromUtf8(name);
    lua_pop(L, 1);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return luaL_error(L, "include: no file \"%s\" in this plugin", name.c_str());
    const std::string code = withoutBom(readFile(file));
    if (luaL_loadbufferx(L, code.data(), code.size(), ("@" + name).c_str(), "t") != LUA_OK) return lua_error(L);
    lua_call(L, 0, 1);
    // Kept, so a big data file is read once.
    lua_getfield(L, LUA_REGISTRYINDEX, kIncludedKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kIncludedKey);
    }
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, name.c_str());
    lua_pop(L, 1);
    return 1;
}

int bmCached(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, "bemotion.cache");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.cache");
    }
    const int cache = lua_gettop(L);
    lua_getfield(L, cache, key);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    const int saved = g_budget;
    g_budget = kCommandBudget;
    lua_pushvalue(L, 2);
    const int status = sandboxCall(L, 0, 1);
    g_budget = saved;
    if (status != LUA_OK) return lua_error(L);
    if (!lua_isnil(L, -1)) {
        lua_getfield(L, cache, "__count");
        const lua_Integer count = lua_tointeger(L, -1) + 1;
        lua_pop(L, 1);
        if (count > 24) { // a fresh cache (old models are let go)
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.cache");
            lua_replace(L, cache);
        }
        lua_pushinteger(L, count > 24 ? 1 : count);
        lua_setfield(L, cache, "__count");
        lua_pushvalue(L, -1);
        lua_setfield(L, cache, key);
    }
    return 1;
}

// bemotion.json(text) -> the value (objects as tables, arrays 1-based); errors on bad JSON.
int bmJson(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    json j;
    try {
        j = json::parse(std::string(s, n));
    } catch (const std::exception& e) {
        return luaL_error(L, "bad JSON: %s", e.what());
    }
    pushJson(L, j);
    return 1;
}

// bemotion.base64(text) -> the decoded bytes (data URIs inside .gltf files).
int bmBase64(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    std::string out;
    out.reserve(n * 3 / 4);
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        int v = c >= 'A' && c <= 'Z'   ? c - 'A'
                : c >= 'a' && c <= 'z' ? c - 'a' + 26
                : c >= '0' && c <= '9' ? c - '0' + 52
                : c == '+' || c == '-' ? 62
                : c == '/' || c == '_' ? 63
                                       : -1;
        if (v < 0) continue; // padding, line breaks
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int bmParseCsv(lua_State* L) {
    pushCells(L, parseCsv(luaL_checkstring(L, 1)));
    return 1;
}

// bemotion.number(text) -> number or nil: "1.234,5", "R$ 12", "45%" (core/Data.h).
// bemotion.setting(id): the user's value of one of the plugin's settings (plugin.json
// "settings"), or its default; nil for an id it did not declare.
int bmSetting(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    const Plugin* plugin = pluginOf(L);
    if (!plugin) return lua_pushnil(L), 1;
    const auto it = std::find_if(plugin->settings.begin(), plugin->settings.end(),
                                 [&](const PluginSetting& s) { return s.id == id; });
    if (it == plugin->settings.end()) return lua_pushnil(L), 1;
    json value = nullptr;
    if (const auto p = settings().pluginSettings.find(plugin->id); p != settings().pluginSettings.end())
        if (const auto v = p->second.find(id); v != p->second.end()) value = json::parse(v->second, nullptr, false);
    switch (it->kind) {
    case PluginSetting::Kind::Number: lua_pushnumber(L, value.is_number() ? value.get<double>() : it->number); break;
    case PluginSetting::Kind::Toggle: lua_pushboolean(L, value.is_boolean() ? value.get<bool>() : it->toggle); break;
    case PluginSetting::Kind::Choice: {
        const std::string chosen = value.is_string() ? value.get<std::string>() : it->text;
        const bool known = std::find(it->choices.begin(), it->choices.end(), chosen) != it->choices.end();
        lua_pushstring(L, (known ? chosen : it->text).c_str());
        break;
    }
    case PluginSetting::Kind::Text:
        lua_pushstring(L, (value.is_string() ? value.get<std::string>() : it->text).c_str());
        break;
    }
    return 1;
}

int bmNumber(lua_State* L) {
    float v;
    if (lua_type(L, 1) == LUA_TNUMBER)
        lua_pushvalue(L, 1);
    else if (lua_type(L, 1) == LUA_TSTRING && parseNumber(lua_tostring(L, 1), v))
        lua_pushnumber(L, v);
    else
        lua_pushnil(L);
    return 1;
}

int bmRegisterObject(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!g_registering) return luaL_error(L, "register_object can only be called while the plugin loads");
    Plugin* plugin = pluginOf(L);
    PluginObjectType t;
    const std::string id = fieldString(L, 1, "id", "");
    if (id.empty()) return luaL_error(L, "register_object: missing id");
    t.pluginId = plugin ? plugin->id : "";
    t.id = t.pluginId + "." + id;
    t.name = fieldString(L, 1, "name", id);
    t.description = fieldString(L, 1, "description", "");
    t.icon = fieldString(L, 1, "icon", "plugin");
    t.prototype.objectType = t.id;

    lua_getfield(L, 1, "size");
    if (lua_istable(L, -1)) {
        t.prototype.defaultSize = {fieldNumber(L, -1, "w", 1, 900), fieldNumber(L, -1, "h", 2, 540)};
        t.prototype.size = AnimVec2{t.prototype.defaultSize};
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "params");
    if (lua_istable(L, -1)) {
        const int list = lua_gettop(L);
        for (lua_Integer i = 1; i <= luaL_len(L, list); ++i) {
            lua_rawgeti(L, list, i);
            const int e = lua_gettop(L);
            if (lua_istable(L, e)) {
                PluginParam p;
                p.id = fieldString(L, e, "id", "");
                if (p.id.empty())
                    return luaL_error(L, "register_object '%s': parameter %d has no id", id.c_str(), int(i));
                p.label = fieldString(L, e, "label", p.id);
                p.group = fieldString(L, e, "group", "Parameters");
                p.kind = kindFromName(fieldString(L, e, "type", "number"));
                p.min = fieldNumber(L, e, "min", 0, 0);
                p.max = fieldNumber(L, e, "max", 0, 0);
                p.speed = fieldNumber(L, e, "step", 0, 0.5f);
                p.format = fieldString(L, e, "format", "%.1f");
                lua_getfield(L, e, "default");
                switch (p.kind) {
                case PluginParamKind::Number:
                    p.defaultNumber = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
                    p.number = AnimFloat{p.defaultNumber};
                    break;
                case PluginParamKind::Color:
                    p.defaultColor = readColor(L, -1, Color::fromHex(0xFFFFFF));
                    p.color = AnimColor{p.defaultColor};
                    break;
                case PluginParamKind::Toggle: p.toggle = lua_toboolean(L, -1); break;
                case PluginParamKind::Text: p.text = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : ""; break;
                case PluginParamKind::Choice: break;
                case PluginParamKind::File: break; // chosen by the user
                }
                const int def = lua_gettop(L);
                if (p.kind == PluginParamKind::Choice) {
                    lua_getfield(L, e, "choices");
                    if (lua_istable(L, -1))
                        for (lua_Integer c = 1; c <= luaL_len(L, -1); ++c) {
                            lua_rawgeti(L, -1, c);
                            if (lua_type(L, -1) == LUA_TSTRING) p.choices.emplace_back(lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    lua_pop(L, 1);
                    if (lua_type(L, def) == LUA_TSTRING) {
                        const auto it = std::find(p.choices.begin(), p.choices.end(), lua_tostring(L, def));
                        p.choice = it == p.choices.end() ? 0 : static_cast<int>(it - p.choices.begin());
                    } else if (lua_isnumber(L, def)) {
                        p.choice = std::clamp(static_cast<int>(lua_tointeger(L, def)) - 1, 0,
                                              std::max(0, static_cast<int>(p.choices.size()) - 1));
                    }
                }
                lua_pop(L, 1);                         // default
                if (p.kind == PluginParamKind::File) { // accept = { "glb", "obj" }
                    lua_getfield(L, e, "accept");
                    if (lua_istable(L, -1))
                        for (lua_Integer c = 1; c <= luaL_len(L, -1); ++c) {
                            lua_rawgeti(L, -1, c);
                            if (lua_type(L, -1) == LUA_TSTRING) {
                                std::string ext = lua_tostring(L, -1);
                                if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
                                for (char& ch : ext)
                                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                                p.accept.push_back(ext);
                            }
                            lua_pop(L, 1);
                        }
                    lua_pop(L, 1);
                }
                lua_getfield(L, e, "intro");
                if (lua_istable(L, -1) && p.kind == PluginParamKind::Number)
                    t.intro.push_back({p.id, fieldNumber(L, -1, "from", 0, 0), fieldNumber(L, -1, "duration", 0, 1)});
                lua_pop(L, 1);
                t.prototype.params.push_back(std::move(p));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "rows");
    if (lua_istable(L, -1)) {
        t.prototype.hasRows = true;
        t.prototype.rows = readRows(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "table");
    if (lua_istable(L, -1)) {
        t.prototype.hasTable = true;
        t.prototype.table = readTable(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "import_csv");
    if (lua_isfunction(L, -1))
        t.importRef = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_pop(L, 1);
    lua_getfield(L, 1, "row_labels");
    if (lua_isfunction(L, -1))
        t.labelsRef = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_pop(L, 1);

    lua_getfield(L, 1, "draw");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "register_object '%s': draw must be a function", id.c_str());
    t.drawRef = luaL_ref(L, LUA_REGISTRYINDEX);
    g_registering->push_back(std::move(t));
    return 0;
}

lua_State* newSandbox(const std::string& pluginId) {
    lua_State* L = luaL_newstate();
    auto* memory = new SandboxMemory;
    memory->base = lua_getallocf(L, &memory->baseUd);
    memory->used =
        static_cast<int64_t>(lua_gc(L, LUA_GCCOUNT)) * 1024 + lua_gc(L, LUA_GCCOUNTB); // what the state holds
    lua_setallocf(L, cappedAlloc, memory);
    // Only pure libraries: no io, os, package or debug.
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 5);
    for (const char* name : {"dofile", "loadfile", "load", "require", "collectgarbage"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_pushcfunction(L, bmPrint);
    lua_setglobal(L, "print");

    const luaL_Reg api[] = {{"register_object", bmRegisterObject},
                            {"register_command", bmRegisterCommand},
                            {"ease", bmEase},
                            {"color", bmColor},
                            {"mix", bmMix},
                            {"with_alpha", bmWithAlpha},
                            {"format", bmFormat},
                            {"parse_csv", bmParseCsv},
                            {"number", bmNumber},
                            {"cached", bmCached},
                            {"json", bmJson},
                            {"base64", bmBase64},
                            {"mesh", bmMesh},
                            {"include", bmInclude},
                            {"setting", bmSetting},
                            {"load_mesh", bmLoadMesh},
                            {nullptr, nullptr}};
    luaL_newmetatable(L, kMeshType);
    lua_pushcfunction(L, meshGc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, meshIndex);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    luaL_newlib(L, api);
    lua_pushinteger(
        L, PluginHost::kApiVersion); // 2: commands and operations; 3: tables, CSV import; 4: files, meshes; 5: buttons,
                                     // microphone; 6: include; 7: row_labels; 8: settings; 9: load_mesh
    lua_setfield(L, -2, "api_version");
    const luaL_Reg mic[] = {{"start", micStart}, {"stop", micStop},     {"recording", micRecording},
                            {"level", micLevel}, {"cancel", micCancel}, {nullptr, nullptr}};
    luaL_newlib(L, mic);
    lua_setfield(L, -2, "microphone");
    lua_setglobal(L, "bemotion");

    const luaL_Reg gfx[] = {{"rect", gRect},         {"rect_outline", gRectOutline},
                            {"circle", gCircle},     {"line", gLine},
                            {"clip", gClip},         {"unclip", gUnclip},
                            {"polyline", gPolyline}, {"polygon", gPolygon},
                            {"arc", gArc},           {"text", gText},
                            {"measure", gMeasure},   {"mesh", gMesh},
                            {nullptr, nullptr}};
    luaL_newlib(L, gfx);
    lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.gfx");

    lua_pushstring(L, pluginId.c_str());
    lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.plugin");
    return L;
}

// A file parameter: { path, name, ext, data, version } when the plugin may read files ("files"
// permission) and the file is there, else nil. The bytes are read once per file version and kept
// in the plugin's registry, so drawing every frame does not copy them.
void pushFile(lua_State* L, const std::string& path) {
    Plugin* plugin = pluginOf(L);
    std::error_code ec;
    const fs::path file = platform::fromUtf8(path);
    if (path.empty() || !plugin || !plugin->may("files") || !fs::is_regular_file(file, ec)) {
        lua_pushnil(L);
        return;
    }
    const auto stamp = fs::last_write_time(file, ec).time_since_epoch().count();
    const double version =
        static_cast<double>(stamp % 1000000000000LL) + static_cast<double>(fs::file_size(file, ec) % 1000000);
    lua_getfield(L, LUA_REGISTRYINDEX, "bemotion.files");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.files");
    }
    const int files = lua_gettop(L);
    lua_getfield(L, files, path.c_str());
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "version");
        const bool current = lua_tonumber(L, -1) == version;
        lua_pop(L, 1);
        if (current) {
            lua_remove(L, files);
            return;
        }
    }
    lua_pop(L, 1);
    constexpr uintmax_t kMaxFile = 512u << 20;
    if (fs::file_size(file, ec) > kMaxFile) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return;
    }
    const std::string data = readFile(file);
    std::string ext = platform::toUtf8(file.extension());
    if (!ext.empty()) ext.erase(0, 1);
    for (char& ch : ext)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    lua_createtable(L, 0, 5);
    lua_pushstring(L, path.c_str());
    lua_setfield(L, -2, "path");
    lua_pushstring(L, platform::toUtf8(file.filename()).c_str());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, ext.c_str());
    lua_setfield(L, -2, "ext");
    lua_pushlstring(L, data.data(), data.size());
    lua_setfield(L, -2, "data");
    lua_pushnumber(L, version);
    lua_setfield(L, -2, "version");
    lua_pushvalue(L, -1);
    lua_setfield(L, files, path.c_str());
    lua_remove(L, files);
}

void pushParams(lua_State* L, const Layer& layer, double t) {
    const PluginProps& p = layer.plugin;
    const Vec2 size = p.size.valueAt(t);
    lua_newtable(L);
    lua_pushnumber(L, size.x);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, size.y);
    lua_setfield(L, -2, "height");
    lua_pushnumber(L, t - layer.inPoint);
    lua_setfield(L, -2, "time");
    lua_pushnumber(L, layer.outPoint - layer.inPoint);
    lua_setfield(L, -2, "duration");
    for (const PluginParam& pp : p.params) {
        switch (pp.kind) {
        case PluginParamKind::Number: lua_pushnumber(L, pp.number.valueAt(t)); break;
        case PluginParamKind::Color: pushColor(L, pp.color.valueAt(t)); break;
        case PluginParamKind::Toggle: lua_pushboolean(L, pp.toggle); break;
        case PluginParamKind::Choice:
            lua_pushstring(L, pp.choice < static_cast<int>(pp.choices.size()) ? pp.choices[pp.choice].c_str() : "");
            break;
        case PluginParamKind::Text: lua_pushstring(L, pp.text.c_str()); break;
        case PluginParamKind::File: pushFile(L, pp.text); break;
        }
        lua_setfield(L, -2, pp.id.c_str());
    }
    if (p.hasRows) {
        lua_createtable(L, static_cast<int>(p.rows.size()), 0);
        for (size_t i = 0; i < p.rows.size(); ++i) {
            lua_createtable(L, 0, 3);
            lua_pushstring(L, p.rows[i].label.c_str());
            lua_setfield(L, -2, "label");
            lua_pushnumber(L, p.rows[i].value);
            lua_setfield(L, -2, "value");
            pushColor(L, p.rows[i].color);
            lua_setfield(L, -2, "color");
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_setfield(L, -2, "rows");
    }
    if (p.hasTable) {
        pushTable(L, p.table);
        lua_setfield(L, -2, "table");
    }
}

void drawPlaceholder(render::PluginCanvas& c, const Layer& layer, double t, const std::string& message) {
    const Vec2 half = layer.plugin.size.valueAt(t) * 0.5f;
    const Color red = Color::fromHex(0xE5604D);
    c.rect(half * -1.0f, half, Color::fromHex(0xE5604D, 0.08f), 12.0f);
    c.rectOutline(half * -1.0f, half, red, 3.0f, 12.0f);
    const float size = std::clamp(half.y * 0.12f, 14.0f, 40.0f);
    c.text(layer.name, {0, -size * 0.8f}, size, red, render::TextAnchor::Center, true);
    c.text(message, {0, size * 0.8f}, size * 0.6f, Color::fromHex(0xFFFFFF, 0.7f), render::TextAnchor::Center);
}

} // namespace bemotion::pluginlua
