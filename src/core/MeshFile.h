#pragma once

// 3D model files read into triangle meshes (for the 3D Models plugin, through
// bemotion.load_mesh): OBJ, STL (binary and text), glTF 2.0 (.glb, and .gltf with its data
// embedded or in .bin files beside it). Pure data: the bytes come in, positions, normals,
// colors and triangles come out. Not read: textures, compressed meshes (Draco), skinning, morphs.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bemotion {

struct MeshData {
    std::vector<float> positions; // x, y, z per vertex
    std::vector<float> normals;   // x, y, z per vertex, or empty (worked out when drawn)
    std::vector<float> colors;    // r, g, b, a per vertex, or empty (white)
    std::vector<uint32_t> indices; // three per triangle
    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
};

// `readBeside(name, bytes)`: a file the model names (a .gltf's buffers), from its folder; false
// when it is not there. `ext` without the dot, lower case.
bool loadMeshFile(const std::string& ext, const std::string& data, const std::function<bool(const std::string&, std::string&)>& readBeside,
                  MeshData& out, std::string& error);

// Z-up models (most CAD and STL files) turned so that up is y.
void zUpToYUp(MeshData& mesh);

// Fewer triangles, keeping the shape (vertex clustering on a grid over its bounds): for models
// far more detailed than they show. Nothing changes at or under `maxTriangles`.
void simplifyMesh(MeshData& mesh, size_t maxTriangles);

} // namespace bemotion
