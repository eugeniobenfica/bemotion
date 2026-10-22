#pragma once

// Renders a composition into an off-screen OpenGL framebuffer and reads the
// pixels back. Used by video export; it shares the exact drawing path of the
// viewport, so exports look identical to the preview.
//
// Must be called on the thread that owns the GL context, with an ImGui context
// current (fonts are rasterized through ImGui's atlas).

#include "core/Project.h"
#include "render/EffectRenderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion::render {

class OffscreenRenderer {
public:
    OffscreenRenderer() = default;
    OffscreenRenderer(const OffscreenRenderer&) = delete;
    OffscreenRenderer& operator=(const OffscreenRenderer&) = delete;
    ~OffscreenRenderer();

    bool init(int width, int height, std::string& error);
    void release();

    // Renders `comp` at time `t`, scaled by `scale`, into `rgba` (top-down,
    // straight alpha, width * height * 4 bytes).
    void render(const Composition& comp, double t, float scale, bool background, std::vector<uint8_t>& rgba);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    EffectRenderer effects_; // separate pool: exporting must not reuse the preview's textures
    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace bemotion::render
