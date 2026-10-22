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
};
constexpr EffectInfo kEffects[] = {
    {"outline", "Outline"},
    {"glow", "Glow"},
    {"drop_shadow", "Drop Shadow"},
    {"motion_blur", "Motion Blur"},
};
static_assert(std::size(kEffects) == static_cast<size_t>(EffectType::Count));

constexpr const char* kBuiltinEffectPresets[] = {
    R"({"name":"Soft Shadow","effects":[{"type":"drop_shadow","color":[0,0,0,1],"opacity":55,"size":28,"offset":[0,18]}]})",
    R"({"name":"Hard Shadow","effects":[{"type":"drop_shadow","color":[0,0,0,1],"opacity":90,"size":0,"offset":[10,10]}]})",
    R"({"name":"Neon","effects":[{"type":"outline","color":[1,1,1,1],"opacity":100,"size":2},
                                 {"type":"glow","color":[0.91,0.725,0.192,1],"opacity":100,"size":28}]})",
    R"({"name":"Sticker","effects":[{"type":"outline","color":[1,1,1,1],"opacity":100,"size":10},
                                    {"type":"drop_shadow","color":[0,0,0,1],"opacity":45,"size":14,"offset":[0,8]}]})",
    R"({"name":"Fast Motion","effects":[{"type":"motion_blur","shutter":270,"samples":16}]})",
};

std::vector<float> floats(const json& j) {
    std::vector<float> v;
    if (j.is_number()) v.push_back(j.get<float>());
    else if (j.is_array())
        for (const auto& x : j) v.push_back(x.get<float>());
    return v;
}

} // namespace

const char* effectLabel(EffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? kEffects[i].label : "Effect";
}

const char* effectId(EffectType type) {
    const int i = static_cast<int>(type);
    return i >= 0 && i < static_cast<int>(EffectType::Count) ? kEffects[i].id : "unknown";
}

EffectType effectFromId(const std::string& id) {
    for (int i = 0; i < static_cast<int>(EffectType::Count); ++i)
        if (id == kEffects[i].id) return static_cast<EffectType>(i);
    return EffectType::Count;
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
    default: return 0.0f;
    }
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
                           {"samples", e.samples}});
    }
    return json{{"name", preset.name}, {"effects", effects}}.dump(2);
}

} // namespace bemotion
