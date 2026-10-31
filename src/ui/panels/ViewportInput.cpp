#include "ui/panels/ViewportPanel.h"

// The viewport: mouse and keys, drops from the Objects / Media panels, snapping and guides, text
// edited in place (the rest: ViewportPanel.cpp, ViewportPaths.cpp).

#include "app/PluginHost.h"
#include "app/Settings.h"
#include "core/Geometry.h"
#include "core/Snapping.h"
#include "media/MediaManager.h"
#include "render/Gpu.h"
#include "render/SceneRenderer.h"
#include "ui/AppSignals.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/SkiaQt.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "ui/panels/ViewportInternal.h"

namespace bemotion {

using ui::Draw;
using ui::Pt;
using ui::Rgba;
using namespace viewport_internal;

// --- input ------------------------------------------------------------------------------------

void ViewportPanel::updateCursor(Vec2 mouse, Qt::KeyboardModifiers mods) {
    Qt::CursorShape shape = Qt::ArrowCursor;
    const bool overRulerTop = ctx_.showRulers && mouse.y < kRuler && mouse.x > kRuler;
    const bool overRulerLeft = ctx_.showRulers && mouse.x < kRuler && mouse.y > kRuler;
    if (drag_ == Drag::Pan || drag_ == Drag::OrbitPan || drag_ == Drag::Orbit || drag_ == Drag::CameraTool) shape = Qt::ClosedHandCursor;
    else if (drag_ == Drag::Guide && guideDrag_ >= 0 && guideDrag_ < static_cast<int>(ctx_.comp().guides.size()))
        shape = ctx_.comp().guides[guideDrag_].vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    else if (drag_ == Drag::None) {
        if (spaceHeld_ || (mods & Qt::AltModifier)) shape = Qt::OpenHandCursor;
        else if (overRulerTop && ctx_.showGuides) shape = Qt::SizeVerCursor;
        else if (overRulerLeft && ctx_.showGuides) shape = Qt::SizeHorCursor;
        else if (const int g = guideAt(mouse); g >= 0 && !ctx_.lockGuides)
            shape = ctx_.comp().guides[g].vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor;
        else if (PathPoint hit; pathPointAt(mouse, hit))
            shape = Qt::PointingHandCursor;
        else if ((ctx_.tool == Tool::Pen || ctx_.tool == Tool::Brush || ctx_.tool == Tool::Eraser || ctx_.tool == Tool::Puppet ||
                  ctx_.tool == Tool::Warp || ctx_.tool == Tool::Track) && !orbit_)
            shape = Qt::BitmapCursor; // the tool's own (below)
        else {
            const double t = ctx_.time;
            Layer* single = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
            if (single && (single->locked || !single->activeAt(t) || (single->type == LayerType::Camera && throughCamera_))) single = nullptr;
            Handles h{};
            if (single && cropLayer()) {
                const int edges = cropHit(mouse);
                if (edges == 15) shape = Qt::SizeAllCursor;
                else if (edges == 1 || edges == 4) shape = Qt::SizeHorCursor;
                else if (edges == 2 || edges == 8) shape = Qt::SizeVerCursor;
                else if (edges == 3 || edges == 12) shape = Qt::SizeFDiagCursor;
                else if (edges) shape = Qt::SizeBDiagCursor;
            } else if (single && computeHandles(*single, t, sceneView(), h)) {
                for (int i = 0; i < 4; ++i)
                    if (single->type != LayerType::Camera && near(mouse, h.screen[i], kHandleRadius + 3))
                        shape = i % 2 == 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
                if (near(mouse, h.rotate, kHandleRadius + 4)) shape = Qt::PointingHandCursor;
            }
            int key = -1, part = 0;
            if (pathHit(mouse, key, part)) shape = Qt::PointingHandCursor;
            hoverAxis_ = gizmoAxis(mouse);
            if (hoverAxis_ >= 0) shape = Qt::SizeAllCursor;
            if (cameraTool_ != CameraTool::None) shape = Qt::OpenHandCursor;
            if (orbit_ && cameraAt(mouse)) shape = Qt::PointingHandCursor;
        }
    }
    // A tool on (the pen, crop): its icon, unless something under the mouse has its own pointer.
    if (shape == Qt::BitmapCursor || (shape == Qt::ArrowCursor && drag_ == Drag::None && ctx_.tool == Tool::Crop && !overRulerTop && !overRulerLeft))
        return setCursor(ui::toolCursor(ctx_.tool));
    setCursor(shape);
}

void ViewportPanel::mousePressEvent(QMouseEvent* e) { press(e, false); }
void ViewportPanel::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        requestFit();
        return;
    }
    press(e, true);
}

void ViewportPanel::press(QMouseEvent* e, bool doubleClick) {
    layout();
    const Vec2 mouse = vec(e->position());
    mouse_ = lastMouse_ = mouse;
    const Composition& comp = ctx_.comp();
    const render::View view = sceneView();
    const double t = ctx_.time;
    const bool additive = (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0;
    additive_ = additive;
    drag_ = Drag::None;
    dragMoved_ = false;
    dragStartScreen_ = mouse;
    dragStartMouse_ = view.toComp(mouse);
    snapLinesX_.clear();
    snapLinesY_.clear();
    if (textEdit_) endTextEdit(true);

    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) { // pan (the 3D view: its eye)
        drag_ = orbit_ ? Drag::OrbitPan : Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    // The 3D arrows win over what is underneath them.
    if (const int axis = additive ? -1 : gizmoAxis(mouse); axis >= 0) {
        const Gizmo g = gizmo();
        if (Layer* l = ctx_.comp().findLayer(g.layer)) {
            drag_ = Drag::Axis;
            axis_ = axis;
            axisStart_ = g;
            dragLayer_ = l->id;
            axisStartValue_ = axis == 2 ? l->transform.depth.valueAt(t) : axis == 0 ? l->transform.position.valueAt(t).x : l->transform.position.valueAt(t).y;
            return;
        }
    }
    // Camera tools move the selected camera.
    if (Layer* cam = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
        cameraTool_ != CameraTool::None && cam && cam->type == LayerType::Camera && !cam->locked && throughCamera_ && !orbit_) {
        drag_ = Drag::CameraTool;
        dragLayer_ = cam->id;
        toolStartLens_ = comp.lensOf(*cam, t);
        toolStartRx_ = cam->transform.rotationX.valueAt(t);
        toolStartRy_ = cam->transform.rotationY.valueAt(t);
        return;
    }
    // The 3D view: a click on a camera picks it.
    if (orbit_ && !additive)
        if (const uint64_t cam = cameraAt(mouse)) {
            ctx_.select(cam);
            update();
            return;
        }

    // Drag a guide out of a ruler.
    auto& guides = ctx_.comp().guides;
    if (ctx_.showRulers && ctx_.showGuides && !ctx_.lockGuides && (mouse.y < kRuler || mouse.x < kRuler)) {
        if (mouse.y < kRuler && mouse.x > kRuler) guides.push_back({false, std::round(dragStartMouse_.y)});
        else if (mouse.x < kRuler && mouse.y > kRuler) guides.push_back({true, std::round(dragStartMouse_.x)});
        else return;
        guideDrag_ = static_cast<int>(guides.size()) - 1;
        drag_ = Drag::Guide;
        ctx_.touch();
        return;
    }

    // Brush, Eraser, Puppet, Warp, Track (ViewportTools.cpp).
    if (!spaceHeld_ && !additive && toolPress(mouse, e->modifiers())) {
        update();
        return;
    }
    // Paths: the pen draws; the points of a path shape or of masks drag.
    if (!spaceHeld_ && !additive && pathPress(e, mouse, doubleClick)) {
        update();
        return;
    }

    // The Crop tool: the selected picture's edges.
    if (!additive && !spaceHeld_ && !(e->modifiers() & Qt::AltModifier))
        if (const int edges = cropHit(mouse)) {
            Layer* l = cropLayer();
            Projective m;
            Vec2 full[4], shown[4];
            cropFrame(*l, m, full, shown);
            drag_ = Drag::Crop;
            cropEdges_ = edges;
            dragLayer_ = l->id;
            const MediaProps& md = l->media;
            cropStart_[0] = md.cropLeft.valueAt(t), cropStart_[1] = md.cropTop.valueAt(t);
            cropStart_[2] = md.cropRight.valueAt(t), cropStart_[3] = md.cropBottom.valueAt(t);
            cropStartLocal_ = m.inverse().apply(mouse);
            update();
            return;
        }

    // Scale/rotate handles exist only for a single editable layer.
    Layer* single = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
    if (single && single == cropLayer()) single = nullptr; // cropping, not scaling
    if (single && (single->locked || !single->activeAt(t))) single = nullptr;
    // Looking through a camera, the camera itself is not grabbed (it would drag the view along).
    if (single && single->type == LayerType::Camera && throughCamera_) single = nullptr;
    const bool canScale = single && single->type != LayerType::Camera; // a camera zooms instead
    Handles h{};
    const bool haveHandles = single && computeHandles(*single, t, view, h);
    const int guide = guideAt(mouse);

    if (spaceHeld_ || (e->modifiers() & Qt::AltModifier)) {
        drag_ = Drag::Pan;
        if (spaceHeld_) spaceUsed_ = true;
    } else if (guide >= 0 && !ctx_.lockGuides) {
        if (doubleClick) {
            editGuide(guide, e->globalPosition().toPoint());
            return;
        }
        guideDrag_ = guide; // guides win over the layers underneath
        drag_ = Drag::Guide;
    } else if (int key = -1, part = 0; !additive && pathHit(mouse, key, part)) {
        drag_ = part == 0 ? Drag::PathKey : Drag::PathHandle;
        pathKey_ = key;
        pathPart_ = part;
        dragLayer_ = ctx_.selected()->id;
    } else {
        if (haveHandles && !additive) {
            for (int i = 0; i < 4; ++i)
                if (canScale && near(mouse, h.screen[i], kHandleRadius + 3)) {
                    drag_ = Drag::Scale;
                    dragCorner_ = h.local[i];
                }
            if (near(mouse, h.rotate, kHandleRadius + 4)) drag_ = Drag::Rotate;
            if (drag_ != Drag::None) {
                dragLayer_ = single->id;
                dragStartPosition_ = single->transform.position.valueAt(t);
                dragStartScale_ = single->transform.scale.valueAt(t);
                dragStartRotation_ = single->transform.rotation.valueAt(t);
                const Vec2 dd = mouse - parentView(view, *single, t).sceneToScreen(dragStartPosition_);
                dragStartAngle_ = radToDeg(std::atan2(dd.y, dd.x));
            }
        }
        if (drag_ == Drag::None) {
            const Vec2 compPoint = view.toComp(mouse);
            // With a compound open (timeline), clicks pick among its contents.
            if (ctx_.scope && !comp.findLayer(ctx_.scope)) ctx_.scope = 0;
            uint64_t hit = ctx_.scope && !orbit_ ? render::hitTestInside(comp, t, compPoint, ctx_.scope, throughCamera_)
                                                 : render::hitTest(comp, t, compPoint, throughCamera_, !throughCamera_ && !orbit_, g_lens);
            // Inside a compound, clicks pick among its layers; a double-click on a
            // compound goes one level in (like After Effects / Figma groups).
            if (const Layer* cur = ctx_.selected(); cur && !additive) {
                uint64_t inner = 0;
                if (cur->parent) inner = render::hitTestInside(comp, t, compPoint, cur->parent, throughCamera_);
                if (!inner && cur->type == LayerType::Compound && doubleClick) {
                    inner = render::hitTestInside(comp, t, compPoint, cur->id, throughCamera_);
                    if (inner) ctx_.enterScope(cur->id); // its timeline opens too
                }
                if (inner) hit = inner;
            }
            // A double-click on a text edits it right there.
            const Layer* hitLayer = hit ? comp.findLayer(hit) : nullptr;
            if (hitLayer && !additive && hitLayer->type == LayerType::Text && !hitLayer->locked && doubleClick) {
                ctx_.select(hit);
                beginTextEdit(hit);
            } else if (hit) {
                if (additive) ctx_.toggleSelected(hit);
                else if (!ctx_.isSelected(hit)) ctx_.select(hit);
                else { // keep the group, make the clicked layer primary
                    std::erase(ctx_.selection, hit);
                    ctx_.selection.push_back(hit);
                }
                if (ctx_.isSelected(hit)) {
                    drag_ = Drag::Move;
                    dragLayer_ = hit;
                    dragStartPositions_.clear();
                    std::vector<uint64_t> moving;
                    dragBoxMin_ = {1e9f, 1e9f};
                    dragBoxMax_ = {-1e9f, -1e9f};
                    for (Layer* l : ctx_.selectedLayers()) {
                        if (l->locked || (l->type == LayerType::Camera && throughCamera_) || l->type == LayerType::Background) continue;
                        dragStartPositions_.emplace_back(l->id, l->transform.position.valueAt(t));
                        moving.push_back(l->id);
                        Vec2 mn, mx;
                        if (l->activeAt(t) && compBox(*l, t, mn, mx)) {
                            dragBoxMin_ = {std::min(dragBoxMin_.x, mn.x), std::min(dragBoxMin_.y, mn.y)};
                            dragBoxMax_ = {std::max(dragBoxMax_.x, mx.x), std::max(dragBoxMax_.y, mx.y)};
                        }
                    }
                    collectSnapTargets(moving);
                }
            } else if (orbit_ && !additive) { // empty space in the 3D view: turn around
                drag_ = Drag::Orbit;
            } else {
                drag_ = Drag::Marquee;
                marqueeBase_ = additive ? ctx_.selection : std::vector<uint64_t>{};
                if (!additive) ctx_.select(0);
            }
        }
    }
    update();
}

void ViewportPanel::mouseMoveEvent(QMouseEvent* e) {
    const Vec2 mouse = vec(e->position());
    mouse_ = mouse;
    hovered_ = true;
    if (drag_ != Drag::None) {
        layout();
        dragTo(mouse, e->modifiers());
    } else {
        updateCursor(mouse, e->modifiers());
    }
    lastMouse_ = mouse;
    update();
}

void ViewportPanel::dragTo(Vec2 mouse, Qt::KeyboardModifiers mods) {
    const Composition& comp = ctx_.comp();
    const render::View view = sceneView();
    const Vec2 mouseComp = view.toComp(mouse);
    const double t = ctx_.time;
    if ((mouse - dragStartScreen_).length() > 2.0f) dragMoved_ = true;
    snapLinesX_.clear();
    snapLinesY_.clear();

    switch (drag_) {
    case Drag::Paint:
    case Drag::Pin:
    case Drag::WarpPoint:
    case Drag::TrackBox: toolDrag(mouse); break;
    case Drag::Pan:
        pan_ += mouse - lastMouse_;
        autoFit_ = false;
        updateCursor(mouse, mods);
        return;
    case Drag::Orbit: { // turn around the point the 3D view looks at
        const Vec2 d = mouse - lastMouse_;
        orbitView_.yaw += d.x * 0.3f;
        orbitView_.pitch = std::clamp(orbitView_.pitch - d.y * 0.3f, -89.0f, 89.0f);
        return;
    }
    case Drag::OrbitPan: { // move that point with the mouse
        const Vec2 d = (mouse - lastMouse_) / zoom_;
        V3 right, down, forward;
        axesOf(orbitLens_, right, down, forward);
        const float dist = orbitView_.distance > 0.0f ? orbitView_.distance : ctx_.comp().focalLength() * 3.2f;
        const float perPx = dist / orbitLens_.focal;
        const V3 target = V3{orbitView_.tx, orbitView_.ty, orbitView_.tz} - right * (d.x * perPx) - down * (d.y * perPx);
        orbitView_.tx = target.x, orbitView_.ty = target.y, orbitView_.tz = target.z;
        return;
    }
    case Drag::Guide: {
        auto& guides = ctx_.comp().guides;
        if (guideDrag_ < 0 || guideDrag_ >= static_cast<int>(guides.size())) return;
        Guide& g = guides[guideDrag_];
        double pos = std::round(g.vertical ? mouseComp.x : mouseComp.y);
        const double size = g.vertical ? compSize_.x : compSize_.y;
        if (ctx_.snapping && !(mods & Qt::ControlModifier))
            if (const SnapResult s = snap1D({pos}, {0.0, size * 0.5, size}, 8.0 / zoom_); s.snapped) pos = s.target;
        g.position = static_cast<float>(pos);
        updateCursor(mouse, mods);
        ctx_.touch();
        return;
    }
    case Drag::Marquee: {
        const Vec2 a{std::min(dragStartScreen_.x, mouse.x), std::min(dragStartScreen_.y, mouse.y)};
        const Vec2 b{std::max(dragStartScreen_.x, mouse.x), std::max(dragStartScreen_.y, mouse.y)};
        ctx_.selection = marqueeBase_;
        for (const Layer& l : comp.layers)
            if (l.activeAt(t) && !l.locked && !ctx_.isSelected(l.id) && !(l.parent && comp.findLayer(l.parent)) &&
                l.type != LayerType::Background &&
                layerOverlapsRect(l, t, view, a, b))
                ctx_.selection.push_back(l.id);
        ctx_.clearKeySelection();
        return;
    }
    default: break;
    }

    if (drag_ == Drag::PenHandle || drag_ == Drag::PathPoint) {
        dragPath(mouse, mods);
        return;
    }
    if (!dragMoved_) return;
    switch (drag_) {
    case Drag::Axis: { // along one world axis, as far as the mouse went along its arrow
        Layer* l = ctx_.comp().findLayer(dragLayer_);
        if (!l || axis_ < 0) break;
        const Vec2 moved = mouse - dragStartScreen_;
        const float along = moved.x * axisStart_.dir[axis_].x + moved.y * axisStart_.dir[axis_].y;
        float v = axisStartValue_ + along * axisStart_.perPx[axis_];
        if (mods & Qt::ShiftModifier) v = std::round(v / 10.0f) * 10.0f; // steps of 10 px
        if (axis_ == 2) {
            l->transform.depth.set(t, v);
        } else {
            Vec2 p = l->transform.position.valueAt(t);
            (axis_ == 0 ? p.x : p.y) = v;
            l->transform.position.set(t, p);
        }
        break;
    }
    case Drag::CameraTool: moveCamera(mouse); break;
    case Drag::Crop: {
        Layer* l = ctx_.comp().findLayer(dragLayer_);
        Projective m;
        Vec2 full[4], shown[4];
        if (!l || l->media.size.x <= 0 || l->media.size.y <= 0 || !cropFrame(*l, m, full, shown)) break;
        const Vec2 size = l->media.size;
        const Vec2 q = m.inverse().apply(mouse); // in the layer, the picture centered on 0, 0
        float v[4] = {cropStart_[0], cropStart_[1], cropStart_[2], cropStart_[3]};
        if (cropEdges_ == 15) { // the shown part moves, its size kept
            const float dx = std::clamp((q.x - cropStartLocal_.x) / size.x * 100.0f, -v[0], v[2]);
            const float dy = std::clamp((q.y - cropStartLocal_.y) / size.y * 100.0f, -v[1], v[3]);
            v[0] += dx, v[2] -= dx, v[1] += dy, v[3] -= dy;
        } else { // percents from the left / top of the picture
            const float x = (q.x / size.x + 0.5f) * 100.0f, y = (q.y / size.y + 0.5f) * 100.0f;
            if (cropEdges_ & 1) v[0] = std::clamp(x, 0.0f, 99.0f - v[2]);
            if (cropEdges_ & 4) v[2] = std::clamp(100.0f - x, 0.0f, 99.0f - v[0]);
            if (cropEdges_ & 2) v[1] = std::clamp(y, 0.0f, 99.0f - v[3]);
            if (cropEdges_ & 8) v[3] = std::clamp(100.0f - y, 0.0f, 99.0f - v[1]);
            if (mods & Qt::ShiftModifier)
                for (float& c : v) c = std::round(c);
        }
        AnimFloat* sides[4] = {&l->media.cropLeft, &l->media.cropTop, &l->media.cropRight, &l->media.cropBottom};
        for (int i = 0; i < 4; ++i)
            if (std::fabs(sides[i]->valueAt(t) - v[i]) > 1e-4f) sides[i]->set(t, v[i]); // a keyframe when animated
        break;
    }
    case Drag::Move: {
        Vec2 delta = mouseComp - dragStartMouse_;
        if (mods & Qt::ShiftModifier) (std::fabs(delta.x) > std::fabs(delta.y) ? delta.y : delta.x) = 0.0f;
        if (ctx_.snapping && !(mods & Qt::ControlModifier) && !orbit_) delta = delta + snapMove(dragBoxMin_, dragBoxMax_, delta);
        // The camera may zoom or rotate the view (3D layers: each at its depth); inside a
        // compound the position is in its (scaled, rotated) space.
        // Where the position was on screen, moved by the mouse, back in the plane it moves in.
        for (const auto& [id, start] : dragStartPositions_) {
            Layer* l = ctx_.comp().findLayer(id);
            Projective plane;
            if (!l || !cameraOf(*l, t, plane, false)) continue;
            plane = plane * parentOf(*l, t);
            l->transform.position.set(t, plane.inverse().apply(plane.apply(start) + delta));
        }
        break;
    }
    case Drag::Scale: {
        Layer* layer = ctx_.comp().findLayer(dragLayer_);
        if (!layer) break;
        auto& tr = layer->transform;
        const Affine pre = Affine::translate(dragStartPosition_) * Affine::rotateDeg(dragStartRotation_);
        const Vec2 q = pre.inverse().apply(parentView(view, *layer, t).screenToScene(mouse));
        const Vec2 v = dragCorner_ - tr.anchor.valueAt(t);
        Vec2 s = dragStartScale_;
        if (mods & Qt::ShiftModifier) {
            const Vec2 v0 = v * dragStartScale_ * 0.01f;
            const float dd = v0.x * v0.x + v0.y * v0.y;
            if (dd > 1e-6f) s = dragStartScale_ * ((q.x * v0.x + q.y * v0.y) / dd);
        } else {
            if (std::fabs(v.x) > 1e-3f) s.x = q.x / v.x * 100.0f;
            if (std::fabs(v.y) > 1e-3f) s.y = q.y / v.y * 100.0f;
        }
        tr.scale.set(t, s);
        break;
    }
    case Drag::Rotate: {
        Layer* layer = ctx_.comp().findLayer(dragLayer_);
        if (!layer) break;
        const Vec2 dd = mouse - parentView(view, *layer, t).sceneToScreen(dragStartPosition_);
        float r = dragStartRotation_ + radToDeg(std::atan2(dd.y, dd.x)) - dragStartAngle_;
        if (mods & Qt::ShiftModifier) r = std::round(r / 15.0f) * 15.0f;
        layer->transform.rotation.set(t, r);
        break;
    }
    case Drag::PathKey:
    case Drag::PathHandle: {
        Layer* layer = ctx_.comp().findLayer(dragLayer_);
        if (!layer || pathKey_ < 0 || pathKey_ >= layer->transform.position.keyCount()) break;
        auto& keys = layer->transform.position.keys;
        auto& k = keys[pathKey_];
        const Vec2 q = parentView(view, *layer, t, false).screenToScene(mouse);
        if (drag_ == Drag::PathKey) {
            k.value = q;
        } else {
            Vec2 in, out;
            pathTangents(keys, static_cast<size_t>(pathKey_), in, out);
            const Vec2 handle = q - k.value;
            // Handles stay mirrored (smooth) unless Alt breaks them into a corner.
            const auto mirror = [&](Vec2 other) {
                const float hl = handle.length(), ol = other.length();
                if ((mods & Qt::AltModifier) || hl < 1e-4f) return other;
                return handle * (-(ol > 1e-4f ? ol : hl) / hl); // opposite direction, own length
            };
            if (pathPart_ == 1) {
                k.inTangent = handle;
                k.outTangent = mirror(out);
            } else {
                k.outTangent = handle;
                k.inTangent = mirror(in);
            }
            k.path = PathMode::Custom;
        }
        break;
    }
    default: break;
    }
    ctx_.touch();
}

void ViewportPanel::mouseReleaseEvent(QMouseEvent* e) {
    const Vec2 mouse = vec(e->position());
    toolRelease();
    if (drag_ == Drag::Guide) {
        // Dropping a guide on a ruler (or outside) deletes it.
        auto& guides = ctx_.comp().guides;
        const bool overRuler = mouse.x < kRuler || mouse.y < kRuler;
        const bool inside = mouse.x >= 0 && mouse.y >= 0 && mouse.x < width() && mouse.y < height();
        if (guideDrag_ >= 0 && guideDrag_ < static_cast<int>(guides.size()) && (overRuler || !inside)) {
            guides.erase(guides.begin() + guideDrag_);
            ctx_.touch();
        }
        guideDrag_ = -1;
    }
    // A right click that did not pan: the layer under it, and its menu.
    if (e->button() == Qt::RightButton && (drag_ == Drag::Pan || drag_ == Drag::OrbitPan) && !dragMoved_) {
        layout();
        const Composition& comp = ctx_.comp();
        const Vec2 at = sceneView().toComp(mouse);
        const uint64_t hit = ctx_.scope && !orbit_ ? render::hitTestInside(comp, ctx_.time, at, ctx_.scope, throughCamera_)
                                                   : render::hitTest(comp, ctx_.time, at, throughCamera_, !throughCamera_ && !orbit_, g_lens);
        if (hit) {
            if (!ctx_.isSelected(hit)) ctx_.select(hit);
            drag_ = Drag::None;
            update();
            emit layerMenuRequested(e->globalPosition().toPoint());
            return;
        }
    }
    // A plain click inside a multi-selection narrows it to the clicked layer.
    if (drag_ == Drag::Move && !dragMoved_ && !additive_) ctx_.select(dragLayer_);
    drag_ = Drag::None;
    axis_ = -1;
    snapLinesX_.clear();
    snapLinesY_.clear();
    updateCursor(mouse, e->modifiers());
    update();
}

void ViewportPanel::wheelEvent(QWheelEvent* e) {
    const float steps = e->angleDelta().y() / 120.0f;
    if ((ctx_.tool == Tool::Brush || ctx_.tool == Tool::Eraser) && (e->modifiers() & Qt::ControlModifier) && steps != 0.0f) {
        float& size = ctx_.tool == Tool::Eraser ? settings().tools.eraserSize : settings().tools.brushSize; // the brush's size
        size = std::clamp(size * std::pow(1.15f, steps), 1.0f, 500.0f);
        positionHud();
        update();
        return;
    }
    if (orbit_ && steps != 0.0f) { // the 3D view: its eye goes closer / farther
        const float dist = orbitView_.distance > 0.0f ? orbitView_.distance : ctx_.comp().focalLength() * 3.2f;
        orbitView_.distance = std::clamp(dist * std::pow(1.15f, -steps), ctx_.comp().focalLength() * 0.05f, ctx_.comp().focalLength() * 40.0f);
        update();
    } else if (steps != 0.0f) {
        zoomAround(vec(e->position()), zoom_ * std::pow(1.15f, steps));
    }
    e->accept();
}

void ViewportPanel::leaveEvent(QEvent*) {
    hovered_ = false;
    update();
}

// --- drag and drop from the Objects / Media panels --------------------------------------------

const PluginObjectType* ViewportPanel::droppedPluginType() const {
    if (dropKind_ > ui::kDropPlugin) return nullptr;
    const auto types = PluginHost::get().objectTypes();
    const size_t i = static_cast<size_t>(ui::kDropPlugin - dropKind_);
    return i < types.size() ? types[i] : nullptr;
}

bool ViewportPanel::updateDrop(const QMimeData* mime, Vec2 mouse, Qt::KeyboardModifiers mods) {
    dropMedia_ = 0;
    int kind = 0;
    uint64_t media = 0;
    if (ui::readMime(mime, ui::kObjectMime, kind)) dropKind_ = kind;
    else if (ui::readMime(mime, ui::kMediaMime, media)) dropMedia_ = media;
    else return false;
    layout();
    dropPos_ = (mouse - origin_) / zoom_;
    dropPos_ = {std::round(dropPos_.x), std::round(dropPos_.y)};
    snapLinesX_.clear();
    snapLinesY_.clear();
    if (ctx_.snapping && !(mods & Qt::ControlModifier)) { // the new object's center snaps too
        collectSnapTargets({});
        dropPos_ = dropPos_ + snapMove(dropPos_, dropPos_, {0, 0});
    }
    dropHover_ = true;
    update();
    return true;
}

void ViewportPanel::dragEnterEvent(QDragEnterEvent* e) {
    if (updateDrop(e->mimeData(), vec(e->position()), e->modifiers())) e->acceptProposedAction();
    else e->ignore();
}

void ViewportPanel::dragMoveEvent(QDragMoveEvent* e) {
    if (updateDrop(e->mimeData(), vec(e->position()), e->modifiers())) e->acceptProposedAction();
    else e->ignore();
}

void ViewportPanel::dragLeaveEvent(QDragLeaveEvent*) {
    dropHover_ = false;
    snapLinesX_.clear();
    snapLinesY_.clear();
    update();
}

void ViewportPanel::dropEvent(QDropEvent* e) {
    if (!updateDrop(e->mimeData(), vec(e->position()), e->modifiers())) return;
    const Vec2 scenePos = world_.inverse().apply(dropPos_); // where the camera shows that point
    if (dropMedia_) ctx_.addMedia(dropMedia_, scenePos);
    else if (dropKind_ == ui::kDropCamera) ctx_.addCamera();
    else if (dropKind_ == ui::kDropBackground) ctx_.addBackground();
    else if (dropKind_ == ui::kDropSubtitles) ctx_.addSubtitles();
    else if (dropKind_ == ui::kDropCounter) ctx_.addCounter(scenePos);
    else if (const PluginObjectType* type = droppedPluginType()) ctx_.addPluginObject(*type, scenePos);
    else if (dropKind_ < 0) ctx_.addText(scenePos);
    else ctx_.addShape(static_cast<ShapeKind>(dropKind_), scenePos);
    dropHover_ = false;
    snapLinesX_.clear();
    snapLinesY_.clear();
    e->acceptProposedAction();
    update();
}

// --- snapping & guides ----------------------------------------------------------------------------

void ViewportPanel::collectSnapTargets(const std::vector<uint64_t>& exclude) {
    const Composition& comp = ctx_.comp();
    snapTargetsX_ = {0.0, comp.width * 0.5, static_cast<double>(comp.width)};
    snapTargetsY_ = {0.0, comp.height * 0.5, static_cast<double>(comp.height)};
    if (ctx_.showGuides)
        for (const Guide& g : comp.guides) (g.vertical ? snapTargetsX_ : snapTargetsY_).push_back(g.position);
    for (const Layer& l : comp.layers) {
        if (!l.activeAt(ctx_.time) || std::find(exclude.begin(), exclude.end(), l.id) != exclude.end()) continue;
        Vec2 mn, mx;
        if (!compBox(l, ctx_.time, mn, mx)) continue;
        snapTargetsX_.insert(snapTargetsX_.end(), {mn.x, (mn.x + mx.x) * 0.5, mx.x});
        snapTargetsY_.insert(snapTargetsY_.end(), {mn.y, (mn.y + mx.y) * 0.5, mx.y});
    }
}

Vec2 ViewportPanel::snapMove(Vec2 boxMin, Vec2 boxMax, Vec2 delta) {
    const double threshold = settings().snapDistance / zoom_; // screen pixels
    Vec2 correction{0, 0};
    const SnapResult sx = snap1D({boxMin.x + delta.x, (boxMin.x + boxMax.x) * 0.5 + delta.x, boxMax.x + delta.x}, snapTargetsX_, threshold);
    if (sx.snapped) {
        correction.x = static_cast<float>(sx.offset);
        snapLinesX_.push_back(static_cast<float>(sx.target));
    }
    const SnapResult sy = snap1D({boxMin.y + delta.y, (boxMin.y + boxMax.y) * 0.5 + delta.y, boxMax.y + delta.y}, snapTargetsY_, threshold);
    if (sy.snapped) {
        correction.y = static_cast<float>(sy.offset);
        snapLinesY_.push_back(static_cast<float>(sy.target));
    }
    return correction;
}

int ViewportPanel::guideAt(Vec2 screen) const {
    if (!ctx_.showGuides) return -1;
    const render::View view = sceneView();
    const auto& guides = ctx_.comp().guides;
    for (int i = static_cast<int>(guides.size()) - 1; i >= 0; --i) {
        const Vec2 s = view.toScreen({guides[i].position, guides[i].position});
        if (guides[i].vertical ? std::fabs(screen.x - s.x) <= 4.0f : std::fabs(screen.y - s.y) <= 4.0f) return i;
    }
    return -1;
}

void ViewportPanel::editGuide(int index, QPoint at) {
    // Double-click on a guide: its exact position, or delete it.
    auto& guides = ctx_.comp().guides;
    if (index < 0 || index >= static_cast<int>(guides.size())) return;
    QMenu menu(this);
    menu.addSection(guides[index].vertical ? tr("Vertical guide") : tr("Horizontal guide"));
    auto* value = new ui::DragValue;
    value->setSpeed(1.0f);
    value->setFormat("%.0f px");
    value->setValue(guides[index].position);
    value->setMinimumWidth(140);
    connect(value, &ui::DragValue::valueChanged, this, [this, index](float v) {
        auto& g = ctx_.comp().guides;
        if (index < static_cast<int>(g.size())) g[index].position = v;
        ctx_.touch();
        update();
    });
    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(value);
    menu.addAction(action);
    menu.addAction(tr("Delete Guide"), this, [this, index] {
        auto& g = ctx_.comp().guides;
        if (index < static_cast<int>(g.size())) g.erase(g.begin() + index);
        ctx_.touch();
    });
    menu.exec(at);
}

// --- in-place text editing ----------------------------------------------------------------------

void ViewportPanel::beginTextEdit(uint64_t layerId) {
    Layer* layer = ctx_.comp().findLayer(layerId);
    if (!layer || layer->type != LayerType::Text) return;
    if (!textEditor_) {
        textEditor_ = new QPlainTextEdit(this);
        textEditor_->setTabChangesFocus(true);
        textEditor_->installEventFilter(this);
        connect(textEditor_, &QPlainTextEdit::textChanged, this, [this] {
            if (Layer* l = ctx_.comp().findLayer(textEdit_)) {
                l->text.text = textEditor_->toPlainText().toStdString();
                ctx_.touch();
                update();
            }
        });
        textHint_ = new QLabel(tr("Enter to finish  ·  Ctrl+Enter for a new line  ·  Esc to cancel"), this);
        ui::setTone(textHint_, ui::Tone::TextFaint);
    }
    textEdit_ = layerId;
    textBefore_ = layer->text.text;
    {
        const QSignalBlocker block(textEditor_);
        textEditor_->setPlainText(QString::fromStdString(layer->text.text));
    }
    textEditor_->selectAll();
    placeTextEditor();
    textEditor_->show();
    textHint_->show();
    textEditor_->setFocus();
}

void ViewportPanel::placeTextEditor() {
    Layer* layer = ctx_.comp().findLayer(textEdit_);
    Vec2 mn, mx;
    if (!layer || !layer->activeAt(ctx_.time) || !compBox(*layer, ctx_.time, mn, mx)) {
        endTextEdit(true);
        return;
    }
    // The text updates live on the canvas; the box under it holds the caret.
    const render::View view = sceneView();
    const Vec2 a = view.toScreen(mn), b = view.toScreen(mx);
    const int lineH = textEditor_->fontMetrics().height();
    const int lines = std::min(6, 1 + static_cast<int>(std::count(layer->text.text.begin(), layer->text.text.end(), '\n')));
    const int w = std::clamp(static_cast<int>(b.x - a.x + 8), 220, std::max(220, width() - 16));
    const int h = lineH * lines + 14;
    const int x = std::clamp(static_cast<int>((a.x + b.x) * 0.5f - w * 0.5f), 8, std::max(8, width() - w - 8));
    int y = static_cast<int>(b.y) + 12;
    if (y + h + lineH * 2 > height()) y = std::max(8, static_cast<int>(a.y) - h - lineH * 2 - 12); // no room below
    textEditor_->setGeometry(x, y, w, h);
    textHint_->adjustSize();
    textHint_->move(x, y + h + 3);
}

void ViewportPanel::endTextEdit(bool keep) {
    if (!textEdit_) return;
    if (!keep)
        if (Layer* l = ctx_.comp().findLayer(textEdit_)) {
            l->text.text = textBefore_;
            ctx_.touch();
        }
    textEdit_ = 0;
    if (textEditor_) {
        textEditor_->hide();
        textHint_->hide();
    }
    setFocus();
    update();
}

bool ViewportPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == textEditor_) {
        // While typing, Esc / Enter belong to the box, not to the editor's shortcuts (Deselect, ...).
        if (event->type() == QEvent::ShortcutOverride) {
            const int k = static_cast<QKeyEvent*>(event)->key();
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                endTextEdit(false);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ControlModifier)) {
                endTextEdit(true);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && (key->modifiers() & Qt::ControlModifier)) {
                textEditor_->insertPlainText("\n");
                return true;
            }
        } else if (event->type() == QEvent::FocusOut && textEdit_) {
            endTextEdit(true);
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace bemotion
