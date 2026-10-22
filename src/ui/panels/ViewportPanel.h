#pragma once

#include "core/Math.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace bemotion {

class EditorContext;

// Payload id for dragging objects from the Objects panel onto the canvas.
// The payload is an int: -1 for text, otherwise a ShapeKind.
inline constexpr const char* kObjectPayload = "BEMOTION_OBJECT";

// Center panel: renders the composition and provides direct manipulation.
//
// Mouse:
//   wheel                         zoom around the cursor
//   middle / right drag           pan (also Space+drag or Alt+drag)
//   click                         select (Shift/Ctrl: add or remove)
//   drag on empty space           box selection
//   drag a layer                  move every selected layer
//   corner / top handle           scale (Shift: uniform) / rotate (Shift: 15° steps)
class ViewportPanel {
public:
    static constexpr const char* kWindowName = "Viewport";

    void draw(EditorContext& ctx);

    // Keeps the composition fitted to the panel until the user zooms or pans.
    void requestFit() { autoFit_ = true; }
    void zoomBy(float factor); // around the canvas center
    void setZoom(float zoom);

    // True when Space was used as the hand tool since the last call, so the
    // editor should not treat that Space press as play/pause.
    bool takeSpaceUsed() {
        const bool used = spaceUsed_;
        spaceUsed_ = false;
        return used;
    }

private:
    enum class Drag { None, Move, Scale, Rotate, Pan, Marquee };

    void handleNavigation(bool hovered);
    void handleSelectionInput(EditorContext& ctx, bool hovered);
    void handleDrop(EditorContext& ctx);
    void drawOverlay(EditorContext& ctx, bool hovered);
    void drawHud(EditorContext& ctx);
    void zoomAround(Vec2 screenPoint, float newZoom);

    float zoom_ = 0.5f;
    Vec2 pan_{0.0f, 0.0f};    // offset of the composition center from the canvas center
    Vec2 origin_{0.0f, 0.0f}; // screen position of comp (0,0), updated each frame
    Vec2 canvasMin_, canvasMax_;
    Vec2 compSize_;
    bool autoFit_ = true;
    bool spaceUsed_ = false;

    Drag drag_ = Drag::None;
    bool dragMoved_ = false;
    uint64_t dragLayer_ = 0; // layer under the cursor when the drag started
    Vec2 dragStartMouse_;    // composition space
    Vec2 dragStartScreen_;
    std::vector<std::pair<uint64_t, Vec2>> dragStartPositions_; // every moved layer
    Vec2 dragStartPosition_;
    Vec2 dragStartScale_;
    float dragStartRotation_ = 0.0f;
    float dragStartAngle_ = 0.0f;
    Vec2 dragCorner_; // local space corner being dragged
    std::vector<uint64_t> marqueeBase_; // selection kept when Shift-box-selecting

    // Object being dragged in from the Objects panel (ghost preview).
    bool dropHover_ = false;
    int dropKind_ = 0;
    Vec2 dropPos_;
};

} // namespace bemotion
