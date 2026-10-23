#pragma once

// Drawing surface handed to plugin objects (see app/PluginHost).
//
// Coordinates are in the layer's local space: the origin is the object's
// center and its frame spans +-size/2. The canvas already carries the layer
// transform, so plugin objects rotate, scale, take effects and export like
// any layer.

#include "core/Layer.h"
#include "core/Math.h"
#include "render/Text.h"

#include <functional>
#include <string>
#include <vector>

class SkCanvas;

namespace bemotion::render {

class PluginCanvas {
public:
    PluginCanvas(SkCanvas* canvas, float opacity) : canvas_(canvas), opacity_(opacity) {}

    void rect(Vec2 min, Vec2 max, Color color, float radius = 0.0f);
    void rectOutline(Vec2 min, Vec2 max, Color color, float width, float radius = 0.0f);
    void circle(Vec2 center, float radius, Color color);
    void line(Vec2 a, Vec2 b, float width, Color color);
    void polyline(const std::vector<Vec2>& points, float width, Color color, bool closed = false);
    void polygon(const std::vector<Vec2>& points, Color color); // any simple polygon
    // Annular sector (pie slice / donut segment). Angles in degrees, clockwise from 12 o'clock.
    void arc(Vec2 center, float innerRadius, float outerRadius, float fromDeg, float toDeg, Color color);
    // `at.y` is the vertical center of the line of text.
    void text(const std::string& s, Vec2 at, float size, Color color, TextAnchor anchor = TextAnchor::Left,
              bool bold = false);
    float measure(const std::string& s, float size, bool bold = false) const;

private:
    Color faded(Color c) const { return {c.r, c.g, c.b, c.a * opacity_}; }
    SkCanvas* canvas_;
    float opacity_; // layer opacity, multiplied into every color
};

// Installed by the plugin host; called for every visible plugin layer.
using PluginDrawer = std::function<void(const Layer& layer, double time, PluginCanvas& canvas)>;
void setPluginDrawer(PluginDrawer drawer);

} // namespace bemotion::render
