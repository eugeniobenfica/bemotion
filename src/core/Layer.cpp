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

std::vector<PropertyRef> Layer::properties() {
    std::vector<PropertyRef> p;
    auto& tr = transform;
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
    } else {
        auto& t = text;
        p.push_back({"font_size", "Text", "Font Size", PropertyKind::Float, &t.fontSize, 0.5f, 1, 2000, "%.0f px"});
        p.push_back({"text_color", "Text", "Color", PropertyKind::Color, &t.color});
        p.push_back({"tracking", "Text", "Tracking", PropertyKind::Float, &t.tracking, 0.25f, -200, 1000});
        p.push_back({"line_height", "Text", "Line Height", PropertyKind::Float, &t.lineHeight, 0.5f, 10, 500, "%.0f%%"});
        p.push_back({"reveal", "Text", "Reveal", PropertyKind::Float, &t.reveal, 0.5f, 0, 100, "%.0f%%"});
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
    const Vec2 scl = transform.scale.valueAt(t) * 0.01f;
    const Vec2 anc = transform.anchor.valueAt(t);
    const float rot = transform.rotation.valueAt(t);
    return Affine::translate(pos) * Affine::rotateDeg(rot) * Affine::scale(scl) * Affine::translate(anc * -1.0f);
}

void Layer::shiftTime(double delta) {
    inPoint += delta;
    outPoint += delta;
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

std::vector<float> defaultPropertyValue(const Layer& layer, const char* propertyId, Vec2 compCenter) {
    Layer fresh = layer.type == LayerType::Text ? makeTextLayer(layer.text.text, compCenter, 1.0)
                                                : makeShapeLayer(layer.shape.kind, compCenter, 1.0);
    auto props = fresh.properties();
    if (const PropertyRef* p = fresh.findProperty(props, propertyId)) return propertyValue(*p, 0.0);
    return {};
}

} // namespace bemotion
