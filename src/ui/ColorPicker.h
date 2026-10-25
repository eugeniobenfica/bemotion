#pragma once

// bemotion's color picker: a popup under the control it edits, drawn with the theme (no system
// dialog). A saturation / brightness square, hue and opacity bars, an eyedropper that picks
// from anywhere on the screen, hex and RGB fields, the color before (click it to go back) and
// the last colors used. Changes apply live; a click outside or Enter keeps them, Esc undoes them.

#include "core/Math.h"

#include <functional>

class QWidget;

namespace bemotion::ui {

// Opens the picker under `anchor` and returns when it closes: true when the color was kept
// (`color` is then the new one). `changed` gets every color on the way.
bool pickColor(QWidget* anchor, Color& color, bool alpha, const std::function<void(Color)>& changed = {});

} // namespace bemotion::ui
