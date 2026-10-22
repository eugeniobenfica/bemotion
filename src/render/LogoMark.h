#pragma once

// The bemotion mark: a disc with a lowercase "b" cut out of it (stem and
// bowl ring), the bowl itself filled. Unit coordinates: the disc is centered
// at (0, 0) with radius 1, y down.
//
//   mark = (disc - ring circle - stem) + bowl
//
// Shared by the window icon, the UI logo and the brand assets so they all
// draw exactly the same shape.

#include "core/Math.h"

#include <vector>

namespace bemotion::logo {

inline constexpr Vec2 kBowlCenter{0.368f, 0.045f};
inline constexpr float kBowlRadius = 0.525f;  // the filled bowl
inline constexpr float kRingRadius = 0.71f;   // outer edge of the cut around it
inline constexpr float kStemLeft = kBowlCenter.x - kRingRadius;
inline constexpr float kStemRight = kBowlCenter.x - kBowlRadius;

// Signed distance to the mark (negative inside), in unit coordinates.
float distance(Vec2 p);

// The mark as filled shapes: the bowl disc plus the outline(s) of the rest,
// as closed concave polygons (`segments` points per full circle).
std::vector<std::vector<Vec2>> outlines(int segments = 96);

} // namespace bemotion::logo
