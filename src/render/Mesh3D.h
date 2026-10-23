#pragma once

// Triangle meshes drawn flat into the 2D scene (plugin objects such as 3D
// models). The mesh is projected with an orbit camera (perspective or
// orthographic), lit by one directional light plus ambient light, and its
// triangles painted back to front, so it composes, animates and exports like
// any layer. Not a 3D engine: no depth buffer (intersecting surfaces may sort
// wrongly), no textures, no shadows.

#include "core/Layer.h"
#include "core/Math.h"

#include <cstdint>
#include <vector>

class SkCanvas;

namespace bemotion::render {

struct Mesh {
    std::vector<float> positions; // x, y, z per vertex
    std::vector<float> normals;   // per vertex (computed when the source had none)
    std::vector<Color> colors;    // per vertex
    std::vector<uint32_t> indices; // three per triangle
    float center[3] = {0, 0, 0};
    float radius = 1.0f; // of the bounding sphere around `center`

    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
    // After filling positions / indices (and maybe normals / colors): fills what is missing,
    // drops bad indices, computes the bounds.
    void finish();
};

struct MeshView {
    float rotation[3] = {0, 0, 0}; // degrees: turns around x (tilt), y (spin), z (roll)
    float zoom = 1.0f;             // 1: the mesh fits the frame
    Vec2 frame{600, 600};          // the object's frame, centered on the origin
    Vec2 offset{0, 0};
    float fov = 30.0f;             // degrees; 0: orthographic
    float light[3] = {-0.4f, 0.6f, 0.7f}; // direction toward the light (x right, y up, z toward the viewer)
    float ambient = 0.35f;         // 0..1
    float shine = 0.25f;           // specular highlight 0..1
    Color tint = Color::fromHex(0xFFFFFF); // multiplies the mesh colors
    bool meshColors = true;        // false: every vertex is white (the tint alone colors it)
    bool flat = false;             // faceted (one normal per triangle) or smooth
    bool cullBack = false;         // skip triangles facing away
    float opacity = 1.0f;
};

void drawMesh(SkCanvas* canvas, const Mesh& mesh, const MeshView& view);

} // namespace bemotion::render
