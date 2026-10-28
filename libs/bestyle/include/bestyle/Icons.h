#pragma once

// The vector icon set, drawn with bestyle::Draw (no icon font or pictures to ship): any size,
// any color, sharp on every screen.

#include "bestyle/Draw.h"

#include <QIcon>

#include <functional>

namespace bestyle {

enum class Icon {
    Text,
    Rectangle,
    Ellipse,
    Polygon,
    Star,
    Line,
    Eye,
    EyeOff,
    Lock,
    Unlock,
    Play,
    Pause,
    StepBack,
    StepForward,
    ToStart,
    ToEnd,
    Loop,
    Plus,
    Folder,
    ChevronRight,
    ChevronDown,
    ChevronUp,
    Diamond,
    Trash,
    Logo,
    Pointer,       // selection tool
    Razor,         // cut tool
    SplitPlayhead, // split at playhead
    Export,
    ZoomIn,
    ZoomOut,
    Reset,  // circular arrow
    Magnet, // snapping
    Gear,   // settings
    Camera,
    Plugin,
    BarChart,
    LineChart,
    PieChart,
    // Effect categories
    FxBlur,
    FxColor,
    FxChannel,
    FxDistort,
    FxGenerate,
    FxPerspective,
    FxStylize,
    FxTime,
    // Settings pages
    Palette,
    Sliders,
    Graph,    // timeline graph editor
    Compound, // layers grouped together
    Back,     // "go back" arrow
    // Media
    Film,
    Image,
    Audio,
    Import,
    // Tools
    Duplicate,
    Ungroup,
    Ease,
    LayerUp,
    LayerDown,
    ChevronLeft,
    Minimize, // a panel's minimize button
    Fullscreen,
    Copy,
    Paste,
    Transition,
    Table,
    Cube,       // 3D models
    Background, // the background object
    Coffee,     // support (Buy Me a Coffee)
    Microphone, // recording
    Subtitles,  // the subtitle object
    Map,        // map charts
    Counter,    // the counter object: a number sign
    // Motion (Quick Animations, transitions)
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Burst,     // pop: a dot with rays
    Ripple,    // pulse: rings
    Heartbeat, // a heart monitor line
    Zigzag,    // shake
    Wave,      // float
    Bounce,    // a ball over the floor
    Fade,      // squares fading out
    Expression, // an italic f: a property's expression
    Crop,       // two crossing corners
    Keyboard,   // Settings › Shortcuts
    Edit,       // a pencil: Settings › Editor
    Help,       // a question mark in a circle
    ExitFullscreen, // corners pointing in
    Close,          // a cross
    Maximize,       // a panel's maximize button: a square
    Restore,        // a maximized panel's button: two squares
    Eyedropper,     // picks a color from the screen
    Meter,          // audio meters: two level bars
    Snapshot,       // a photo camera: save the frame as a picture
    Pen,            // the pen tool: a nib
    PathShape,      // a path shape: a curve between two points
    Mask,           // a mask: a dashed outline on a square
    RippleEdit,     // the ripple edit tool: an edge with arrows pushing the next clip
    RollEdit,       // the rolling edit tool: a cut between two clips, arrows both ways
    SlipEdit,       // the slip tool: a clip with arrows inside it
    Marker,         // a marker: a flag on the ruler
    Speaker,        // sound on
    SpeakerMuted,   // sound off
    Solo,           // solo: headphones
    Speed,          // a clip's speed: a gauge
    FxKeying,       // keying effects: a square with a hole
    Scopes,         // the Scopes panel: a waveform trace on a grid
    Proxy,          // proxies: a small film frame
    AlignLeft,      // text alignment: lines of text against the left edge
    AlignCenter,    // ... centered
    AlignRight,     // ... against the right edge
    FitView,        // fit the composition in the view: a frame inside corner marks
    KeyPrev,        // go to the previous keyframe: a diamond with an arrow to the left
    KeyNext,        // ... to the next one
    Brush,          // the brush tool: a round brush
    Eraser,         // the eraser tool: a tilted block
    Puppet,         // the puppet tool: pins joined by bones
    Warp,           // the warp tool: a bent grid
    Track,          // motion tracking: a target with a path
    Gradient,       // a gradient fill: a square fading across
    Levels,         // automatic audio levels: bars brought to one height
};

void drawIcon(Draw& d, Icon icon, Pt center, float size, Rgba color);
void drawDiamond(Draw& d, Pt center, float radius, Rgba fill, Rgba outline);
QIcon makeIcon(Icon icon, Rgba color, int size = 32); // for Qt buttons
QIcon menuIcon(Icon icon, Rgba color);                 // fills a menu's icon column
// Icon::Logo is the app's own mark: how to paint it (a disc of `radius` around `center`).
void setLogoPainter(std::function<void(QPainter& p, Pt center, float radius, Rgba color)> paint);

} // namespace bestyle
