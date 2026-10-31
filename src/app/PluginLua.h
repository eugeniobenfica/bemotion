#pragma once

// The Lua side of the plugin host, shared by its files (not a public header): the sandbox
// (PluginSandbox.cpp: instruction budget, memory cap, protected calls), the API plugins call
// (PluginApi.cpp: g, bemotion, doc, data; PluginApiMedia.cpp: meshes, the microphone) and the
// host itself (PluginHost.cpp: loading, commands, objects, drawing).

#include "app/PluginHost.h"
#include "core/Data.h"
#include "render/PluginCanvas.h"

// Lua is built as C++ (see cmake/Dependencies.cmake): no extern "C".
#include <lauxlib.h>
#include <lua.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bemotion::pluginlua {

namespace fs = std::filesystem;
using json = nlohmann::json;
inline constexpr int kHookEvery = 10000;     // instructions between budget checks
inline constexpr int kDrawBudget = 400;      // x kHookEvery instructions per draw call
inline constexpr int kLoadBudget = 2000;     // ... and for running main.lua
inline constexpr int kCommandBudget = 20000; // ... and for a command (one-off, may build a whole scene)
inline constexpr size_t kMaxLogLines = 50;
inline constexpr int64_t kMemoryCap = int64_t(256) << 20;
inline constexpr int64_t kTotalMemoryCap = int64_t(768) << 20;
inline constexpr const char* kPluginDirKey = "bemotion.plugin_dir";
inline constexpr const char* kIncludedKey = "bemotion.included";
inline constexpr const char* kMeshType = "bemotion.mesh";

struct SandboxMemory {
    lua_Alloc base = nullptr;
    void* baseUd = nullptr;
    int64_t used = 0;
    bool limited = false;
};

struct CommandRun {
    Project work; // a copy: becomes the project only if the command succeeds
    const OpEnvironment* env = nullptr;
    bool mayImport = false;
    int ops = 0;
    int play = -1;   // doc.play()
    double time = 0; // doc.time
};

// Per-call state shared with the Lua C functions (the editor is single threaded).
extern render::PluginCanvas* g_canvas;
extern int g_budget;
extern std::atomic<int64_t> g_pluginMemory;
extern std::vector<PluginObjectType>* g_registering;
extern std::vector<PluginCommand>* g_registeringCommands;
extern CommandRun* g_run;

std::string readFile(const fs::path& file);
std::string withoutBom(std::string code);
void budgetHook(lua_State* L, lua_Debug*);
void* cappedAlloc(void* ud, void* ptr, size_t osize, size_t nsize);
SandboxMemory* memoryOf(lua_State* L);
int sandboxCall(lua_State* L, int nargs, int nresults);
void closeSandbox(lua_State* L);
bool parseHex(const char* s, Color& out);
float fieldNumber(lua_State* L, int idx, const char* key, int arrayIndex, float fallback);
Color readColor(lua_State* L, int idx, Color fallback);
void pushColor(lua_State* L, Color c);
std::string fieldString(lua_State* L, int idx, const char* key, const std::string& fallback);
Vec2 vec(lua_State* L, int x, int y);
std::vector<Vec2> readPoints(lua_State* L, int idx);
render::PluginCanvas& canvas(lua_State* L);
int gRect(lua_State* L);
int gRectOutline(lua_State* L);
int gCircle(lua_State* L);
int gLine(lua_State* L);
int gPolyline(lua_State* L);
int gClip(lua_State* L);
int gUnclip(lua_State* L);
int gPolygon(lua_State* L);
int gArc(lua_State* L);
render::TextAnchor anchorArg(lua_State* L, int idx);
int gText(lua_State* L);
int gMeasure(lua_State* L);
int bmEase(lua_State* L);
int bmColor(lua_State* L);
int bmMix(lua_State* L);
int bmWithAlpha(lua_State* L);
int bmFormat(lua_State* L);
Plugin* pluginOf(lua_State* L);
int bmPrint(lua_State* L);
json toJson(lua_State* L, int idx, int depth = 0);
void pushJson(lua_State* L, const json& j);
int docApply(lua_State* L);
int docQuery(lua_State* L, json op);
json layerRef(lua_State* L, int idx);
int docLayer(lua_State* L);
int docExpression(lua_State* L);
int docPlay(lua_State* L);
void pushDoc(lua_State* L, double time, const std::vector<uint64_t>& selection, bool pressed = false);
int bmRegisterCommand(lua_State* L);
PluginParamKind kindFromName(const std::string& s);
std::vector<DataRow> readRows(lua_State* L, int idx);
DataTable readTable(lua_State* L, int idx);
void pushCells(lua_State* L, const Cells& cells);
void pushTable(lua_State* L, const DataTable& t);
int bmInclude(lua_State* L);
int bmCached(lua_State* L);
int bmJson(lua_State* L);
int bmBase64(lua_State* L);
std::vector<float> readFloats(lua_State* L, int idx);
std::vector<uint32_t> readIndices(lua_State* L, int idx, int size);
void addMeshPart(lua_State* L, int part, render::Mesh& m);
int bmMesh(lua_State* L);
int bmLoadMesh(lua_State* L);
int meshGc(lua_State* L);
int meshIndex(lua_State* L);
int gMesh(lua_State* L);
bool mayRecord(lua_State* L);
int micStart(lua_State* L);
int micStop(lua_State* L);
int micRecording(lua_State* L);
int micLevel(lua_State* L);
int micCancel(lua_State* L);
int bmParseCsv(lua_State* L);
int bmSetting(lua_State* L);
int bmNumber(lua_State* L);
int bmRegisterObject(lua_State* L);
lua_State* newSandbox(const std::string& pluginId);
void pushFile(lua_State* L, const std::string& path);
void pushParams(lua_State* L, const Layer& layer, double t);
void drawPlaceholder(render::PluginCanvas& c, const Layer& layer, double t, const std::string& message);

} // namespace bemotion::pluginlua
