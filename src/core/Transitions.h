#pragma once

// Transitions: how a clip comes in at its start and goes away at its end
// (Transitions panel, and the transition.add operation). Each one is keyframes
// placed at the clip's edges, so it stays editable like any animation.

#include "core/Presets.h"
#include "core/Project.h"

#include <string>
#include <vector>

namespace bemotion {

enum class TransitionEdge { In, Out, Both };

// Whether a layer can take transitions: not cameras, nor sound without a picture.
bool canTakeTransition(const Layer& layer);
// ... and this one (3D transitions need a top-level layer that can be 3D).
bool canTakeTransition(const Layer& layer, const std::string& id);

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

// Changes the duration while replacing the keyframes made at the old duration.
bool resizeTransition(Layer& layer, TransitionEdge edge, double duration);

// Moves a clip's transition to its other end (In <-> Out), same kind and length; one there is
// replaced. False for a joined one (a cut's) or none.
bool moveTransition(Layer& layer, TransitionEdge from);

// A transition's way in (or out) as a Quick Animation of `duration` seconds, to play anywhere in a
// clip ("Slide Left In", category "In" / "Out"); a preset with no tracks for an unknown id.
AnimationPreset transitionPreset(const std::string& id, TransitionEdge edge, double duration);

// Transitions and Quick Animations are placed alike (Animate panel, dropped on the timeline): on
// a clip's start or end, across the cut after it, or anywhere along it. A transition along the
// clip plays its way in (first half) or out as a Quick Animation; a Quick Animation on a cut plays
// on the clip that comes in (an exit on the one that goes). `preset` null: `transition` is meant.
enum class MotionSpot { In, Out, Cut, At };
// Where the playhead says (`time`): an end when it is within `length` of it, else there.
MotionSpot motionSpotAt(const Layer& layer, double time, double length);
// Returns the layer it went on (null: nothing placed).
Layer* placeMotion(Composition& comp, Layer& layer, const std::string& transition, const AnimationPreset* preset, MotionSpot spot,
                   double time, double length);

// Removes the transition metadata and the keyframes generated for its current duration.
bool removeTransition(Layer& layer, TransitionEdge edge);

// --- on a cut ---------------------------------------------------------------------
// The clip that starts where `layer` ends on its track (the other side of its cut), if any.
Layer* nextOnTrack(Composition& comp, const Layer& layer);
// The other half of a joined transition (see ClipTransition::joined).
Layer* transitionPartner(Composition& comp, const Layer& layer, TransitionEdge edge);
// A transition across the cut after `first`: `first` goes out during the first half of
// `duration`, the next clip on its track comes in during the second half. False without a
// next clip, or when either cannot take it.
bool applyCutTransition(Composition& comp, Layer& first, const std::string& id, double duration);
// Like resizeTransition / removeTransition, the joined partner along (each half keeps half).
bool resizeTransition(Composition& comp, Layer& layer, TransitionEdge edge, double duration);
bool removeTransition(Composition& comp, Layer& layer, TransitionEdge edge);

} // namespace bemotion
