#include "core/Effects.h"

#include "core/Language.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

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
    {"outline", N_("Stroke"), N_("Generate"), N_("A solid border around the layer's silhouette.")},
    {"glow", N_("Glow"), N_("Stylize"), N_("A soft, colored halo of light around the layer.")},
    {"drop_shadow", N_("Drop Shadow"), N_("Perspective"), N_("A blurred, offset shadow behind the layer.")},
    {"motion_blur", N_("Motion Blur"), N_("Time"), N_("Blurs movement by blending moments within the shutter.")},
    {"gaussian_blur", N_("Gaussian Blur"), N_("Blur & Sharpen"), N_("Softens the whole layer evenly.")},
    {"directional_blur", N_("Directional Blur"), N_("Blur & Sharpen"), N_("Smears the layer along one direction.")},
    {"brightness_contrast", N_("Brightness & Contrast"), N_("Color Correction"), N_("Brightens or darkens; adds or removes contrast.")},
    {"hue_saturation", N_("Hue/Saturation"), N_("Color Correction"), N_("Shifts hues, changes saturation and lightness.")},
    {"tint", N_("Tint"), N_("Color Correction"), N_("Maps the layer's tones from black to a color.")},
    {"fill", N_("Fill"), N_("Generate"), N_("Paints the layer with a solid color, keeping its shape.")},
    {"invert", N_("Invert"), N_("Channel"), N_("Inverts the layer's colors.")},
    {"wave_warp", N_("Wave Warp"), N_("Distort"), N_("Bends the layer into a moving wave.")},
    {"lut", N_("Apply LUT"), N_("Color Correction"), N_("Grades the layer with a color look-up table (a .cube file).")},
    {"curves", N_("Curves"), N_("Color Correction"), N_("Reshapes the layer's tones, overall or one channel at a time.")},
    {"chroma_key", N_("Chroma Key"), N_("Keying"), N_("Makes one color transparent: a green or blue screen.")},
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
    R"({"name":"Teal & Orange","effects":[{"type":"lut","file":"bundled:Teal & Orange.cube","opacity":100}]})",
    R"({"name":"Fade","effects":[{"type":"curves","opacity":100,"curves":{"rgb":[[0,0.1],[0.5,0.52],[1,0.92]]}}]})",
    R"({"name":"Punchy","effects":[{"type":"curves","opacity":100,"curves":{"rgb":[[0,0],[0.25,0.2],[0.75,0.82],[1,1]]}}]})",
    R"({"name":"Green Screen","effects":[{"type":"chroma_key"}]})",
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
        {{S::Color, N_("Color"), 1}, {S::Size, N_("Width"), 0.1f, 0, 200, "%.1f px"}, {S::Opacity, N_("Opacity"), 0.5f, 0, 100, "%.0f%%"}},
        // Glow
        {{S::Color, N_("Color"), 1}, {S::Size, N_("Radius"), 0.25f, 0, 400, "%.1f px"},
         {S::Opacity, N_("Intensity"), 0.5f, 0, 300, "%.0f%%"}},
        // Drop Shadow
        {{S::Color, N_("Color"), 1}, {S::Offset, N_("Offset"), 0.5f}, {S::Size, N_("Softness"), 0.25f, 0, 400, "%.1f px"},
         {S::Opacity, N_("Opacity"), 0.5f, 0, 100, "%.0f%%"}},
        // Motion Blur
        {{S::Shutter, N_("Shutter Angle"), 1, 0, 720, "%.0f°"}},
        // Gaussian Blur
        {{S::Size, N_("Blurriness"), 0.25f, 0, 500, "%.1f px"}},
        // Directional Blur
        {{S::Value1, N_("Direction"), 0.5f, 0, 0, "%.0f°"}, {S::Size, N_("Blur Length"), 0.25f, 0, 1000, "%.1f px"}},
        // Brightness & Contrast
        {{S::Value1, N_("Brightness"), 0.5f, -150, 150, "%.0f"}, {S::Value2, N_("Contrast"), 0.5f, -100, 100, "%.0f"}},
        // Hue/Saturation
        {{S::Value1, N_("Hue"), 0.5f, 0, 0, "%.0f°"}, {S::Value2, N_("Saturation"), 0.5f, -100, 100, "%.0f"},
         {S::Value3, N_("Lightness"), 0.5f, -100, 100, "%.0f"}},
        // Tint
        {{S::Color, N_("Map White To"), 1}, {S::Opacity, N_("Amount"), 0.5f, 0, 100, "%.0f%%"}},
        // Fill
        {{S::Color, N_("Color"), 1}, {S::Opacity, N_("Opacity"), 0.5f, 0, 100, "%.0f%%"}},
        // Invert
        {{S::Opacity, N_("Blend"), 0.5f, 0, 100, "%.0f%%"}},
        // Wave Warp
        {{S::Value1, N_("Wave Height"), 0.25f, 0, 500, "%.1f px"}, {S::Value2, N_("Wave Width"), 0.5f, 2, 4000, "%.0f px"},
         {S::Value3, N_("Wave Speed"), 0.01f, -20, 20, "%.2f /s"}, {S::Shutter, N_("Direction"), 0.5f, 0, 0, "%.0f°"}},
        // Apply LUT
        {{S::Opacity, N_("Amount"), 0.5f, 0, 100, "%.0f%%"}},
        // Curves
        {{S::Opacity, N_("Amount"), 0.5f, 0, 100, "%.0f%%"}},
        // Chroma Key
        {{S::Color, N_("Key Color"), 1}, {S::Value1, N_("Tolerance"), 0.25f, 0, 100, "%.0f%%"},
         {S::Value2, N_("Softness"), 0.25f, 0, 100, "%.0f%%"}, {S::Value3, N_("Spill Suppression"), 0.25f, 0, 100, "%.0f%%"},
         {S::Size, N_("Choke"), 0.05f, 0, 20, "%.1f px"}},
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
    case EffectType::Lut:
    case EffectType::Curves: e.opacity.base = 100.0f; break;
    case EffectType::ChromaKey:
        e.color.base = Color::fromHex(0x00B140); // a studio green screen
        e.value1.base = 30.0f;
        e.value2.base = 20.0f;
        e.value3.base = 60.0f;
        e.size.base = 0.0f;
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

const char* curveId(int channel) {
    static constexpr const char* ids[] = {"rgb", "red", "green", "blue"};
    return channel >= 0 && channel < 4 ? ids[channel] : "";
}

void cleanCurve(std::vector<Vec2>& points) {
    for (Vec2& p : points) p = {std::isfinite(p.x) ? clamp01(p.x) : 0.0f, std::isfinite(p.y) ? clamp01(p.y) : 0.0f};
    std::stable_sort(points.begin(), points.end(), [](Vec2 a, Vec2 b) { return a.x < b.x; });
    points.erase(std::unique(points.begin(), points.end(), [](Vec2 a, Vec2 b) { return std::fabs(a.x - b.x) < 1e-4f; }), points.end());
    if (points.size() > static_cast<size_t>(kMaxCurvePoints)) points.resize(kMaxCurvePoints);
}

bool curveIsIdentity(const std::vector<Vec2>& points) {
    for (size_t i = 0; i < 64; ++i) {
        const float x = static_cast<float>(i) / 63.0f;
        if (std::fabs(curveValue(points, x) - x) > 1e-3f) return false;
    }
    return true;
}

float curveValue(const std::vector<Vec2>& p, float x) {
    if (p.empty()) return x;
    if (p.size() == 1 || x <= p.front().x) return p.front().y;
    if (x >= p.back().x) return p.back().y;
    // Monotone cubic (Fritsch-Carlson): smooth, and never beyond its neighbors' values.
    const size_t n = p.size();
    size_t k = 0;
    while (k + 2 < n && x > p[k + 1].x) ++k;
    auto slope = [&](size_t i) { return (p[i + 1].y - p[i].y) / std::max(1e-6f, p[i + 1].x - p[i].x); };
    auto tangent = [&](size_t i) {
        if (i == 0) return slope(0);
        if (i == n - 1) return slope(n - 2);
        const float a = slope(i - 1), b = slope(i);
        if (a * b <= 0.0f) return 0.0f; // a peak or a valley: flat there
        const float h0 = p[i].x - p[i - 1].x, h1 = p[i + 1].x - p[i].x;
        return 3.0f * (h0 + h1) / ((2.0f * h1 + h0) / a + (h1 + 2.0f * h0) / b); // weighted harmonic mean (Fritsch-Butland)
    };
    const float h = std::max(1e-6f, p[k + 1].x - p[k].x), u = (x - p[k].x) / h;
    const float m0 = tangent(k) * h, m1 = tangent(k + 1) * h;
    const float u2 = u * u, u3 = u2 * u;
    const float y = (2 * u3 - 3 * u2 + 1) * p[k].y + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p[k + 1].y + (u3 - u2) * m1;
    return clamp01(y);
}

bool parseCubeLut(const std::string& text, Lut3D& out, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::istringstream in(text);
    std::string line;
    Lut3D lut;
    int size1d = 0;
    float dmin[3] = {0, 0, 0}, dmax[3] = {1, 1, 1};
    std::vector<float> values;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        std::istringstream words(line.substr(start));
        std::string key;
        words >> key;
        if (key == "TITLE") {
            const size_t q = line.find('"'), r = line.rfind('"');
            if (q != std::string::npos && r > q) lut.title = line.substr(q + 1, r - q - 1);
        } else if (key == "LUT_3D_SIZE") {
            words >> lut.size;
        } else if (key == "LUT_1D_SIZE") {
            words >> size1d;
        } else if (key == "DOMAIN_MIN") {
            words >> dmin[0] >> dmin[1] >> dmin[2];
        } else if (key == "DOMAIN_MAX") {
            words >> dmax[0] >> dmax[1] >> dmax[2];
        } else if (key == "LUT_3D_INPUT_RANGE" || key == "LUT_1D_INPUT_RANGE") {
            float lo = 0, hi = 1;
            words >> lo >> hi;
            for (int c = 0; c < 3; ++c) dmin[c] = lo, dmax[c] = hi;
        } else if ((key[0] >= '0' && key[0] <= '9') || key[0] == '-' || key[0] == '.' || key[0] == '+') {
            std::istringstream nums(line.substr(start));
            float r = 0, g = 0, b = 0;
            if (!(nums >> r >> g >> b)) return fail("A line of the LUT is not three numbers: " + line);
            values.insert(values.end(), {r, g, b});
        } // other keywords (from newer formats) are ignored
    }
    for (int c = 0; c < 3; ++c)
        if (!(dmax[c] > dmin[c])) return fail("The LUT's DOMAIN_MIN must be below its DOMAIN_MAX.");
    if (lut.size == 0 && size1d >= 2) { // a 1D LUT: the same curve per channel, as a small 3D table
        if (values.size() != static_cast<size_t>(size1d) * 3) return fail("The 1D LUT does not have LUT_1D_SIZE rows.");
        auto curve = [&](int c, float x) {
            const float u = std::clamp((x - dmin[c]) / (dmax[c] - dmin[c]), 0.0f, 1.0f) * static_cast<float>(size1d - 1);
            const int i = std::min(static_cast<int>(u), size1d - 2);
            return values[i * 3 + c] + (values[(i + 1) * 3 + c] - values[i * 3 + c]) * (u - static_cast<float>(i));
        };
        lut.size = 33;
        lut.rgb.reserve(33 * 33 * 33 * 3);
        for (int b = 0; b < 33; ++b)
            for (int g = 0; g < 33; ++g)
                for (int r = 0; r < 33; ++r)
                    lut.rgb.insert(lut.rgb.end(), {curve(0, r / 32.0f), curve(1, g / 32.0f), curve(2, b / 32.0f)});
    } else {
        if (lut.size < 2 || lut.size > 129) return fail("The file has no LUT_3D_SIZE (2 to 129): is it a .cube LUT?");
        if (values.size() != static_cast<size_t>(lut.size) * lut.size * lut.size * 3)
            return fail("The LUT should have " + std::to_string(lut.size * lut.size * lut.size) + " rows (LUT_3D_SIZE " +
                        std::to_string(lut.size) + "), it has " + std::to_string(values.size() / 3) + ".");
        lut.rgb = std::move(values);
        // A domain other than 0..1: sampled again on 0..1 (rare; nearest entry is close enough at these sizes).
        if (dmin[0] != 0 || dmin[1] != 0 || dmin[2] != 0 || dmax[0] != 1 || dmax[1] != 1 || dmax[2] != 1) {
            const int n = lut.size;
            std::vector<float> remapped(lut.rgb.size());
            auto index = [&](int c, int i) {
                const float x = static_cast<float>(i) / (n - 1);
                return std::clamp(static_cast<int>(std::lround((x - dmin[c]) / (dmax[c] - dmin[c]) * (n - 1))), 0, n - 1);
            };
            for (int b = 0; b < n; ++b)
                for (int g = 0; g < n; ++g)
                    for (int r = 0; r < n; ++r)
                        for (int c = 0; c < 3; ++c)
                            remapped[((b * n + g) * n + r) * 3 + c] = lut.rgb[((index(2, b) * n + index(1, g)) * n + index(0, r)) * 3 + c];
            lut.rgb = std::move(remapped);
        }
    }
    for (float& v : lut.rgb)
        if (!std::isfinite(v)) v = 0.0f;
    out = std::move(lut);
    return true;
}

namespace {
struct AudioEffectInfo {
    const char* id;
    const char* label;
    const char* description;
};
constexpr AudioEffectInfo kAudioEffects[] = {
    {"gain", N_("Gain"), N_("Makes the clip louder or quieter.")},
    {"fade_in", N_("Fade In"), N_("Rises from silence at the start of the clip.")},
    {"fade_out", N_("Fade Out"), N_("Falls to silence at the end of the clip.")},
    {"eq", N_("EQ"), N_("Lowers or raises the lows, mids and highs.")},
    {"noise_reduction", N_("Noise Reduction"), N_("Lowers steady background noise: hum, hiss, fans.")},
    {"ducking", N_("Ducking"), N_("Lowers this clip (music) while other audio (voice) plays.")},
};
static_assert(std::size(kAudioEffects) == static_cast<size_t>(AudioEffectType::Count));
} // namespace

const char* audioEffectId(AudioEffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(AudioEffectType::Count) ? kAudioEffects[i].id : "unknown";
}

const char* audioEffectLabel(AudioEffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(AudioEffectType::Count) ? kAudioEffects[i].label : "Audio Effect";
}

const char* audioEffectDescription(AudioEffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(AudioEffectType::Count) ? kAudioEffects[i].description : "";
}

AudioEffectType audioEffectFromId(const std::string& id) {
    for (int i = 0; i < static_cast<int>(AudioEffectType::Count); ++i)
        if (id == kAudioEffects[i].id) return static_cast<AudioEffectType>(i);
    return AudioEffectType::Count;
}

const std::vector<AudioEffectParam>& audioEffectParams(AudioEffectType type) {
    static const std::vector<AudioEffectParam> table[] = {
        {{0, "value", N_("Gain"), -60, 24, 0.25f, "%.1f dB"}},
        {{0, "value", N_("Duration"), 0.01f, 60, 0.01f, "%.2f s"}},
        {{0, "value", N_("Duration"), 0.01f, 60, 0.01f, "%.2f s"}},
        {{0, "low", N_("Low"), -24, 24, 0.1f, "%+.1f dB"}, {1, "mid", N_("Mid"), -24, 24, 0.1f, "%+.1f dB"},
         {2, "high", N_("High"), -24, 24, 0.1f, "%+.1f dB"}},
        {{0, "reduction", N_("Reduction"), 0, 40, 0.1f, "%.1f dB"}, {1, "sensitivity", N_("Sensitivity"), 0, 100, 0.25f, "%.0f%%"}},
        {{0, "amount", N_("Lower By"), 0, 40, 0.1f, "%.1f dB"}, {1, "threshold", N_("Sound Above"), -70, 0, 0.25f, "%.0f dB"},
         {2, "fade", N_("Fade"), 0.05f, 3, 0.01f, "%.2f s"}},
    };
    static_assert(std::size(table) == static_cast<size_t>(AudioEffectType::Count));
    static const std::vector<AudioEffectParam> none;
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(AudioEffectType::Count) ? table[i] : none;
}

float& audioEffectValue(AudioEffect& e, int index) { return index == 1 ? e.value2 : index == 2 ? e.value3 : e.value; }
float audioEffectValue(const AudioEffect& e, int index) { return index == 1 ? e.value2 : index == 2 ? e.value3 : e.value; }

void clampAudioEffect(AudioEffect& e) {
    const AudioEffect defaults = makeAudioEffect(e.type);
    for (const AudioEffectParam& p : audioEffectParams(e.type)) {
        float& v = audioEffectValue(e, p.index);
        v = std::isfinite(v) ? std::clamp(v, p.min, p.max) : audioEffectValue(defaults, p.index);
    }
}

AudioEffect makeAudioEffect(AudioEffectType type) {
    AudioEffect effect;
    effect.type = type;
    switch (type) {
    case AudioEffectType::Gain: effect.value = 0.0f; break;
    case AudioEffectType::FadeIn:
    case AudioEffectType::FadeOut: effect.value = 0.5f; break;
    case AudioEffectType::Eq: break; // flat until edited
    case AudioEffectType::NoiseReduction:
        effect.value = 12.0f;
        effect.value2 = 50.0f;
        break;
    case AudioEffectType::Ducking:
        effect.value = 15.0f;
        effect.value2 = -40.0f;
        effect.value3 = 0.4f;
        break;
    case AudioEffectType::Count: break;
    }
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
            e.file = ej.value("file", std::string());
            if (const auto curves = ej.find("curves"); curves != ej.end() && curves->is_object())
                for (int c = 0; c < 4; ++c)
                    for (const auto& pt : curves->value(curveId(c), json::array()))
                        if (auto xy = floats(pt); xy.size() >= 2) e.curves[c].push_back({xy[0], xy[1]});
            for (auto& curve : e.curves) cleanCurve(curve);
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
        json ej = {{"type", effectId(e.type)},
                   {"enabled", e.enabled},
                   {"color", {c.r, c.g, c.b, c.a}},
                   {"opacity", e.opacity.base},
                   {"size", e.size.base},
                   {"offset", {o.x, o.y}},
                   {"shutter", e.shutter.base},
                   {"v1", e.value1.base},
                   {"v2", e.value2.base},
                   {"v3", e.value3.base},
                   {"samples", e.samples}};
        if (!e.file.empty()) ej["file"] = e.file;
        json curves = json::object();
        for (int ch = 0; ch < 4; ++ch)
            if (!e.curves[ch].empty()) {
                json pts = json::array();
                for (const Vec2& p : e.curves[ch]) pts.push_back({p.x, p.y});
                curves[curveId(ch)] = std::move(pts);
            }
        if (!curves.empty()) ej["curves"] = std::move(curves);
        effects.push_back(std::move(ej));
    }
    return json{{"name", preset.name}, {"effects", effects}}.dump(2);
}

} // namespace bemotion
