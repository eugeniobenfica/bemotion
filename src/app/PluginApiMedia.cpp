// Meshes (bemotion.mesh, g.mesh) and the microphone (permission "microphone") for plugins
// (app/PluginLua.h).

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

// --- meshes (bemotion.mesh, g.mesh) ------------------------------------------------------

// Numbers from a Lua array, or from a string of packed little-endian float32s.
std::vector<float> readFloats(lua_State* L, int idx) {
    std::vector<float> out;
    idx = lua_absindex(L, idx);
    if (lua_type(L, idx) == LUA_TSTRING) {
        size_t n = 0;
        const char* s = lua_tolstring(L, idx, &n);
        out.resize(n / 4);
        std::memcpy(out.data(), s, out.size() * 4);
    } else if (lua_istable(L, idx)) {
        const lua_Integer n = luaL_len(L, idx);
        out.reserve(static_cast<size_t>(n));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, idx, i);
            out.push_back(static_cast<float>(lua_tonumber(L, -1)));
            lua_pop(L, 1);
        }
    }
    return out;
}

// Indices: a Lua array (1-based) or packed unsigned integers of `size` bytes (0-based).
std::vector<uint32_t> readIndices(lua_State* L, int idx, int size) {
    std::vector<uint32_t> out;
    idx = lua_absindex(L, idx);
    if (lua_type(L, idx) == LUA_TSTRING) {
        size_t n = 0;
        const auto* s = reinterpret_cast<const unsigned char*>(lua_tolstring(L, idx, &n));
        size = size == 1 || size == 2 ? size : 4;
        out.resize(n / static_cast<size_t>(size));
        for (size_t i = 0; i < out.size(); ++i) {
            uint32_t v = 0;
            for (int b = 0; b < size; ++b)
                v |= static_cast<uint32_t>(s[i * size + b]) << (8 * b);
            out[i] = v;
        }
    } else if (lua_istable(L, idx)) {
        const lua_Integer n = luaL_len(L, idx);
        out.reserve(static_cast<size_t>(n));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, idx, i);
            out.push_back(static_cast<uint32_t>(std::max<lua_Integer>(0, lua_tointeger(L, -1) - 1)));
            lua_pop(L, 1);
        }
    }
    return out;
}

// One part of bemotion.mesh{...}: its vertices, appended to `m` (with its matrix applied).
void addMeshPart(lua_State* L, int part, render::Mesh& m) {
    lua_getfield(L, part, "positions");
    const std::vector<float> pos = readFloats(L, -1);
    lua_pop(L, 1);
    const size_t n = pos.size() / 3, base = m.vertexCount();
    if (!n) return;
    lua_getfield(L, part, "normals");
    std::vector<float> nor = readFloats(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, part, "index_size");
    const int indexSize = static_cast<int>(luaL_optinteger(L, -1, 4));
    lua_pop(L, 1);
    lua_getfield(L, part, "indices");
    std::vector<uint32_t> idx = readIndices(L, -1, indexSize);
    lua_pop(L, 1);
    if (idx.empty()) {
        idx.resize(n - n % 3);
        for (size_t i = 0; i < idx.size(); ++i)
            idx[i] = static_cast<uint32_t>(i);
    }
    // Colors: per vertex (rgba, or rgb with color_size = 3) or one for the part.
    lua_getfield(L, part, "color");
    const Color partColor = readColor(L, -1, Color::fromHex(0xFFFFFF));
    lua_pop(L, 1);
    lua_getfield(L, part, "color_size");
    const int colorSize = static_cast<int>(luaL_optinteger(L, -1, 4)) == 3 ? 3 : 4;
    lua_pop(L, 1);
    lua_getfield(L, part, "colors");
    const std::vector<float> cols = readFloats(L, -1);
    lua_pop(L, 1);
    // The part's place in the model: a 4x4 matrix, column-major (as in glTF).
    lua_getfield(L, part, "matrix");
    std::vector<float> mat = readFloats(L, -1);
    lua_pop(L, 1);
    if (mat.size() != 16) mat = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (size_t i = 0; i < n; ++i) {
        const float x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
        m.positions.insert(m.positions.end(), {mat[0] * x + mat[4] * y + mat[8] * z + mat[12],
                                               mat[1] * x + mat[5] * y + mat[9] * z + mat[13],
                                               mat[2] * x + mat[6] * y + mat[10] * z + mat[14]});
        Color c = partColor;
        if (cols.size() >= (i + 1) * colorSize)
            c = {cols[i * colorSize] * partColor.r, cols[i * colorSize + 1] * partColor.g,
                 cols[i * colorSize + 2] * partColor.b, (colorSize == 4 ? cols[i * 4 + 3] : 1.0f) * partColor.a};
        m.colors.push_back(c);
    }
    if (nor.size() == n * 3) {
        for (size_t i = 0; i < n; ++i) {
            const float x = nor[i * 3], y = nor[i * 3 + 1], z = nor[i * 3 + 2];
            m.normals.insert(m.normals.end(),
                             {mat[0] * x + mat[4] * y + mat[8] * z, mat[1] * x + mat[5] * y + mat[9] * z,
                              mat[2] * x + mat[6] * y + mat[10] * z});
        }
    } else {
        m.normals.clear(); // a part without normals: they are all computed (Mesh::finish)
        nor.clear();
    }
    for (uint32_t i : idx)
        m.indices.push_back(i < n ? static_cast<uint32_t>(base + i) : UINT32_MAX);
}

// bemotion.mesh{ positions, normals, colors | color, indices, matrix } or { parts = { {...}, ... } }
// -> a mesh to draw with g.mesh (built once: keep it with bemotion.cached).
int bmMesh(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* m = static_cast<render::Mesh*>(lua_newuserdatauv(L, sizeof(render::Mesh), 0));
    new (m) render::Mesh();
    luaL_setmetatable(L, kMeshType);
    lua_getfield(L, 1, "parts");
    bool hadNormals = true;
    auto add = [&](int part) {
        const size_t before = m->normals.size();
        const size_t verts = m->vertexCount();
        addMeshPart(L, part, *m);
        if (m->normals.size() == before && m->vertexCount() > verts) hadNormals = false;
    };
    if (lua_istable(L, -1)) {
        const int parts = lua_gettop(L);
        for (lua_Integer i = 1; i <= luaL_len(L, parts); ++i) {
            lua_rawgeti(L, parts, i);
            if (lua_istable(L, -1)) add(lua_gettop(L));
            lua_pop(L, 1);
            if (m->vertexCount() > 20000000) return luaL_error(L, "mesh too large (over 20 million vertices)");
        }
    } else {
        add(1);
    }
    lua_pop(L, 1);
    if (!hadNormals) m->normals.clear();
    m->finish();
    return 1;
}

// bemotion.load_mesh(file, { up = "Auto" | "Y Up" | "Z Up", max_triangles = n }) -> mesh, or
// nil and why: a model file read natively (core/MeshFile: OBJ, STL, glTF / GLB; a .gltf's .bin
// beside it), far quicker than in Lua, simplified to at most `max_triangles` (0: all of them).
int bmLoadMesh(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "data");
    size_t n = 0;
    const char* bytes = lua_tolstring(L, -1, &n);
    lua_getfield(L, 1, "ext");
    std::string ext = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_getfield(L, 1, "path");
    const std::string path = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 3);
    if (!bytes) return lua_pushnil(L), lua_pushstring(L, "the file could not be read"), 2;
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string up = "Auto";
    lua_Integer maxTriangles = 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "up");
        if (lua_isstring(L, -1)) up = lua_tostring(L, -1);
        lua_getfield(L, 2, "max_triangles");
        maxTriangles = lua_isnumber(L, -1) ? lua_tointeger(L, -1) : 0;
        lua_pop(L, 2);
    }
    // A .gltf's buffers in its own folder (the user picked the file: its parts come along).
    Plugin* plugin = pluginOf(L);
    auto beside = [&](const std::string& name, std::string& out) {
        if (!plugin || !plugin->may("files") || path.empty() || name.find("..") != std::string::npos) return false;
        const fs::path file = platform::fromUtf8(path).parent_path() / platform::fromUtf8(name);
        std::ifstream in(file, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    };
    MeshData data;
    std::string error;
    if (!loadMeshFile(ext, std::string(bytes, n), beside, data, error))
        return lua_pushnil(L), lua_pushstring(L, error.c_str()), 2;
    if (up == "Z Up" || (up == "Auto" && ext == "stl")) zUpToYUp(data);
    if (maxTriangles > 0) simplifyMesh(data, static_cast<size_t>(maxTriangles));
    auto* m = static_cast<render::Mesh*>(lua_newuserdatauv(L, sizeof(render::Mesh), 0));
    new (m) render::Mesh();
    luaL_setmetatable(L, kMeshType);
    m->positions = std::move(data.positions);
    m->normals = std::move(data.normals);
    m->indices = std::move(data.indices);
    m->colors.reserve(data.colors.size() / 4);
    for (size_t i = 0; i + 3 < data.colors.size(); i += 4)
        m->colors.push_back({data.colors[i], data.colors[i + 1], data.colors[i + 2], data.colors[i + 3]});
    m->finish();
    return 1;
}

int meshGc(lua_State* L) {
    static_cast<render::Mesh*>(luaL_checkudata(L, 1, kMeshType))->~Mesh();
    return 0;
}

// mesh.triangles, mesh.vertices
int meshIndex(lua_State* L) {
    const auto* m = static_cast<render::Mesh*>(luaL_checkudata(L, 1, kMeshType));
    const std::string key = luaL_checkstring(L, 2);
    if (key == "triangles")
        lua_pushinteger(L, static_cast<lua_Integer>(m->triangleCount()));
    else if (key == "vertices")
        lua_pushinteger(L, static_cast<lua_Integer>(m->vertexCount()));
    else
        lua_pushnil(L);
    return 1;
}

// g.mesh(mesh, { width, height, rotation = {x, y, z}, zoom, offset = {x, y}, fov, light = {x, y, z},
//                ambient, shine, tint, model_colors, flat, cull })
int gMesh(lua_State* L) {
    const auto* m = static_cast<render::Mesh*>(luaL_checkudata(L, 1, kMeshType));
    render::MeshView v;
    if (lua_istable(L, 2)) {
        auto num = [&](const char* key, float fallback) { return fieldNumber(L, 2, key, 0, fallback); };
        auto vec3 = [&](const char* key, float* out) {
            lua_getfield(L, 2, key);
            if (lua_istable(L, -1)) {
                out[0] = fieldNumber(L, -1, "x", 1, out[0]);
                out[1] = fieldNumber(L, -1, "y", 2, out[1]);
                out[2] = fieldNumber(L, -1, "z", 3, out[2]);
            }
            lua_pop(L, 1);
        };
        v.frame = {num("width", v.frame.x), num("height", v.frame.y)};
        vec3("rotation", v.rotation);
        vec3("light", v.light);
        float offset[3] = {0, 0, 0};
        vec3("offset", offset);
        v.offset = {offset[0], offset[1]};
        v.zoom = num("zoom", v.zoom);
        v.fov = num("fov", v.fov);
        v.ambient = num("ambient", v.ambient);
        v.shine = num("shine", v.shine);
        lua_getfield(L, 2, "tint");
        v.tint = readColor(L, -1, v.tint);
        lua_pop(L, 1);
        lua_getfield(L, 2, "flat");
        v.flat = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "cull");
        v.cullBack = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "model_colors");
        v.meshColors = lua_isnil(L, -1) || lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    canvas(L).mesh(*m, v);
    return 0;
}

// --- the microphone (permission "microphone") -----------------------------------------------

bool mayRecord(lua_State* L) {
    Plugin* p = pluginOf(L);
    return p && p->may("microphone");
}

int micStart(lua_State* L) {
    if (!mayRecord(L)) return luaL_error(L, "add \"microphone\" to the plugin's permissions to record");
    std::string error;
    if (!media::Recorder::get().start(&error)) return luaL_error(L, "%s", error.c_str());
    return 0;
}

// stop() -> the recording's file: <project folder>/Recordings/Recording N.wav
int micStop(lua_State* L) {
    if (!mayRecord(L)) return luaL_error(L, "add \"microphone\" to the plugin's permissions to record");
    fs::path folder = PluginHost::get().projectFolder();
    if (folder.empty()) folder = projectsFolderOrDocuments();
    folder /= "Recordings";
    fs::path file;
    std::error_code ec;
    for (int n = 1;; ++n) {
        file = folder / platform::fromUtf8("Recording " + std::to_string(n) + ".wav");
        if (!fs::exists(file, ec)) break;
    }
    std::string error;
    if (!media::Recorder::get().stop(file, &error)) return luaL_error(L, "%s", error.c_str());
    lua_pushstring(L, platform::toUtf8(file).c_str());
    return 1;
}

int micRecording(lua_State* L) {
    lua_pushboolean(L, media::Recorder::get().recording());
    return 1;
}

int micLevel(lua_State* L) {
    lua_pushnumber(L, media::Recorder::get().level());
    return 1;
}

int micCancel(lua_State* L) {
    if (mayRecord(L)) media::Recorder::get().cancel();
    return 0;
}

// bemotion.parse_csv(text) -> { {cell, ...}, ... } (rows of strings; see core/Data.h).

} // namespace bemotion::pluginlua
