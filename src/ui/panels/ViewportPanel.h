#pragma once

#include "core/Math.h"
#include "render/SceneRenderer.h"

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

#include <QImage>
#include <QWidget>

#include <map>

#include <cstdint>
#include <utility>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QMimeData;
class QPainter;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace bemotion {

class EditorContext;
namespace ui {
class IconButton;
}

// Center panel: renders the composition (Skia) and provides direct manipulation.
//
// Mouse:
//   wheel                         zoom around the cursor
//   middle / right drag           pan (also Space+drag or Alt+drag)
//   click                         select (Shift/Ctrl: add or remove)
//   drag on empty space           box selection
//   drag a layer                  move every selected layer
//   corner / top handle           scale (Shift: uniform) / rotate (Shift: 15° steps)
//   double-click a text           edit it in place; a compound: enter it
//   red / green / blue arrows      move a 3D layer (or, in the 3D view, the camera) along X / Y / Z
// Crop tool (Shift+C): a selected video or image shows its whole picture, and its edges, corners
//   or inside (the shown part moves) drag the crop (Shift: whole percents).
// 3D view (the cube button): the layers and cameras in space, seen from an eye of its own:
//   drag empty space               turn around      right / middle drag  pan      wheel  closer / farther
// Camera tools (a camera selected, looking through it): Orbit, Pan, Dolly move the camera by dragging.
class ViewportPanel : public QWidget {
    Q_OBJECT
signals:
    void viewportFullscreenRequested();
    void snapshotRequested(); // its snapshot button: the frame as a picture
    void previewQualityChanged(int divisor);
    void layerMenuRequested(QPoint global); // right click on a layer (now selected)

public:
    explicit ViewportPanel(EditorContext& ctx, QWidget* parent = nullptr);

    void sync(); // redraws when what it shows changed
    void setPreviewQuality(int divisor); // 1 full, 2 half, 4 quarter; exports are unaffected
    int previewQuality() const { return previewDivisor_; }
    void setViewportFullscreen(bool fullscreen);

    // Keeps the composition fitted to the panel until the user zooms or pans.
    void requestFit() {
        autoFit_ = true;
        update();
    }
    void zoomBy(float factor); // around the canvas center
    void setZoom(float zoom);

    // Space held (the hand tool). Returns, when released, whether it was used to pan
    // (then it is not a play / pause tap).
    bool setSpaceHeld(bool held);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Drag { None, Move, Scale, Rotate, Pan, Marquee, PathKey, PathHandle, Guide, Axis, Orbit, OrbitPan, CameraTool, Crop,
                      PathPoint, PenHandle };

    void layout(); // zoom, origin and camera for the current size and time
    void press(QMouseEvent* e, bool doubleClick);
    void dragTo(Vec2 mouse, Qt::KeyboardModifiers mods);
    void updateCursor(Vec2 mouse, Qt::KeyboardModifiers mods);
    bool updateDrop(const QMimeData* mime, Vec2 mouse, Qt::KeyboardModifiers mods);
    const struct PluginObjectType* droppedPluginType() const;
    void drawOverlay(QPainter& p);
    void drawGuides(QPainter& p);
    void drawRulers(QPainter& p);
    void drawHudBackground(QPainter& p);
    void positionHud();
    void beginTextEdit(uint64_t layerId);
    void placeTextEditor();
    void endTextEdit(bool keep);
    void editGuide(int index, QPoint at);

    // --- motion path ---------------------------------------------------------------
    // The single selected layer's position keyframes, drawn as an editable curve.
    Layer* pathLayer() const;
    // Point under the screen position: key index and part (0 = key, 1 = in handle, 2 = out handle).
    bool pathHit(Vec2 screen, int& key, int& part) const;
    void drawMotionPath(QPainter& p);
    int pathKey_ = -1; // key being dragged
    int pathPart_ = 0;

    // --- Bézier paths: the pen (Tool::Pen) and the points of the selected path shape / masks ---
    struct PathRef {
        uint64_t layer = 0;
        int mask = -1; // -1: the layer's own path (a path shape)
        bool operator==(const PathRef&) const = default;
    };
    struct PathPoint {
        PathRef ref;
        int vertex = -1;
        int part = 0; // 0 the point, 1 its in handle, 2 its out handle
    };
    BezierPath* pathOf(const PathRef& r) const;
    bool pathMatrix(uint64_t layerId, Projective& m) const; // layer space -> screen
    Vec2 pathLocal(uint64_t layerId, Vec2 screen) const;
    std::vector<PathRef> editablePaths() const;            // the single selected layer's
    bool pathPointAt(Vec2 screen, PathPoint& out) const;
    bool pathPress(QMouseEvent* e, Vec2 mouse, bool doubleClick); // true: the press was for a path
    void dragPath(Vec2 mouse, Qt::KeyboardModifiers mods);
    void startPen(Vec2 mouse); // a new path shape, or a mask on the selected layer
    void addPenPoint(Vec2 mouse);
    void finishPen();
    void drawPaths(QPainter& p);
    bool penDrawing_ = false;
    PathRef penTarget_;
    std::optional<BezierPath> penRestore_; // a picked mask being drawn again: its shape before
    PathPoint pathDrag_;
    Vec2 pathGrab_; // where a dragged point was held (layer space, from the point)
    void zoomAround(Vec2 screenPoint, float newZoom);

    EditorContext& ctx_;
    float zoom_ = 0.5f;
    Vec2 pan_{0.0f, 0.0f};    // offset of the composition center from the canvas center
    Vec2 origin_{0.0f, 0.0f}; // widget position of comp (0,0)
    Vec2 compSize_;
    // Camera: through it (the default) or a free view showing camera frames.
    bool throughCamera_ = true;
    Affine world_; // scene -> composition for this frame

    // --- 3D -----------------------------------------------------------------------
    // The 3D view: every layer in space and the cameras with what they see, from an eye that
    // turns around a point (yaw / pitch in degrees, distance 0 = the default).
    bool orbit_ = false;
    struct Orbit {
        float yaw = 32.0f, pitch = 16.0f, distance = 0.0f;
        float tx = 960.0f, ty = 540.0f, tz = 0.0f; // the point it turns around
        bool operator==(const Orbit&) const = default;
    } orbitView_;
    Lens orbitLens_; // for this frame (layout)
    Lens orbitLens() const;
    void resetOrbit();
    void draw3D(QPainter& p); // the Z 0 frame and the cameras, in the 3D view
    uint64_t cameraAt(Vec2 screen) const; // a camera's body under the mouse (3D view), or 0

    // The arrows that move the single selected 3D layer (or a camera, in the 3D view) along
    // the world's X, Y and Z.
    struct Gizmo {
        bool valid = false;
        uint64_t layer = 0;
        Vec2 base;          // screen
        Vec2 tip[3];        // screen
        Vec2 dir[3];        // screen, length 1
        float perPx[3] = {}; // world units per screen pixel along the arrow
    };
    Gizmo gizmo() const;
    int gizmoAxis(Vec2 screen) const; // the arrow under the mouse, or -1
    void drawGizmo(QPainter& p);
    int axis_ = -1;           // arrow being dragged
    Gizmo axisStart_;         // the gizmo when the drag started
    float axisStartValue_ = 0.0f;
    int hoverAxis_ = -1;

    // Camera tools: with a camera selected, looking through it, dragging moves the camera.
    enum class CameraTool { None, Orbit, Pan, Dolly };
    CameraTool cameraTool_ = CameraTool::None;
    QPushButton* cameraTools_[3] = {};
    Lens toolStartLens_;       // the camera's point of view when the drag started
    float toolStartRx_ = 0.0f, toolStartRy_ = 0.0f;
    void moveCamera(Vec2 mouse);
    render::View sceneView() const { return {origin_, zoom_, world_}; }

    // --- crop (the Crop tool) ----------------------------------------------------------
    Layer* cropLayer() const; // the single selected video / image, with the Crop tool on
    // Layer -> screen, and the whole picture's and the shown part's corners on screen (TL, TR, BR, BL).
    bool cropFrame(const Layer& layer, Projective& m, Vec2 full[4], Vec2 shown[4]) const;
    int cropHit(Vec2 screen) const; // the edges under the mouse: 1 left, 2 top, 4 right, 8 bottom; 15 inside
    void drawCrop(QPainter& p);
    int cropEdges_ = 0;
    float cropStart_[4] = {}; // left, top, right, bottom when the drag started
    Vec2 cropStartLocal_;
    bool autoFit_ = true;
    bool spaceHeld_ = false, spaceUsed_ = false;
    Vec2 mouse_{-1e6f, -1e6f};
    bool hovered_ = false;

    Drag drag_ = Drag::None;
    bool dragMoved_ = false;
    uint64_t dragLayer_ = 0; // layer under the cursor when the drag started
    Vec2 dragStartMouse_;    // composition space
    Vec2 dragStartScreen_;
    Vec2 lastMouse_;
    std::vector<std::pair<uint64_t, Vec2>> dragStartPositions_; // every moved layer
    Vec2 dragStartPosition_;
    Vec2 dragStartScale_;
    float dragStartRotation_ = 0.0f;
    float dragStartAngle_ = 0.0f;
    Vec2 dragCorner_; // local space corner being dragged
    std::vector<uint64_t> marqueeBase_; // selection kept when Shift-box-selecting
    bool additive_ = false;

    // Object being dragged in from the Objects / Media panels (ghost preview).
    bool dropHover_ = false;
    int dropKind_ = 0;
    uint64_t dropMedia_ = 0; // a Media panel item being dropped (instead of dropKind_)
    Vec2 dropPos_;

    // --- snapping & guides ---------------------------------------------------------
    // Targets in composition pixels: canvas center/edges, guides, other layers' edges/centers.
    void collectSnapTargets(const std::vector<uint64_t>& exclude);
    // Snaps a box moving by `delta` (composition pixels); records the lines to draw.
    Vec2 snapMove(Vec2 boxMin, Vec2 boxMax, Vec2 delta);
    int guideAt(Vec2 screen) const; // -1 if none
    std::vector<double> snapTargetsX_, snapTargetsY_;
    std::vector<float> snapLinesX_, snapLinesY_; // snapped by the current drag (composition pixels)
    Vec2 dragBoxMin_, dragBoxMax_;                // moving layers' bounds at drag start
    int guideDrag_ = -1;                          // index into Composition::guides

    // In-place text editing.
    uint64_t textEdit_ = 0;
    std::string textBefore_;
    QPlainTextEdit* textEditor_ = nullptr;
    QLabel* textHint_ = nullptr;

    // Heads-up controls (bottom of the canvas).
    ui::IconButton* cameraButton_ = nullptr;
    ui::IconButton* orbitButton_ = nullptr; // the 3D view
    ui::IconButton* snapButton_ = nullptr;
    ui::IconButton* zoomOutButton_ = nullptr;
    ui::IconButton* zoomInButton_ = nullptr;
    QToolButton* zoomLevel_ = nullptr;
    QPushButton* fitButton_ = nullptr;
    QToolButton* qualityButton_ = nullptr;
    ui::IconButton* fullscreenButton_ = nullptr;
    ui::IconButton* snapshotButton_ = nullptr;
    int previewDivisor_ = 1;
    // Playing: lowered further when a frame takes longer than the composition's frame (Settings'
    // adaptivePlayback), raised again when there is room; paused, previewDivisor_ alone.
    int playbackDivisor_ = 1;
    double renderMs_ = 0.0; // how long the last frames took to render (smoothed), at playbackDivisor_
    int shownDivisor() const;

    // What was last drawn (to redraw only on change).
    struct Signature {
        uint64_t revision = 0;
        double time = -1;
        std::vector<uint64_t> selection;
        uint64_t scope = 0;
        bool snapping = false, guides = false, rulers = false, playing = false;
        int tool = 0;
        uint64_t arrivals = 0; // video frames decoded in the background (scrubbing)
        bool operator==(const Signature&) const = default;
    } drawn_;
    struct FrameKey {
        uint64_t revision = 0;
        double time = -1.0;
        QSize pixels;
        Vec2 origin;
        float zoom = 0.0f;
        qreal scale = 0.0;
        bool throughCamera = true;
        bool orbit = false;
        Orbit orbitView;
        uint64_t uncropped = 0; // the layer being cropped shows its whole picture
        bool operator==(const FrameKey&) const = default;
    } frameKey_;
    bool frameValid_ = false;
    bool frameStale_ = false;     // the frame shown used a video frame still on its way
    uint64_t frameArrivals_ = 0;  // MediaManager::arrivals() when it was rendered
    QImage frame_; // the rendered composition
    sk_sp<SkSurface> gpuSurface_; // where it is drawn on the graphics card (render/Gpu.h), when there is one
    // Rendered frames by frame number and resolution divisor, while the view and the document
    // stay the same.
    std::map<std::pair<int, int>, QImage> cache_;
    FrameKey cacheLook_;             // frameKey_ without the time and the resolution
    uint64_t cacheGeneration_ = 0;   // MediaManager::generation()
    size_t cacheBytes_ = 0;
    void clearCache();
};

} // namespace bemotion
