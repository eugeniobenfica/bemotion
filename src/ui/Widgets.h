#pragma once

// Reusable UI building blocks: vector icons and custom buttons.
// Icons are drawn with ImDrawList primitives, so there is no icon font to ship.

#include "core/Layer.h"

#include <imgui.h>

#include <string>

namespace mirage::ui {

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
    Diamond,
    Trash,
    Logo,
};

Icon iconForLayer(const Layer& layer);
Icon iconForShape(ShapeKind kind);

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

// "HH:MM:SS:FF" timecode.
std::string formatTimecode(double seconds, double fps);

} // namespace mirage::ui
