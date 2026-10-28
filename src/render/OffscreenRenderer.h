#pragma once

// Renders a composition into an off-screen Skia surface and reads the pixels
// back. Used by video export, thumbnails and the intro; it is the same
// drawing path as the viewport, so exports look identical to the preview.

#include "core/Project.h"

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion::render {

class OffscreenRenderer {
public:
    bool init(int width, int height, std::string& error);
    void release() { surface_.reset(); }

    // Renders `comp` at time `t`, scaled by `scale`, into `rgba` (top-down,
    // straight alpha, width * height * 4 bytes).
    void render(const Composition& comp, double t, float scale, bool background, std::vector<uint8_t>& rgba);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    sk_sp<SkSurface> surface_;
    bool onGpu_ = false; // on the graphics card (render/Gpu.h)
    int width_ = 0;
    int height_ = 0;
};

} // namespace bemotion::render
