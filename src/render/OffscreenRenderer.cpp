#include "render/OffscreenRenderer.h"

#include "render/SceneRenderer.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"

namespace bemotion::render {

bool OffscreenRenderer::init(int width, int height, std::string& error) {
    surface_ = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
    if (!surface_) {
        error = "Could not allocate a " + std::to_string(width) + " x " + std::to_string(height) + " frame.";
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void OffscreenRenderer::render(const Composition& comp, double t, float scale, bool background, std::vector<uint8_t>& rgba) {
    SkCanvas* canvas = surface_->getCanvas();
    canvas->resetMatrix();
    canvas->clear(SK_ColorTRANSPARENT);
    drawComposition(canvas, comp, t, View{{0.0f, 0.0f}, scale, {}}, background);
    rgba.resize(static_cast<size_t>(width_) * height_ * 4);
    const SkImageInfo straight = SkImageInfo::Make(width_, height_, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    surface_->readPixels(straight, rgba.data(), static_cast<size_t>(width_) * 4, 0, 0);
}

} // namespace bemotion::render
