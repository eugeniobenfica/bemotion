#include "app/PluginHost.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "core/Easing.h"
#include "render/PluginCanvas.h"

// Lua is built as C++ (see cmake/Dependencies.cmake): no extern "C".
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr int kHookEvery = 10000;   // instructions between budget checks
constexpr int kDrawBudget = 400;    // x kHookEvery instructions per draw call
constexpr int kLoadBudget = 2000;   // ... and for running main.lua
constexpr int kCommandBudget = 20000; // ... and for a command (one-off, may build a whole scene)
constexpr size_t kMaxLogLines = 50;

// Per-call state shared with the Lua C functions (the editor is single threaded).
render::PluginCanvas* g_canvas = nullptr;
int g_budget = 0;

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Windows editors often start UTF-8 files with a byte order mark, which Lua does not accept.
std::string withoutBom(std::string code) {
    if (code.rfind("\xEF\xBB\xBF", 0) == 0) code.erase(0, 3);
    return code;
}

void budgetHook(lua_State* L, lua_Debug*) {
    if (--g_budget <= 0) luaL_error(L, "script ran too long (instruction limit)");
}

// --- values ------------------------------------------------------------------

bool parseHex(const char* s, Color& out) {
    if (!s || s[0] != '#') return false;
    const size_t n = std::strlen(s);
    if (n != 7 && n != 9) return false;
    unsigned v = 0;
    for (size_t i = 1; i < n; ++i) {
        const char c = s[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + static_cast<unsigned>(d);
    }
    if (n == 7) out = Color::fromHex(v);
    else out = Color::fromHex(v >> 8, static_cast<float>(v & 0xFF) / 255.0f);
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
    canvas(L).polyline(readPoints(L, 1), static_cast<float>(luaL_checknumber(L, 2)), readColor(L, 3, {}), lua_toboolean(L, 4));
    return 0;
}
int gPolygon(lua_State* L) {
    canvas(L).polygon(readPoints(L, 1), readColor(L, 2, {}));
    return 0;
}
int gArc(lua_State* L) {
    canvas(L).arc(vec(L, 1, 2), static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4)),
                  static_cast<float>(luaL_checknumber(L, 5)), static_cast<float>(luaL_checknumber(L, 6)), readColor(L, 7, {}));
    return 0;
}
render::TextAnchor anchorArg(lua_State* L, int idx) {
    const char* a = luaL_optstring(L, idx, "left");
    return std::strcmp(a, "center") == 0 ? render::TextAnchor::Center
           : std::strcmp(a, "right") == 0 ? render::TextAnchor::Right
                                          : render::TextAnchor::Left;
}
int gText(lua_State* L) {
    canvas(L).text(luaL_checkstring(L, 1), vec(L, 2, 3), static_cast<float>(luaL_checknumber(L, 4)), readColor(L, 5, {}),
                   anchorArg(L, 6), lua_toboolean(L, 7));
    return 0;
}
int gMeasure(lua_State* L) {
    lua_pushnumber(L, canvas(L).measure(luaL_checkstring(L, 1), static_cast<float>(luaL_checknumber(L, 2)), lua_toboolean(L, 3)));
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
    for (int i = static_cast<int>(dot == std::string::npos ? s.size() : dot) - 3; i > 0; i -= 3) s.insert(i, ",");
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

json toJson(lua_State* L, int idx, int depth = 0) {
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

struct CommandRun {
    Project work; // a copy: becomes the project only if the command succeeds
    const OpEnvironment* env = nullptr;
    bool mayImport = false;
    int ops = 0;
};
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

void pushDoc(lua_State* L, double time, const std::vector<uint64_t>& selection) {
    const luaL_Reg fns[] = {{"apply", docApply}, {"layers", docLayers}, {"layer", docLayer}, {"composition", docComposition},
                            {nullptr, nullptr}};
    luaL_newlib(L, fns);
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
    return PluginParamKind::Number;
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
                if (p.id.empty()) return luaL_error(L, "register_object '%s': parameter %d has no id", id.c_str(), int(i));
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
                lua_pop(L, 1); // default
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
        const int list = lua_gettop(L);
        for (lua_Integer i = 1; i <= luaL_len(L, list); ++i) {
            lua_rawgeti(L, list, i);
            const int e = lua_gettop(L);
            if (lua_istable(L, e)) {
                DataRow r;
                lua_getfield(L, e, "label");
                if (lua_isnil(L, -1)) {
                    lua_pop(L, 1);
                    lua_rawgeti(L, e, 1);
                }
                r.label = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
                lua_pop(L, 1);
                r.value = fieldNumber(L, e, "value", 2, 0);
                lua_getfield(L, e, "color");
                if (lua_isnil(L, -1)) {
                    lua_pop(L, 1);
                    lua_rawgeti(L, e, 3);
                }
                r.color = readColor(L, -1, labelColor(static_cast<int>(i - 1)));
                lua_pop(L, 1);
                t.prototype.rows.push_back(std::move(r));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "draw");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "register_object '%s': draw must be a function", id.c_str());
    t.drawRef = luaL_ref(L, LUA_REGISTRYINDEX);
    g_registering->push_back(std::move(t));
    return 0;
}

lua_State* newSandbox(const std::string& pluginId) {
    lua_State* L = luaL_newstate();
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

    const luaL_Reg api[] = {{"register_object", bmRegisterObject}, {"register_command", bmRegisterCommand}, {"ease", bmEase},
                            {"color", bmColor},
                            {"mix", bmMix},                         {"with_alpha", bmWithAlpha}, {"format", bmFormat},
                            {nullptr, nullptr}};
    luaL_newlib(L, api);
    lua_pushinteger(L, 2); // 2: commands and operations
    lua_setfield(L, -2, "api_version");
    lua_setglobal(L, "bemotion");

    const luaL_Reg gfx[] = {{"rect", gRect},       {"rect_outline", gRectOutline}, {"circle", gCircle}, {"line", gLine},
                            {"polyline", gPolyline}, {"polygon", gPolygon},         {"arc", gArc},       {"text", gText},
                            {"measure", gMeasure}, {nullptr, nullptr}};
    luaL_newlib(L, gfx);
    lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.gfx");

    lua_pushstring(L, pluginId.c_str());
    lua_setfield(L, LUA_REGISTRYINDEX, "bemotion.plugin");
    return L;
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
        if (p.L) lua_close(p.L);
    plugins_.clear();
    types_.clear();
    render::setPluginDrawer({});
}

void PluginHost::load() {
    shutdown();
    std::error_code ec;
    for (const fs::path& root : {platform::assetsDir() / "plugins", userDir()}) {
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
    } catch (const std::exception& e) {
        p.id = platform::toUtf8(dir.filename());
        p.name = p.id;
        p.error = std::string("plugin.json: ") + e.what();
    }
    if (p.id.empty()) p.error = "plugin.json: missing \"id\"";
    // A user plugin with a bundled plugin's id replaces it.
    if (Plugin* existing = findPlugin(p.id)) {
        if (existing->L) lua_close(existing->L);
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
    const std::string code = withoutBom(readFile(dir / platform::fromUtf8(mainFile)));
    std::vector<PluginObjectType> registered;
    std::vector<PluginCommand> registeredCommands;
    g_registering = &registered;
    g_registeringCommands = &registeredCommands;
    g_budget = kLoadBudget;
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kHookEvery);
    const std::string chunk = "@" + plugin.id + "/" + mainFile;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        plugin.error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error loading the script";
        lua_pop(L, 1);
    }
    g_registering = nullptr;
    g_registeringCommands = nullptr;
    if (!plugin.error.empty()) {
        lua_close(L);
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
                const OpEnvironment& env, bool mayImport, std::string& error) {
    CommandRun run;
    run.work = project;
    run.env = &env;
    run.mayImport = mayImport;
    g_run = &run;
    pushDoc(L, time, selection);
    g_budget = kCommandBudget;
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kHookEvery);
    const bool ok = lua_pcall(L, 1, 0, 0) == LUA_OK;
    g_run = nullptr;
    if (!ok) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "the command failed";
        lua_pop(L, 1);
        return false;
    }
    if (run.ops > 0) project = std::move(run.work);
    return true;
}

} // namespace

bool PluginHost::runCommand(const std::string& commandId, Project& project, double time, const std::vector<uint64_t>& selection,
                            const OpEnvironment& env, std::string& error) {
    const auto it = std::find_if(commands_.begin(), commands_.end(), [&](const PluginCommand& c) { return c.id == commandId; });
    Plugin* plugin = it != commands_.end() ? findPlugin(it->pluginId) : nullptr;
    if (!plugin || !plugin->L || !plugin->enabled) {
        error = "the command " + commandId + " is not available";
        return false;
    }
    lua_State* L = plugin->L;
    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->runRef);
    const bool ok = runWithDoc(L, project, time, selection, env, plugin->may("media"), error);
    if (!ok) {
        plugin->log.push_back(it->name + ": " + error);
        if (plugin->log.size() > kMaxLogLines) plugin->log.erase(plugin->log.begin());
    }
    lua_settop(L, top);
    return ok;
}

bool PluginHost::runScript(const std::string& source, const std::string& chunkName, Project& project, double time,
                           const OpEnvironment& env, std::vector<std::string>& log, std::string& error) {
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
        ok = runWithDoc(L, project, time, {}, env, true, error);
    }
    log = host.plugins_.back().log;
    lua_close(L);
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

void PluginHost::setEnabled(const std::string& pluginId, bool enabled) {
    if (Plugin* p = findPlugin(pluginId)) p->enabled = enabled;
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
}

void PluginHost::reconcile(Composition& comp) const {
    for (Layer& l : comp.layers) reconcile(l);
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
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        plugin->drawError = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
        drawPlaceholder(c, layer, t, plugin->drawError);
    } else {
        plugin->drawError.clear();
    }
    g_canvas = nullptr;
    lua_settop(L, top);
}

} // namespace bemotion
