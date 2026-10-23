#pragma once

// Copy and paste of layers, as text: the copied layers (compounds with their
// contents) and the media they use, in the project file format. Works across
// projects and survives in the system clipboard.

#include "core/Project.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

// The layers `ids` (plus everything inside copied compounds) of `comp`, "" if none exist.
std::string copyLayers(const Composition& comp, const std::vector<uint64_t>& ids);

// True for text made by copyLayers.
bool isCopiedLayers(const std::string& text);

// Adds copied layers to `comp`: the earliest starts at `time`, they go above stack index
// `index`, each top-level copy on a track of its own, and media items are imported (or
// reused when already there). Copied children whose compound was not copied go into `scope`
// (0: top level). Returns the new top-level layers' ids, top first; empty on bad text.
std::vector<uint64_t> pasteLayers(Composition& comp, const std::string& text, double time, int index, uint64_t scope = 0);

} // namespace bemotion
