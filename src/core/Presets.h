#pragma once

// Animation presets ("Quick Animations").
//
// Presets are data: a list of keyframe tracks per property, with times
// normalized to the preset duration. Built-in presets use the same JSON
// format users can import or export, e.g.
//
//   { "name": "Fade In", "category": "Entrance", "applies_to": "any",
//     "duration": 0.6,
//     "tracks": [ { "property": "opacity",
//                   "keys": [ { "t": 0, "v": 0, "ease": "ease_out" },
//                             { "t": 1, "v": "rest" } ] } ] }
//
// Key values: a number / [x, y] / [r, g, b, a]; "rest" (the value the
// property should settle on); { "offset": ... } (rest + offset) or
// { "mul": ... } (rest * factor).
//
// Applying a preset first removes the property's keys inside the preset's
// time window, so presets can be stacked or re-applied without breaking.

#include "core/Layer.h"

#include <string>
#include <vector>

namespace bemotion {

struct PresetKey {
    enum class Kind { Absolute, Rest, Offset, Mul };
    double t = 0.0; // 0..1 of the preset duration
    Kind kind = Kind::Absolute;
    std::vector<float> value; // 1, 2 or 4 components
    Easing easing = Easing::EaseInOut;
    BezierEase curve; // when easing is Custom ("curve": [x1, y1, x2, y2])
};

struct PresetTrack {
    std::string property; // PropertyRef::id
    std::vector<PresetKey> keys;
};

// Text: letters animated one by one (TextProps::letterIn...); empty ids are left as they are.
struct PresetLetters {
    bool set = false;
    std::string in, out, loop, by, order; // ids (kLetterAnimIds...)
    double delay = -1.0, duration = -1.0; // < 0: unchanged
};

struct AnimationPreset {
    std::string name;
    std::string category = "Custom";
    std::string appliesTo = "any"; // any | text | shape
    double duration = 0.6;         // seconds
    double perGlyph = 0.0;         // text: extra seconds per character (typewriter)
    bool enableStroke = false;     // shapes: turn the stroke on (draw-on effects)
    std::vector<PresetTrack> tracks;
    PresetLetters letters; // "letters": {"in": "rise", "by": "word", "delay": 0.1, ...}
};

std::vector<AnimationPreset> builtinPresets();

bool parsePreset(const std::string& json, AnimationPreset& out, std::string* error = nullptr);
std::string presetToJson(const AnimationPreset& preset);

bool presetAppliesTo(const AnimationPreset& preset, const Layer& layer);
double presetDuration(const AnimationPreset& preset, const Layer& layer);
void applyPreset(const AnimationPreset& preset, Layer& layer, double time);

// Captures the keyframes of `layer` between `start` and `end` as a preset
// with absolute values ("Save as Quick Animation").
AnimationPreset presetFromLayer(Layer& layer, double start, double end, const std::string& name);

} // namespace bemotion
