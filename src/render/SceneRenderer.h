#pragma once

// Draws compositions into an ImDrawList.
//
// Each primitive is generated in layer space (pre-scaled to roughly screen
// resolution so anti-aliasing stays crisp) and its vertices are then mapped
// through the layer -> screen affine transform. This gives full support for
// rotation and non-uniform scale on shapes and text alike.

#include "core/Math.h"
#include "core/Project.h"

struct ImDrawList;

namespace mirage::render {

// Maps composition pixels to screen pixels.
struct View {
    Vec2 origin; // screen position of the composition's top-left corner
    float zoom = 1.0f;

    Affine matrix() const { return Affine::translate(origin) * Affine::scale(zoom); }
    Vec2 toScreen(Vec2 p) const { return origin + p * zoom; }
    Vec2 toComp(Vec2 s) const { return (s - origin) / zoom; }
};

void drawComposition(ImDrawList* dl, const Composition& comp, double time, const View& view);
void drawLayer(ImDrawList* dl, const Layer& layer, double time, const Affine& layerToScreen);

// Axis-aligned bounds of the layer content in its local space.
bool localBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax);

// Top-most visible, unlocked layer under `compPoint`, or 0.
uint64_t hitTest(const Composition& comp, double time, Vec2 compPoint);

} // namespace mirage::render
