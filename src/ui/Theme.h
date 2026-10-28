#pragma once

// The interface's colors (palettes, the tokens kText, kAccent... read at paint time, apply()) are
// bmstyle's theme (libs/bmstyle): theme:: is that namespace.

#include "bmstyle/Theme.h"
#include "ui/Draw.h"

namespace bemotion {
namespace theme = ::bmstyle::theme;
} // namespace bemotion
