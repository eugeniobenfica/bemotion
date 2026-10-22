#pragma once

#include "core/Animated.h"
#include "core/Effects.h"
#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

enum class LayerType { Shape, Text, Camera, Plugin, Compound };

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
    int effectIndex = -1; // >= 0: parameter of Layer::effects[effectIndex]

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

// A camera frames the scene: its position is the composition point shown at
// the center, rotation rolls the view and zoom magnifies it.
struct CameraProps {
    AnimFloat zoom{100.0f}; // percent
    Vec2 frameSize{1920.0f, 1080.0f}; // composition size, kept in sync (not saved)
};

struct TextProps {
    std::string text = "Text";
    std::string fontFamily = "Inter"; // any installed family; Inter ships with the app
    std::string fontStyle = "SemiBold";
    TextAlign align = TextAlign::Center;
    AnimFloat fontSize{120.0f};
    AnimColor color{Color::fromHex(0xFFFFFF)};
    AnimFloat tracking{0.0f};    // extra letter spacing, px
    AnimFloat lineHeight{120.0f}; // percent of font size
    AnimFloat reveal{100.0f};    // percent of characters shown (typewriter)
};

// --- plugin objects ---------------------------------------------------------------
// Object types registered by plugins (e.g. charts). The plugin declares a
// parameter schema; the layer stores the parameters with their definitions so
// projects stay readable (and editable) even when the plugin is missing.

enum class PluginParamKind { Number, Color, Toggle, Choice, Text };

struct PluginParam {
    std::string id;
    std::string label;
    std::string group = "Parameters";
    PluginParamKind kind = PluginParamKind::Number;
    // Numbers and colors are animatable properties ("p.<id>").
    AnimFloat number{0.0f};
    AnimColor color{Color::fromHex(0xFFFFFF)};
    float defaultNumber = 0.0f;
    Color defaultColor = Color::fromHex(0xFFFFFF);
    float min = 0.0f, max = 0.0f, speed = 0.5f; // min == max: unbounded
    std::string format = "%.1f";
    bool toggle = false;
    int choice = 0;
    std::vector<std::string> choices;
    std::string text;
    std::string propertyId; // "p.<id>", kept here so PropertyRef::id stays valid
};

// One data point (charts and other data-driven objects).
struct DataRow {
    std::string label;
    float value = 0.0f;
    Color color = Color::fromHex(0xE8B931);
};

struct PluginProps {
    std::string objectType; // "<plugin id>.<object id>", e.g. "charts.bar"
    AnimVec2 size{Vec2{900.0f, 540.0f}}; // the object frame; plugins draw inside +-size/2
    Vec2 defaultSize{900.0f, 540.0f};
    std::vector<PluginParam> params;
    bool hasRows = false;
    std::vector<DataRow> rows;

    PluginParam* find(const std::string& id);
};

constexpr int kLabelColorCount = 8;
Color labelColor(int index);

struct Layer {
    uint64_t id = 0;
    // Timeline row. Several layers ("clips") may share a track as long as
    // they do not overlap in time; 0 means "give me a new track".
    uint64_t track = 0;
    // The compound this layer belongs to (0: none). Children are drawn by their
    // compound, through its transform: their own transform is in its space.
    uint64_t parent = 0;
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
    CameraProps camera;
    PluginProps plugin;
    std::vector<Effect> effects; // applied in order

    // All animatable properties that apply to this layer's type, in UI order.
    std::vector<PropertyRef> properties();
    PropertyRef* findProperty(std::vector<PropertyRef>& props, const char* propertyId);

    bool activeAt(double t) const { return visible && t >= inPoint && t < outPoint; }

    // Local (layer) space -> composition space at time t.
    Affine matrixAt(double t) const;

    // Shifts the layer and all its keyframes in time.
    void shiftTime(double delta);
};

Layer makeShapeLayer(ShapeKind kind, Vec2 position, double duration);
Layer makeTextLayer(const std::string& text, Vec2 position, double duration);
Layer makeCameraLayer(Vec2 compSize, double duration);
Layer makeCompoundLayer(Vec2 position, double duration);
// A plugin object with the given parameters at their defaults (see PluginHost).
Layer makePluginLayer(const std::string& name, PluginProps props, Vec2 position, double duration);

// Generic access to a property value as 1 (float), 2 (vec2) or 4 (color) components.
std::vector<float> propertyValue(const PropertyRef& p, double t);
// Edits the value at `t`: updates/creates a key when animated, else the static value.
void setPropertyValue(const PropertyRef& p, double t, const std::vector<float>& v);
// The value a property has on a freshly created layer of the same kind
// (position: the composition center). Used by the reset buttons.
std::vector<float> defaultPropertyValue(const Layer& layer, const char* propertyId, Vec2 compCenter);

} // namespace bemotion
