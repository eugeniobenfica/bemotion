#include "ui/panels/ViewportPanel.h"

// The viewport: motion paths, Pen paths and masks, and the crop frame (the rest: ViewportPanel.cpp,
// ViewportInput.cpp).

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

// --- motion path ------------------------------------------------------------------------

Layer* ViewportPanel::pathLayer() const {
    if (ctx_.selection.size() != 1) return nullptr;
    Layer* l = const_cast<EditorContext&>(ctx_).selected();
    if (!l || l->locked || !l->visible || l->transform.position.keyCount() < 2) return nullptr;
    return l;
}

bool ViewportPanel::pathHit(Vec2 screen, int& key, int& part) const {
    Layer* l = pathLayer();
    if (!l) return false;
    const render::View view = parentView(sceneView(), *l, ctx_.time, false);
    const auto& keys = l->transform.position.keys;
    const int n = static_cast<int>(keys.size());
    for (int i = 0; i < n; ++i) {
        Vec2 in, out;
        pathTangents(keys, static_cast<size_t>(i), in, out);
        if (i > 0 && near(screen, view.sceneToScreen(keys[i].value + in), kHandleRadius + 2)) return key = i, part = 1, true;
        if (i + 1 < n && near(screen, view.sceneToScreen(keys[i].value + out), kHandleRadius + 2)) return key = i, part = 2, true;
    }
    for (int i = 0; i < n; ++i) {
        // A key at the playhead sits on the layer itself: let the normal move handle it.
        if (std::fabs(keys[i].time - ctx_.time) < kTimeEpsilon) continue;
        if (near(screen, view.sceneToScreen(keys[i].value), kHandleRadius + 2)) return key = i, part = 0, true;
    }
    return false;
}

// --- paths: the pen, a path shape's points, masks -----------------------------------------------

BezierPath* ViewportPanel::pathOf(const PathRef& r) const {
    Layer* l = const_cast<EditorContext&>(ctx_).comp().findLayer(r.layer);
    if (!l) return nullptr;
    if (r.mask < 0) return l->type == LayerType::Shape && l->shape.kind == ShapeKind::Path ? &l->shape.path : nullptr;
    if (r.mask >= static_cast<int>(l->masks.size())) return nullptr;
    Mask& m = l->masks[r.mask];
    if (m.pathKeys.empty()) return &m.path;
    int k = m.pathKeyAt(ctx_.time);
    if (k < 0) k = m.setPathKey(ctx_.time, m.pathAt(ctx_.time));
    return &m.pathKeys[static_cast<size_t>(k)].path;
}

std::optional<BezierPath> ViewportPanel::shownPath(const PathRef& r) const {
    const Layer* l = const_cast<EditorContext&>(ctx_).comp().findLayer(r.layer);
    if (!l) return std::nullopt;
    if (r.mask < 0) return l->type == LayerType::Shape && l->shape.kind == ShapeKind::Path ? std::optional(l->shape.path) : std::nullopt;
    if (r.mask >= static_cast<int>(l->masks.size())) return std::nullopt;
    return l->masks[r.mask].pathAt(ctx_.time);
}

bool ViewportPanel::pathMatrix(uint64_t layerId, Projective& m) const {
    const Layer* l = const_cast<EditorContext&>(ctx_).comp().findLayer(layerId);
    Projective camera;
    if (!l || !cameraOf(*l, ctx_.time, camera)) return false;
    m = parentView(sceneView(), *l, ctx_.time).matrix() * l->matrixAt(ctx_.time);
    return true;
}

std::vector<ViewportPanel::PathRef> ViewportPanel::editablePaths() const {
    std::vector<PathRef> out;
    if (orbit_ || ctx_.selection.size() != 1) return out;
    const Layer* l = const_cast<EditorContext&>(ctx_).selected();
    if (!l || l->locked || !l->activeAt(ctx_.time) || l->type == LayerType::Camera) return out;
    // A mask picked on the timeline: that one alone, as an object of its own.
    if (ctx_.maskFocus >= 0 && ctx_.maskFocus < static_cast<int>(l->masks.size())) return {{l->id, ctx_.maskFocus}};
    if (l->type == LayerType::Shape && l->shape.kind == ShapeKind::Path) out.push_back({l->id, -1});
    for (int i = 0; i < static_cast<int>(l->masks.size()); ++i) out.push_back({l->id, i});
    return out;
}

bool ViewportPanel::pathPointAt(Vec2 s, PathPoint& out) const {
    for (const PathRef& r : editablePaths()) {
        const std::optional<BezierPath> shown = shownPath(r);
        const BezierPath* path = shown ? &*shown : nullptr;
        Projective m;
        if (!path || !pathMatrix(r.layer, m)) continue;
        for (int i = 0; i < static_cast<int>(path->vertices.size()); ++i) { // handles over points: they sit on top
            const PathVertex& v = path->vertices[i];
            if (v.in != Vec2{} && near(s, m.apply(v.point + v.in), kHandleRadius + 2)) return out = {r, i, 1}, true;
            if (v.out != Vec2{} && near(s, m.apply(v.point + v.out), kHandleRadius + 2)) return out = {r, i, 2}, true;
        }
        for (int i = 0; i < static_cast<int>(path->vertices.size()); ++i)
            if (near(s, m.apply(path->vertices[i].point), kHandleRadius + 3)) return out = {r, i, 0}, true;
    }
    return false;
}

Vec2 ViewportPanel::pathLocal(uint64_t layerId, Vec2 screen) const {
    Projective m;
    return pathMatrix(layerId, m) ? m.inverse().apply(screen) : Vec2{};
}

void ViewportPanel::startPen(Vec2 mouse) {
    Layer* sel = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
    // A layer selected that is not a shape gets a mask; otherwise a new shape layer starts here.
    const bool mask = sel && sel->type != LayerType::Shape && sel->type != LayerType::Camera && !sel->locked && sel->activeAt(ctx_.time) &&
                      !(sel->type == LayerType::Media && !sel->media.hasVideo);
    penRestore_.reset();
    if (sel && !sel->locked && ctx_.maskFocus >= 0 && ctx_.maskFocus < static_cast<int>(sel->masks.size())) {
        // A mask picked on the timeline: the pen draws its shape again (any layer, shapes too); a
        // drawing given up leaves the old one.
        Mask& m = sel->masks[static_cast<size_t>(ctx_.maskFocus)];
        penRestore_ = m.path;
        m.path.vertices.clear();
        m.path.closed = false;
        penTarget_ = {sel->id, ctx_.maskFocus};
    } else if (mask) {
        Mask m;
        m.name = tr("Mask %1").arg(sel->masks.size() + 1).toStdString();
        m.path.vertices.clear();
        sel->masks.push_back(std::move(m));
        penTarget_ = {sel->id, static_cast<int>(sel->masks.size()) - 1};
    } else {
        Layer& l = ctx_.addShape(ShapeKind::Path, sceneView().screenToScene(mouse));
        l.shape.path.vertices.clear();
        l.shape.path.closed = false;
        l.shape.fillEnabled = false; // an open path is a line until it is closed
        l.shape.strokeEnabled = true;
        l.shape.strokeColor.base = Color::fromHex(0xE8B931);
        l.shape.strokeWidth.base = 6.0f;
        penTarget_ = {l.id, -1};
    }
    penDrawing_ = true;
    addPenPoint(mouse);
}

void ViewportPanel::addPenPoint(Vec2 mouse) {
    BezierPath* path = pathOf(penTarget_);
    if (!path) return;
    path->vertices.push_back({pathLocal(penTarget_.layer, mouse), {}, {}});
    drag_ = Drag::PenHandle;
    pathDrag_ = {penTarget_, static_cast<int>(path->vertices.size()) - 1, 2};
    ctx_.touch();
}

void ViewportPanel::finishPen() {
    if (!penDrawing_) return;
    penDrawing_ = false;
    Layer* l = ctx_.comp().findLayer(penTarget_.layer);
    BezierPath* path = pathOf(penTarget_);
    if (l && path) {
        if (penTarget_.mask >= 0 && path->vertices.size() < 3 && penRestore_) *path = *penRestore_; // given up: the shape it had
        else if (penTarget_.mask >= 0 && path->vertices.size() < 3) l->masks.erase(l->masks.begin() + penTarget_.mask); // nothing to cut with
        else if (penTarget_.mask >= 0) path->closed = true; // a mask is always a closed shape
        else if (penTarget_.mask < 0 && path->vertices.size() < 2) ctx_.comp().removeLayer(l->id), ctx_.pruneSelection();
    }
    ctx_.touch();
    update();
}

bool ViewportPanel::pathPress(QMouseEvent* e, Vec2 mouse, bool doubleClick) {
    const bool pen = ctx_.tool == Tool::Pen && !orbit_;
    const auto mods = e->modifiers();
    if (pen && penDrawing_ && doubleClick) { // done, open
        finishPen();
        return true;
    }
    PathPoint hit;
    if (pathPointAt(mouse, hit)) {
        BezierPath* path = pathOf(hit.ref);
        if (pen && penDrawing_ && hit.ref == penTarget_ && hit.vertex == 0 && hit.part == 0 && path->vertices.size() >= 2) {
            path->closed = true; // the first point again: closed
            if (Layer* l = ctx_.comp().findLayer(penTarget_.layer); l && penTarget_.mask < 0)
                l->shape.fillEnabled = true, l->shape.strokeEnabled = false;
            finishPen();
            return true;
        }
        if (pen && (mods & Qt::ControlModifier) && hit.part == 0) { // Ctrl+click: the point goes
            path->vertices.erase(path->vertices.begin() + hit.vertex);
            ctx_.touch();
            return true;
        }
        if (pen && (mods & Qt::AltModifier) && hit.part == 0) { // Alt+click: corner <-> smooth
            PathVertex& v = path->vertices[hit.vertex];
            if (v.in != Vec2{} || v.out != Vec2{}) {
                v.in = v.out = {};
            } else { // handles along the line between its neighbors, a third of the way to each
                const size_t n = path->vertices.size();
                const Vec2 prev = path->vertices[(hit.vertex + n - 1) % n].point, next = path->vertices[(hit.vertex + 1) % n].point;
                const Vec2 dir = next - prev;
                const float len = dir.length();
                if (len > 1e-3f) {
                    v.out = dir * ((next - v.point).length() / (3.0f * len));
                    v.in = dir * (-(prev - v.point).length() / (3.0f * len));
                }
            }
            ctx_.touch();
            return true;
        }
        drag_ = Drag::PathPoint;
        pathDrag_ = hit;
        pathGrab_ = pathLocal(hit.ref.layer, mouse) - path->vertices[hit.vertex].point;
        return true;
    }
    if (!pen || (mods & Qt::AltModifier)) return false; // (Alt+drag pans)
    if (penDrawing_) {
        addPenPoint(mouse);
        return true;
    }
    // On an outline: a new point there, keeping the curve.
    for (const PathRef& r : editablePaths()) {
        const std::optional<BezierPath> shown = shownPath(r);
        Projective m;
        if (!shown || !pathMatrix(r.layer, m)) continue;
        const Vec2 local = m.inverse().apply(mouse);
        const float scale = std::max(1e-4f, m.localScale(local));
        size_t segment = 0;
        float u = 0.0f;
        BezierPath outline = *shown;
        if (r.mask >= 0) outline.closed = true; // masks are always closed
        if (nearestSegment(outline, local, 6.0f / scale, segment, u)) {
            BezierPath* path = pathOf(r);
            if (!path) continue;
            path->closed = outline.closed;
            insertVertex(*path, segment, u);
            // An animated mask gets the point in every keyframe, so they still move into each other.
            if (Layer* l = ctx_.comp().findLayer(r.layer); l && r.mask >= 0)
                for (Mask::PathKey& k : l->masks[r.mask].pathKeys)
                    if (&k.path != path && segment < segmentCount(k.path)) insertVertex(k.path, segment, u);
            drag_ = Drag::PathPoint;
            pathDrag_ = {r, static_cast<int>(segment) + 1, 0};
            pathGrab_ = {};
            ctx_.touch();
            return true;
        }
    }
    startPen(mouse);
    return true;
}

void ViewportPanel::dragPath(Vec2 mouse, Qt::KeyboardModifiers mods) {
    BezierPath* path = pathOf(pathDrag_.ref);
    if (!path || pathDrag_.vertex < 0 || pathDrag_.vertex >= static_cast<int>(path->vertices.size())) return;
    PathVertex& v = path->vertices[pathDrag_.vertex];
    const Vec2 q = pathLocal(pathDrag_.ref.layer, mouse);
    if (drag_ == Drag::PenHandle) { // dragging out a new point's handles (a tiny drag: a corner)
        const Vec2 d = (mouse - dragStartScreen_).length() < 3.0f ? Vec2{} : q - v.point;
        v.out = d;
        v.in = d * -1.0f;
    } else if (pathDrag_.part == 0) {
        v.point = q - pathGrab_;
    } else {
        Vec2& own = pathDrag_.part == 1 ? v.in : v.out;
        Vec2& other = pathDrag_.part == 1 ? v.out : v.in;
        own = q - v.point;
        // The other handle mirrors it (a smooth point) unless Alt breaks them into a corner.
        const float ol = other.length(), hl = own.length();
        if (!(mods & Qt::AltModifier) && ol > 1e-4f && hl > 1e-4f) other = own * (-ol / hl);
    }
    ctx_.touch();
}

void ViewportPanel::drawPaths(QPainter& p) {
    Draw d(p);
    for (const PathRef& r : editablePaths()) {
        const std::optional<BezierPath> shown = shownPath(r);
        const BezierPath* path = shown ? &*shown : nullptr;
        Projective m;
        if (!path || path->vertices.empty() || !pathMatrix(r.layer, m)) continue;
        BezierPath outline = *path;
        if (r.mask >= 0 && !(penDrawing_ && r == penTarget_)) outline.closed = true;
        const Rgba line = r.mask >= 0 ? theme::withAlpha(theme::kGuide, 230) : theme::kAccent;
        const Path flat = flattenBezier(outline, 24);
        std::vector<Pt> pts;
        for (const Vec2& q : flat.points) pts.push_back(pt(m.apply(q)));
        if (pts.size() >= 2) d.polyline(pts.data(), static_cast<int>(pts.size()), line, outline.closed, 1.4f);
        for (int i = 0; i < static_cast<int>(path->vertices.size()); ++i) {
            const PathVertex& v = path->vertices[i];
            const Vec2 c = m.apply(v.point);
            for (const Vec2& h : {v.in, v.out}) {
                if (h == Vec2{}) continue;
                const Vec2 hs = m.apply(v.point + h);
                d.line(pt(c), pt(hs), ui::rgba(255, 255, 255, 110), 1.0f);
                d.circleFilled(pt(hs), 3.5f, line);
            }
            const bool first = i == 0 && penDrawing_ && r == penTarget_;
            const float s = first ? 5.0f : 4.0f; // the first point closes the path
            d.rectFilled(Pt(c.x - s, c.y - s), Pt(c.x + s, c.y + s), first ? line : theme::kBgDeep);
            d.rect(Pt(c.x - s, c.y - s), Pt(c.x + s, c.y + s), line, 0.0f, 1.5f);
        }
        // Drawing: where the next segment would go.
        if (penDrawing_ && r == penTarget_ && drag_ == Drag::None && hovered_)
            d.line(pt(m.apply(path->vertices.back().point)), pt(mouse_), theme::withAlpha(line, 140), 1.0f);
    }
}

void ViewportPanel::keyPressEvent(QKeyEvent* e) {
    if (penDrawing_ && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        finishPen();
        return;
    }
    QWidget::keyPressEvent(e);
}

// --- crop ---------------------------------------------------------------------------------------

Layer* ViewportPanel::cropLayer() const {
    if (ctx_.tool != Tool::Crop || orbit_ || ctx_.selection.size() != 1) return nullptr;
    Layer* l = const_cast<EditorContext&>(ctx_).selected();
    if (!l || l->type != LayerType::Media || !l->media.hasVideo || l->locked || !l->activeAt(ctx_.time)) return nullptr;
    return l;
}

bool ViewportPanel::cropFrame(const Layer& layer, Projective& m, Vec2 full[4], Vec2 shown[4]) const {
    const double t = ctx_.time;
    Projective camera;
    if (!cameraOf(layer, t, camera)) return false;
    m = parentView(sceneView(), layer, t).matrix() * layer.matrixAt(t);
    const Vec2 h = layer.media.size * 0.5f;
    Vec2 mn, mx;
    layer.media.cropRect(t, mn, mx);
    const Vec2 whole[4] = {{-h.x, -h.y}, {h.x, -h.y}, {h.x, h.y}, {-h.x, h.y}};
    const Vec2 part[4] = {mn, {mx.x, mn.y}, mx, {mn.x, mx.y}};
    for (int i = 0; i < 4; ++i) {
        if (!m.inFront(whole[i])) return false;
        full[i] = m.apply(whole[i]);
        shown[i] = m.apply(part[i]);
    }
    return true;
}

int ViewportPanel::cropHit(Vec2 s) const {
    const Layer* l = cropLayer();
    Projective m;
    Vec2 full[4], q[4];
    if (!l || !cropFrame(*l, m, full, q)) return 0;
    const int corners[4] = {1 | 2, 4 | 2, 4 | 8, 1 | 8};
    for (int i = 0; i < 4; ++i)
        if (near(s, q[i], kHandleRadius + 4)) return corners[i];
    auto distance = [](Vec2 p, Vec2 a, Vec2 b) {
        const Vec2 ab = b - a;
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        const float u = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
        return (p - (a + ab * u)).length();
    };
    const int edges[4] = {2, 4, 8, 1}; // top, right, bottom, left
    for (int i = 0; i < 4; ++i)
        if (distance(s, q[i], q[(i + 1) % 4]) <= 6.0f) return edges[i];
    Vec2 mn, mx;
    l->media.cropRect(ctx_.time, mn, mx);
    const Vec2 p = m.inverse().apply(s);
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y ? 15 : 0;
}

void ViewportPanel::drawCrop(QPainter& p) {
    const Layer* l = cropLayer();
    Projective m;
    Vec2 full[4], q[4];
    if (!l || !cropFrame(*l, m, full, q)) return;
    auto polygon = [](const Vec2* v) {
        QPolygonF poly;
        for (int i = 0; i < 4; ++i) poly << QPointF(v[i].x, v[i].y);
        return poly;
    };
    // What is cut away, dimmed; the whole picture's outline faint; the shown part's edges strong.
    QPainterPath away;
    away.setFillRule(Qt::OddEvenFill);
    away.addPolygon(polygon(full));
    away.addPolygon(polygon(q));
    p.fillPath(away, theme::q(theme::withAlpha(theme::kCanvas, 175)));
    Draw d(p);
    const Pt whole[4] = {pt(full[0]), pt(full[1]), pt(full[2]), pt(full[3])};
    d.polyline(whole, 4, theme::withAlpha(theme::kTextFaint, 200), true, 1.0f);
    const Pt shown[4] = {pt(q[0]), pt(q[1]), pt(q[2]), pt(q[3])};
    // Thirds, to compose with.
    for (float u : {1.0f / 3.0f, 2.0f / 3.0f}) {
        d.line(pt(q[0] + (q[1] - q[0]) * u), pt(q[3] + (q[2] - q[3]) * u), theme::accent(60), 1.0f);
        d.line(pt(q[0] + (q[3] - q[0]) * u), pt(q[1] + (q[2] - q[1]) * u), theme::accent(60), 1.0f);
    }
    d.polyline(shown, 4, theme::kAccent, true, 1.5f);
    // Corners (both edges) and edge middles (one).
    for (int i = 0; i < 4; ++i) {
        for (const Vec2 c : {q[i], (q[i] + q[(i + 1) % 4]) * 0.5f}) {
            const Pt a(c.x - kHandleRadius, c.y - kHandleRadius), b(c.x + kHandleRadius, c.y + kHandleRadius);
            d.rectFilled(a, b, theme::kBgPanel, 2.0f);
            d.rect(a, b, theme::kAccent, 2.0f, 1.5f);
        }
    }
}

} // namespace bemotion
