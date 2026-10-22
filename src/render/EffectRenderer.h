#pragma once

// GPU layer effects.
//
// A layer with enabled effects is rendered off screen at output resolution
// (premultiplied RGBA16F), then each effect runs as a shader pass:
//
//   content   the layer itself; with Motion Blur, several moments inside the
//             shutter interval are rendered and averaged (accumulation)
//   Outline   dilate the silhouette (max alpha on a disc), tint, put underneath
//   Glow      tinted silhouette -> separable Gaussian blur -> underneath
//   Shadow    like glow, shifted by the offset
//
// The result is un-premultiplied and added to the draw list as an image, so
// it composites like any other primitive (and is clipped the same way).
// Render targets live in a pool that is recycled once per frame.

#include "core/Project.h"
#include "core/Math.h"

#include <deque>
#include <vector>

struct ImDrawList;

namespace bemotion::render {

class EffectRenderer {
public:
    EffectRenderer() = default;
    EffectRenderer(const EffectRenderer&) = delete;
    EffectRenderer& operator=(const EffectRenderer&) = delete;

    // Recycles the previous frame's render targets. Call once per rendered frame.
    void beginFrame() { used_ = 0; }
    // Frees GPU resources (call while the GL context is still alive).
    void release();

    // Draws `layer` with its effects. `viewMatrix` maps composition pixels to
    // output pixels and `compToScreen` is its scale (effect sizes are in
    // composition pixels).
    // `comp` lets compounds draw their children; `opacityScale` is an enclosing compound's opacity.
    void drawLayer(ImDrawList* dl, const Layer& layer, double t, double frameDuration, const Affine& viewMatrix,
                   float compToScreen, const Composition* comp = nullptr, float opacityScale = 1.0f);

private:
    struct Target {
        unsigned int texture = 0;
        unsigned int fbo = 0;
        int width = 0;
        int height = 0;
    };
    Target& acquire(int width, int height);

    std::deque<Target> pool_; // a deque: nested draws (compounds) add targets while outer ones are in use
    size_t used_ = 0;
};

// The instance used for on-screen drawing (viewport, thumbnails).
EffectRenderer& screenEffects();

bool hasActiveEffects(const Layer& layer);

} // namespace bemotion::render
