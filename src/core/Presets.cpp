#include "core/Presets.h"

#include <algorithm>

namespace mirage {

namespace {
constexpr double kDuration = 0.6; // seconds

// Value the property should land on: what it shows at `t` right now.
template <typename T>
T restingValue(const Animated<T>& a, double t) {
    return a.valueAt(t);
}
} // namespace

const char* presetLabel(Preset preset) {
    switch (preset) {
    case Preset::FadeIn: return "Fade In";
    case Preset::FadeOut: return "Fade Out";
    case Preset::PopIn: return "Pop In";
    case Preset::SlideUp: return "Slide Up";
    case Preset::SlideLeft: return "Slide In";
    case Preset::Spin: return "Spin";
    case Preset::Typewriter: return "Typewriter";
    case Preset::DrawOn: return "Draw On";
    case Preset::Count: break;
    }
    return "?";
}

bool presetAppliesTo(Preset preset, const Layer& layer) {
    switch (preset) {
    case Preset::Typewriter: return layer.type == LayerType::Text;
    case Preset::DrawOn: return layer.type == LayerType::Shape;
    default: return true;
    }
}

void applyPreset(Preset preset, Layer& layer, double t) {
    auto& tr = layer.transform;
    const double t1 = t + kDuration;
    switch (preset) {
    case Preset::FadeIn: {
        const float target = std::max(1.0f, restingValue(tr.opacity, t1));
        tr.opacity.setKey(t, 0.0f, Easing::EaseOut);
        tr.opacity.setKey(t1, target);
        break;
    }
    case Preset::FadeOut: {
        const float from = restingValue(tr.opacity, t);
        tr.opacity.setKey(t, from, Easing::EaseIn);
        tr.opacity.setKey(t1, 0.0f);
        break;
    }
    case Preset::PopIn: {
        const Vec2 target = restingValue(tr.scale, t1);
        tr.scale.setKey(t, {0.0f, 0.0f}, Easing::BackOut);
        tr.scale.setKey(t1, target);
        break;
    }
    case Preset::SlideUp:
    case Preset::SlideLeft: {
        const Vec2 target = restingValue(tr.position, t1);
        const Vec2 offset = preset == Preset::SlideUp ? Vec2{0.0f, 160.0f} : Vec2{-240.0f, 0.0f};
        tr.position.setKey(t, target + offset, Easing::EaseOut);
        tr.position.setKey(t1, target);
        const float opacity = std::max(1.0f, restingValue(tr.opacity, t1));
        tr.opacity.setKey(t, 0.0f, Easing::EaseOut);
        tr.opacity.setKey(t1, opacity);
        break;
    }
    case Preset::Spin: {
        const float from = restingValue(tr.rotation, t);
        tr.rotation.setKey(t, from, Easing::EaseInOut);
        tr.rotation.setKey(t + kDuration * 2.0, from + 360.0f);
        break;
    }
    case Preset::Typewriter: {
        int glyphs = 0;
        for (unsigned char c : layer.text.text)
            if ((c & 0xC0) != 0x80 && c != '\n') ++glyphs;
        layer.text.reveal.setKey(t, 0.0f, Easing::Linear);
        layer.text.reveal.setKey(t + std::max(0.3, glyphs * 0.05), 100.0f);
        break;
    }
    case Preset::DrawOn: {
        layer.shape.strokeEnabled = true;
        layer.shape.trimEnd.setKey(t, 0.0f, Easing::EaseInOut);
        layer.shape.trimEnd.setKey(t + kDuration * 2.0, 100.0f);
        break;
    }
    case Preset::Count: break;
    }
}

} // namespace mirage
