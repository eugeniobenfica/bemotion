#pragma once

// The interface's colors: palettes (Dark, Light, Midnight, or imported JSON)
// applied to Qt (palette + style sheet) and readable by custom-drawn widgets.

#include "ui/Draw.h"

#include <string>

namespace bemotion::theme {

using ui::Rgba;
using ui::rgba;

// A complete UI color scheme. Themes are plain JSON (see paletteToJson), so
// users can share and import them. The colors after `canvas` are optional: 0 means "worked
// out from the others" (older themes have none of them), so a theme sets only what it wants.
struct Palette {
    std::string name = "Dark";
    bool light = false;
    Rgba bgDeep = rgba(14, 14, 16);   // app background, viewport
    Rgba bgPanel = rgba(22, 22, 25);  // panels
    Rgba bgRaised = rgba(30, 30, 34); // inputs, cards
    Rgba bgHover = rgba(40, 40, 46);
    Rgba border = rgba(44, 44, 50);
    Rgba text = rgba(232, 232, 236);
    Rgba textDim = rgba(150, 150, 160);
    Rgba textFaint = rgba(95, 95, 105);
    Rgba accent = rgba(232, 185, 49); // golden yellow
    Rgba danger = rgba(229, 96, 77);
    Rgba snap = rgba(255, 79, 163);   // smart-guide lines
    Rgba guide = rgba(64, 196, 255, 200); // ruler guides
    Rgba canvas = rgba(30, 30, 34);   // around the composition frame
    // Optional roles (0: derived).
    Rgba header = 0;       // the menu bar and the panels' tab bars
    Rgba field = 0;        // text fields, number fields, drop-downs: where you type or pick
    Rgba fieldBorder = 0;
    Rgba button = 0;       // push buttons: what you press
    Rgba buttonBorder = 0;
    Rgba focus = 0;        // the outline of the focused field
    Rgba selection = 0;    // selected items and text
    Rgba menu = 0;         // menus, drop-down lists, tooltips
    Rgba onAccent = 0;     // text on accent-colored buttons
    Rgba success = 0;
    Rgba warning = 0;
    Rgba playhead = 0;
    Rgba keyframe = 0;
    float rounding = 5.0f;     // frames and buttons (others derive from it)
    float borderWidth = 1.0f;  // fields' and buttons' outlines (0 to 2 px)
    float controlHeight = 28.0f; // buttons, fields and drop-downs in dialogs and Settings
};

Palette darkPalette();
Palette lightPalette();
Palette midnightPalette();

bool paletteFromJson(const std::string& json, Palette& out, std::string* error = nullptr);
std::string paletteToJson(const Palette& palette);
std::string toHex(Rgba c);
bool fromHex(const std::string& s, Rgba& out);

// Active colors. Runtime values (set by apply), named like constants because
// drawing code reads them everywhere.
inline Rgba kBgDeep, kBgPanel, kBgRaised, kBgHover, kBorder;
inline Rgba kText, kTextDim, kTextFaint;
inline Rgba kAccent, kAccentHover, kAccentActive, kAccentSoft, kOnAccent;
inline Rgba kDanger, kSnap, kGuide, kCanvas;
inline Rgba kHeader, kField, kFieldBorder, kButton, kButtonBorder, kFocus, kSelection, kMenu;
inline Rgba kSuccess, kWarning, kPlayhead, kKeyframe;

inline QColor q(Rgba c) { return ui::qcolor(c); }
Rgba withAlpha(Rgba c, int alpha);
Rgba mix(Rgba a, Rgba b, float t);
Rgba accent(int alpha); // the accent color at the given alpha
Rgba ink(int alpha);    // overlay strokes: white on dark themes, black on light ones
Rgba shade(int alpha);  // darkening overlays, softened on light themes

const Palette& current();
// The palette with every optional role worked out (what is on screen).
Palette resolved(const Palette& palette);

// Colors and rounding for the whole application (Qt palette and style sheet).
void apply(const Palette& palette);
// Extra rules for the docking library's own style sheet (see EditorWindow).
QString dockStyleSheet();

} // namespace bemotion::theme
