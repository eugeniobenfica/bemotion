#pragma once

#include "core/Animated.h"
#include "core/Effects.h"
#include "core/Media.h"
#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

enum class LayerType { Shape, Text, Camera, Plugin, Compound, Media, Background };

enum class ShapeKind { Rectangle, Ellipse, Polygon, Star, Line, Path, Count };

// How a layer mixes with what is under it (After Effects' blending modes; ids: the file format
// and operations).
enum class BlendMode { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight,
                       Difference, Exclusion, Hue, Saturation, Color, Luminosity, Add, Count };
inline constexpr const char* kBlendModeIds[] = {"normal",     "multiply",   "screen",     "overlay",    "darken",  "lighten",
                                                "color_dodge", "color_burn", "hard_light", "soft_light", "difference",
                                                "exclusion",  "hue",        "saturation", "color",      "luminosity", "add"};
inline constexpr const char* kBlendModeLabels[] = {"Normal",     "Multiply",   "Screen",     "Overlay",    "Darken",  "Lighten",
                                                   "Color Dodge", "Color Burn", "Hard Light", "Soft Light", "Difference",
                                                   "Exclusion",  "Hue",        "Saturation", "Color",      "Luminosity", "Add"};
// A track matte: the layer shows only where another layer (its matte, not drawn itself) is opaque
// or bright, or only where it is not.
enum class MatteMode { None, Alpha, AlphaInverted, Luma, LumaInverted, Count };
inline constexpr const char* kMatteModeIds[] = {"none", "alpha", "alpha_inverted", "luma", "luma_inverted"};
inline constexpr const char* kMatteModeLabels[] = {"No Track Matte", "Alpha Matte", "Alpha Inverted Matte", "Luma Matte",
                                                   "Luma Inverted Matte"};
// How a mask combines with the masks above it.
enum class MaskMode { Add, Subtract, Intersect, Difference, Count };
inline constexpr const char* kMaskModeIds[] = {"add", "subtract", "intersect", "difference"};
inline constexpr const char* kMaskModeLabels[] = {"Add", "Subtract", "Intersect", "Difference"};
// A shape's fill: a color, or a gradient from its color to a second one.
enum class ShapeFill { Solid, Linear, Radial, Count };
inline constexpr const char* kShapeFillIds[] = {"solid", "linear", "radial"};
inline constexpr const char* kShapeFillLabels[] = {"Solid Color", "Linear Gradient", "Radial Gradient"};

enum class TextAlign { Left, Center, Right };

// Text: how a counter eases, and how letters animate (ids: the file format and operations).
enum class CountEasing { Linear, EaseOut, EaseInOut };
enum class LetterAnim { None, Fade, Rise, Drop, Pop, Slide, Spin, Blur };
enum class LetterLoop { None, Wave, Jitter, Pulse };
enum class LetterUnit { Letter, Word, Line };
enum class LetterOrder { Forward, Backward, Center, Random };
inline constexpr const char* kCountEasingIds[] = {"linear", "ease_out", "ease_in_out"};
inline constexpr const char* kCountEasingLabels[] = {"Linear", "Ease Out", "Ease In Out"};
inline constexpr const char* kLetterAnimIds[] = {"none", "fade", "rise", "drop", "pop", "slide", "spin", "blur"};
inline constexpr const char* kLetterAnimLabels[] = {"None", "Fade", "Rise", "Drop", "Pop", "Slide", "Spin", "Blur"};
inline constexpr const char* kLetterLoopIds[] = {"none", "wave", "jitter", "pulse"};
inline constexpr const char* kLetterLoopLabels[] = {"None", "Wave", "Jitter", "Pulse"};
inline constexpr const char* kLetterUnitIds[] = {"letter", "word", "line"};
inline constexpr const char* kLetterUnitLabels[] = {"Letters", "Words", "Lines"};
inline constexpr const char* kLetterOrderIds[] = {"forward", "backward", "center", "random"};
inline constexpr const char* kLetterOrderLabels[] = {"First to Last", "Last to First", "From the Center", "Random"};
// The index of `id` in `ids`, or -1.
template <size_t N>
int indexOfId(const char* const (&ids)[N], const std::string& id) {
    for (size_t i = 0; i < N; ++i)
        if (id == ids[i]) return static_cast<int>(i);
    return -1;
}

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
    bool whileAnimated = false; // shown in the panels only once it has keyframes (kept for scripts)
    int maskIndex = -1;   // >= 0: parameter of Layer::masks[maskIndex] (saved with the mask)

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
    AnimFloat depth{0.0f};                // Z position, px (3D layers and cameras): larger is farther away
    // 3D layers and cameras, degrees: X tips the top away, Y turns the right side away
    // (rotation above turns around Z). A camera turned so looks down / to the left.
    AnimFloat rotationX{0.0f};
    AnimFloat rotationY{0.0f};
};

// A point of a Bézier path and its handles, relative to it (zero: a corner without handles).
struct PathVertex {
    Vec2 point, in, out;
    bool operator==(const PathVertex&) const = default;
};

// An outline made of cubic Bézier segments from vertex to vertex (shape paths, masks), in the
// layer's own space. Closed: the last vertex joins the first.
struct BezierPath {
    std::vector<PathVertex> vertices;
    bool closed = true;
    bool operator==(const BezierPath&) const = default;
    bool empty() const { return vertices.size() < 2; }
};

struct ShapeProps {
    ShapeKind kind = ShapeKind::Rectangle;
    AnimVec2 size{Vec2{400.0f, 400.0f}};
    AnimFloat roundness{0.0f};    // rectangle corner radius, px
    AnimFloat sides{5.0f};        // polygon sides / star points
    AnimFloat innerRadius{50.0f}; // star inner radius, percent of outer
    BezierPath path;              // ShapeKind::Path: the outline (drawn with the pen tool)

    bool fillEnabled = true;
    ShapeFill fill = ShapeFill::Solid;
    AnimColor fillColor{Color::fromHex(0xE8B931)};
    AnimColor fillColor2{Color::fromHex(0xE5604D)}; // gradients: where they end
    AnimFloat fillAngle{90.0f};                     // linear gradient direction: 0 left to right, 90 top to bottom
    bool strokeEnabled = false;
    AnimColor strokeColor{Color::fromHex(0xFFFFFF)};
    AnimFloat strokeWidth{8.0f};
    AnimFloat trimStart{0.0f}; // percent of the outline
    AnimFloat trimEnd{100.0f};
    // Dashes along the stroke: `dash` px drawn, `dashGap` px left out (0: a solid line), moved
    // along the outline by `dashOffset` px.
    AnimFloat dash{0.0f}, dashGap{0.0f}, dashOffset{0.0f};
    // Repeater (like After Effects'): `repeatCopies` copies, each moved, turned and scaled from the
    // last by the same amounts, fading to `repeatEndOpacity` for the last one.
    bool repeater = false;
    AnimFloat repeatCopies{3.0f};
    AnimVec2 repeatOffset{Vec2{120.0f, 0.0f}};
    AnimFloat repeatRotation{0.0f};
    AnimVec2 repeatScale{Vec2{100.0f, 100.0f}};
    AnimFloat repeatEndOpacity{100.0f};
    int copiesAt(double t) const; // 1 without a repeater
};

// A mask cuts a layer to the inside (or outside) of a path in its own space; several combine
// from the first down (Add, Subtract, Intersect, Difference), like After Effects' masks.
struct Mask {
    std::string name = "Mask 1";
    BezierPath path;
    MaskMode mode = MaskMode::Add;
    bool inverted = false;
    bool enabled = true;
    AnimFloat feather{0.0f};   // px of soft edge
    AnimFloat opacity{100.0f}; // percent
    AnimFloat expansion{0.0f}; // px: grows (or, negative, shrinks) the outline
    // Runtime: property ids ("mask0.feather") and the group label, as for effects.
    std::string group, ids[3];
};

// A camera frames the scene: its position is the composition point shown at
// the center, rotation rolls the view and zoom magnifies it. Its Z position
// moves it toward (larger) or away from the layers: 3D layers are seen in
// perspective at their own Z position (see Composition::viewFor).
struct CameraProps {
    AnimFloat zoom{100.0f}; // percent
    Vec2 frameSize{1920.0f, 1080.0f}; // composition size, kept in sync (not saved)
};

// One line of a subtitle layer: shown from `start` to `end` (composition seconds).
struct Caption {
    double start = 0.0;
    double end = 0.0;
    std::string text;
    bool operator==(const Caption&) const = default;
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
    AnimFloat wrapWidth{0.0f};   // px: lines also break at spaces past this width (0: only at line breaks)
    bool outlineEnabled = false; // an outline around the letters
    AnimColor outlineColor{Color::fromHex(0x000000)};
    AnimFloat outlineWidth{6.0f};
    bool boxEnabled = false;     // a box behind the text
    AnimColor boxColor{Color::fromHex(0x000000, 0.6f)};
    AnimFloat boxPadding{20.0f};
    AnimFloat boxRoundness{12.0f};
    // A subtitle layer shows, at each moment, the caption that covers it (nothing between
    // captions; `text` while there are none). Captions are sorted and do not overlap.
    bool subtitle = false;
    std::vector<Caption> captions;
    // A counter shows `counterValue` as a number: `counterDecimals` digits after the decimal
    // mark (0 to 6), thousands grouped by `counterSeparator` ("", ",", "." or " "; with "." the
    // decimal mark is ","), between `counterPrefix` and `counterSuffix` ("R$ ", "%").
    // It counts by itself from `counterFrom` to `counterTo` over `counterDuration` seconds from
    // the clip's start (0: the whole clip), eased; keyframes on `counterValue` (older counters,
    // or animated by hand) take over.
    bool counter = false;
    AnimFloat counterValue{0.0f};
    AnimFloat counterFrom{0.0f};
    AnimFloat counterTo{100.0f};
    float counterDuration = 2.0f;
    CountEasing counterEasing = CountEasing::EaseOut;
    int counterDecimals = 0;
    std::string counterPrefix, counterSuffix, counterSeparator;
    // Letters (or words, or lines) come in one after another from the clip's start, leave one
    // after another before its end, and may move all along (a loop).
    LetterAnim letterIn = LetterAnim::None, letterOut = LetterAnim::None;
    LetterLoop letterLoop = LetterLoop::None;
    LetterUnit letterUnit = LetterUnit::Letter;
    LetterOrder letterOrder = LetterOrder::Forward;
    float letterDelay = 0.04f;    // seconds between one and the next
    float letterDuration = 0.5f;  // seconds each takes
    // Auto fit: with a Wrap Width, the font shrinks (down to 40%) so the text fits in that many
    // lines (0: off). Subtitles start with 2.
    int fitLines = 0;

    // What the layer shows at `t`; `in` / `out`: the clip's (a counter counts from `in`).
    std::string textAt(double t, double in = 0.0, double out = 0.0) const;
    bool lettersAnimated() const { return letterIn != LetterAnim::None || letterOut != LetterAnim::None || letterLoop != LetterLoop::None; }
};

// --- plugin objects ---------------------------------------------------------------
// Object types registered by plugins (e.g. charts). The plugin declares a
// parameter schema; the layer stores the parameters with their definitions so
// projects stay readable (and editable) even when the plugin is missing.

enum class PluginParamKind { Number, Color, Toggle, Choice, Text, File };

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
    std::string text;                // Text: the text; File: the file's path (UTF-8, absolute)
    std::string relative;            // File: the path relative to the project file (kept on save)
    std::vector<std::string> accept; // File: extensions without the dot ("glb", "obj")
    std::string propertyId; // "p.<id>", kept here so PropertyRef::id stays valid
};

// One data point (charts and other data-driven objects).
struct DataRow {
    std::string label;
    float value = 0.0f;
    Color color = Color::fromHex(0xE8B931);
};

// A table of text cells (plugin objects like tables): a header and rows of the same width.
struct DataTable {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

struct PluginProps {
    std::string objectType; // "<plugin id>.<object id>", e.g. "charts.bar"
    AnimVec2 size{Vec2{900.0f, 540.0f}}; // the object frame; plugins draw inside +-size/2
    Vec2 defaultSize{900.0f, 540.0f};
    std::vector<PluginParam> params;
    bool hasRows = false;
    std::vector<DataRow> rows;
    bool hasTable = false;
    DataTable table;

    PluginParam* find(const std::string& id);
};

// A background: always fills the whole composition frame (no position, size or camera),
// with a color, a gradient or a picture / video.
enum class BackgroundFill { Solid, Linear, Radial, Image, Count };
enum class BackgroundFit { Cover, Contain, Stretch, Count };
const char* backgroundFillId(BackgroundFill f); // "solid", "linear", "radial", "image"
const char* backgroundFillLabel(BackgroundFill f);
const char* backgroundFitId(BackgroundFit f);    // "cover", "contain", "stretch"
const char* backgroundFitLabel(BackgroundFit f);

struct BackgroundProps {
    BackgroundFill fill = BackgroundFill::Linear;
    AnimColor color{Color::fromHex(0x262B3D)};  // solid color, gradient start, behind a picture
    AnimColor color2{Color::fromHex(0x0C0D12)}; // gradient end
    AnimFloat angle{90.0f};                     // linear gradient direction: 0 left to right, 90 top to bottom
    uint64_t mediaId = 0;                       // Image: a picture or video of the Media panel
    BackgroundFit fit = BackgroundFit::Cover;
    AnimFloat zoom{100.0f};                     // Image: percent of the fitted size
    AnimVec2 offset{Vec2{0.0f, 0.0f}};          // Image: pan, composition pixels
    AnimFloat darken{0.0f};                     // percent of black over everything (text stays readable)
};

// A layer showing / playing a media item (video, image or audio).
struct MediaProps {
    uint64_t mediaId = 0;
    Vec2 size{0.0f, 0.0f};    // source pixels (0 for audio only)
    double start = 0.0;       // composition time of the source's first frame (moves with the layer;
                              // trimming the in point reveals later source frames, like After Effects)
    AnimFloat volume{100.0f}; // percent, layers with audio
    // Crop: how much of each side of the picture is cut away, percent of the source.
    AnimFloat cropLeft{0.0f}, cropTop{0.0f}, cropRight{0.0f}, cropBottom{0.0f};
    bool hasVideo = false;
    bool hasAudio = false;
    // Speed: 1 plays the source as it is, 2 twice as fast. Reversed, it plays backwards and
    // `start` is where the source's first frame would come, after the clip.
    double speed = 1.0;
    bool reversed = false;
    // Time Remap (After Effects): the source second shown at each moment, keyframed like any
    // property (a single value: a frozen frame). It replaces speed and reverse while on.
    bool timeRemap = false;
    AnimFloat remap{0.0f};

    // The source second shown at composition time `t`.
    double sourceTime(double t) const {
        if (timeRemap) return remap.valueAt(t);
        return (reversed ? start - t : t - start) * speed;
    }
    // The part of the picture shown, in layer space (the source is centered on 0, 0); at least
    // 1% of it on each axis.
    void cropRect(double t, Vec2& mn, Vec2& mx) const;
    bool cropped(double t) const;
};

// Timeline-facing record of a transition. Its animation remains compatible
// with existing projects, while this metadata lets the clip show and accept it.
struct ClipTransition {
    std::string id;
    float duration = 0.5f;
    // Half of a transition on the cut between this clip and its neighbor on the track (this
    // clip's out and the next one's in): they change together.
    bool joined = false;
    bool active() const { return !id.empty(); }
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
    bool muted = false; // its sound is not heard
    bool solo = false;  // while any layer beside it is solo, only those are seen and heard
    int label = 0;
    BlendMode blend = BlendMode::Normal;
    // Track matte: `matteLayer` (a layer beside it, in the same compound or none) decides where
    // this layer shows; the matte layer itself is then not drawn.
    MatteMode matte = MatteMode::None;
    uint64_t matteLayer = 0;
    std::vector<Mask> masks; // applied from the first down, before the effects
    // A 3D layer has a Z position and the camera sees it in perspective (nearer is
    // bigger and moves faster). Only top-level layers count; never cameras or backgrounds.
    bool threeD = false;

    double inPoint = 0.0; // seconds, layer is active in [inPoint, outPoint)
    double outPoint = 10.0;

    TransformProps transform;
    ShapeProps shape;
    TextProps text;
    CameraProps camera;
    PluginProps plugin;
    MediaProps media;
    BackgroundProps background;
    std::vector<Effect> effects; // applied in order
    std::vector<AudioEffect> audioEffects; // mixed in order for audio media
    ClipTransition transitionIn, transitionOut;

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
Layer makeCounterLayer(Vec2 position, double duration);
// A counter's number: "1,234.5" (separator ","), "1.234,5" ("."), "1234.5" ("").
std::string formatCounter(double value, int decimals, const std::string& separator);
inline bool isCounterSeparator(const std::string& s) { return s.empty() || s == "," || s == "." || s == " "; }
// Subtitles: captions at the bottom of a `compSize` frame, white on a dark box.
Layer makeSubtitleLayer(Vec2 compSize, double duration);
Layer makeBackgroundLayer(double duration); // fills the frame, from 0 for `duration`
Layer makeCameraLayer(Vec2 compSize, double duration);
Layer makeCompoundLayer(Vec2 position, double duration);
// A layer for `item` starting at `start` (videos and audio last as long as the source).
// Footage lasts as long as its source; a still image `stillLength` seconds.
Layer makeMediaLayer(const MediaItem& item, Vec2 position, double start, double stillLength);
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
