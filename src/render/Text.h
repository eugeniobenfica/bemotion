#pragma once

// Text layout for text layers and plugin labels: Skia's shaper (HarfBuzz +
// ICU) does kerning, ligatures, right-to-left scripts and font fallback; the
// layout on top adds the animatable parts (size, tracking, line height,
// alignment, the typewriter reveal, letters animated one by one and auto fit).
//
// Shaping happens once per text / font (cached) at a reference size; every
// frame only scales and offsets the glyph positions.

#include "core/Layer.h"
#include "core/Math.h"

class SkCanvas;

namespace bemotion::render {

// The clip a text is on: counters count and letters animate from its start (and leave before its end).
struct TextClip {
    double in = 0.0, out = 0.0;
};

// Local bounds of a text layer (the box its lines occupy, centered on the origin vertically).
void textBounds(const TextProps& text, double t, Vec2& outMin, Vec2& outMax, TextClip clip = {});
void drawText(SkCanvas* canvas, const TextProps& text, double t, float opacity, TextClip clip = {});

// One line in Inter (plugin labels). `at.y` is the vertical center of the line.
enum class TextAnchor { Left, Center, Right };
float measurePlainText(const std::string& s, float size, bool bold);
void drawPlainText(SkCanvas* canvas, const std::string& s, Vec2 at, float size, Color color, TextAnchor anchor, bool bold);

} // namespace bemotion::render
