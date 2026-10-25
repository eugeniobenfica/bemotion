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

// --- Bézier paths (pen tool shapes, masks) ----------------------------------------------
// A shape's outline as a Bézier path (After Effects' Convert to Bezier Path): rectangles and
// ellipses with curved handles, polygons, stars and lines as corners. A path shape: its own.
BezierPath shapeToBezier(const ShapeProps& shape, double t);
// A rectangle / an ellipse filling the box from `mn` to `mx` (masks made from a layer's bounds).
BezierPath rectanglePath(Vec2 mn, Vec2 mx);
BezierPath ellipsePath(Vec2 mn, Vec2 mx);
size_t segmentCount(const BezierPath& path);
// The point at `u` (0..1) of the segment from vertex i to the next.
Vec2 bezierPoint(const BezierPath& path, size_t i, float u);
// The path as straight lines, `steps` per curved segment (trims, hit tests).
Path flattenBezier(const BezierPath& path, int steps = 16);
// Bounds of the points and their handles (the curve stays inside them).
bool bezierBounds(const BezierPath& path, Vec2& mn, Vec2& mx);
// The segment nearest to `p` (within `maxDistance`), and where along it (u): to add a vertex.
bool nearestSegment(const BezierPath& path, Vec2 p, float maxDistance, size_t& segment, float& u);
// Splits segment i at u with a new vertex, keeping the curve's shape (de Casteljau).
void insertVertex(BezierPath& path, size_t segment, float u);
// The path through an affine transform (points and handles).
BezierPath transformed(const BezierPath& path, const Affine& m);

} // namespace bemotion
