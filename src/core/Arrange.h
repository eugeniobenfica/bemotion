#pragma once

// Arranging several layers at once (Layer > Arrange, and the arrange.*
// operations): even spacing on screen and staggered start times.

#include "core/Project.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

// Spaces the layers evenly along x (or y) at time `t`, keeping the other coordinate:
// three or more keep the outermost two where they are; two spread across the composition.
// Returns false (and explains) when there is nothing to arrange.
bool distributeLayers(Composition& comp, const std::vector<uint64_t>& ids, bool vertical, double t, std::string* error = nullptr);

// Starts each layer `step` seconds after the previous one, in stacking order (top first),
// from the earliest start among them. Keyframes move with their layer.
bool staggerLayers(Composition& comp, const std::vector<uint64_t>& ids, double step, std::string* error = nullptr);

} // namespace bemotion
