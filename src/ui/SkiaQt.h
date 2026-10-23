#pragma once

// Skia drawing straight into a QImage (Format_ARGB32_Premultiplied). That format
// is B, G, R, A in memory on little-endian machines (all the ones bemotion runs
// on), while Skia's native "N32" order is RGBA on macOS: always name the order.

#include "include/core/SkImageInfo.h"

namespace bemotion {

inline SkImageInfo qtImageInfo(int width, int height) {
    return SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
}

} // namespace bemotion
