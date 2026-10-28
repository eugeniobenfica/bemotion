#pragma once

// The interface's colors (palettes, the tokens kText, kAccent... read at paint time, apply()) are
// bestyle's theme (libs/bestyle): theme:: is that namespace.

#include "bestyle/Theme.h"
#include "ui/Draw.h"

namespace bemotion {
namespace theme = ::bestyle::theme;
} // namespace bemotion
