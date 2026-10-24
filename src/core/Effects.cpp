#include "core/Effects.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace bemotion {

using json = nlohmann::json;

namespace {

struct EffectInfo {
    const char* id;
    const char* label;
    const char* category;
    const char* description;
};
constexpr EffectInfo kEffects[] = {
    {"outline", "Stroke", "Generate", "A solid border around the layer's silhouette."},
    {"glow", "Glow", "Stylize", "A soft, colored halo of light around the layer."},
    {"drop_shadow", "Drop Shadow", "Perspective", "A blurred, offset shadow behind the layer."},
    {"motion_blur", "Motion Blur", "Time", "Blurs movement by blending moments within the shutter."},
    {"gaussian_blur", "Gaussian Blur", "Blur & Sharpen", "Softens the whole layer evenly."},
    {"directional_blur", "Directional Blur", "Blur & Sharpen", "Smears the layer along one direction."},
    {"brightness_contrast", "Brightness & Contrast", "Color Correction", "Brightens or darkens; adds or removes contrast."},
    {"hue_saturation", "Hue/Saturation", "Color Correction", "Shifts hues, changes saturation and lightness."},
    {"tint", "Tint", "Color Correction", "Maps the layer's tones from black to a color."},
    {"fill", "Fill", "Generate", "Paints the layer with a solid color, keeping its shape."},
    {"invert", "Invert", "Channel", "Inverts the layer's colors."},
    {"wave_warp", "Wave Warp", "Distort", "Bends the layer into a moving wave."},
};
static_assert(std::size(kEffects) == static_cast<size_t>(EffectType::Count));

constexpr const char* kSlotIds[] = {"color", "opacity", "size", "offset", "shutter", "v1", "v2", "v3"};
static_assert(std::size(kSlotIds) == static_cast<size_t>(EffectSlot::Count));

constexpr const char* kBuiltinEffectPresets[] = {
    R"({"name":"Soft Shadow","effects":[{"type":"drop_shadow","color":[0,0,0,1],"opacity":55,"size":28,"offset":[0,18]}]})",
    R"({"name":"Hard Shadow","effects":[{"type":"drop_shadow","color":[0,0,0,1],"opacity":90,"size":0,"offset":[10,10]}]})",
    R"({"name":"Neon","effects":[{"type":"outline","color":[1,1,1,1],"opacity":100,"size":2},
                                 {"type":"glow","color":[0.91,0.725,0.192,1],"opacity":100,"size":28}]})",
    R"({"name":"Sticker","effects":[{"type":"outline","color":[1,1,1,1],"opacity":100,"size":10},
                                    {"type":"drop_shadow","color":[0,0,0,1],"opacity":45,"size":14,"offset":[0,8]}]})",
    R"({"name":"Fast Motion","effects":[{"type":"motion_blur","shutter":270,"samples":16}]})",
    R"({"name":"Dreamy","effects":[{"type":"gaussian_blur","size":6},{"type":"glow","color":[1,1,1,1],"opacity":60,"size":30}]})",
    R"({"name":"Black & White","effects":[{"type":"hue_saturation","v2":-100}]})",
    R"({"name":"Sepia","effects":[{"type":"tint","color":[0.94,0.78,0.55,1],"opacity":100}]})",
    R"({"name":"Wavy","effects":[{"type":"wave_warp","v1":14,"v2":180,"v3":1}]})",
};

std::vector<float> floats(const json& j) {
    std::vector<float> v;
    if (j.is_number()) v.push_back(j.get<float>());
    else if (j.is_array())
        for (const auto& x : j) v.push_back(x.get<float>());
    return v;
}

using S = EffectSlot;

} // namespace

const char* effectLabel(EffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? kEffects[i].label : "Effect";
}

const char* effectId(EffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? kEffects[i].id : "unknown";
}

const char* effectCategory(EffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? kEffects[i].category : "";
}

const char* effectDescription(EffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? kEffects[i].description : "";
}

EffectType effectFromId(const std::string& id) {
    for (int i = 0; i < static_cast<int>(EffectType::Count); ++i)
        if (id == kEffects[i].id) return static_cast<EffectType>(i);
    return EffectType::Count;
}

const char* effectSlotId(EffectSlot slot) {
    const int i = static_cast<int>(slot);
    return i >= 0 && i < kEffectSlotCount ? kSlotIds[i] : "";
}

const std::vector<EffectParam>& effectParams(EffectType type) {
    static const std::vector<EffectParam> table[] = {
        // Outline (Stroke)
        {{S::Color, "Color", 1}, {S::Size, "Width", 0.1f, 0, 200, "%.1f px"}, {S::Opacity, "Opacity", 0.5f, 0, 100, "%.0f%%"}},
        // Glow
        {{S::Color, "Color", 1}, {S::Size, "Radius", 0.25f, 0, 400, "%.1f px"},
         {S::Opacity, "Intensity", 0.5f, 0, 300, "%.0f%%"}},
        // Drop Shadow
        {{S::Color, "Color", 1}, {S::Offset, "Offset", 0.5f}, {S::Size, "Softness", 0.25f, 0, 400, "%.1f px"},
         {S::Opacity, "Opacity", 0.5f, 0, 100, "%.0f%%"}},
        // Motion Blur
        {{S::Shutter, "Shutter Angle", 1, 0, 720, "%.0f°"}},
        // Gaussian Blur
        {{S::Size, "Blurriness", 0.25f, 0, 500, "%.1f px"}},
        // Directional Blur
        {{S::Value1, "Direction", 0.5f, 0, 0, "%.0f°"}, {S::Size, "Blur Length", 0.25f, 0, 1000, "%.1f px"}},
        // Brightness & Contrast
        {{S::Value1, "Brightness", 0.5f, -150, 150, "%.0f"}, {S::Value2, "Contrast", 0.5f, -100, 100, "%.0f"}},
        // Hue/Saturation
        {{S::Value1, "Hue", 0.5f, 0, 0, "%.0f°"}, {S::Value2, "Saturation", 0.5f, -100, 100, "%.0f"},
         {S::Value3, "Lightness", 0.5f, -100, 100, "%.0f"}},
        // Tint
        {{S::Color, "Map White To", 1}, {S::Opacity, "Amount", 0.5f, 0, 100, "%.0f%%"}},
        // Fill
        {{S::Color, "Color", 1}, {S::Opacity, "Opacity", 0.5f, 0, 100, "%.0f%%"}},
        // Invert
        {{S::Opacity, "Blend", 0.5f, 0, 100, "%.0f%%"}},
        // Wave Warp
        {{S::Value1, "Wave Height", 0.25f, 0, 500, "%.1f px"}, {S::Value2, "Wave Width", 0.5f, 2, 4000, "%.0f px"},
         {S::Value3, "Wave Speed", 0.01f, -20, 20, "%.2f /s"}, {S::Shutter, "Direction", 0.5f, 0, 0, "%.0f°"}},
    };
    static_assert(std::size(table) == static_cast<size_t>(EffectType::Count));
    static const std::vector<EffectParam> none;
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? table[i] : none;
}

AnimatedBase& Effect::slot(EffectSlot s) {
    switch (s) {
    case S::Color: return color;
    case S::Opacity: return opacity;
    case S::Size: return size;
    case S::Offset: return offset;
    case S::Shutter: return shutter;
    case S::Value1: return value1;
    case S::Value2: return value2;
    case S::Value3: return value3;
    case S::Count: break;
    }
    return opacity;
}

Effect makeEffect(EffectType type) {
    Effect e;
    e.type = type;
    switch (type) {
    case EffectType::Outline:
        e.color.base = Color(0, 0, 0, 1);
        e.opacity.base = 100.0f;
        e.size.base = 4.0f;
        break;
    case EffectType::Glow:
        e.color.base = Color::fromHex(0xE8B931);
        e.opacity.base = 80.0f;
        e.size.base = 24.0f;
        break;
    case EffectType::DropShadow:
        e.color.base = Color(0, 0, 0, 1);
        e.opacity.base = 60.0f;
        e.size.base = 16.0f;
        e.offset.base = {10.0f, 14.0f};
        break;
    case EffectType::GaussianBlur: e.size.base = 10.0f; break;
    case EffectType::DirectionalBlur:
        e.size.base = 40.0f;
        e.value1.base = 90.0f;
        break;
    case EffectType::BrightnessContrast: break; // both 0: no change until edited
    case EffectType::HueSaturation: break;
    case EffectType::Tint:
        e.color.base = Color(1, 1, 1, 1);
        e.opacity.base = 100.0f;
        break;
    case EffectType::Fill:
        e.color.base = Color::fromHex(0xE8B931);
        e.opacity.base = 100.0f;
        break;
    case EffectType::Invert: e.opacity.base = 100.0f; break;
    case EffectType::WaveWarp:
        e.value1.base = 12.0f;
        e.value2.base = 160.0f;
        e.value3.base = 1.0f;
        e.shutter.base = 0.0f;
        break;
    case EffectType::MotionBlur:
    case EffectType::Count: break;
    }
    return e;
}

float effectMargin(const Effect& e, double t) {
    if (!e.enabled) return 0.0f;
    const float size = std::max(0.0f, e.size.valueAt(t));
    switch (e.type) {
    case EffectType::Outline: return size + 2.0f;
    case EffectType::Glow: return size * 1.5f + 2.0f;
    case EffectType::DropShadow: return size * 1.5f + e.offset.valueAt(t).length() + 2.0f;
    case EffectType::GaussianBlur: return size * 1.5f + 2.0f;
    case EffectType::DirectionalBlur: return size * 0.5f + 2.0f;
    case EffectType::WaveWarp: return std::fabs(e.value1.valueAt(t)) + 2.0f;
    default: return 0.0f;
    }
}

const char* audioEffectId(AudioEffectType type) {
    static constexpr const char* ids[] = {"gain", "fade_in", "fade_out"};
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(AudioEffectType::Count) ? ids[i] : "unknown";
}

const char* audioEffectLabel(AudioEffectType type) {
    static constexpr const char* labels[] = {"Gain", "Fade In", "Fade Out"};
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(AudioEffectType::Count) ? labels[i] : "Audio Effect";
}

AudioEffect makeAudioEffect(AudioEffectType type) {
    AudioEffect effect;
    effect.type = type;
    effect.value = type == AudioEffectType::Gain ? 0.0f : 0.5f;
    return effect;
}

std::vector<EffectPreset> builtinEffectPresets() {
    std::vector<EffectPreset> out;
    for (const char* text : kBuiltinEffectPresets) {
        EffectPreset p;
        if (parseEffectPreset(text, p)) out.push_back(std::move(p));
    }
    return out;
}

bool parseEffectPreset(const std::string& text, EffectPreset& out, std::string* error) {
    try {
        const json j = json::parse(text);
        EffectPreset p;
        p.name = j.value("name", std::string());
        if (p.name.empty()) throw std::runtime_error("The effect preset has no name.");
        for (const auto& ej : j.value("effects", json::array())) {
            const EffectType type = effectFromId(ej.value("type", std::string()));
            if (type == EffectType::Count) continue;
            Effect e = makeEffect(type);
            e.enabled = ej.value("enabled", true);
            if (auto c = floats(ej.value("color", json())); c.size() >= 3) e.color.base = {c[0], c[1], c[2], c.size() > 3 ? c[3] : 1.0f};
            e.opacity.base = ej.value("opacity", e.opacity.base);
            e.size.base = ej.value("size", e.size.base);
            if (auto o = floats(ej.value("offset", json())); o.size() >= 2) e.offset.base = {o[0], o[1]};
            e.shutter.base = ej.value("shutter", e.shutter.base);
            e.value1.base = ej.value("v1", e.value1.base);
            e.value2.base = ej.value("v2", e.value2.base);
            e.value3.base = ej.value("v3", e.value3.base);
            e.samples = std::clamp(ej.value("samples", e.samples), 2, 64);
            p.effects.push_back(std::move(e));
        }
        if (p.effects.empty()) throw std::runtime_error("The effect preset has no known effects.");
        out = std::move(p);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::string effectPresetToJson(const EffectPreset& preset) {
    json effects = json::array();
    for (const Effect& e : preset.effects) {
        const Color c = e.color.base;
        const Vec2 o = e.offset.base;
        effects.push_back({{"type", effectId(e.type)},
                           {"enabled", e.enabled},
                           {"color", {c.r, c.g, c.b, c.a}},
                           {"opacity", e.opacity.base},
                           {"size", e.size.base},
                           {"offset", {o.x, o.y}},
                           {"shutter", e.shutter.base},
                           {"v1", e.value1.base},
                           {"v2", e.value2.base},
                           {"v3", e.value3.base},
                           {"samples", e.samples}});
    }
    return json{{"name", preset.name}, {"effects", effects}}.dump(2);
}

} // namespace bemotion
