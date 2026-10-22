#pragma once

// One-click animation presets ("Quick animations").
// Each preset writes regular keyframes starting at the given time, so the
// result stays fully editable in the timeline afterwards.

#include "core/Layer.h"

namespace bemotion {

enum class Preset { FadeIn, FadeOut, PopIn, SlideUp, SlideLeft, Spin, Typewriter, DrawOn, Count };

const char* presetLabel(Preset preset);
bool presetAppliesTo(Preset preset, const Layer& layer);
void applyPreset(Preset preset, Layer& layer, double time);

} // namespace bemotion
