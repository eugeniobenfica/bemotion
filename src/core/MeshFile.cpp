#include "core/MeshFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace bemotion {

using json = nlohmann::json;

namespace {

// --- helpers ------------------------------------------------------------------------------

template <typename T> T readLE(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

// Numbers in text without locales or allocations (OBJ and text STL have millions of them).
const char* skipSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}
bool parseFloat(const char*& p, const char* end, float& out) {
    p = skipSpace(p, end);
    const auto r = std::from_chars(p, end, out);
    if (r.ec != std::errc()) return false;
    p = r.ptr;
    return true;
}
bool parseInt(const char*& p, const char* end, long& out) {
    const auto r = std::from_chars(p, end, out);
    if (r.ec != std::errc()) return false;
    p = r.ptr;
    return true;
}

std::string base64Decode(const std::string& in) {
    static const auto table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[static_cast<unsigned char>(chars[i])] = i;
        return t;
    }();
    std::string out;
    out.reserve(in.size() * 3 / 4);
    int bits = 0, value = 0;
    for (unsigned char c : in) {
        if (table[c] < 0) continue;
        value = (value << 6) | table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return out;
}

// One vertex per distinct position (and normal), shared by the triangles that use it.
struct Welder {
    MeshData& mesh;
    std::unordered_map<uint64_t, uint32_t> seen;
    uint32_t add(const float* p, const float* n) {
        // The bits of the coordinates (exact duplicates only: files repeat them bit for bit).
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](const float* v, int count) {
            for (int i = 0; i < count; ++i) {
                uint32_t b;
                std::memcpy(&b, &v[i], 4);
                h = (h ^ b) * 1099511628211ull;
            }
        };
        mix(p, 3);
        if (n) mix(n, 3);
        const auto it = seen.find(h);
        if (it != seen.end()) return it->second;
        const auto index = static_cast<uint32_t>(mesh.vertexCount());
        mesh.positions.insert(mesh.positions.end(), p, p + 3);
        if (n) mesh.normals.insert(mesh.normals.end(), n, n + 3);
        seen.emplace(h, index);
        return index;
    }
};

// --- OBJ ------------------------------------------------------------------------------------

bool loadObj(const std::string& text, MeshData& out, std::string& error) {
    std::vector<float> vp, vn, vc;
    bool anyColor = false, allNormals = true;
    std::unordered_map<uint64_t, uint32_t> corners; // (position, normal) -> vertex
    std::vector<uint32_t> face;
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
        const char* eol = static_cast<const char*>(std::memchr(p, '\n', static_cast<size_t>(end - p)));
        if (!eol) eol = end;
        const char* q = skipSpace(p, eol);
        if (q + 1 < eol && q[0] == 'v' && (q[1] == ' ' || q[1] == '\t')) {
            float v[6];
            const char* r = q + 1;
            int got = 0;
            while (got < 6 && parseFloat(r, eol, v[got])) ++got;
            vp.insert(vp.end(), {got > 0 ? v[0] : 0.0f, got > 1 ? v[1] : 0.0f, got > 2 ? v[2] : 0.0f});
            if (got >= 6) anyColor = true;
            vc.insert(vc.end(), {got >= 6 ? v[3] : 1.0f, got >= 6 ? v[4] : 1.0f, got >= 6 ? v[5] : 1.0f});
        } else if (q + 2 < eol && q[0] == 'v' && q[1] == 'n') {
            float v[3] = {0, 0, 1};
            const char* r = q + 2;
            for (float& x : v) parseFloat(r, eol, x);
            vn.insert(vn.end(), v, v + 3);
        } else if (q + 1 < eol && q[0] == 'f' && (q[1] == ' ' || q[1] == '\t')) {
            face.clear();
            const char* r = q + 1;
            while (true) {
                r = skipSpace(r, eol);
                if (r >= eol || *r == '\r') break;
                long vi = 0, ni = 0;
                if (!parseInt(r, eol, vi)) break;
                bool hasNormal = false;
                if (r < eol && *r == '/') {
                    ++r;
                    long ti = 0;
                    if (r < eol && *r != '/') parseInt(r, eol, ti);
                    if (r < eol && *r == '/') {
                        ++r;
                        hasNormal = parseInt(r, eol, ni);
                    }
                }
                while (r < eol && *r != ' ' && *r != '\t') ++r;
                const long np = static_cast<long>(vp.size() / 3), nn = static_cast<long>(vn.size() / 3);
                if (vi < 0) vi = np + vi + 1;
                if (hasNormal && ni < 0) ni = nn + ni + 1;
                if (vi < 1 || vi > np) continue;
                if (!hasNormal || ni < 1 || ni > nn) hasNormal = false, allNormals = false;
                const uint64_t key = static_cast<uint64_t>(vi) << 32 | static_cast<uint32_t>(hasNormal ? ni : 0);
                auto it = corners.find(key);
                if (it == corners.end()) {
                    const auto index = static_cast<uint32_t>(out.vertexCount());
                    out.positions.insert(out.positions.end(), &vp[(vi - 1) * 3], &vp[(vi - 1) * 3] + 3);
                    if (hasNormal) out.normals.insert(out.normals.end(), &vn[(ni - 1) * 3], &vn[(ni - 1) * 3] + 3);
                    else out.normals.insert(out.normals.end(), {0.0f, 0.0f, 0.0f});
                    out.colors.insert(out.colors.end(), {vc[(vi - 1) * 3], vc[(vi - 1) * 3 + 1], vc[(vi - 1) * 3 + 2], 1.0f});
                    it = corners.emplace(key, index).first;
                }
                face.push_back(it->second);
            }
            for (size_t i = 2; i < face.size(); ++i) out.indices.insert(out.indices.end(), {face[0], face[i - 1], face[i]}); // a fan
        }
        p = eol + 1;
    }
    if (out.indices.empty()) {
        error = "no faces in this OBJ file";
        return false;
    }
    if (!allNormals) out.normals.clear();
    if (!anyColor) out.colors.clear();
    return true;
}

// --- STL ------------------------------------------------------------------------------------

bool loadStl(const std::string& data, MeshData& out, std::string& error) {
    Welder weld{out, {}};
    const bool binary = data.size() >= 84 && 84 + static_cast<size_t>(readLE<uint32_t>(data.data() + 80)) * 50 == data.size();
    if (binary) {
        const uint32_t n = readLE<uint32_t>(data.data() + 80);
        out.indices.reserve(static_cast<size_t>(n) * 3);
        for (uint32_t t = 0; t < n; ++t) {
            const char* at = data.data() + 84 + static_cast<size_t>(t) * 50 + 12;
            for (int v = 0; v < 3; ++v) {
                float p[3];
                std::memcpy(p, at + v * 12, 12);
                out.indices.push_back(weld.add(p, nullptr));
            }
        }
    } else {
        const char* p = data.data();
        const char* end = p + data.size();
        while ((p = static_cast<const char*>(std::memchr(p, 'v', static_cast<size_t>(end - p))))) {
            if (static_cast<size_t>(end - p) > 6 && std::memcmp(p, "vertex", 6) == 0) {
                p += 6;
                float v[3] = {0, 0, 0};
                for (float& x : v) parseFloat(p, end, x);
                out.indices.push_back(weld.add(v, nullptr));
            } else {
                ++p;
            }
        }
        out.indices.resize(out.indices.size() / 3 * 3);
    }
    if (out.indices.size() < 3) {
        error = "no triangles in this STL file";
        return false;
    }
    return true;
}

// --- glTF ------------------------------------------------------------------------------------

using Mat4 = std::array<float, 16>; // column-major, as glTF
constexpr Mat4 kIdentity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 c{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[col * 4 + k];
            c[col * 4 + row] = s;
        }
    return c;
}

Mat4 nodeMatrix(const json& node) {
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16) {
        Mat4 m;
        for (int i = 0; i < 16; ++i) m[i] = node["matrix"][i].get<float>();
        return m;
    }
    auto vec = [&](const char* key, std::vector<float> fallback) {
        if (node.contains(key) && node[key].is_array() && node[key].size() == fallback.size())
            for (size_t i = 0; i < fallback.size(); ++i) fallback[i] = node[key][i].get<float>();
        return fallback;
    };
    const auto t = vec("translation", {0, 0, 0}), r = vec("rotation", {0, 0, 0, 1}), s = vec("scale", {1, 1, 1});
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    return {(1 - 2 * (y * y + z * z)) * s[0], (2 * (x * y + z * w)) * s[0], (2 * (x * z - y * w)) * s[0], 0,
            (2 * (x * y - z * w)) * s[1], (1 - 2 * (x * x + z * z)) * s[1], (2 * (y * z + x * w)) * s[1], 0,
            (2 * (x * z + y * w)) * s[2], (2 * (y * z - x * w)) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0,
            t[0], t[1], t[2], 1};
}

struct Gltf {
    static constexpr size_t kMaxValues = size_t{1} << 26; // per accessor: far past any model shown

    json doc;
    std::vector<std::string> buffers;

    // An accessor's values as floats (normalized integers to 0..1 / -1..1), `count` x `size`.
    bool read(int index, std::vector<float>& out, int& size, std::string& error) const {
        const json& accs = doc.value("accessors", json::array());
        if (index < 0 || index >= static_cast<int>(accs.size())) return error = "a missing accessor", false;
        const json& a = accs[static_cast<size_t>(index)];
        if (a.contains("sparse")) return error = "sparse accessors are not supported", false;
        static const std::unordered_map<std::string, int> sizes = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT4", 16}};
        const auto s = sizes.find(a.value("type", std::string("SCALAR")));
        size = s == sizes.end() ? 1 : s->second;
        const size_t count = a.value("count", 0u);
        const int type = a.value("componentType", 5126);
        const bool normalized = a.value("normalized", false);
        const int bytes = type == 5126 || type == 5125 ? 4 : type == 5122 || type == 5123 ? 2 : 1;
        if (count > kMaxValues) return error = "an accessor too large", false;
        if (!a.contains("bufferView")) { // all zeros
            out.assign(count * static_cast<size_t>(size), 0.0f);
            return true;
        }
        const json views = doc.value("bufferViews", json::array());
        const size_t viewIndex = a["bufferView"].get<size_t>();
        if (viewIndex >= views.size()) return error = "a missing buffer view", false;
        const json& view = views[viewIndex];
        const size_t buffer = view.value("buffer", 0u);
        if (buffer >= buffers.size()) return error = "a missing buffer", false;
        const std::string& data = buffers[buffer];
        const size_t start = view.value("byteOffset", 0u) + a.value("byteOffset", 0u);
        const size_t stride = view.value("byteStride", 0u) ? view.value("byteStride", 0u) : static_cast<size_t>(size * bytes);
        // Every number below the limits above, so the sums cannot wrap around.
        if (start > data.size() || stride > data.size()) return error = "an accessor past its buffer", false;
        if (count && start + (count - 1) * stride + static_cast<size_t>(size * bytes) > data.size()) return error = "an accessor past its buffer", false;
        out.assign(count * static_cast<size_t>(size), 0.0f);
        for (size_t i = 0; i < count; ++i)
            for (int c = 0; c < size; ++c) {
                const char* p = data.data() + start + i * stride + static_cast<size_t>(c * bytes);
                float v = 0;
                switch (type) {
                case 5126: v = readLE<float>(p); break;
                case 5125: v = static_cast<float>(readLE<uint32_t>(p)); break;
                case 5123: v = static_cast<float>(readLE<uint16_t>(p)) / (normalized ? 65535.0f : 1.0f); break;
                case 5122: v = static_cast<float>(readLE<int16_t>(p)) / (normalized ? 32767.0f : 1.0f); break;
                case 5121: v = static_cast<float>(static_cast<uint8_t>(*p)) / (normalized ? 255.0f : 1.0f); break;
                case 5120: v = static_cast<float>(static_cast<int8_t>(*p)) / (normalized ? 127.0f : 1.0f); break;
                default: return error = "an unknown component type", false;
                }
                out[i * static_cast<size_t>(size) + static_cast<size_t>(c)] = v;
            }
        return true;
    }
};

bool loadGltf(const std::string& ext, const std::string& data, const std::function<bool(const std::string&, std::string&)>& readBeside,
              MeshData& out, std::string& error) {
    Gltf g;
    std::string bin;
    if (ext == "glb") {
        if (data.size() < 20 || data.compare(0, 4, "glTF") != 0) return error = "not a GLB file", false;
        size_t pos = 12;
        std::string text;
        while (pos + 8 <= data.size()) {
            const uint32_t length = readLE<uint32_t>(data.data() + pos), kind = readLE<uint32_t>(data.data() + pos + 4);
            if (pos + 8 + length > data.size()) break;
            if (kind == 0x4E4F534A) text = data.substr(pos + 8, length);
            else if (kind == 0x004E4942) bin = data.substr(pos + 8, length);
            pos += 8 + length;
        }
        g.doc = json::parse(text, nullptr, false);
    } else {
        g.doc = json::parse(data, nullptr, false);
    }
    if (!g.doc.is_object()) return error = "the model's JSON could not be read", false;
    for (const auto& e : g.doc.value("extensionsRequired", json::array())) return error = "this model needs " + e.get<std::string>() + ", which is not supported", false;
    for (const json& b : g.doc.value("buffers", json::array())) {
        const std::string uri = b.value("uri", std::string());
        if (uri.empty()) g.buffers.push_back(bin);
        else if (uri.rfind("data:", 0) == 0) g.buffers.push_back(base64Decode(uri.substr(uri.find(',') + 1)));
        else {
            std::string bytes;
            if (!readBeside || !readBeside(uri, bytes)) return error = "the model's data \"" + uri + "\" is not beside it", false;
            g.buffers.push_back(std::move(bytes));
        }
    }
    const json& meshes = g.doc.value("meshes", json::array());
    const json& materials = g.doc.value("materials", json::array());
    auto addMesh = [&](size_t meshIndex, const Mat4& m) -> bool {
        if (meshIndex >= meshes.size()) return true;
        if (out.positions.size() > Gltf::kMaxValues) return error = "a model too large", false;
        for (const json& prim : meshes[meshIndex].value("primitives", json::array())) {
            if (prim.value("mode", 4) != 4 || !prim.contains("attributes") || !prim["attributes"].contains("POSITION")) continue;
            const json& attrs = prim["attributes"];
            std::vector<float> pos, nor, col;
            int ps = 3, ns = 3, cs = 4;
            if (!g.read(attrs["POSITION"].get<int>(), pos, ps, error)) return false;
            if (ps != 3) return error = "positions that are not 3D", false;
            const size_t n = pos.size() / 3;
            // Normals and colors only when there is one for every position.
            const bool hasNormals = attrs.contains("NORMAL") && g.read(attrs["NORMAL"].get<int>(), nor, ns, error) &&
                                    ns == 3 && nor.size() >= n * 3;
            const bool hasColors = attrs.contains("COLOR_0") && g.read(attrs["COLOR_0"].get<int>(), col, cs, error) &&
                                   cs >= 3 && col.size() >= n * static_cast<size_t>(cs);
            float base[4] = {1, 1, 1, 1};
            if (prim.contains("material") && prim["material"].get<size_t>() < materials.size()) {
                const json& f = materials[prim["material"].get<size_t>()].value("pbrMetallicRoughness", json::object()).value("baseColorFactor", json::array());
                if (f.size() == 4)
                    for (int i = 0; i < 4; ++i) base[i] = f[static_cast<size_t>(i)].get<float>();
            }
            const auto first = static_cast<uint32_t>(out.vertexCount());
            for (size_t i = 0; i < n; ++i) {
                const float* p = &pos[i * 3];
                for (int r = 0; r < 3; ++r) out.positions.push_back(m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]);
                if (hasNormals) {
                    const float* q = &nor[i * 3];
                    float v[3];
                    for (int r = 0; r < 3; ++r) v[r] = m[r] * q[0] + m[4 + r] * q[1] + m[8 + r] * q[2];
                    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    for (float& x : v) x = l > 1e-12f ? x / l : 0.0f;
                    out.normals.insert(out.normals.end(), v, v + 3);
                } else {
                    out.normals.insert(out.normals.end(), {0.0f, 0.0f, 0.0f}); // worked out when drawn
                }
                for (int c = 0; c < 4; ++c) out.colors.push_back(base[c] * (hasColors && c < cs ? col[i * static_cast<size_t>(cs) + static_cast<size_t>(c)] : 1.0f));
            }
            if (prim.contains("indices")) {
                std::vector<float> idx;
                int is = 1;
                if (!g.read(prim["indices"].get<int>(), idx, is, error)) return false;
                for (size_t i = 0; i + 2 < idx.size(); i += 3)
                    for (int k = 0; k < 3; ++k) {
                        const float v = idx[i + static_cast<size_t>(k)];
                        if (!(v >= 0.0f && v < static_cast<float>(n)))
                            return error = "a triangle with a missing vertex", false;
                        out.indices.push_back(first + static_cast<uint32_t>(v));
                    }
            } else {
                for (uint32_t i = 0; i + 2 < n; i += 3) out.indices.insert(out.indices.end(), {first + i, first + i + 1, first + i + 2});
            }
        }
        return true;
    };
    const json& nodes = g.doc.value("nodes", json::array());
    // Nodes form a tree; a node among its own ancestors, or one shared so often that the walk
    // would never end, is a broken file.
    std::vector<char> onPath(nodes.size(), 0);
    size_t visits = 0;
    std::function<bool(size_t, const Mat4&, int)> visit = [&](size_t index, const Mat4& parent, int depth) -> bool {
        if (index >= nodes.size() || depth > 64 || onPath[index]) return true;
        if (++visits > 100000) return error = "too many nodes", false;
        const json& node = nodes[index];
        const Mat4 m = mul(parent, nodeMatrix(node));
        if (node.contains("mesh") && !addMesh(node["mesh"].get<size_t>(), m)) return false;
        onPath[index] = 1;
        for (const json& c : node.value("children", json::array()))
            if (!visit(c.get<size_t>(), m, depth + 1)) return false;
        onPath[index] = 0;
        return true;
    };
    const json& scenes = g.doc.value("scenes", json::array());
    const size_t scene = g.doc.value("scene", 0u);
    if (scene < scenes.size()) {
        for (const json& root : scenes[scene].value("nodes", json::array()))
            if (!visit(root.get<size_t>(), kIdentity, 0)) return false;
    } else {
        for (size_t i = 0; i < meshes.size(); ++i)
            if (!addMesh(i, kIdentity)) return false;
    }
    if (out.indices.empty()) return error = "no triangle meshes in this file", false;
    // Normals all or none (a part without them: worked out for every vertex when drawn).
    for (size_t i = 0; i + 2 < out.normals.size(); i += 3)
        if (out.normals[i] == 0.0f && out.normals[i + 1] == 0.0f && out.normals[i + 2] == 0.0f) {
            out.normals.clear();
            break;
        }
    if (std::all_of(out.colors.begin(), out.colors.end(), [](float c) { return c == 1.0f; })) out.colors.clear();
    return true;
}

} // namespace

bool loadMeshFile(const std::string& ext, const std::string& data, const std::function<bool(const std::string&, std::string&)>& readBeside,
                  MeshData& out, std::string& error) {
    out = {};
    bool ok = false;
    try {
        if (ext == "obj") ok = loadObj(data, out, error);
        else if (ext == "stl") ok = loadStl(data, out, error);
        else if (ext == "glb" || ext == "gltf") ok = loadGltf(ext, data, readBeside, out, error);
        else error = "unsupported file type ." + ext;
    } catch (const std::exception& e) { // a malformed file's JSON of the wrong kinds
        error = std::string("the file could not be read: ") + e.what();
        ok = false;
    }
    if (!ok) out = {};
    // No index past the vertices.
    const auto n = static_cast<uint32_t>(out.vertexCount());
    if (ok && std::any_of(out.indices.begin(), out.indices.end(), [n](uint32_t i) { return i >= n; })) {
        std::vector<uint32_t> kept;
        for (size_t t = 0; t + 2 < out.indices.size(); t += 3)
            if (out.indices[t] < n && out.indices[t + 1] < n && out.indices[t + 2] < n) kept.insert(kept.end(), &out.indices[t], &out.indices[t] + 3);
        out.indices = std::move(kept);
    }
    return ok;
}

void zUpToYUp(MeshData& mesh) {
    auto turn = [](std::vector<float>& v) {
        for (size_t i = 0; i + 2 < v.size(); i += 3) {
            const float y = v[i + 1], z = v[i + 2];
            v[i + 1] = z, v[i + 2] = -y;
        }
    };
    turn(mesh.positions);
    turn(mesh.normals);
}

void simplifyMesh(MeshData& mesh, size_t maxTriangles) {
    if (maxTriangles == 0 || mesh.triangleCount() <= maxTriangles) return;
    const size_t n = mesh.vertexCount();
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) lo[c] = std::min(lo[c], mesh.positions[i * 3 + c]), hi[c] = std::max(hi[c], mesh.positions[i * 3 + c]);
    // Finer grids until the result fits (the cells' count grows as the cube of the resolution).
    MeshData best;
    for (int res = 4; res <= 2048; res = static_cast<int>(res * 1.4f) + 1) {
        const float size = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-9f}) / static_cast<float>(res);
        std::unordered_map<uint64_t, uint32_t> cells;
        std::vector<uint32_t> cellOf(n);
        MeshData m;
        std::vector<float> weight;
        for (size_t i = 0; i < n; ++i) {
            uint64_t key = 0;
            for (int c = 0; c < 3; ++c) key = key * 4099 + static_cast<uint64_t>((mesh.positions[i * 3 + c] - lo[c]) / size);
            auto it = cells.find(key);
            if (it == cells.end()) {
                it = cells.emplace(key, static_cast<uint32_t>(weight.size())).first;
                weight.push_back(0.0f);
                m.positions.insert(m.positions.end(), {0, 0, 0});
                if (!mesh.normals.empty()) m.normals.insert(m.normals.end(), {0, 0, 0});
                if (!mesh.colors.empty()) m.colors.insert(m.colors.end(), {0, 0, 0, 0});
            }
            const uint32_t v = cellOf[i] = it->second;
            weight[v] += 1.0f; // each cell's vertex: the average of the vertices in it
            for (int c = 0; c < 3; ++c) m.positions[v * 3 + c] += mesh.positions[i * 3 + c];
            if (!mesh.normals.empty())
                for (int c = 0; c < 3; ++c) m.normals[v * 3 + c] += mesh.normals[i * 3 + c];
            if (!mesh.colors.empty())
                for (int c = 0; c < 4; ++c) m.colors[v * 4 + c] += mesh.colors[i * 4 + c];
        }
        for (size_t v = 0; v < weight.size(); ++v) {
            for (int c = 0; c < 3; ++c) m.positions[v * 3 + c] /= weight[v];
            if (!m.colors.empty())
                for (int c = 0; c < 4; ++c) m.colors[v * 4 + c] /= weight[v];
            if (!m.normals.empty()) {
                float* q = &m.normals[v * 3];
                const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
                for (int c = 0; c < 3; ++c) q[c] = l > 1e-12f ? q[c] / l : 0.0f;
            }
        }
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const uint32_t a = cellOf[mesh.indices[t]], b = cellOf[mesh.indices[t + 1]], c = cellOf[mesh.indices[t + 2]];
            if (a != b && b != c && a != c) m.indices.insert(m.indices.end(), {a, b, c}); // collapsed ones go
        }
        if (m.triangleCount() > maxTriangles) {
            if (best.indices.empty()) best = std::move(m); // even the coarsest grid keeps more: it, then
            break;
        }
        best = std::move(m);
    }
    if (!best.indices.empty()) mesh = std::move(best);
}

} // namespace bemotion
