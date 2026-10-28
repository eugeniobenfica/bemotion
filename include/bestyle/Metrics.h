#pragma once

// The sizes bestyle's layouts use, so every panel spaces the same way: a 4 px step, gutters,
// control heights. Rounding, border width and dialog control height come from the theme
// (theme::current()), since users change them.

namespace bestyle::metrics {

constexpr int kStep = 4;            // the base unit: every gap is a multiple
constexpr int kGap = kStep;         // between rows of a form, controls in a row
constexpr int kGroupGap = kStep * 2; // between groups (sections, cards)
constexpr int kPanelMargin = 8;     // a panel's content from its edges
constexpr int kRowHeight = 24;      // compact controls in panels (number fields, switches' row)
constexpr int kIconButton = 26;     // square icon buttons
constexpr int kIconSize = 16;       // icons in buttons and menus
constexpr int kHeaderHeight = 30;   // category and section headings
constexpr int kLabelMin = 60;       // labels shorten (…) below this width
constexpr int kTileRadius = 8;      // tiles and cards

} // namespace bestyle::metrics
