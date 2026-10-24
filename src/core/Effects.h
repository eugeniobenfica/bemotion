#pragma once

// Layer effects, in the spirit of After Effects' Effects & Presets: a
// per-layer stack applied in order to the layer's pixels (rendered on the
// GPU, see render/EffectRenderer). Effects are grouped in categories:
//
//   Blur & Sharpen     Gaussian Blur, Directional Blur
//   Color Correction   Brightness & Contrast, Hue/Saturation, Tint
//   Channel            Invert
//   Distort            Wave Warp
//   Generate           Fill, Stroke (outline around the silhouette)
//   Perspective        Drop Shadow
//   Stylize            Glow
//   Time               Motion Blur
//
// Every effect stores its parameters in the same fixed set of animatable
// slots; effectParams() says which slots a type uses and how they are
// labeled, so the inspector, timeline and files handle all types alike.
// Effect stacks can be saved and imported as JSON presets (EffectPreset).

#include "core/Animated.h"

#include <array>
#include <string>
#include <vector>

namespace bemotion {

enum class EffectType {
    Outline,
    Glow,
    DropShadow,
    MotionBlur,
    GaussianBlur,
    DirectionalBlur,
    BrightnessContrast,
    HueSaturation,
    Tint,
    Fill,
    Invert,
    WaveWarp,
    Count
};

const char* effectLabel(EffectType type);
const char* effectId(EffectType type);
const char* effectCategory(EffectType type);
const char* effectDescription(EffectType type);
EffectType effectFromId(const std::string& id); // Count if unknown

// Parameter storage slots (see Effect).
enum class EffectSlot { Color, Opacity, Size, Offset, Shutter, Value1, Value2, Value3, Count };
constexpr int kEffectSlotCount = static_cast<int>(EffectSlot::Count);
const char* effectSlotId(EffectSlot slot); // stable name used in files ("color", "v1", ...)

struct EffectParam {
    EffectSlot slot;
    const char* label;
    float speed = 0.5f;
    float min = 0.0f, max = 0.0f; // min == max: unbounded
    const char* format = "%.1f";
};
// The parameters a type shows, in order.
const std::vector<EffectParam>& effectParams(EffectType type);

struct Effect {
    EffectType type = EffectType::DropShadow;
    bool enabled = true;

    AnimColor color{Color(0, 0, 0, 1)};
    AnimFloat opacity{70.0f};      // percent (strength / blend amount)
    AnimFloat size{12.0f};         // radius, blurriness, width or length (px)
    AnimVec2 offset{Vec2{12, 12}}; // shadow offset (px, composition space)
    AnimFloat shutter{180.0f};     // motion blur shutter angle (degrees)
    AnimFloat value1{0.0f}, value2{0.0f}, value3{0.0f}; // type specific (see effectParams)
    int samples = 12;              // motion blur quality (moments per frame)

    AnimatedBase& slot(EffectSlot s);

    // Storage for the PropertyRef ids/groups handed out by Layer::properties().
    std::array<std::string, kEffectSlotCount> ids;
    std::string group;
};

Effect makeEffect(EffectType type);

// Extra pixels an effect can draw outside the layer's own bounds.
float effectMargin(const Effect& e, double t);

// Audio effects are separate from image effects: their values alter the
// mixer, never the Skia render chain.
enum class AudioEffectType { Gain, FadeIn, FadeOut, Count };
const char* audioEffectId(AudioEffectType type);
const char* audioEffectLabel(AudioEffectType type);

struct AudioEffect {
    AudioEffectType type = AudioEffectType::Gain;
    bool enabled = true;
    float value = 0.0f; // Gain: dB. Fades: seconds.
};

AudioEffect makeAudioEffect(AudioEffectType type);

struct EffectPreset {
    std::string name;
    std::vector<Effect> effects;
};

std::vector<EffectPreset> builtinEffectPresets();
bool parseEffectPreset(const std::string& json, EffectPreset& out, std::string* error = nullptr);
std::string effectPresetToJson(const EffectPreset& preset);

} // namespace bemotion
