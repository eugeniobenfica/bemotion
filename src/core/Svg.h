#pragma once

// SVG drawings as shape layers (File › Import SVG, svg.add): paths, rectangles, circles,
// ellipses, lines, polylines and polygons, through their groups' transforms, with their fill
// (a color, or a gradient's first and last colors), stroke and opacity. Each outline becomes a
// Path shape layer; an outline inside another (a letter's hole) cuts it as a Subtract mask.
// Text, images, filters, patterns and CSS classes are left out.

#include "core/Project.h"

#include <string>
#include <vector>

namespace bemotion {

struct SvgShape {
    std::string name;            // the element's id, else its kind ("path", "circle"...)
    BezierPath outline;          // in the drawing's units, the transforms applied
    std::vector<BezierPath> holes;
    bool fill = true;
    ShapeFill fillKind = ShapeFill::Solid;
    Color fillColor{0, 0, 0, 1}, fillColor2{0, 0, 0, 1};
    float fillAngle = 90.0f; // linear gradients
    bool stroke = false;
    Color strokeColor{0, 0, 0, 1};
    float strokeWidth = 1.0f;
    float opacity = 1.0f;
};

struct SvgDrawing {
    Vec2 origin{0, 0}, size{0, 0}; // the view box
    std::vector<SvgShape> shapes;  // bottom first
};

// False (and why) when the text is not an SVG with something to draw.
bool parseSvg(const std::string& text, SvgDrawing& out, std::string* error = nullptr);

// Adds the drawing at `time`, fitted in the middle of the composition (80% of its size): one
// shape layer per outline, in a compound named `name` when there are several. Returns the top
// layer's id (0 and why when nothing could be added).
uint64_t addSvg(Composition& comp, const std::string& text, const std::string& name, double time, std::string* error = nullptr);

} // namespace bemotion
