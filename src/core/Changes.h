#pragma once

// What an edit changed, in time: which moments of the composition may look different after it.
// The viewport keeps the frames it rendered (the RAM preview); an edit to one clip then throws
// away only the frames where that clip shows, not all of them.

#include "core/Project.h"

#include <cstdint>
#include <vector>

namespace bemotion {

// A composition, summed up: a hash of what is not a layer (size, frame rate, background,
// media...) and, for each layer, a hash of everything about it and when it shows.
struct CompositionPrint {
    struct LayerPrint {
        uint64_t id = 0;
        uint64_t hash = 0;
        double from = 0.0, until = 0.0; // shownFrom, shownUntil
    };
    uint64_t settings = 0;
    bool expressions = false; // some property has one (it may read any other layer, at any time)
    std::vector<LayerPrint> layers;
};

CompositionPrint printOf(const Composition& comp);

struct TimeSpan {
    double from = 0.0, until = 0.0; // [from, until)
    bool contains(double t) const { return t >= from && t < until; }
};

// The moments whose picture may differ from `before` to `after`: none when nothing that is drawn
// changed, one span from 0 to infinity when it cannot tell (the composition's settings, the
// order of layers, expressions), else the times shown by every layer added, removed or changed
// (as it was and as it is).
std::vector<TimeSpan> changedSpans(const CompositionPrint& before, const CompositionPrint& after);

} // namespace bemotion
