#pragma once

// Bending a layer's picture: Puppet pins (After Effects' Puppet tool) and Mesh Warp. Pure
// geometry, shared by the renderer (which draws the picture through a mesh of these points)
// and the viewport (which shows the pins and the grid where they are).
//
//   Puppet: as in After Effects, a mesh covers what the picture shows (its outline: the
//   renderer tells which cells of a grid hold something, see PuppetShape); each pin sits on the
//   mesh at rest and is moved to its position, and the mesh follows as rigidly as it can
//   (as-rigid-as-possible, Sorkine & Alexa 2007, started from moving least squares, Schaefer et
//   al. 2006). Separate pieces of the picture have meshes of their own: a piece without pins
//   stays put, one pin moves its piece, two or more bend it. Starch pins hold nothing in place:
//   they stiffen the mesh around them (an arm that should not bend there).
//   Warp: a 4 x 4 grid over the layer's bounds; each grid point moved by an offset, the points
//   between them smoothly (bicubic, Catmull-Rom).

#include "core/Layer.h"
#include "core/Math.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bemotion {

// One pin: where it is at rest and where it went.
using PinMove = std::pair<Vec2, Vec2>;
Vec2 puppetPoint(const std::vector<PinMove>& pins, Vec2 v);

// `offsets`: kWarpPoints of them, row by row from the top left; `mn` / `mx`: the grid's box.
Vec2 warpPoint(const Vec2* offsets, Vec2 mn, Vec2 mx, Vec2 v);

// Where the picture is, for its Puppet mesh: which cells of a cols x rows grid over [lo, hi]
// (the picture at rest, in the layer's space) hold something. The renderer makes it from the
// picture's alpha once (`key` names what it was made from); without one, the whole box counts.
struct PuppetShape {
    std::string key;
    Vec2 lo, hi;
    int cols = 0, rows = 0;
    std::vector<uint8_t> inside; // cols * rows, row by row
};
// The grid's size for a box: about 24 cells along its longer side.
void puppetGrid(Vec2 lo, Vec2 hi, int& cols, int& rows);
void setPuppetShape(uint64_t layerId, PuppetShape shape);
std::string puppetShapeKey(uint64_t layerId); // empty: none yet
// The rest point `v` is on the picture's mesh (a pin can go there).
bool puppetOnMesh(const Layer& layer, Vec2 v, Vec2 mn, Vec2 mx);
// The mesh's edges where they are at `t` (warp and pins applied), to show it.
std::vector<std::pair<Vec2, Vec2>> puppetMeshLines(const Layer& layer, double t, Vec2 mn, Vec2 mx);

// The layer at `t` bends its picture (pins moved, or a warp with moved points).
bool deforms(const Layer& layer, double t);
// Where the point `v` of the layer's picture (its own space) lands at `t`: the warp first, then
// the pins. `mn` / `mx`: the picture's bounds (the warp grid's box).
Vec2 deformPoint(const Layer& layer, double t, Vec2 v, Vec2 mn, Vec2 mx);

} // namespace bemotion
