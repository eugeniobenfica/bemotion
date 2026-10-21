#pragma once

namespace mirage {

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
    Count
};

// Maps t in [0, 1] to the eased progress (may overshoot for Back/Elastic).
float applyEasing(Easing easing, float t);

const char* easingLabel(Easing easing); // human readable, for the UI
const char* easingId(Easing easing);    // stable identifier, for files
Easing easingFromId(const char* id);

} // namespace mirage
