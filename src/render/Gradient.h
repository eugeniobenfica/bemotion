#pragma once

// Gradient fills, shared by shapes, text and plugin objects: from one color to another across a
// box, along an angle (linear) or out from its center (radial).

#include "core/Layer.h"

#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"

namespace bemotion::render {

// Null for ShapeFill::Solid. `angleDeg`: 0 left to right, 90 top to bottom (linear only).
sk_sp<SkShader> gradientShader(ShapeFill fill, SkColor4f from, SkColor4f to, float angleDeg, const SkRect& box);

} // namespace bemotion::render
