#include "render/Gradient.h"

#include "core/Math.h"

#include "include/effects/SkGradientShader.h"

#include <algorithm>
#include <cmath>

namespace bemotion::render {

sk_sp<SkShader> gradientShader(ShapeFill fill, SkColor4f from, SkColor4f to, float angleDeg, const SkRect& box) {
    if (fill == ShapeFill::Solid) return nullptr;
    const SkColor4f colors[2] = {from, to};
    const float w = std::max(1.0f, box.width()), h = std::max(1.0f, box.height());
    if (fill == ShapeFill::Linear) { // end to end along the angle, like a background's
        const float a = degToRad(angleDeg);
        const float dx = std::cos(a), dy = std::sin(a);
        const float half = (std::fabs(w * dx) + std::fabs(h * dy)) * 0.5f;
        const SkPoint pts[2] = {{box.centerX() - dx * half, box.centerY() - dy * half}, {box.centerX() + dx * half, box.centerY() + dy * half}};
        return SkGradientShader::MakeLinear(pts, colors, nullptr, nullptr, 2, SkTileMode::kClamp);
    }
    return SkGradientShader::MakeRadial({box.centerX(), box.centerY()}, std::max(w, h) * 0.5f, colors, nullptr, nullptr, 2, SkTileMode::kClamp);
}

} // namespace bemotion::render
