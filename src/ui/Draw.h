#pragma once

// The drawing vocabulary (points, packed colors, anti-aliased primitives) is bmstyle's
// (libs/bmstyle): brought into ui:: for the custom-drawn parts of the interface.

#include "bmstyle/Draw.h"

namespace bemotion::ui {

using bmstyle::alpha;
using bmstyle::blue;
using bmstyle::Draw;
using bmstyle::fromQColor;
using bmstyle::green;
using bmstyle::Pt;
using bmstyle::qcolor;
using bmstyle::rectOf;
using bmstyle::red;
using bmstyle::Rgba;
using bmstyle::rgba;

} // namespace bemotion::ui
