#include "core/Animated.h"

namespace bemotion {

namespace {

constexpr int kArcSamples = 24; // arc-length table resolution per segment

} // namespace

Vec2 PathSegment::at(float s) const {
    const float u = 1.0f - s;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * s) + p2 * (3.0f * u * s * s) + p3 * (s * s * s);
}

void pathTangents(const std::vector<Keyframe<Vec2>>& keys, size_t i, Vec2& in, Vec2& out) {
    const Keyframe<Vec2>& k = keys[i];
    const bool hasPrev = i > 0, hasNext = i + 1 < keys.size();
    const Vec2 toPrev = hasPrev ? keys[i - 1].value - k.value : Vec2{};
    const Vec2 toNext = hasNext ? keys[i + 1].value - k.value : Vec2{};
    switch (k.path) {
    case PathMode::Custom:
        in = k.inTangent;
        out = k.outTangent;
        return;
    case PathMode::Linear:
        in = toPrev / 3.0f;
        out = toNext / 3.0f;
        return;
    case PathMode::Auto:
        if (hasPrev && hasNext) {
            // Catmull-Rom: the tangent runs parallel to prev -> next.
            const Vec2 dir = (keys[i + 1].value - keys[i - 1].value) / 6.0f;
            in = dir * -1.0f;
            out = dir;
        } else {
            in = toPrev / 3.0f;
            out = toNext / 3.0f;
        }
        return;
    }
}

PathSegment pathSegment(const std::vector<Keyframe<Vec2>>& keys, size_t i) {
    Vec2 in0, out0, in1, out1;
    pathTangents(keys, i, in0, out0);
    pathTangents(keys, i + 1, in1, out1);
    const Vec2 a = keys[i].value, b = keys[i + 1].value;
    return {a, a + out0, b + in1, b};
}

Vec2 segmentValue(const std::vector<Keyframe<Vec2>>& keys, size_t i, float u) {
    const PathSegment seg = pathSegment(keys, i);
    // Overshooting easings (Back, Elastic) extrapolate the curve itself.
    if (u <= 0.0f || u >= 1.0f) return seg.at(u);

    // Reparameterize by arc length so the easing controls speed along the path.
    float lengths[kArcSamples + 1];
    lengths[0] = 0.0f;
    Vec2 prev = seg.p0;
    for (int s = 1; s <= kArcSamples; ++s) {
        const Vec2 p = seg.at(static_cast<float>(s) / kArcSamples);
        lengths[s] = lengths[s - 1] + (p - prev).length();
        prev = p;
    }
    const float total = lengths[kArcSamples];
    if (total < 1e-4f) return lerp(seg.p0, seg.p3, u);
    const float target = u * total;
    int s = 1;
    while (s < kArcSamples && lengths[s] < target) ++s;
    const float span = lengths[s] - lengths[s - 1];
    const float f = span > 0.0f ? (target - lengths[s - 1]) / span : 0.0f;
    return seg.at((static_cast<float>(s - 1) + f) / kArcSamples);
}

} // namespace bemotion
