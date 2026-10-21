#pragma once

#include "core/Math.h"

#include <cstdint>

namespace mirage {

class EditorContext;

// Center panel: renders the composition and provides direct manipulation
// (select, move, scale, rotate) plus zoom and pan.
class ViewportPanel {
public:
    static constexpr const char* kWindowName = "Viewport";

    void draw(EditorContext& ctx);
    // Keeps the composition fitted to the panel until the user zooms or pans.
    void requestFit() { autoFit_ = true; }

private:
    enum class Drag { None, Move, Scale, Rotate, Pan };

    void handleInput(EditorContext& ctx, Vec2 canvasMin, Vec2 canvasMax);
    void drawSelection(EditorContext& ctx);

    float zoom_ = 0.5f;
    Vec2 pan_{0.0f, 0.0f};    // offset of the composition center from the canvas center
    Vec2 origin_{0.0f, 0.0f}; // screen position of comp (0,0), updated each frame
    bool autoFit_ = true;

    Drag drag_ = Drag::None;
    uint64_t dragLayer_ = 0;
    Vec2 dragStartMouse_;      // composition space
    Vec2 dragStartPosition_;
    Vec2 dragStartScale_;
    float dragStartRotation_ = 0.0f;
    float dragStartAngle_ = 0.0f;
    Vec2 dragCorner_;          // local space corner being dragged
};

} // namespace mirage
