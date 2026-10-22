#pragma once

#include "core/Animated.h"
#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

enum class LayerType { Shape, Text };

enum class ShapeKind { Rectangle, Ellipse, Polygon, Star, Line, Count };

enum class TextAlign { Left, Center, Right };

enum class FontWeight { Regular, Medium, SemiBold, Bold, Count };

const char* shapeKindLabel(ShapeKind kind);
const char* fontWeightLabel(FontWeight weight);

enum class PropertyKind { Float, Vec2, Color };

// Type-erased handle to one animatable property of a layer. It powers the
// generic inspector, timeline and serialization, so adding a new property only
// requires declaring the member and listing it in Layer::properties().
struct PropertyRef {
    const char* id;    // stable identifier used in project files
    const char* group; // UI grouping ("Transform", "Shape", ...)
    const char* label; // UI label
    PropertyKind kind;
    AnimatedBase* anim;

    // UI hints for the value editor.
    float speed = 1.0f;
    float min = 0.0f;
    float max = 0.0f; // min == max means unbounded
    const char* format = "%.1f";

    AnimFloat& asFloat() const { return *static_cast<AnimFloat*>(anim); }
    AnimVec2& asVec2() const { return *static_cast<AnimVec2*>(anim); }
    AnimColor& asColor() const { return *static_cast<AnimColor*>(anim); }
};

struct TransformProps {
    AnimVec2 anchor{Vec2{0.0f, 0.0f}};
    AnimVec2 position{Vec2{0.0f, 0.0f}};
    AnimVec2 scale{Vec2{100.0f, 100.0f}}; // percent
    AnimFloat rotation{0.0f};             // degrees, clockwise
    AnimFloat opacity{100.0f};            // percent
};

struct ShapeProps {
    ShapeKind kind = ShapeKind::Rectangle;
    AnimVec2 size{Vec2{400.0f, 400.0f}};
    AnimFloat roundness{0.0f};    // rectangle corner radius, px
    AnimFloat sides{5.0f};        // polygon sides / star points
    AnimFloat innerRadius{50.0f}; // star inner radius, percent of outer

    bool fillEnabled = true;
    AnimColor fillColor{Color::fromHex(0xE8B931)};
    bool strokeEnabled = false;
    AnimColor strokeColor{Color::fromHex(0xFFFFFF)};
    AnimFloat strokeWidth{8.0f};
    AnimFloat trimStart{0.0f}; // percent of the outline
    AnimFloat trimEnd{100.0f};
};

struct TextProps {
    std::string text = "Text";
    FontWeight weight = FontWeight::SemiBold;
    TextAlign align = TextAlign::Center;
    AnimFloat fontSize{120.0f};
    AnimColor color{Color::fromHex(0xFFFFFF)};
    AnimFloat tracking{0.0f};    // extra letter spacing, px
    AnimFloat lineHeight{120.0f}; // percent of font size
    AnimFloat reveal{100.0f};    // percent of characters shown (typewriter)
};

constexpr int kLabelColorCount = 8;
Color labelColor(int index);

struct Layer {
    uint64_t id = 0;
    std::string name;
    LayerType type = LayerType::Shape;
    bool visible = true;
    bool locked = false;
    int label = 0;

    double inPoint = 0.0; // seconds, layer is active in [inPoint, outPoint)
    double outPoint = 10.0;

    TransformProps transform;
    ShapeProps shape;
    TextProps text;

    // All animatable properties that apply to this layer's type, in UI order.
    std::vector<PropertyRef> properties();
    PropertyRef* findProperty(std::vector<PropertyRef>& props, const char* id);

    bool activeAt(double t) const { return visible && t >= inPoint && t < outPoint; }

    // Local (layer) space -> composition space at time t.
    Affine matrixAt(double t) const;

    // Shifts the layer and all its keyframes in time.
    void shiftTime(double delta);
};

Layer makeShapeLayer(ShapeKind kind, Vec2 position, double duration);
Layer makeTextLayer(const std::string& text, Vec2 position, double duration);

} // namespace bemotion
