#pragma once

// What the viewport's source files share (ViewportPanel.cpp: the view and its painting;
// ViewportPaths.cpp: motion paths, Pen paths and masks, crop; ViewportInput.cpp: mouse, keys,
// drops, snapping, text editing). Internal to them.

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

namespace bemotion::viewport_internal {

using ui::Draw;
using ui::Pt;
using ui::Rgba;

inline constexpr float kMinZoom = 0.02f;
inline constexpr float kMaxZoom = 32.0f;
inline constexpr float kHandleRadius = 5.0f;
inline constexpr float kRotateOffset = 26.0f;
inline constexpr float kRuler = 20.0f; // ruler band thickness

inline Pt pt(Vec2 v) { return {v.x, v.y}; }
inline Vec2 vec(const QPointF& p) { return {static_cast<float>(p.x()), static_cast<float>(p.y())}; }

struct Handles {
    Vec2 local[4];  // bounds corners in layer space (TL, TR, BR, BL)
    Vec2 screen[4]; // same corners on screen
    Vec2 rotate;    // rotation handle on screen
    Vec2 topMid;
    Vec2 pivot;     // layer position on screen
};

// The composition being edited, for the helpers below: layers inside compounds live in their
// compound's space.
inline const Composition* g_comp = nullptr;
inline bool g_throughCamera = true; // the view looks through the camera (else the free view)
inline const Lens* g_lens = nullptr; // the 3D view's eye (null: the camera or the free view)

// The camera the layer is seen through (scene -> composition): one per layer, since 3D layers
// are seen at their own depth. False when the camera does not see the layer (behind it).
// `tilted` = false: the plane a 3D layer's position moves in (unturned, at its Z).
inline bool cameraOf(const Layer& layer, double t, Projective& out, bool tilted = true) {
    out = {};
    if (!g_comp) return true;
    if (g_lens) return g_comp->viewThrough(*g_lens, layer, t, out, tilted, true);
    return g_comp->viewFor(layer, t, out, g_throughCamera, tilted);
}

// Small 3D helpers for the gizmo and the camera tools.
struct V3 {
    float x = 0, y = 0, z = 0;
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline void axesOf(const Lens& l, V3& right, V3& down, V3& forward) {
    float r[3], d[3], f[3];
    lensAxes(l, r, d, f);
    right = {r[0], r[1], r[2]}, down = {d[0], d[1], d[2]}, forward = {f[0], f[1], f[2]};
}

inline const Rgba kAxisColors[3] = {ui::rgba(235, 86, 86), ui::rgba(96, 200, 110), ui::rgba(92, 146, 245)}; // X, Y, Z

// `view` with the layer's camera and enclosing compounds: maps the layer's
// parent space (where its position lives) to the screen.
inline render::View parentView(const render::View& view, const Layer& layer, double t, bool tilted = true) {
    render::View v = view;
    Projective camera;
    if (g_comp && cameraOf(layer, t, camera, tilted)) v.camera = camera * g_comp->parentMatrix(layer, t);
    return v;
}

inline Affine parentOf(const Layer& layer, double t) { return g_comp ? g_comp->parentMatrix(layer, t) : Affine{}; }

inline bool computeHandles(const Layer& layer, double t, const render::View& sceneView, Handles& h) {
    if (layer.type == LayerType::Background) return false; // it is the frame: nothing to move or scale
    Projective camera;
    if (!cameraOf(layer, t, camera)) return false; // behind the camera: not seen
    const render::View view = parentView(sceneView, layer, t);
    Vec2 mn, mx;
    if (!render::localBounds(layer, t, mn, mx, g_comp)) return false;
    h.local[0] = {mn.x, mn.y};
    h.local[1] = {mx.x, mn.y};
    h.local[2] = {mx.x, mx.y};
    h.local[3] = {mn.x, mx.y};
    const Projective m = view.matrix() * layer.matrixAt(t);
    for (int i = 0; i < 4; ++i) {
        if (!m.inFront(h.local[i])) return false; // partly behind the camera: no box to show
        h.screen[i] = m.apply(h.local[i]);
    }
    h.topMid = (h.screen[0] + h.screen[1]) * 0.5f;
    const Vec2 center = (h.screen[0] + h.screen[2]) * 0.5f;
    Vec2 dir = h.topMid - center;
    const float len = dir.length();
    dir = len > 1e-3f ? dir / len : Vec2{0.0f, -1.0f};
    // A mirrored layer flips its local "up"; keep the handle outside the box.
    if (m.determinant() < 0.0f) dir = dir * -1.0f, h.topMid = (h.screen[2] + h.screen[3]) * 0.5f;
    h.rotate = h.topMid + dir * kRotateOffset;
    h.pivot = view.sceneToScreen(layer.transform.position.valueAt(t));
    return true;
}

// Exact overlap between a layer's (possibly rotated) box on screen and an
// axis-aligned rectangle, using the separating axis test on both shapes.
inline bool layerOverlapsRect(const Layer& layer, double t, const render::View& view, Vec2 rectMin, Vec2 rectMax) {
    Handles h{};
    if (!computeHandles(layer, t, view, h)) return false;
    const Vec2 rect[4] = {rectMin, {rectMax.x, rectMin.y}, rectMax, {rectMin.x, rectMax.y}};
    const Vec2 axes[4] = {{1, 0}, {0, 1}, h.screen[1] - h.screen[0], h.screen[3] - h.screen[0]};
    for (const Vec2& axis : axes) {
        float aMin = 1e30f, aMax = -1e30f, bMin = 1e30f, bMax = -1e30f;
        for (int i = 0; i < 4; ++i) {
            const float a = h.screen[i].x * axis.x + h.screen[i].y * axis.y;
            const float b = rect[i].x * axis.x + rect[i].y * axis.y;
            aMin = std::min(aMin, a), aMax = std::max(aMax, a);
            bMin = std::min(bMin, b), bMax = std::max(bMax, b);
        }
        if (aMax < bMin || bMax < aMin) return false;
    }
    return true;
}

inline void outline(Draw& d, const Handles& h, Rgba col, float thickness) {
    const Pt pts[4] = {pt(h.screen[0]), pt(h.screen[1]), pt(h.screen[2]), pt(h.screen[3])};
    d.polyline(pts, 4, col, true, thickness);
}

inline bool near(Vec2 a, Vec2 b, float r) { return (a - b).length() <= r; }

// Axis-aligned bounds of a layer in composition pixels (as the view shows it).
inline bool compBox(const Layer& layer, double t, Vec2& mn, Vec2& mx) {
    Vec2 lmn, lmx;
    Projective camera;
    if (!render::localBounds(layer, t, lmn, lmx, g_comp) || !cameraOf(layer, t, camera)) return false;
    const Projective m = camera * parentOf(layer, t) * layer.matrixAt(t);
    for (const Vec2& c : {lmn, Vec2{lmx.x, lmn.y}, lmx, Vec2{lmn.x, lmx.y}})
        if (!m.inFront(c)) return false;
    const Vec2 corners[4] = {m.apply(lmn), m.apply({lmx.x, lmn.y}), m.apply(lmx), m.apply({lmn.x, lmx.y})};
    mn = mx = corners[0];
    for (const Vec2& c : corners) {
        mn = {std::min(mn.x, c.x), std::min(mn.y, c.y)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y)};
    }
    return true;
}

} // namespace bemotion::viewport_internal
