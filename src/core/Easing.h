#pragma once

namespace bemotion {

// Interpolation curve applied to the segment that starts at a keyframe.
// Values are persisted by name (see easingId), so the order may change freely.
enum class Easing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    BackIn,
    BackOut,
    Elastic,
    Bounce,
    Hold,
    Custom, // cubic-bezier curve stored on the keyframe (see BezierEase)
    Count
};

// CSS-style cubic-bezier timing curve through (0,0), (x1,y1), (x2,y2), (1,1).
// x1/x2 stay in [0, 1]; y may overshoot.
struct BezierEase {
    float x1 = 0.33f, y1 = 0.0f, x2 = 0.67f, y2 = 1.0f;
    bool operator==(const BezierEase&) const = default;
};

float applyBezierEase(const BezierEase& curve, float t);
// A cubic-bezier close to a built-in easing (a starting point for custom curves).
BezierEase bezierFor(Easing easing);

// Maps t in [0, 1] to the eased progress (may overshoot for Back/Elastic).
float applyEasing(Easing easing, float t);
float applyEasing(Easing easing, const BezierEase& curve, float t); // uses `curve` for Custom

const char* easingLabel(Easing easing); // human readable, for the UI
const char* easingId(Easing easing);    // stable identifier, for files
Easing easingFromId(const char* id);

} // namespace bemotion
