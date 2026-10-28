#pragma once

// Auto-track (the Track Point tool): a point of a video clip followed frame by frame from the
// playhead (core/Tracking), forward or back (Settings::tools), with progress and Stop. The result
// is Position keyframes on every frame: on the layer picked in Tool Settings
// (EditorContext::trackTarget, which keeps its place and moves as the point does), or on a new
// small marker layer.

#include "core/Math.h"

#include <cstdint>

class QWidget;

namespace bemotion {
class EditorContext;
namespace ui {

// `point`: the detail's center on the clip, in its own space; `radius`: half its box. Runs
// modally (a progress window).
void trackPoint(EditorContext& ctx, uint64_t layerId, Vec2 point, float radius, QWidget* parent);

} // namespace ui
} // namespace bemotion
