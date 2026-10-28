#pragma once

// The drawing vocabulary (points, packed colors, anti-aliased primitives) is bestyle's
// (libs/bestyle): brought into ui:: for the custom-drawn parts of the interface.

#include "bestyle/Draw.h"

namespace bemotion::ui {

using bestyle::alpha;
using bestyle::blue;
using bestyle::Draw;
using bestyle::fromQColor;
using bestyle::green;
using bestyle::Pt;
using bestyle::qcolor;
using bestyle::rectOf;
using bestyle::red;
using bestyle::Rgba;
using bestyle::rgba;

} // namespace bemotion::ui
