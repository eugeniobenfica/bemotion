#pragma once

// Layer effects, in the spirit of After Effects' Effects & Presets: a
// per-layer stack applied in order to the layer's pixels (rendered on the
// GPU, see render/EffectRenderer). Effects are grouped in categories:
//
//   Blur & Sharpen     Gaussian Blur, Directional Blur
//   Color Correction   Brightness & Contrast, Hue/Saturation, Tint, Curves, Apply LUT
//   Channel            Invert
//   Keying             Chroma Key
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
    Lut,       // a .cube color look-up table (Effect::file)
    Curves,    // tone curves (Effect::curves)
    ChromaKey, // a color made transparent
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
    // Apply LUT: the .cube file (UTF-8; "bundled:Name.cube" for the app's own LUTs), and the
    // file relative to the project (set when saving, like media paths).
    std::string file, relative;
    // Curves: the RGB (master), red, green and blue curves, each points (x = input, y = output,
    // 0..1) sorted by x; an empty curve leaves its channel alone.
    std::array<std::vector<Vec2>, 4> curves;

    AnimatedBase& slot(EffectSlot s);

    // Storage for the PropertyRef ids/groups handed out by Layer::properties().
    std::array<std::string, kEffectSlotCount> ids;
    std::string group;
};

Effect makeEffect(EffectType type);

// Extra pixels an effect can draw outside the layer's own bounds.
float effectMargin(const Effect& e, double t);

// Curves: the names of the four curves ("rgb", "red", "green", "blue"), a curve's output for
// input x (a smooth curve through its points that never overshoots them, flat past the ends),
// and its points made valid (inside 0..1, sorted, one per x, at most kMaxCurvePoints).
constexpr int kMaxCurvePoints = 16;
const char* curveId(int channel);
float curveValue(const std::vector<Vec2>& points, float x);
void cleanCurve(std::vector<Vec2>& points);
bool curveIsIdentity(const std::vector<Vec2>& points);

// A 3D color look-up table: size^3 RGB entries, red changing fastest (as in .cube files).
struct Lut3D {
    std::string title;
    int size = 0;
    std::vector<float> rgb;
};
// Reads an Adobe / Resolve .cube file (3D, or 1D turned into 3D); false with an error otherwise.
bool parseCubeLut(const std::string& text, Lut3D& out, std::string* error = nullptr);

// Audio effects are separate from image effects: their values alter the
// mixer, never the Skia render chain.
//   Gain              value = dB
//   Fade In / Out     value = seconds
//   EQ                value / value2 / value3 = low (100 Hz shelf) / mid (1 kHz) / high (8 kHz shelf), dB
//   Noise Reduction   value = reduction (dB), value2 = sensitivity (%); sampleFrom / sampleTo: a
//                     stretch of the source with only the noise (its seconds), else it is guessed
//   Ducking           value = how much quieter (dB) while other audio plays, value2 = what counts
//                     as sound (dB), value3 = fade (seconds)
enum class AudioEffectType { Gain, FadeIn, FadeOut, Eq, NoiseReduction, Ducking, Reverb, Echo, Compressor, Filter, AutoLevel, Count };
const char* audioEffectId(AudioEffectType type);
const char* audioEffectLabel(AudioEffectType type);
const char* audioEffectDescription(AudioEffectType type);
AudioEffectType audioEffectFromId(const std::string& id); // Count if unknown

struct AudioEffect {
    AudioEffectType type = AudioEffectType::Gain;
    bool enabled = true;
    float value = 0.0f, value2 = 0.0f, value3 = 0.0f; // see AudioEffectType
    double sampleFrom = -1.0, sampleTo = -1.0;        // Noise Reduction's sample (source seconds; -1: none)
    bool hasSample() const { return sampleFrom >= 0.0 && sampleTo > sampleFrom; }
};

// The values an audio effect type shows, in order (`index` 0..2: value, value2, value3).
struct AudioEffectParam {
    int index;
    const char* id; // in operations: "value", "low"...
    const char* label;
    float min, max, speed;
    const char* format;
};
const std::vector<AudioEffectParam>& audioEffectParams(AudioEffectType type);
float& audioEffectValue(AudioEffect& e, int index);
float audioEffectValue(const AudioEffect& e, int index);
void clampAudioEffect(AudioEffect& e); // every value inside its range

AudioEffect makeAudioEffect(AudioEffectType type);

struct EffectPreset {
    std::string name;
    std::vector<Effect> effects;
};

std::vector<EffectPreset> builtinEffectPresets();
bool parseEffectPreset(const std::string& json, EffectPreset& out, std::string* error = nullptr);
std::string effectPresetToJson(const EffectPreset& preset);

} // namespace bemotion
