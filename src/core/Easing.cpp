#include "core/Easing.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bemotion {

namespace {

float easeOutBounce(float t) {
    constexpr float n = 7.5625f, d = 2.75f;
    if (t < 1.0f / d) return n * t * t;
    if (t < 2.0f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

struct EasingInfo {
    const char* id;
    const char* label;
};

constexpr EasingInfo kInfo[] = {
    {"linear", "Linear"},
    {"ease_in", "Ease In"},
    {"ease_out", "Ease Out"},
    {"ease_in_out", "Ease In Out"},
    {"back_in", "Back In"},
    {"back_out", "Back Out"},
    {"elastic", "Elastic"},
    {"bounce", "Bounce"},
    {"hold", "Hold"},
    {"custom", "Custom Curve"},
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<int>(Easing::Count));

} // namespace

float applyEasing(Easing easing, float t) {
    t = clamp01(t);
    constexpr float back = 1.70158f;
    switch (easing) {
    case Easing::Linear: return t;
    case Easing::EaseIn: return t * t * t;
    case Easing::EaseOut: { const float u = 1.0f - t; return 1.0f - u * u * u; }
    case Easing::EaseInOut:
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case Easing::BackIn: return (back + 1.0f) * t * t * t - back * t * t;
    case Easing::BackOut: {
        const float u = t - 1.0f;
        return 1.0f + (back + 1.0f) * u * u * u + back * u * u;
    }
    case Easing::Elastic: {
        if (t <= 0.0f || t >= 1.0f) return t;
        constexpr float c4 = (2.0f * kPi) / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    case Easing::Bounce: return easeOutBounce(t);
    case Easing::Hold: return t >= 1.0f ? 1.0f : 0.0f;
    case Easing::Custom: return applyBezierEase(BezierEase{}, t);
    case Easing::Count: break;
    }
    return t;
}

float applyEasing(Easing easing, const BezierEase& curve, float t) {
    return easing == Easing::Custom ? applyBezierEase(curve, t) : applyEasing(easing, t);
}

float applyBezierEase(const BezierEase& c, float t) {
    t = clamp01(t);
    const float x1 = std::clamp(c.x1, 0.0f, 1.0f), x2 = std::clamp(c.x2, 0.0f, 1.0f);
    auto bez = [](float a, float b, float s) {
        const float u = 1.0f - s;
        return 3.0f * u * u * s * a + 3.0f * u * s * s * b + s * s * s;
    };
    // Solve x(s) = t: Newton steps, falling back to bisection (x is monotonic).
    float s = t;
    for (int i = 0; i < 6; ++i) {
        const float u = 1.0f - s;
        const float dx = 3.0f * u * u * x1 + 6.0f * u * s * (x2 - x1) + 3.0f * s * s * (1.0f - x2);
        if (std::fabs(dx) < 1e-5f) break;
        s = std::clamp(s - (bez(x1, x2, s) - t) / dx, 0.0f, 1.0f);
    }
    if (std::fabs(bez(x1, x2, s) - t) > 1e-4f) {
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 30; ++i) {
            s = (lo + hi) * 0.5f;
            (bez(x1, x2, s) < t ? lo : hi) = s;
        }
    }
    return bez(c.y1, c.y2, s);
}

BezierEase bezierFor(Easing easing) {
    switch (easing) {
    case Easing::Linear: return {0.0f, 0.0f, 1.0f, 1.0f};
    case Easing::EaseIn: return {0.55f, 0.055f, 0.675f, 0.19f};
    case Easing::EaseOut: return {0.215f, 0.61f, 0.355f, 1.0f};
    case Easing::EaseInOut: return {0.645f, 0.045f, 0.355f, 1.0f};
    case Easing::BackIn: return {0.6f, -0.28f, 0.735f, 0.045f};
    case Easing::BackOut: return {0.175f, 0.885f, 0.32f, 1.275f};
    default: return {};
    }
}

const char* easingLabel(Easing easing) {
    const int i = static_cast<int>(easing);
    return (i >= 0 && i < static_cast<int>(Easing::Count)) ? kInfo[i].label : "?";
}

const char* easingId(Easing easing) {
    const int i = static_cast<int>(easing);
    return (i >= 0 && i < static_cast<int>(Easing::Count)) ? kInfo[i].id : "linear";
}

Easing easingFromId(const char* id) {
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i)
        if (std::strcmp(kInfo[i].id, id) == 0) return static_cast<Easing>(i);
    return Easing::Linear;
}

} // namespace bemotion
