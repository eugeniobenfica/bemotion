#include "core/Layer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace bemotion {

namespace {
const char* const kDecimalFormats[7] = {"%.0f", "%.1f", "%.2f", "%.3f", "%.4f", "%.5f", "%.6f"};
} // namespace

uint32_t nextKeyframeUid() {
    static std::atomic<uint32_t> counter{1};
    return counter++;
}

const char* shapeKindLabel(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Rectangle: return "Rectangle";
    case ShapeKind::Ellipse: return "Ellipse";
    case ShapeKind::Polygon: return "Polygon";
    case ShapeKind::Star: return "Star";
    case ShapeKind::Line: return "Line";
    case ShapeKind::Path: return "Path";
    case ShapeKind::Count: break;
    }
    return "Shape";
}

int ShapeProps::copiesAt(double t) const {
    if (!repeater) return 1;
    return std::clamp(static_cast<int>(std::lround(repeatCopies.valueAt(t))), 1, 100);
}

const char* fontWeightLabel(FontWeight weight) {
    switch (weight) {
    case FontWeight::Regular: return "Regular";
    case FontWeight::Medium: return "Medium";
    case FontWeight::SemiBold: return "SemiBold";
    case FontWeight::Bold: return "Bold";
    case FontWeight::Count: break;
    }
    return "Regular";
}

Color labelColor(int index) {
    static constexpr unsigned kColors[kLabelColorCount] = {
        0xE8B931, // gold
        0x4F8EF7, // blue
        0x3DBE8B, // green
        0xE5604D, // red
        0xA77BF3, // purple
        0x3CC6D6, // cyan
        0xF28C38, // orange
        0xD965A8, // pink
    };
    return Color::fromHex(kColors[((index % kLabelColorCount) + kLabelColorCount) % kLabelColorCount]);
}

PluginParam* PluginProps::find(const std::string& id) {
    for (auto& p : params)
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<PropertyRef> Layer::properties() {
    std::vector<PropertyRef> p;
    auto& tr = transform;
    // Masks' feather, opacity and expansion ("mask0.feather"), grouped by mask.
    auto addMasks = [&] {
        for (size_t i = 0; i < masks.size(); ++i) {
            Mask& m = masks[i];
            m.group = m.name;
            const std::string prefix = "mask" + std::to_string(i) + ".";
            m.ids[0] = prefix + "feather", m.ids[1] = prefix + "opacity", m.ids[2] = prefix + "expansion";
            PropertyRef refs[3] = {{m.ids[0].c_str(), m.group.c_str(), "Feather", PropertyKind::Float, &m.feather, 0.5f, 0, 1000, "%.1f px"},
                                   {m.ids[1].c_str(), m.group.c_str(), "Opacity", PropertyKind::Float, &m.opacity, 0.5f, 0, 100, "%.0f%%"},
                                   {m.ids[2].c_str(), m.group.c_str(), "Expansion", PropertyKind::Float, &m.expansion, 0.5f, -1000, 1000, "%.1f px"}};
            for (PropertyRef& r : refs) {
                r.maskIndex = static_cast<int>(i);
                p.push_back(r);
            }
        }
    };
    if (type == LayerType::Media && !media.hasVideo) { // audio only: its level (and its time remap)
        if (media.timeRemap) p.push_back({"time_remap", "Time", "Time Remap", PropertyKind::Float, &media.remap, 0.01f, 0, 1e6f, "%.2f s"});
        p.push_back({"volume", "Audio", "Volume", PropertyKind::Float, &media.volume, 0.5f, 0, 400, "%.0f%%"});
        return p;
    }
    if (type == LayerType::Background) { // fills the frame: no position, size or rotation
        auto& b = background;
        p.push_back({"opacity", "Transform", "Opacity", PropertyKind::Float, &tr.opacity, 0.5f, 0, 100, "%.0f%%"});
        p.push_back({"bg_color", "Background", b.fill == BackgroundFill::Image ? "Behind" : b.fill == BackgroundFill::Solid ? "Color" : "Color 1",
                     PropertyKind::Color, &b.color});
        if (b.fill == BackgroundFill::Linear || b.fill == BackgroundFill::Radial)
            p.push_back({"bg_color2", "Background", "Color 2", PropertyKind::Color, &b.color2});
        if (b.fill == BackgroundFill::Linear)
            p.push_back({"bg_angle", "Background", "Angle", PropertyKind::Float, &b.angle, 0.5f, 0, 0, "%.0f°"});
        if (b.fill == BackgroundFill::Image) {
            p.push_back({"bg_zoom", "Background", "Zoom", PropertyKind::Float, &b.zoom, 0.5f, 1, 1000, "%.0f%%"});
            p.push_back({"bg_offset", "Background", "Offset", PropertyKind::Vec2, &b.offset});
        }
        p.push_back({"bg_darken", "Background", "Darken", PropertyKind::Float, &b.darken, 0.5f, 0, 100, "%.0f%%"});
        addMasks();
        // Effects (blur a picture, tint it...) as on any layer.
        for (size_t i = 0; i < effects.size(); ++i) {
            Effect& e = effects[i];
            int sameType = 0;
            for (size_t j = 0; j <= i; ++j) sameType += effects[j].type == e.type;
            e.group = std::string(effectLabel(e.type)) + (sameType > 1 ? " " + std::to_string(sameType) : "");
            const std::string prefix = "fx" + std::to_string(i) + ".";
            for (int k = 0; k < kEffectSlotCount; ++k) e.ids[k] = prefix + effectSlotId(static_cast<EffectSlot>(k));
            for (const EffectParam& ep : effectParams(e.type)) {
                const PropertyKind kind = ep.slot == EffectSlot::Color    ? PropertyKind::Color
                                          : ep.slot == EffectSlot::Offset ? PropertyKind::Vec2
                                                                          : PropertyKind::Float;
                p.push_back({e.ids[static_cast<int>(ep.slot)].c_str(), e.group.c_str(), ep.label, kind, &e.slot(ep.slot),
                             ep.speed, ep.min, ep.max, ep.format, static_cast<int>(i)});
            }
        }
        return p;
    }
    if (type == LayerType::Camera) { // never drawn: no shape, text or effects
        p.push_back({"position", "Transform", "Position", PropertyKind::Vec2, &tr.position});
        p.push_back({"position_z", "Transform", "Z Position", PropertyKind::Float, &tr.depth, 2.0f, 0, 0, "%.1f"});
        p.push_back({"rotation_x", "Transform", "X Rotation", PropertyKind::Float, &tr.rotationX, 0.5f, 0, 0, "%.1f°"});
        p.push_back({"rotation_y", "Transform", "Y Rotation", PropertyKind::Float, &tr.rotationY, 0.5f, 0, 0, "%.1f°"});
        p.push_back({"rotation", "Transform", "Z Rotation", PropertyKind::Float, &tr.rotation, 0.5f, 0, 0, "%.1f°"});
        p.push_back({"camera_zoom", "Camera", "Zoom", PropertyKind::Float, &camera.zoom, 0.5f, 1, 10000, "%.0f%%"});
        return p;
    }
    p.push_back({"anchor", "Transform", "Anchor Point", PropertyKind::Vec2, &tr.anchor});
    p.push_back({"position", "Transform", "Position", PropertyKind::Vec2, &tr.position});
    if (threeD) p.push_back({"position_z", "Transform", "Z Position", PropertyKind::Float, &tr.depth, 2.0f, 0, 0, "%.1f"});
    p.push_back({"scale", "Transform", "Scale", PropertyKind::Vec2, &tr.scale, 0.5f, 0, 0, "%.1f%%"});
    if (threeD) {
        p.push_back({"rotation_x", "Transform", "X Rotation", PropertyKind::Float, &tr.rotationX, 0.5f, 0, 0, "%.1f°"});
        p.push_back({"rotation_y", "Transform", "Y Rotation", PropertyKind::Float, &tr.rotationY, 0.5f, 0, 0, "%.1f°"});
    }
    p.push_back({"rotation", "Transform", threeD ? "Z Rotation" : "Rotation", PropertyKind::Float, &tr.rotation, 0.5f, 0, 0, "%.1f°"});
    p.push_back({"opacity", "Transform", "Opacity", PropertyKind::Float, &tr.opacity, 0.5f, 0, 100, "%.0f%%"});
    addMasks();

    if (type == LayerType::Shape) {
        auto& s = shape;
        if (s.kind != ShapeKind::Path) p.push_back({"size", "Shape", "Size", PropertyKind::Vec2, &s.size, 1.0f, 0, 100000});
        if (s.kind == ShapeKind::Rectangle)
            p.push_back({"roundness", "Shape", "Roundness", PropertyKind::Float, &s.roundness, 0.5f, 0, 10000});
        if (s.kind == ShapeKind::Polygon || s.kind == ShapeKind::Star)
            p.push_back({"sides", "Shape", s.kind == ShapeKind::Star ? "Points" : "Sides", PropertyKind::Float,
                         &s.sides, 0.05f, 3, 64, "%.0f"});
        if (s.kind == ShapeKind::Star)
            p.push_back({"inner_radius", "Shape", "Inner Radius", PropertyKind::Float, &s.innerRadius, 0.5f, 1, 100,
                         "%.0f%%"});
        p.push_back({"fill_color", "Fill", s.fill == ShapeFill::Solid ? "Color" : "Color 1", PropertyKind::Color, &s.fillColor});
        if (s.fill != ShapeFill::Solid) p.push_back({"fill_color2", "Fill", "Color 2", PropertyKind::Color, &s.fillColor2});
        if (s.fill == ShapeFill::Linear) p.push_back({"fill_angle", "Fill", "Angle", PropertyKind::Float, &s.fillAngle, 0.5f, 0, 0, "%.0f°"});
        p.push_back({"stroke_color", "Stroke", "Color", PropertyKind::Color, &s.strokeColor});
        p.push_back({"stroke_width", "Stroke", "Width", PropertyKind::Float, &s.strokeWidth, 0.25f, 0, 1000});
        p.push_back({"trim_start", "Stroke", "Trim Start", PropertyKind::Float, &s.trimStart, 0.5f, 0, 100, "%.0f%%"});
        p.push_back({"trim_end", "Stroke", "Trim End", PropertyKind::Float, &s.trimEnd, 0.5f, 0, 100, "%.0f%%"});
        p.push_back({"stroke_dash", "Stroke", "Dash", PropertyKind::Float, &s.dash, 0.25f, 0, 10000, "%.1f px"});
        p.push_back({"stroke_gap", "Stroke", "Gap", PropertyKind::Float, &s.dashGap, 0.25f, 0, 10000, "%.1f px"});
        p.push_back({"stroke_dash_offset", "Stroke", "Dash Offset", PropertyKind::Float, &s.dashOffset, 0.5f, 0, 0, "%.1f px"});
        if (s.repeater) {
            p.push_back({"repeat_copies", "Repeater", "Copies", PropertyKind::Float, &s.repeatCopies, 0.05f, 1, 100, "%.0f"});
            p.push_back({"repeat_offset", "Repeater", "Position", PropertyKind::Vec2, &s.repeatOffset});
            p.push_back({"repeat_rotation", "Repeater", "Rotation", PropertyKind::Float, &s.repeatRotation, 0.5f, 0, 0, "%.1f°"});
            p.push_back({"repeat_scale", "Repeater", "Scale", PropertyKind::Vec2, &s.repeatScale, 0.5f, 0, 0, "%.1f%%"});
            p.push_back({"repeat_end_opacity", "Repeater", "End Opacity", PropertyKind::Float, &s.repeatEndOpacity, 0.5f, 0, 100, "%.0f%%"});
        }
    } else if (type == LayerType::Compound) {
        // Only a transform: its contents are other layers.
    } else if (type == LayerType::Media) {
        if (media.timeRemap) {
            PropertyRef remap{"time_remap", "Time", "Time Remap", PropertyKind::Float, &media.remap, 0.01f, 0, 1e6f, "%.2f s"};
            p.push_back(remap);
        }
        p.push_back({"crop_left", "Crop", "Left", PropertyKind::Float, &media.cropLeft, 0.2f, 0, 100, "%.1f%%"});
        p.push_back({"crop_top", "Crop", "Top", PropertyKind::Float, &media.cropTop, 0.2f, 0, 100, "%.1f%%"});
        p.push_back({"crop_right", "Crop", "Right", PropertyKind::Float, &media.cropRight, 0.2f, 0, 100, "%.1f%%"});
        p.push_back({"crop_bottom", "Crop", "Bottom", PropertyKind::Float, &media.cropBottom, 0.2f, 0, 100, "%.1f%%"});
        if (media.hasAudio)
            p.push_back({"volume", "Audio", "Volume", PropertyKind::Float, &media.volume, 0.5f, 0, 400, "%.0f%%"});
    } else if (type == LayerType::Plugin) {
        p.push_back({"size", "Object", "Size", PropertyKind::Vec2, &plugin.size, 1.0f, 0, 100000});
        for (PluginParam& pp : plugin.params) {
            pp.propertyId = "p." + pp.id;
            const char* id = pp.propertyId.c_str();
            if (pp.kind == PluginParamKind::Number)
                p.push_back({id, pp.group.c_str(), pp.label.c_str(), PropertyKind::Float, &pp.number, pp.speed, pp.min,
                             pp.max, pp.format.c_str()});
            else if (pp.kind == PluginParamKind::Color)
                p.push_back({id, pp.group.c_str(), pp.label.c_str(), PropertyKind::Color, &pp.color});
        }
    } else {
        auto& t = text;
        if (t.counter) {
            const char* format = kDecimalFormats[std::clamp(t.counterDecimals, 0, 6)];
            p.push_back({"counter_from", "Counter", "Start", PropertyKind::Float, &t.counterFrom, 1.0f, -1e12f, 1e12f, format});
            p.push_back({"counter_to", "Counter", "End", PropertyKind::Float, &t.counterTo, 1.0f, -1e12f, 1e12f, format});
            PropertyRef value{"counter_value", "Counter", "Value", PropertyKind::Float, &t.counterValue, 1.0f, -1e12f, 1e12f, format};
            value.whileAnimated = true; // keyframes on it replace the count
            p.push_back(value);
        }
        p.push_back({"font_size", "Text", "Font Size", PropertyKind::Float, &t.fontSize, 0.5f, 1, 2000, "%.0f px"});
        p.push_back({"text_color", "Text", "Color", PropertyKind::Color, &t.color});
        p.push_back({"tracking", "Text", "Tracking", PropertyKind::Float, &t.tracking, 0.25f, -200, 1000});
        p.push_back({"line_height", "Text", "Line Height", PropertyKind::Float, &t.lineHeight, 0.5f, 10, 500, "%.0f%%"});
        p.push_back({"wrap_width", "Text", "Wrap Width", PropertyKind::Float, &t.wrapWidth, 2.0f, 0, 100000, "%.0f px"});
        p.push_back({"reveal", "Text", "Reveal", PropertyKind::Float, &t.reveal, 0.5f, 0, 100, "%.0f%%"});
        p.push_back({"outline_color", "Outline", "Color", PropertyKind::Color, &t.outlineColor});
        p.push_back({"outline_width", "Outline", "Width", PropertyKind::Float, &t.outlineWidth, 0.25f, 0, 500});
        p.push_back({"box_color", "Box", "Color", PropertyKind::Color, &t.boxColor});
        p.push_back({"box_padding", "Box", "Padding", PropertyKind::Float, &t.boxPadding, 0.5f, 0, 2000});
        p.push_back({"box_roundness", "Box", "Roundness", PropertyKind::Float, &t.boxRoundness, 0.5f, 0, 2000});
    }

    // Effect parameters. Ids are index based ("fx0.size") and only used at runtime;
    // effects are saved separately (see Serialization).
    for (size_t i = 0; i < effects.size(); ++i) {
        Effect& e = effects[i];
        int sameType = 0;
        for (size_t j = 0; j <= i; ++j) sameType += effects[j].type == e.type;
        e.group = std::string(effectLabel(e.type)) + (sameType > 1 ? " " + std::to_string(sameType) : "");
        const std::string prefix = "fx" + std::to_string(i) + ".";
        for (int k = 0; k < kEffectSlotCount; ++k) e.ids[k] = prefix + effectSlotId(static_cast<EffectSlot>(k));
        for (const EffectParam& ep : effectParams(e.type)) {
            const PropertyKind kind = ep.slot == EffectSlot::Color    ? PropertyKind::Color
                                      : ep.slot == EffectSlot::Offset ? PropertyKind::Vec2
                                                                      : PropertyKind::Float;
            p.push_back({e.ids[static_cast<int>(ep.slot)].c_str(), e.group.c_str(), ep.label, kind, &e.slot(ep.slot),
                         ep.speed, ep.min, ep.max, ep.format, static_cast<int>(i)});
        }
    }
    return p;
}

PropertyRef* Layer::findProperty(std::vector<PropertyRef>& props, const char* propertyId) {
    for (auto& p : props)
        if (std::strcmp(p.id, propertyId) == 0) return &p;
    return nullptr;
}

Affine Layer::matrixAt(double t) const {
    if (type == LayerType::Background) return Affine{}; // drawn in composition space, over the whole frame
    const Vec2 pos = transform.position.valueAt(t);
    // A camera is only a point of view: position and roll (zoom has its own property).
    if (type == LayerType::Camera) return Affine::translate(pos) * Affine::rotateDeg(transform.rotation.valueAt(t));
    const Vec2 scl = transform.scale.valueAt(t) * 0.01f;
    const Vec2 anc = transform.anchor.valueAt(t);
    const float rot = transform.rotation.valueAt(t);
    return Affine::translate(pos) * Affine::rotateDeg(rot) * Affine::scale(scl) * Affine::translate(anc * -1.0f);
}

void Layer::shiftTime(double delta) {
    inPoint += delta;
    outPoint += delta;
    media.start += delta; // the footage moves with its layer
    for (auto& p : properties())
        for (int i = 0; i < p.anim->keyCount(); ++i) p.anim->setKeyTime(i, p.anim->keyTime(i) + delta);
}

Layer makeShapeLayer(ShapeKind kind, Vec2 position, double duration) {
    Layer l;
    l.type = LayerType::Shape;
    l.name = shapeKindLabel(kind);
    l.outPoint = duration;
    l.shape.kind = kind;
    l.transform.position.base = position;
    switch (kind) {
    case ShapeKind::Rectangle: l.shape.size.base = {480, 300}; l.shape.roundness.base = 24; break;
    case ShapeKind::Ellipse: l.shape.size.base = {320, 320}; break;
    case ShapeKind::Polygon: l.shape.size.base = {340, 340}; l.shape.sides.base = 6; break;
    case ShapeKind::Star: l.shape.size.base = {360, 360}; l.shape.sides.base = 5; break;
    case ShapeKind::Line:
        l.shape.size.base = {500, 0};
        l.shape.fillEnabled = false;
        l.shape.strokeEnabled = true;
        l.shape.strokeColor.base = Color::fromHex(0xE8B931);
        l.shape.strokeWidth.base = 10;
        break;
    case ShapeKind::Path: // drawn with the pen tool: a soft triangle to start from
        l.shape.path.vertices = {{{0, -170}, {}, {}}, {{190, 140}, {}, {}}, {{-190, 140}, {}, {}}};
        break;
    case ShapeKind::Count: break;
    }
    return l;
}

const char* mediaKindLabel(MediaKind kind) {
    switch (kind) {
    case MediaKind::Image: return "Image";
    case MediaKind::Video: return "Video";
    case MediaKind::Audio: return "Audio";
    }
    return "Media";
}

Layer makeMediaLayer(const MediaItem& item, Vec2 position, double start, double stillLength) {
    Layer l;
    l.type = LayerType::Media;
    l.name = item.name;
    l.label = item.kind == MediaKind::Audio ? 2 : item.kind == MediaKind::Video ? 5 : 1;
    l.media.mediaId = item.id;
    l.media.size = {static_cast<float>(item.width), static_cast<float>(item.height)};
    l.media.hasVideo = item.kind != MediaKind::Audio;
    l.media.hasAudio = item.hasAudio;
    l.media.start = start;
    l.inPoint = start;
    const double length = std::max(0.1, item.kind == MediaKind::Image ? stillLength : item.duration);
    l.outPoint = start + length;
    l.transform.position.base = position;
    return l;
}

Layer makeCompoundLayer(Vec2 position, double duration) {
    Layer l;
    l.type = LayerType::Compound;
    l.name = "Compound";
    l.outPoint = duration;
    l.label = 4;
    l.transform.position.base = position;
    return l;
}

Layer makeCameraLayer(Vec2 compSize, double duration) {
    Layer l;
    l.type = LayerType::Camera;
    l.name = "Camera";
    l.outPoint = duration;
    l.label = 5;
    l.transform.position.base = compSize * 0.5f;
    l.camera.frameSize = compSize;
    return l;
}

std::string formatCounter(double value, int decimals, const std::string& separator) {
    decimals = std::clamp(decimals, 0, 6);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string s = buf;
    if (s.find_first_not_of("-0.") == std::string::npos && s[0] == '-') s.erase(0, 1); // no "-0"
    const size_t dot = s.find('.');
    std::string whole = s.substr(0, dot), out;
    const size_t digits = whole.size() - (whole[0] == '-' ? 1 : 0);
    for (size_t i = 0; i < whole.size(); ++i) {
        out += whole[i];
        const size_t left = whole.size() - 1 - i; // digits still to come
        if (!separator.empty() && whole[i] != '-' && left > 0 && left % 3 == 0 && digits > 3) out += separator;
    }
    if (dot != std::string::npos) out += (separator == "." ? "," : ".") + s.substr(dot + 1);
    return out;
}

std::string TextProps::textAt(double t, double in, double out) const {
    if (counter) {
        double v = counterValue.valueAt(t);
        if (!counterValue.isAnimated()) { // counts by itself, from the clip's start
            const double span = counterDuration > 0.0f ? counterDuration : std::max(1e-6, out - in);
            const double u = std::clamp((t - in) / span, 0.0, 1.0);
            const double e = counterEasing == CountEasing::Linear ? u
                           : counterEasing == CountEasing::EaseOut ? 1.0 - std::pow(1.0 - u, 3.0)
                                                                   : (u < 0.5 ? 4.0 * u * u * u : 1.0 - std::pow(-2.0 * u + 2.0, 3.0) * 0.5);
            const double from = counterFrom.valueAt(t), to = counterTo.valueAt(t);
            v = from + (to - from) * e;
        }
        return counterPrefix + formatCounter(v, counterDecimals, counterSeparator) + counterSuffix;
    }
    if (!subtitle || captions.empty()) return text;
    // The last caption starting at or before t (they are sorted), if it still covers t.
    auto it = std::upper_bound(captions.begin(), captions.end(), t, [](double v, const Caption& c) { return v < c.start; });
    if (it == captions.begin()) return {};
    --it;
    return t < it->end ? it->text : std::string();
}

Layer makeCounterLayer(Vec2 position, double duration) {
    Layer l = makeTextLayer("Counter", position, duration);
    l.text.counter = true; // counts 0 to 100 in its first 2 seconds, by itself
    return l;
}

Layer makeSubtitleLayer(Vec2 compSize, double duration) {
    Layer l = makeTextLayer("Subtitles", {compSize.x * 0.5f, compSize.y * 0.92f}, duration); // the bottom of the last line
    l.name = "Subtitles";
    l.label = 6;
    TextProps& t = l.text;
    t.subtitle = true;
    t.fontStyle = "SemiBold";
    t.fontSize.base = std::max(16.0f, compSize.y * 0.055f);
    t.lineHeight.base = 125.0f;
    t.wrapWidth.base = compSize.x * 0.78f;
    t.fitLines = 2; // long captions get smaller rather than taller
    t.outlineEnabled = true;
    t.outlineWidth.base = std::max(2.0f, t.fontSize.base * 0.045f);
    t.boxEnabled = true;
    t.boxColor.base = Color::fromHex(0x000000, 0.74f);
    t.boxPadding.base = t.fontSize.base * 0.4f;
    t.boxRoundness.base = t.fontSize.base * 0.2f;
    return l;
}

Layer makeTextLayer(const std::string& text, Vec2 position, double duration) {
    Layer l;
    l.type = LayerType::Text;
    l.name = text;
    l.outPoint = duration;
    l.text.text = text;
    l.transform.position.base = position;
    return l;
}

std::vector<float> propertyValue(const PropertyRef& p, double t) {
    switch (p.kind) {
    case PropertyKind::Float: return {p.asFloat().valueAt(t)};
    case PropertyKind::Vec2: {
        const Vec2 v = p.asVec2().valueAt(t);
        return {v.x, v.y};
    }
    case PropertyKind::Color: {
        const Color c = p.asColor().valueAt(t);
        return {c.r, c.g, c.b, c.a};
    }
    }
    return {};
}

void setPropertyValue(const PropertyRef& p, double t, const std::vector<float>& v) {
    switch (p.kind) {
    case PropertyKind::Float: p.asFloat().set(t, v.at(0)); break;
    case PropertyKind::Vec2: p.asVec2().set(t, {v.at(0), v.at(1)}); break;
    case PropertyKind::Color: p.asColor().set(t, {v.at(0), v.at(1), v.at(2), v.at(3)}); break;
    }
}

Layer makePluginLayer(const std::string& name, PluginProps props, Vec2 position, double duration) {
    Layer l;
    l.type = LayerType::Plugin;
    l.name = name;
    l.outPoint = duration;
    l.transform.position.base = position;
    l.plugin = std::move(props);
    return l;
}

const char* backgroundFillId(BackgroundFill f) {
    static const char* ids[] = {"solid", "linear", "radial", "image"};
    return ids[std::clamp(static_cast<int>(f), 0, 3)];
}
const char* backgroundFillLabel(BackgroundFill f) {
    static const char* labels[] = {"Solid Color", "Linear Gradient", "Radial Gradient", "Image or Video"};
    return labels[std::clamp(static_cast<int>(f), 0, 3)];
}
const char* backgroundFitId(BackgroundFit f) {
    static const char* ids[] = {"cover", "contain", "stretch"};
    return ids[std::clamp(static_cast<int>(f), 0, 2)];
}
const char* backgroundFitLabel(BackgroundFit f) {
    static const char* labels[] = {"Cover", "Contain", "Stretch"};
    return labels[std::clamp(static_cast<int>(f), 0, 2)];
}

Layer makeBackgroundLayer(double duration) {
    Layer l;
    l.type = LayerType::Background;
    l.name = "Background";
    l.outPoint = std::max(duration, 0.04);
    return l;
}

void MediaProps::cropRect(double t, Vec2& mn, Vec2& mx) const {
    // Fractions of the source cut from each side; opposite sides leave at least 1% between them.
    auto cut = [&](const AnimFloat& a) { return std::clamp(a.valueAt(t), 0.0f, 100.0f) * 0.01f; };
    float l = cut(cropLeft), r = cut(cropRight), tp = cut(cropTop), b = cut(cropBottom);
    if (l + r > 0.99f) {
        const float k = 0.99f / (l + r);
        l *= k, r *= k;
    }
    if (tp + b > 0.99f) {
        const float k = 0.99f / (tp + b);
        tp *= k, b *= k;
    }
    mn = {size.x * (l - 0.5f), size.y * (tp - 0.5f)};
    mx = {size.x * (0.5f - r), size.y * (0.5f - b)};
}

bool MediaProps::cropped(double t) const {
    return cropLeft.valueAt(t) > 0.0f || cropTop.valueAt(t) > 0.0f || cropRight.valueAt(t) > 0.0f || cropBottom.valueAt(t) > 0.0f;
}

std::vector<float> defaultPropertyValue(const Layer& layer, const char* propertyId, Vec2 compCenter) {
    if (std::strncmp(propertyId, "mask", 4) == 0) { // "mask0.opacity": 100%, feather and expansion 0
        const char* dot = std::strchr(propertyId, '.');
        return {dot && std::strcmp(dot + 1, "opacity") == 0 ? 100.0f : 0.0f};
    }
    if (layer.type == LayerType::Media && std::strcmp(propertyId, "time_remap") == 0) return {0.0f};
    if (layer.type == LayerType::Media && std::strcmp(propertyId, "volume") == 0) return {100.0f};
    if (layer.type == LayerType::Media && std::strncmp(propertyId, "crop_", 5) == 0) return {0.0f};
    if (layer.type == LayerType::Background) {
        Layer fresh = makeBackgroundLayer(1.0);
        fresh.background.fill = layer.background.fill;
        auto props = fresh.properties();
        if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
        return {};
    }
    if (layer.type == LayerType::Plugin) {
        Layer fresh = makePluginLayer(layer.name, layer.plugin, compCenter, 1.0);
        fresh.plugin.size = AnimVec2{layer.plugin.defaultSize};
        for (PluginParam& pp : fresh.plugin.params) {
            pp.number = AnimFloat{pp.defaultNumber};
            pp.color = AnimColor{pp.defaultColor};
        }
        auto props = fresh.properties();
        if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
        return {};
    }
    if (layer.type == LayerType::Text && layer.text.counter) {
        Layer fresh = makeCounterLayer(compCenter, 1.0);
        auto props = fresh.properties();
        if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
        return {};
    }
    if (layer.type == LayerType::Text && layer.text.subtitle) {
        Layer fresh = makeSubtitleLayer(compCenter * 2.0f, 1.0);
        auto props = fresh.properties();
        if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
        return {};
    }
    Layer fresh = layer.type == LayerType::Text     ? makeTextLayer(layer.text.text, compCenter, 1.0)
                  : layer.type == LayerType::Camera ? makeCameraLayer(compCenter * 2.0f, 1.0)
                                                    : makeShapeLayer(layer.shape.kind, compCenter, 1.0);
    fresh.shape.fill = layer.shape.fill; // which gradient and repeater rows exist
    fresh.shape.repeater = layer.shape.repeater;
    auto props = fresh.properties();
    if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
    return {};
}

} // namespace bemotion
