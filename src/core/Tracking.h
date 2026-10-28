#pragma once

// Point tracking (After Effects' Track Motion): a small patch of one frame found again in the
// next, frame after frame. Pure pixels in, positions out; the editor reads the video's frames
// (ui/Tracking) and turns the positions into keyframes.

#include "core/Math.h"

#include <cstdint>
#include <vector>

namespace bemotion {

// A frame in shades of gray (0..255), row by row.
struct GrayImage {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    uint8_t at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};
GrayImage grayFromRgba(const uint8_t* rgba, int width, int height);

struct TrackResult {
    Vec2 point;             // where the patch is in `next`
    float confidence = 0.0f; // 0..1: how alike (normalized cross-correlation of the best match)
};

// Finds the patch of `prev` centered on `point` (`patchRadius` px around it) in `next`, within
// `searchRadius` px of `guess` (where it is expected: the last position, or that plus the last
// motion). Whole pixels, then refined to a fraction of one.
TrackResult trackPatch(const GrayImage& prev, const GrayImage& next, Vec2 point, Vec2 guess, int patchRadius = 12, int searchRadius = 32);

// The same, coarse to fine: a wide search first on pictures half the size (fast motion,
// big searches stay quick), then refined at full size near what it found.
TrackResult trackPyramid(const GrayImage& prev, const GrayImage& next, Vec2 point, Vec2 guess, int patchRadius, int searchRadius);
GrayImage halfSize(const GrayImage& g);

} // namespace bemotion
