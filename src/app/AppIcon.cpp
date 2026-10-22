#include "app/AppIcon.h"

#include <algorithm>
#include <cmath>

namespace bemotion {

namespace {

float segmentDistance(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    const float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return std::sqrt(cx * cx + cy * cy);
}

float roundedBoxDistance(float px, float py, float half, float radius) {
    const float qx = std::fabs(px) - half + radius, qy = std::fabs(py) - half + radius;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

} // namespace

std::vector<uint8_t> renderAppIcon(int size) {
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4);
    const float s = static_cast<float>(size);
    // Lowercase "b" in unit coordinates: a stem and a round bowl.
    const float bowlX = 0.54f * s, bowlY = 0.60f * s, bowlR = 0.17f * s;
    const float stemX = bowlX - bowlR, stemTop = 0.20f * s, stemBottom = bowlY + bowlR * 0.35f;
    const float stroke = 0.085f * s;

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const float box = roundedBoxDistance(fx - s * 0.5f, fy - s * 0.5f, s * 0.47f, s * 0.22f);
            const float boxA = std::clamp(0.5f - box, 0.0f, 1.0f);
            const float ring = std::fabs(std::hypot(fx - bowlX, fy - bowlY) - bowlR);
            const float d = std::min(ring, segmentDistance(fx, fy, stemX, stemTop, stemX, stemBottom));
            const float markA = std::clamp(stroke * 0.5f - d + 0.5f, 0.0f, 1.0f);

            // Background #17171A, mark #E8B931.
            const float r = 23 + (232 - 23) * markA, g = 23 + (185 - 23) * markA, b = 26 + (49 - 26) * markA;
            uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 4];
            p[0] = static_cast<uint8_t>(r);
            p[1] = static_cast<uint8_t>(g);
            p[2] = static_cast<uint8_t>(b);
            p[3] = static_cast<uint8_t>(boxA * 255.0f);
        }
    return px;
}

} // namespace bemotion
