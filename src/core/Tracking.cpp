#include "core/Tracking.h"

#include <algorithm>
#include <cmath>

namespace bemotion {

GrayImage grayFromRgba(const uint8_t* rgba, int width, int height) {
    GrayImage g;
    g.width = std::max(0, width);
    g.height = std::max(0, height);
    g.pixels.resize(static_cast<size_t>(g.width) * g.height);
    for (size_t i = 0; i < g.pixels.size(); ++i) {
        const uint8_t* p = rgba + i * 4;
        g.pixels[i] = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8); // Rec. 601 luma
    }
    return g;
}

TrackResult trackPatch(const GrayImage& prev, const GrayImage& next, Vec2 point, Vec2 guess, int patchRadius, int searchRadius) {
    TrackResult r{guess, 0.0f};
    const int pr = std::max(2, patchRadius), sr = std::max(1, searchRadius);
    const int px = static_cast<int>(std::lround(point.x)), py = static_cast<int>(std::lround(point.y));
    if (prev.width <= 0 || next.width <= 0 || px - pr < 0 || py - pr < 0 || px + pr >= prev.width || py + pr >= prev.height) return r;
    // The patch, minus its mean (normalized cross-correlation ignores brightness changes).
    const int side = 2 * pr + 1, count = side * side;
    std::vector<float> patch(static_cast<size_t>(count));
    double mean = 0.0;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) mean += prev.at(px - pr + x, py - pr + y);
    mean /= count;
    double norm = 0.0;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const float v = static_cast<float>(prev.at(px - pr + x, py - pr + y) - mean);
            patch[static_cast<size_t>(y * side + x)] = v;
            norm += static_cast<double>(v) * v;
        }
    if (norm < 1e-6) return r; // a flat patch: nothing to follow
    const int gx = static_cast<int>(std::lround(guess.x)), gy = static_cast<int>(std::lround(guess.y));
    auto score = [&](int cx, int cy) -> float {
        if (cx - pr < 0 || cy - pr < 0 || cx + pr >= next.width || cy + pr >= next.height) return -2.0f;
        double m = 0.0;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) m += next.at(cx - pr + x, cy - pr + y);
        m /= count;
        double dot = 0.0, n2 = 0.0;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const double v = next.at(cx - pr + x, cy - pr + y) - m;
                dot += v * patch[static_cast<size_t>(y * side + x)];
                n2 += v * v;
            }
        return n2 < 1e-6 ? -1.0f : static_cast<float>(dot / std::sqrt(norm * n2));
    };
    // Coarse to fine: every other pixel over the whole search area, then every pixel around the best.
    int bx = gx, by = gy;
    float best = -3.0f;
    for (int y = gy - sr; y <= gy + sr; y += 2)
        for (int x = gx - sr; x <= gx + sr; x += 2)
            if (const float s = score(x, y); s > best) best = s, bx = x, by = y;
    const int cx0 = bx, cy0 = by;
    for (int y = cy0 - 2; y <= cy0 + 2; ++y)
        for (int x = cx0 - 2; x <= cx0 + 2; ++x)
            if (const float s = score(x, y); s > best) best = s, bx = x, by = y;
    if (best < -1.5f) return r;
    // A fraction of a pixel: the top of a parabola through the best and its neighbors.
    auto refine = [](float a, float b, float c) {
        const float d = a - 2.0f * b + c;
        return std::fabs(d) < 1e-6f ? 0.0f : std::clamp(0.5f * (a - c) / d, -0.5f, 0.5f);
    };
    const float dx = refine(score(bx - 1, by), best, score(bx + 1, by));
    const float dy = refine(score(bx, by - 1), best, score(bx, by + 1));
    // The patch's own offset from the whole-pixel point it was cut around.
    const Vec2 fraction{point.x - static_cast<float>(px), point.y - static_cast<float>(py)};
    r.point = Vec2{static_cast<float>(bx) + dx, static_cast<float>(by) + dy} + fraction;
    r.confidence = std::clamp(best, 0.0f, 1.0f);
    return r;
}

GrayImage halfSize(const GrayImage& g) {
    GrayImage h;
    h.width = g.width / 2;
    h.height = g.height / 2;
    h.pixels.resize(static_cast<size_t>(h.width) * h.height);
    for (int y = 0; y < h.height; ++y)
        for (int x = 0; x < h.width; ++x)
            h.pixels[static_cast<size_t>(y) * h.width + x] =
                static_cast<uint8_t>((g.at(2 * x, 2 * y) + g.at(2 * x + 1, 2 * y) + g.at(2 * x, 2 * y + 1) + g.at(2 * x + 1, 2 * y + 1) + 2) / 4);
    return h;
}

TrackResult trackPyramid(const GrayImage& prev, const GrayImage& next, Vec2 point, Vec2 guess, int patchRadius, int searchRadius) {
    if (searchRadius <= 16 || patchRadius < 8 || prev.width < 64) return trackPatch(prev, next, point, guess, patchRadius, searchRadius);
    const TrackResult coarse = trackPatch(halfSize(prev), halfSize(next), point * 0.5f, guess * 0.5f, patchRadius / 2, (searchRadius + 1) / 2);
    const Vec2 near = coarse.confidence > 0.0f ? coarse.point * 2.0f : guess;
    const TrackResult fine = trackPatch(prev, next, point, near, patchRadius, 4);
    // The full-size search around the guess when the coarse one went wrong.
    if (fine.confidence < 0.5f) {
        const TrackResult wide = trackPatch(prev, next, point, guess, patchRadius, searchRadius);
        if (wide.confidence > fine.confidence) return wide;
    }
    return fine;
}

} // namespace bemotion
