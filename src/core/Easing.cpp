#include "core/Easing.h"

#include "core/Math.h"

#include <cmath>
#include <cstring>

namespace mirage {

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
    case Easing::Count: break;
    }
    return t;
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

} // namespace mirage
