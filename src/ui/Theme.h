#pragma once

#include <imgui.h>

namespace bemotion::theme {

// Palette. Dark neutrals with a single golden accent.
inline constexpr ImU32 kBgDeep = IM_COL32(14, 14, 16, 255);     // app background, viewport
inline constexpr ImU32 kBgPanel = IM_COL32(22, 22, 25, 255);    // panels
inline constexpr ImU32 kBgRaised = IM_COL32(30, 30, 34, 255);   // inputs, cards
inline constexpr ImU32 kBgHover = IM_COL32(40, 40, 46, 255);
inline constexpr ImU32 kBorder = IM_COL32(44, 44, 50, 255);
inline constexpr ImU32 kText = IM_COL32(232, 232, 236, 255);
inline constexpr ImU32 kTextDim = IM_COL32(150, 150, 160, 255);
inline constexpr ImU32 kTextFaint = IM_COL32(95, 95, 105, 255);
inline constexpr ImU32 kAccent = IM_COL32(232, 185, 49, 255);   // golden yellow
inline constexpr ImU32 kAccentHover = IM_COL32(244, 203, 84, 255);
inline constexpr ImU32 kAccentActive = IM_COL32(208, 160, 30, 255);
inline constexpr ImU32 kAccentSoft = IM_COL32(232, 185, 49, 40);
inline constexpr ImU32 kOnAccent = IM_COL32(20, 18, 12, 255);   // text on gold
inline constexpr ImU32 kDanger = IM_COL32(229, 96, 77, 255);

inline ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

void apply();

} // namespace bemotion::theme
