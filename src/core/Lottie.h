#pragma once

// Lottie (Bodymovin JSON): the composition as a vector animation for the web and apps
// (lottie-web, lottie-android, lottie-ios...). Shapes (rectangles, ellipses, polygons, stars,
// lines, paths) with their fill, gradient, stroke, dashes, trim and repeater; plain text;
// pictures (embedded); solid and gradient backgrounds; masks, blending modes and compounds (as
// parents). Keyframes keep their easing as Bézier curves (the motion path too); expressions,
// Elastic and Bounce are baked frame by frame. Video, sound, effects, cameras, 3D, track mattes
// and plugin objects have no place in Lottie: they are left out, and said so.

#include "core/Project.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bemotion {

struct LottieOptions {
    // The bytes of a media item's file (pictures are embedded); empty: left out.
    std::function<std::string(const MediaItem&)> readFile;
};

// The JSON text of the animation. `leftOut`: what could not be exported (one line each).
std::string toLottie(const Composition& comp, const LottieOptions& options, std::vector<std::string>* leftOut = nullptr);
// ... written to `file`. False (and why) when it cannot be written.
bool exportLottie(const Composition& comp, const std::filesystem::path& file, std::vector<std::string>* leftOut, std::string* error);

} // namespace bemotion
