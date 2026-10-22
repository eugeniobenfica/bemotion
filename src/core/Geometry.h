#pragma once

#include "core/Layer.h"
#include "core/Math.h"

#include <vector>

namespace bemotion {

struct Path {
    std::vector<Vec2> points;
    bool closed = false;
    bool convex = true;
};

// Outline of a shape layer in local space, centered on the origin.
Path buildShapePath(const ShapeProps& shape, double t);

// Portion of `path` between `start` and `end` (0..1 of its total length).
// The result is always an open path.
Path trimPath(const Path& path, float start, float end);

} // namespace bemotion
