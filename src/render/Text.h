#pragma once

// Text layout for text layers and plugin labels: Skia's shaper (HarfBuzz +
// ICU) does kerning, ligatures, right-to-left scripts and font fallback; the
// layout on top adds the animatable parts (size, tracking, line height,
// alignment and the typewriter reveal).
//
// Shaping happens once per text / font (cached) at a reference size; every
// frame only scales and offsets the glyph positions.

#include "core/Layer.h"
#include "core/Math.h"

class SkCanvas;

namespace bemotion::render {

// Local bounds of a text layer (the box its lines occupy, centered on the origin vertically).
void textBounds(const TextProps& text, double t, Vec2& outMin, Vec2& outMax);
void drawText(SkCanvas* canvas, const TextProps& text, double t, float opacity);

// One line in Inter (plugin labels). `at.y` is the vertical center of the line.
enum class TextAnchor { Left, Center, Right };
float measurePlainText(const std::string& s, float size, bool bold);
void drawPlainText(SkCanvas* canvas, const std::string& s, Vec2 at, float size, Color color, TextAnchor anchor, bool bold);

} // namespace bemotion::render
