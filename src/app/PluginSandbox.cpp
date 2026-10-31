// Where plugin code runs: an instruction budget and a memory cap per plugin (app/PluginLua.h).

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

// Each plugin's Lua heap is capped, so a plugin that hoards memory (string.rep of a gigabyte is
// one instruction, which the instruction budget cannot stop) gets "not enough memory" instead of
// taking the app down. Only plugin code runs under the cap (sandboxCall): the host's own pushes
// before a call are outside any protected call, where a failed allocation would abort.
// Two caps: each plugin's, and all plugins' together (several plugins may each stay under theirs).
std::atomic<int64_t> g_pluginMemory{0}; // every plugin's Lua heap (exports draw on other threads)

void* cappedAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* m = static_cast<SandboxMemory*>(ud);
    const int64_t old = ptr ? static_cast<int64_t>(osize) : 0; // without ptr, osize is a type tag
    const int64_t grow = static_cast<int64_t>(nsize) - old;
    if (grow > 0 && m->limited && (m->used + grow > kMemoryCap || g_pluginMemory.load() + grow > kTotalMemoryCap))
        return nullptr;
    void* p = m->base(m->baseUd, ptr, osize, nsize);
    if (p || nsize == 0) {
        m->used += grow;
        g_pluginMemory += grow;
    }
    return p;
}

SandboxMemory* memoryOf(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    return static_cast<SandboxMemory*>(ud);
}

// lua_pcall for plugin code: under the memory cap, and the cap as it was afterwards (calls nest).
int sandboxCall(lua_State* L, int nargs, int nresults) {
    SandboxMemory* m = memoryOf(L);
    const bool was = m->limited;
    m->limited = true;
    const int status = lua_pcall(L, nargs, nresults, 0);
    m->limited = was;
    return status;
}

void closeSandbox(lua_State* L) {
    SandboxMemory* m = memoryOf(L);
    lua_close(L); // frees through cappedAlloc, so m goes after
    delete m;
}

} // namespace bemotion::pluginlua
