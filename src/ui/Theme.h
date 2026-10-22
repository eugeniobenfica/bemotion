#pragma once

#include <imgui.h>

#include <string>

namespace bemotion::theme {

// A complete UI color scheme. Themes are plain JSON (see paletteToJson), so
// users can share and import them.
struct Palette {
    std::string name = "Dark";
    bool light = false;
    ImU32 bgDeep = IM_COL32(14, 14, 16, 255);   // app background, viewport
    ImU32 bgPanel = IM_COL32(22, 22, 25, 255);  // panels
    ImU32 bgRaised = IM_COL32(30, 30, 34, 255); // inputs, cards
    ImU32 bgHover = IM_COL32(40, 40, 46, 255);
    ImU32 border = IM_COL32(44, 44, 50, 255);
    ImU32 text = IM_COL32(232, 232, 236, 255);
    ImU32 textDim = IM_COL32(150, 150, 160, 255);
    ImU32 textFaint = IM_COL32(95, 95, 105, 255);
    ImU32 accent = IM_COL32(232, 185, 49, 255); // golden yellow
    ImU32 danger = IM_COL32(229, 96, 77, 255);
    ImU32 snap = IM_COL32(255, 79, 163, 255);   // smart-guide lines
    ImU32 guide = IM_COL32(64, 196, 255, 200);  // ruler guides
    ImU32 canvas = IM_COL32(30, 30, 34, 255);   // around the composition frame
    float rounding = 5.0f;                      // frames and buttons (others derive from it)
};

Palette darkPalette();
Palette lightPalette();
Palette midnightPalette();

bool paletteFromJson(const std::string& json, Palette& out, std::string* error = nullptr);
std::string paletteToJson(const Palette& palette);

// Active colors. Runtime values (set by apply), named like constants because
// drawing code reads them everywhere.
inline ImU32 kBgDeep, kBgPanel, kBgRaised, kBgHover, kBorder;
inline ImU32 kText, kTextDim, kTextFaint;
inline ImU32 kAccent, kAccentHover, kAccentActive, kAccentSoft, kOnAccent;
inline ImU32 kDanger, kSnap, kGuide, kCanvas;

inline ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 withAlpha(ImU32 c, int alpha);
ImU32 accent(int alpha); // the accent color at the given alpha
ImU32 ink(int alpha);    // overlay strokes: white on dark themes, black on light ones
ImU32 shade(int alpha);  // darkening overlays, softened on light themes

const Palette& current();

// Sizes and spacing; call once (before DPI scaling).
void applyStyle();
// Colors and rounding; can be called any time. `dpiScale` scales the rounding.
void apply(const Palette& palette, float dpiScale = 1.0f);

} // namespace bemotion::theme
