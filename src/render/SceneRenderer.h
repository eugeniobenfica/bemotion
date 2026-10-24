#pragma once

// Draws compositions with Skia.
//
// Layers draw in their own local space through the canvas transform, so
// shapes, text and footage stay sharp at any zoom, rotation or scale. A layer
// with effects is drawn into an offscreen layer (SkCanvas::saveLayer) whose
// image filter chain applies the effects in output pixels; Motion Blur
// averages several moments inside the shutter.

#include "core/Math.h"
#include "core/Project.h"

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"

#include <functional>

class SkCanvas;

namespace bemotion::render {

// Maps composition pixels to output (screen or file) pixels. `camera` (scene ->
// composition, see Composition::cameraAt) only affects matrix() and the scene
// helpers: layers live in scene space, guides and rulers in composition space.
struct View {
    Vec2 origin; // output position of the composition's top-left corner
    float zoom = 1.0f;
    Affine camera;

    Affine matrix() const { return Affine::translate(origin) * Affine::scale(zoom) * camera; }
    Vec2 toScreen(Vec2 p) const { return origin + p * zoom; }
    Vec2 toComp(Vec2 s) const { return (s - origin) / zoom; }
    Vec2 sceneToScreen(Vec2 p) const { return matrix().apply(p); }
    Vec2 screenToScene(Vec2 s) const { return matrix().inverse().apply(s); }
};

// The canvas' base transform is identity: View maps straight to output pixels.
// `background` = false leaves the frame transparent (exports with alpha).
// throughCamera: render the scene as the active camera sees it (the default).
void drawComposition(SkCanvas* canvas, const Composition& comp, double time, const View& view, bool background = true,
                     bool throughCamera = true);

// One layer with its effects, `layerToOutput` being its full transform and
// `zoom` the composition -> output scale (effect sizes are in composition
// pixels). opacityOverride >= 0 replaces the layer's opacity. Compounds draw
// their children, which needs `comp`.
void drawLayer(SkCanvas* canvas, const Layer& layer, double time, const Affine& layerToOutput, float zoom,
               const Composition* comp, float opacityOverride = -1.0f);

// Axis-aligned bounds of the layer content in its local space (a compound's
// cover its children, which needs `comp`).
bool localBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax, const Composition* comp = nullptr);

bool hasActiveEffects(const Layer& layer);

// Media layers get their picture from the media module (null while not available).
using MediaProvider = std::function<sk_sp<SkImage>(const Composition& comp, const Layer& layer, double time)>;
void setMediaProvider(MediaProvider provider);

// The direct child of a compound under a composition point (0 if none), for
// picking a layer inside a compound. throughCamera: as the camera shows it
// (Composition::viewFor), else as the free view does.
uint64_t hitTestInside(const Composition& comp, double time, Vec2 compPoint, uint64_t compoundId, bool throughCamera);

// Top-most visible, unlocked layer under a composition point, in drawing order
// (3D layers by depth). Cameras only count with includeCameras (their frame is their body).
uint64_t hitTest(const Composition& comp, double time, Vec2 compPoint, bool throughCamera, bool includeCameras = false);

} // namespace bemotion::render
