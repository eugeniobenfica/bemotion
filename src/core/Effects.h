#pragma once

// Layer effects: a per-layer stack applied in order on top of the layer's
// own pixels (rendered on the GPU, see render/EffectRenderer).
//
//   Outline      solid border around the layer's silhouette (text, shapes)
//   Glow         blurred colored halo
//   DropShadow   blurred, offset, colored copy behind the layer
//   MotionBlur   averages several moments within the shutter interval
//
// All parameters are animatable. Effect stacks can be saved and imported as
// JSON presets (see EffectPreset).

#include "core/Animated.h"

#include <array>
#include <string>
#include <vector>

namespace bemotion {

enum class EffectType { Outline, Glow, DropShadow, MotionBlur, Count };

const char* effectLabel(EffectType type);
const char* effectId(EffectType type);
EffectType effectFromId(const std::string& id); // Count if unknown

struct Effect {
    EffectType type = EffectType::DropShadow;
    bool enabled = true;

    AnimColor color{Color(0, 0, 0, 1)};
    AnimFloat opacity{70.0f};     // percent (outline/glow/shadow strength)
    AnimFloat size{12.0f};        // blur radius, glow radius or outline width (px)
    AnimVec2 offset{Vec2{12, 12}}; // shadow offset (px, composition space)
    AnimFloat shutter{180.0f};    // motion blur shutter angle (degrees)
    int samples = 12;             // motion blur quality (moments per frame)

    // Storage for the PropertyRef ids/groups handed out by Layer::properties().
    std::array<std::string, 5> ids;
    std::string group;
};

Effect makeEffect(EffectType type);

// Extra pixels an effect can draw outside the layer's own bounds.
float effectMargin(const Effect& e, double t);

struct EffectPreset {
    std::string name;
    std::vector<Effect> effects;
};

std::vector<EffectPreset> builtinEffectPresets();
bool parseEffectPreset(const std::string& json, EffectPreset& out, std::string* error = nullptr);
std::string effectPresetToJson(const EffectPreset& preset);

} // namespace bemotion
