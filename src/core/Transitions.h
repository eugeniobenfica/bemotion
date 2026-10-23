#pragma once

// Transitions: how a clip comes in at its start and goes away at its end
// (Transitions panel, and the transition.add operation). Each one is keyframes
// placed at the clip's edges, so it stays editable like any animation.

#include "core/Layer.h"

#include <string>
#include <vector>

namespace bemotion {

enum class TransitionEdge { In, Out, Both };

struct TransitionInfo {
    const char* id;       // "fade", "slide_left", ...
    const char* name;     // "Fade"
    const char* about;    // one line for tooltips
    const char* category; // "Fade", "Slide", "Scale", "Rotate"
};

// The categories, in display order.
const std::vector<const char*>& transitionCategories();

const std::vector<TransitionInfo>& transitions();
const TransitionInfo* findTransition(const std::string& id);
const char* transitionEdgeId(TransitionEdge edge); // "in", "out", "both"

// Adds the transition at the clip's start and/or end, lasting `duration` seconds (at most
// half the clip when both edges get one). Keys already inside that time are replaced.
// False when the layer cannot take it (cameras).
bool applyTransition(Layer& layer, const std::string& id, TransitionEdge edge, double duration);

} // namespace bemotion
