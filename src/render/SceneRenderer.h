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

namespace bemotion::render {

class EffectRenderer;

// Maps composition pixels to screen pixels. `camera` (scene -> composition,
// see Composition::cameraAt) only affects matrix() and the scene helpers:
// layers live in scene space, guides and rulers in composition space.
struct View {
    Vec2 origin; // screen position of the composition's top-left corner
    float zoom = 1.0f;
    Affine camera;

    Affine matrix() const { return Affine::translate(origin) * Affine::scale(zoom) * camera; }
    Vec2 toScreen(Vec2 p) const { return origin + p * zoom; }
    Vec2 toComp(Vec2 s) const { return (s - origin) / zoom; }
    Vec2 sceneToScreen(Vec2 p) const { return matrix().apply(p); }
    Vec2 screenToScene(Vec2 s) const { return matrix().inverse().apply(s); }
};

// `background` = false leaves the frame transparent (for exports with alpha).
// Layers with effects go through `effects` (default: the on-screen instance).
// throughCamera: render the scene as the active camera sees it (the default).
void drawComposition(ImDrawList* dl, const Composition& comp, double time, const View& view, bool background = true,
                     EffectRenderer* effects = nullptr, bool throughCamera = true);
// Draws the layer without its effects. opacityOverride >= 0 replaces its opacity.
void drawLayer(ImDrawList* dl, const Layer& layer, double time, const Affine& layerToScreen, float opacityOverride = -1.0f);

// Axis-aligned bounds of the layer content in its local space.
bool localBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax);

// Top-most visible, unlocked layer under a scene-space point. Cameras only
// count with includeCameras (their frame is their body).
uint64_t hitTest(const Composition& comp, double time, Vec2 scenePoint, bool includeCameras = false);

} // namespace bemotion::render
