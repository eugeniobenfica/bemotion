#include "core/Layer.h"

#include <atomic>
#include <cstring>

namespace bemotion {

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
    case ShapeKind::Count: break;
    }
    return "Shape";
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
    if (type == LayerType::Media && !media.hasVideo) { // audio only: just its level
        p.push_back({"volume", "Audio", "Volume", PropertyKind::Float, &media.volume, 0.5f, 0, 400, "%.0f%%"});
        return p;
    }
    if (type == LayerType::Camera) { // never drawn: no shape, text or effects
        p.push_back({"position", "Transform", "Position", PropertyKind::Vec2, &tr.position});
        p.push_back({"rotation", "Transform", "Rotation", PropertyKind::Float, &tr.rotation, 0.5f, 0, 0, "%.1f°"});
        p.push_back({"camera_zoom", "Camera", "Zoom", PropertyKind::Float, &camera.zoom, 0.5f, 1, 10000, "%.0f%%"});
        return p;
    }
    p.push_back({"anchor", "Transform", "Anchor Point", PropertyKind::Vec2, &tr.anchor});
    p.push_back({"position", "Transform", "Position", PropertyKind::Vec2, &tr.position});
    p.push_back({"scale", "Transform", "Scale", PropertyKind::Vec2, &tr.scale, 0.5f, 0, 0, "%.1f%%"});
    p.push_back({"rotation", "Transform", "Rotation", PropertyKind::Float, &tr.rotation, 0.5f, 0, 0, "%.1f°"});
    p.push_back({"opacity", "Transform", "Opacity", PropertyKind::Float, &tr.opacity, 0.5f, 0, 100, "%.0f%%"});

    if (type == LayerType::Shape) {
        auto& s = shape;
        p.push_back({"size", "Shape", "Size", PropertyKind::Vec2, &s.size, 1.0f, 0, 100000});
        if (s.kind == ShapeKind::Rectangle)
            p.push_back({"roundness", "Shape", "Roundness", PropertyKind::Float, &s.roundness, 0.5f, 0, 10000});
        if (s.kind == ShapeKind::Polygon || s.kind == ShapeKind::Star)
            p.push_back({"sides", "Shape", s.kind == ShapeKind::Star ? "Points" : "Sides", PropertyKind::Float,
                         &s.sides, 0.05f, 3, 64, "%.0f"});
        if (s.kind == ShapeKind::Star)
            p.push_back({"inner_radius", "Shape", "Inner Radius", PropertyKind::Float, &s.innerRadius, 0.5f, 1, 100,
                         "%.0f%%"});
        p.push_back({"fill_color", "Fill", "Color", PropertyKind::Color, &s.fillColor});
        p.push_back({"stroke_color", "Stroke", "Color", PropertyKind::Color, &s.strokeColor});
        p.push_back({"stroke_width", "Stroke", "Width", PropertyKind::Float, &s.strokeWidth, 0.25f, 0, 1000});
        p.push_back({"trim_start", "Stroke", "Trim Start", PropertyKind::Float, &s.trimStart, 0.5f, 0, 100, "%.0f%%"});
        p.push_back({"trim_end", "Stroke", "Trim End", PropertyKind::Float, &s.trimEnd, 0.5f, 0, 100, "%.0f%%"});
    } else if (type == LayerType::Compound) {
        // Only a transform: its contents are other layers.
    } else if (type == LayerType::Media) {
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
        p.push_back({"font_size", "Text", "Font Size", PropertyKind::Float, &t.fontSize, 0.5f, 1, 2000, "%.0f px"});
        p.push_back({"text_color", "Text", "Color", PropertyKind::Color, &t.color});
        p.push_back({"tracking", "Text", "Tracking", PropertyKind::Float, &t.tracking, 0.25f, -200, 1000});
        p.push_back({"line_height", "Text", "Line Height", PropertyKind::Float, &t.lineHeight, 0.5f, 10, 500, "%.0f%%"});
        p.push_back({"reveal", "Text", "Reveal", PropertyKind::Float, &t.reveal, 0.5f, 0, 100, "%.0f%%"});
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

std::vector<float> defaultPropertyValue(const Layer& layer, const char* propertyId, Vec2 compCenter) {
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
    Layer fresh = layer.type == LayerType::Text     ? makeTextLayer(layer.text.text, compCenter, 1.0)
                  : layer.type == LayerType::Camera ? makeCameraLayer(compCenter * 2.0f, 1.0)
                                                    : makeShapeLayer(layer.shape.kind, compCenter, 1.0);
    auto props = fresh.properties();
    if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
    return {};
}

} // namespace bemotion
