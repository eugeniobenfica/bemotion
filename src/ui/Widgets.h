#pragma once

// Reusable UI building blocks: vector icons and custom buttons.
// Icons are drawn with ImDrawList primitives, so there is no icon font to ship.

#include "core/Layer.h"

#include <imgui.h>

#include <string>

namespace bemotion::ui {

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
};

Icon iconForLayer(const Layer& layer);
Icon iconForShape(ShapeKind kind);
// Icon named by a plugin object type ("bars", "line_chart", "pie", a shape name, ...).
Icon iconForPlugin(const std::string& name);

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// Square, frameless icon button. `active` tints it with the accent color.
bool iconButton(const char* id, Icon icon, float size, const char* tooltip = nullptr, bool active = false);

// Primary call-to-action: gold background, dark label.
bool accentButton(const char* label, ImVec2 size = ImVec2(0, 0));

// Objects panel tile: big icon with a caption.
bool objectTile(const char* id, Icon icon, const char* label, ImVec2 size);

// Small uppercase caption used to separate sections.
void sectionLabel(const char* text);

// Keyframe toggle shown next to animatable properties.
//   hasKeyHere: a key exists at the current time (filled diamond)
//   animated:   the property has keys somewhere (outlined gold diamond)
bool keyframeButton(const char* id, bool hasKeyHere, bool animated);

void drawDiamond(ImDrawList* dl, ImVec2 center, float radius, ImU32 fill, ImU32 outline);

void tooltip(const char* text);

// Call right before Begin() of a dockable panel: one close button (on its tab), no extra one on the dock node.
void nextPanelWindowClass();

// Minimizing docked panels: a panel calls panelIsMinimized() right after Begin
// (and skips its content when true) and panelMinimizeButton() just before End.
// Minimizing shrinks the panel's dock node to its tab; restoring brings back its size.
bool panelIsMinimized();
void panelMinimizeButton();

// "HH:MM:SS:FF" timecode.
std::string formatTimecode(double seconds, double fps);

} // namespace bemotion::ui
