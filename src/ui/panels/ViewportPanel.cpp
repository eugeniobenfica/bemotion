#include "ui/panels/ViewportPanel.h"

#include "app/PluginHost.h"
#include "app/Settings.h"
#include "core/Snapping.h"
#include "render/SceneRenderer.h"
#include "ui/AppSignals.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/SkiaQt.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bemotion {

using ui::Draw;
using ui::Pt;
using ui::Rgba;

namespace {

constexpr float kMinZoom = 0.02f;
constexpr float kMaxZoom = 32.0f;
constexpr float kHandleRadius = 5.0f;
constexpr float kRotateOffset = 26.0f;
constexpr float kRuler = 20.0f; // ruler band thickness

Pt pt(Vec2 v) { return {v.x, v.y}; }
Vec2 vec(const QPointF& p) { return {static_cast<float>(p.x()), static_cast<float>(p.y())}; }

struct Handles {
    Vec2 local[4];  // bounds corners in layer space (TL, TR, BR, BL)
    Vec2 screen[4]; // same corners on screen
    Vec2 rotate;    // rotation handle on screen
    Vec2 topMid;
    Vec2 pivot;     // layer position on screen
};

// The composition being edited, for the helpers below: layers inside compounds live in their
// compound's space.
const Composition* g_comp = nullptr;

// `view` extended with the layer's enclosing compounds: maps the layer's
// parent space (where its position lives) to the screen.
render::View parentView(const render::View& view, const Layer& layer, double t) {
    render::View v = view;
    if (g_comp && layer.parent) v.camera = view.camera * g_comp->parentMatrix(layer, t);
    return v;
}

Affine parentOf(const Layer& layer, double t) { return g_comp ? g_comp->parentMatrix(layer, t) : Affine{}; }

bool computeHandles(const Layer& layer, double t, const render::View& sceneView, Handles& h) {
    if (layer.type == LayerType::Background) return false; // it is the frame: nothing to move or scale
    const render::View view = parentView(sceneView, layer, t);
    Vec2 mn, mx;
    if (!render::localBounds(layer, t, mn, mx, g_comp)) return false;
    h.local[0] = {mn.x, mn.y};
    h.local[1] = {mx.x, mn.y};
    h.local[2] = {mx.x, mx.y};
    h.local[3] = {mn.x, mx.y};
    const Affine m = view.matrix() * layer.matrixAt(t);
    for (int i = 0; i < 4; ++i) h.screen[i] = m.apply(h.local[i]);
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
bool layerOverlapsRect(const Layer& layer, double t, const render::View& view, Vec2 rectMin, Vec2 rectMax) {
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

void outline(Draw& d, const Handles& h, Rgba col, float thickness) {
    const Pt pts[4] = {pt(h.screen[0]), pt(h.screen[1]), pt(h.screen[2]), pt(h.screen[3])};
    d.polyline(pts, 4, col, true, thickness);
}

bool near(Vec2 a, Vec2 b, float r) { return (a - b).length() <= r; }

// Axis-aligned bounds of a layer in composition pixels (as seen through `camera`).
bool compBox(const Layer& layer, double t, const Affine& camera, Vec2& mn, Vec2& mx) {
    Vec2 lmn, lmx;
    if (!render::localBounds(layer, t, lmn, lmx, g_comp)) return false;
    const Affine m = camera * parentOf(layer, t) * layer.matrixAt(t);
    const Vec2 corners[4] = {m.apply(lmn), m.apply({lmx.x, lmn.y}), m.apply(lmx), m.apply({lmn.x, lmx.y})};
    mn = mx = corners[0];
    for (const Vec2& c : corners) {
        mn = {std::min(mn.x, c.x), std::min(mn.y, c.y)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y)};
    }
    return true;
}

} // namespace

ViewportPanel::ViewportPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(200, 150);

    auto hud = [this](ui::Icon icon, const QString& tip) {
        auto* b = new ui::IconButton(icon, tip, 26, this);
        return b;
    };
    cameraButton_ = hud(ui::Icon::Camera, "");
    connect(cameraButton_, &QAbstractButton::clicked, this, [this] {
        throughCamera_ = !throughCamera_;
        update();
    });
    snapButton_ = hud(ui::Icon::Magnet, "Snapping (S) - hold Ctrl while dragging to bypass");
    connect(snapButton_, &QAbstractButton::clicked, this, [this] { ctx_.snapping = !ctx_.snapping; });
    zoomOutButton_ = hud(ui::Icon::ZoomOut, "Zoom out (Ctrl+-)");
    connect(zoomOutButton_, &QAbstractButton::clicked, this, [this] { zoomBy(1.0f / 1.25f); });
    zoomInButton_ = hud(ui::Icon::ZoomIn, "Zoom in (Ctrl+=)");
    connect(zoomInButton_, &QAbstractButton::clicked, this, [this] { zoomBy(1.25f); });
    zoomLevel_ = new QToolButton(this);
    zoomLevel_->setPopupMode(QToolButton::InstantPopup);
    zoomLevel_->setFixedSize(70, 26);
    auto* menu = new QMenu(zoomLevel_);
    menu->addAction("Fit", this, [this] { requestFit(); });
    for (float z : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f})
        menu->addAction(QString("%1%").arg(static_cast<int>(z * 100)), this, [this, z] { setZoom(z); });
    zoomLevel_->setMenu(menu);
    fitButton_ = new QPushButton("Fit", this);
    fitButton_->setFixedSize(46, 26);
    fitButton_->setToolTip("Fit composition to view (Ctrl+0)\nWheel: zoom  ·  Middle/right drag or Space+drag: pan");
    connect(fitButton_, &QPushButton::clicked, this, [this] { requestFit(); });
    qualityButton_ = new QToolButton(this);
    qualityButton_->setPopupMode(QToolButton::InstantPopup);
    qualityButton_->setFixedSize(70, 26);
    qualityButton_->setText("Full");
    qualityButton_->setToolTip("Preview resolution (export quality is unchanged)");
    auto* qualityMenu = new QMenu(qualityButton_);
    auto* qualityGroup = new QActionGroup(qualityMenu);
    for (const auto& [label, divisor] : {std::pair{"Full", 1}, {"Half", 2}, {"Quarter", 4}}) {
        auto* action = qualityMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(divisor == previewDivisor_);
        action->setData(divisor);
        qualityGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, divisor] { setPreviewQuality(divisor); });
    }
    qualityButton_->setMenu(qualityMenu);
    fullscreenButton_ = new ui::IconButton(ui::Icon::Fullscreen, "Show only the viewport in fullscreen (Esc to return)", 26, this);
    fullscreenButton_->setToolTip("Show only the viewport in fullscreen (Esc to return)");
    connect(fullscreenButton_, &QPushButton::clicked, this, &ViewportPanel::viewportFullscreenRequested);
    connect(&ui::AppSignals::get(), &ui::AppSignals::pluginsChanged, this, [this] {
        frameValid_ = false;
        update();
    });
}

// --- view ---------------------------------------------------------------------------------

void ViewportPanel::layout() {
    Composition& c = ctx_.comp();
    g_comp = &c;
    compSize_ = {static_cast<float>(c.width), static_cast<float>(c.height)};
    const float w = static_cast<float>(width()), h = static_cast<float>(height());
    if (autoFit_) {
        zoom_ = std::clamp(std::min((w - 64.0f) / c.width, (h - 64.0f) / c.height), kMinZoom, kMaxZoom);
        pan_ = {0.0f, 0.0f};
    }
    origin_ = Vec2{w * 0.5f, h * 0.5f} + pan_ - compSize_ * (zoom_ * 0.5f);
    world_ = throughCamera_ ? c.cameraAt(ctx_.time) : Affine{};
}

void ViewportPanel::zoomAround(Vec2 screenPoint, float newZoom) {
    layout();
    newZoom = std::clamp(newZoom, kMinZoom, kMaxZoom);
    const Vec2 compPoint = (screenPoint - origin_) / zoom_;
    const Vec2 newOrigin = screenPoint - compPoint * newZoom;
    pan_ = newOrigin + compSize_ * (newZoom * 0.5f) - Vec2{width() * 0.5f, height() * 0.5f};
    origin_ = newOrigin;
    zoom_ = newZoom;
    autoFit_ = false;
    update();
}

void ViewportPanel::zoomBy(float factor) { zoomAround({width() * 0.5f, height() * 0.5f}, zoom_ * factor); }
void ViewportPanel::setZoom(float zoom) { zoomAround({width() * 0.5f, height() * 0.5f}, zoom); }

void ViewportPanel::setPreviewQuality(int divisor) {
    divisor = divisor == 4 ? 4 : divisor == 2 ? 2 : 1;
    if (previewDivisor_ == divisor) return;
    previewDivisor_ = divisor;
    qualityButton_->setText(divisor == 1 ? "Full" : divisor == 2 ? "Half" : "Quarter");
    for (QAction* action : qualityButton_->menu()->actions()) action->setChecked(action->data().toInt() == divisor);
    frameValid_ = false;
    update();
    emit previewQualityChanged(divisor);
}

void ViewportPanel::setViewportFullscreen(bool fullscreen) {
    fullscreenButton_->setActive(fullscreen);
    fullscreenButton_->setToolTip(fullscreen ? "Exit fullscreen (Esc)" : "Show only the viewport in fullscreen (Esc to return)");
    positionHud();
}

bool ViewportPanel::setSpaceHeld(bool held) {
    spaceHeld_ = held;
    if (held) spaceUsed_ = false;
    updateCursor(mouse_, QApplication::keyboardModifiers());
    return spaceUsed_;
}

void ViewportPanel::sync() {
    if (!isVisible()) return;
    Signature s{ctx_.revision(), ctx_.time, ctx_.selection, ctx_.scope, ctx_.snapping, ctx_.showGuides, ctx_.showRulers, ctx_.playing};
    if (s == drawn_) return;
    drawn_ = std::move(s);
    update();
}

void ViewportPanel::resizeEvent(QResizeEvent*) { positionHud(); }

void ViewportPanel::positionHud() {
    const Composition& c = ctx_.comp();
    const int y = height() - 26 - 10;
    int x = width() - 12 - (26 * 3 + 70 + 46 + 4 * 3 + 12);
    const bool hasCamera = std::any_of(c.layers.begin(), c.layers.end(), [](const Layer& l) { return l.type == LayerType::Camera; });
    cameraButton_->setVisible(hasCamera);
    cameraButton_->move(x - 26 - 6, y);
    snapButton_->move(x, y);
    x += 26 + 12;
    zoomOutButton_->move(x, y);
    x += 26 + 4;
    zoomLevel_->move(x, y);
    x += 70 + 4;
    zoomInButton_->move(x, y);
    x += 26 + 4;
    fitButton_->move(x, y);
    const int controlsY = width() >= 520 ? y : y - 32;
    qualityButton_->move(12, controlsY);
    fullscreenButton_->move(88, controlsY);
}

// --- painting ---------------------------------------------------------------------------------

void ViewportPanel::paintEvent(QPaintEvent*) {
    layout();
    positionHud();
    const Composition& c = ctx_.comp();
    const double t = ctx_.time;
    QPainter p(this);
    Draw d(p);
    d.rectFilled(Pt(0, 0), Pt(width(), height()), theme::kCanvas);

    // Cache the scene independently of hover/selection overlays. Media may finish decoding
    // without a document revision, so compositions with imported media remain uncached.
    const qreal rasterScale = devicePixelRatioF() / previewDivisor_;
    const QSize px(std::max(1, static_cast<int>(std::ceil(width() * rasterScale))),
                   std::max(1, static_cast<int>(std::ceil(height() * rasterScale))));
    const FrameKey key{ctx_.revision(), t, px, origin_, zoom_, rasterScale, throughCamera_};
    if (!frameValid_ || key != frameKey_ || !c.media.empty()) {
        if (frame_.size() != px) frame_ = QImage(px, QImage::Format_ARGB32_Premultiplied);
        frame_.fill(Qt::transparent);
        const SkImageInfo info = qtImageInfo(px.width(), px.height());
        sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, frame_.bits(), static_cast<size_t>(frame_.bytesPerLine()));
        frameValid_ = static_cast<bool>(surface);
        if (surface) {
            const float z = static_cast<float>(rasterScale);
            render::drawComposition(surface->getCanvas(), c, t, render::View{origin_ * z, zoom_ * z, world_}, true, throughCamera_);
            frameKey_ = key;
        }
    }
    const Vec2 compMax = sceneView().toScreen(compSize_);
    d.rectFilled(Pt(origin_.x + 4, origin_.y + 6), Pt(compMax.x + 4, compMax.y + 6), theme::shade(90), 2.0f);
    // Upscale only the scene; handles, rulers, guides and HUD retain native resolution.
    p.setRenderHint(QPainter::SmoothPixmapTransform, previewDivisor_ > 1);
    p.drawImage(rect(), frame_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    d.rect(pt(origin_), pt(compMax), theme::kBorder);
    drawGuides(p);
    drawOverlay(p);
    drawRulers(p);
    drawHudBackground(p);

    char label[16];
    std::snprintf(label, sizeof(label), "%.0f%%", zoom_ * 100.0f);
    zoomLevel_->setText(label);
    snapButton_->setActive(ctx_.snapping);
    cameraButton_->setActive(throughCamera_);
    cameraButton_->setToolTip(throughCamera_ ? "Looking through the camera (click for the free view)"
                                             : "Free view: camera frames shown (click to look through the camera)");
    if (textEdit_) placeTextEditor();
}

void ViewportPanel::drawHudBackground(QPainter&) {
    // Nothing: the composition size is in Properties (with nothing selected), not on the canvas.
}

void ViewportPanel::drawMotionPath(QPainter& p) {
    Layer* l = pathLayer();
    if (!l) return;
    Draw d(p);
    const render::View view = parentView(sceneView(), *l, ctx_.time);
    const Composition& comp = ctx_.comp();
    const auto& keys = l->transform.position.keys;
    const int n = static_cast<int>(keys.size());
    const Rgba line = theme::accent(150);

    // The curve itself.
    for (int i = 0; i + 1 < n; ++i) {
        const PathSegment seg = pathSegment(keys, static_cast<size_t>(i));
        constexpr int kSteps = 32;
        Pt pts[kSteps + 1];
        for (int s = 0; s <= kSteps; ++s) pts[s] = pt(view.sceneToScreen(seg.at(static_cast<float>(s) / kSteps)));
        d.polyline(pts, kSteps + 1, line, false, 1.5f);
    }
    // One dot per frame: the spacing shows the speed.
    const int f0 = comp.frameAt(keys.front().time), f1 = comp.frameAt(keys.back().time);
    for (int f = f0; f <= f1; ++f)
        d.circleFilled(pt(view.sceneToScreen(l->transform.position.valueAt(comp.timeOfFrame(f)))), 1.6f, ui::rgba(255, 255, 255, 120));
    // Keys and their handles.
    for (int i = 0; i < n; ++i) {
        const Vec2 k = view.sceneToScreen(keys[i].value);
        Vec2 in, out;
        pathTangents(keys, static_cast<size_t>(i), in, out);
        const bool custom = keys[i].path == PathMode::Custom;
        const Rgba handleCol = custom ? theme::kAccent : ui::rgba(255, 255, 255, 140);
        for (int side = 0; side < 2; ++side) {
            if ((side == 0 && i == 0) || (side == 1 && i + 1 == n)) continue;
            const Vec2 h = view.sceneToScreen(keys[i].value + (side == 0 ? in : out));
            d.line(pt(k), pt(h), ui::rgba(255, 255, 255, 70), 1.0f);
            d.circleFilled(pt(h), 3.5f, handleCol);
        }
        const bool current = std::fabs(keys[i].time - ctx_.time) < kTimeEpsilon;
        const Pt a(k.x - 4, k.y - 4), b(k.x + 4, k.y + 4);
        d.rectFilled(a, b, current ? theme::kAccent : theme::kBgDeep);
        d.rect(a, b, theme::kAccent, 0.0f, 1.5f);
    }
}

void ViewportPanel::drawOverlay(QPainter& p) {
    Draw d(p);
    const render::View view = sceneView();
    const double t = ctx_.time;
    Composition& comp = ctx_.comp();

    // Hover outline for the layer under the cursor.
    int pathKey = -1, pathPart = 0;
    if (drag_ == Drag::None && hovered_ && !spaceHeld_ && !dropHover_ && !pathHit(mouse_, pathKey, pathPart)) {
        const uint64_t hover = render::hitTest(comp, t, view.screenToScene(mouse_), !throughCamera_);
        Handles h{};
        if (hover && !ctx_.isSelected(hover) && computeHandles(*comp.findLayer(hover), t, view, h)) outline(d, h, theme::accent(110), 1.0f);
    }

    // Free view: show what each camera sees.
    if (!throughCamera_)
        for (const Layer& l : comp.layers) {
            Handles h{};
            if (l.type != LayerType::Camera || !l.activeAt(t) || !computeHandles(l, t, view, h)) continue;
            outline(d, h, theme::withAlpha(theme::kGuide, 220), 1.5f);
            const Pt at(std::max(h.screen[0].x + 6, kRuler + 6), std::max(h.screen[0].y + 4, kRuler + 4));
            d.text(at, theme::withAlpha(theme::kGuide, 255), l.name); // kept clear of the rulers
        }

    drawMotionPath(p);

    // Selection outlines.
    const auto selected = ctx_.selectedLayers();
    Vec2 groupMin{1e9f, 1e9f}, groupMax{-1e9f, -1e9f};
    int visibleSelected = 0;
    for (const Layer* l : selected) {
        Handles h{};
        if (!l->activeAt(t) || !computeHandles(*l, t, view, h)) continue;
        outline(d, h, theme::kAccent, l->id == ctx_.primaryId() ? 1.5f : 1.0f);
        for (const Vec2& q : h.screen) {
            groupMin = {std::min(groupMin.x, q.x), std::min(groupMin.y, q.y)};
            groupMax = {std::max(groupMax.x, q.x), std::max(groupMax.y, q.y)};
        }
        ++visibleSelected;
    }
    if (visibleSelected > 1) d.rect(Pt(groupMin.x - 4, groupMin.y - 4), Pt(groupMax.x + 4, groupMax.y + 4), theme::accent(90), 2.0f, 1.0f);

    // Handles for a single selection.
    const bool cameraView = selected.size() == 1 && selected[0]->type == LayerType::Camera && throughCamera_;
    if (selected.size() == 1 && selected[0]->activeAt(t) && !cameraView) {
        const Layer* sel = selected[0];
        Handles h{};
        if (computeHandles(*sel, t, view, h)) {
            d.circle(pt(h.pivot), 5.0f, theme::kAccent, 1.5f);
            d.line(Pt(h.pivot.x - 8, h.pivot.y), Pt(h.pivot.x + 8, h.pivot.y), theme::kAccent, 1.0f);
            d.line(Pt(h.pivot.x, h.pivot.y - 8), Pt(h.pivot.x, h.pivot.y + 8), theme::kAccent, 1.0f);
            if (!sel->locked) {
                d.line(pt(h.topMid), pt(h.rotate), theme::kAccent, 1.0f);
                d.circleFilled(pt(h.rotate), kHandleRadius, theme::kBgPanel);
                d.circle(pt(h.rotate), kHandleRadius, theme::kAccent, 1.5f);
                for (const Vec2& c : h.screen) {
                    if (sel->type == LayerType::Camera) break; // cameras zoom, they do not scale
                    const Pt a(c.x - kHandleRadius, c.y - kHandleRadius), b(c.x + kHandleRadius, c.y + kHandleRadius);
                    d.rectFilled(a, b, theme::kBgPanel, 2.0f);
                    d.rect(a, b, theme::kAccent, 2.0f, 1.5f);
                }
            }
        }
    }

    // Box selection.
    if (drag_ == Drag::Marquee && dragMoved_) {
        const Pt a(std::min(dragStartScreen_.x, mouse_.x), std::min(dragStartScreen_.y, mouse_.y));
        const Pt b(std::max(dragStartScreen_.x, mouse_.x), std::max(dragStartScreen_.y, mouse_.y));
        d.rectFilled(a, b, theme::accent(22));
        d.rect(a, b, theme::accent(160));
    }

    // Live position readout while moving.
    if (drag_ == Drag::Move && dragMoved_)
        if (const Layer* l = comp.findLayer(dragLayer_)) {
            const Vec2 q = l->transform.position.valueAt(t);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "X %.0f   Y %.0f", q.x, q.y);
            const QString s = QString::fromUtf8(buf);
            const Pt at(mouse_.x + 16, mouse_.y + 18);
            d.rectFilled(Pt(at.x - 6, at.y - 3), Pt(at.x + d.textWidth(s) + 6, at.y + d.textHeight() + 3), theme::withAlpha(theme::kBgDeep, 220), 4);
            d.text(at, theme::kText, s);
        }

    // Smart-guide lines for whatever the current drag snapped to.
    for (float x : snapLinesX_) {
        const float sx = view.toScreen({x, 0}).x;
        d.line(Pt(sx, 0), Pt(sx, height()), theme::kSnap, 1.0f);
    }
    for (float y : snapLinesY_) {
        const float sy = view.toScreen({0, y}).y;
        d.line(Pt(0, sy), Pt(width(), sy), theme::kSnap, 1.0f);
    }

    // Ghost of an object being dragged in from the Objects / Media panels.
    if (dropHover_) {
        const Vec2 scenePos = world_.inverse().apply(dropPos_);
        const PluginObjectType* type = dropMedia_ ? nullptr : droppedPluginType();
        const MediaItem* item = dropMedia_ ? comp.findMedia(dropMedia_) : nullptr;
        Layer ghost = item                         ? makeMediaLayer(*item, scenePos, 0.0, kDefaultLayerLength)
                      : dropKind_ == ui::kDropCamera ? makeCameraLayer(compSize_, comp.duration)
                      : dropKind_ == ui::kDropBackground ? makeBackgroundLayer(comp.duration)
                      : type                       ? makePluginLayer(type->name, type->prototype, scenePos, comp.duration)
                      : dropKind_ < 0              ? makeTextLayer("Your text", scenePos, comp.duration)
                                                   : makeShapeLayer(static_cast<ShapeKind>(dropKind_), scenePos, comp.duration);
        if (item && item->width > 0 && item->height > 0) { // fitted like the layer it becomes
            const float fit = std::min({1.0f, static_cast<float>(comp.width) / item->width, static_cast<float>(comp.height) / item->height});
            ghost.transform.scale.base = {fit * 100.0f, fit * 100.0f};
        }
        ghost.transform.opacity.base = 55.0f;
        // Through Skia, like the real layer. Images show themselves; a video's ghost is its frame
        // (no reader for a layer that does not exist yet).
        const qreal dpr = devicePixelRatioF();
        QImage ghostImg(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr), QImage::Format_ARGB32_Premultiplied);
        ghostImg.fill(Qt::transparent);
        if (auto surface = SkSurfaces::WrapPixels(qtImageInfo(ghostImg.width(), ghostImg.height()), ghostImg.bits(),
                                                  static_cast<size_t>(ghostImg.bytesPerLine()))) {
            const Affine m = Affine::scale(static_cast<float>(dpr)) * view.matrix() * ghost.matrixAt(t);
            render::drawLayer(surface->getCanvas(), ghost, t, m, zoom_ * static_cast<float>(dpr),
                              item && item->kind == MediaKind::Image ? &comp : nullptr);
        }
        ghostImg.setDevicePixelRatio(dpr);
        p.drawImage(QPointF(0, 0), ghostImg);
        const Vec2 s = view.toScreen(dropPos_);
        d.line(Pt(s.x - 10, s.y), Pt(s.x + 10, s.y), theme::kAccent, 1.5f);
        d.line(Pt(s.x, s.y - 10), Pt(s.x, s.y + 10), theme::kAccent, 1.5f);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "X %.0f   Y %.0f", dropPos_.x, dropPos_.y);
        d.text(Pt(s.x + 14, s.y - d.textHeight() - 12), theme::kAccent, std::string(buf));
    }
}

void ViewportPanel::drawGuides(QPainter& p) {
    if (!ctx_.showGuides) return;
    Draw d(p);
    const render::View view = sceneView();
    const auto& guides = ctx_.comp().guides;
    for (int i = 0; i < static_cast<int>(guides.size()); ++i) {
        const Guide& g = guides[i];
        const Vec2 s = view.toScreen({g.position, g.position});
        const bool active = i == guideDrag_;
        const Rgba col = active ? theme::kText : theme::kGuide;
        if (g.vertical) d.line(Pt(s.x, 0), Pt(s.x, height()), col, 1.0f);
        else d.line(Pt(0, s.y), Pt(width(), s.y), col, 1.0f);
        if (active) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f px", g.position);
            const Pt at = g.vertical ? Pt(s.x + 6, kRuler + 6) : Pt(kRuler + 6, s.y + 4);
            d.text(at, theme::kText, std::string(buf));
        }
    }
}

void ViewportPanel::drawRulers(QPainter& p) {
    if (!ctx_.showRulers) return;
    Draw d(p);
    const render::View view = sceneView();
    const float W = static_cast<float>(width()), H = static_cast<float>(height());
    const Rgba bg = theme::withAlpha(theme::kBgPanel, 245);
    d.rectFilled(Pt(0, 0), Pt(W, kRuler), bg);
    d.rectFilled(Pt(0, kRuler), Pt(kRuler, H), bg);
    d.line(Pt(0, kRuler), Pt(W, kRuler), theme::kBorder);
    d.line(Pt(kRuler, 0), Pt(kRuler, H), theme::kBorder);

    // Tick spacing in composition pixels, at least ~60 screen pixels apart.
    float step = 5000.0f;
    for (float s : {1.f, 2.f, 5.f, 10.f, 25.f, 50.f, 100.f, 250.f, 500.f, 1000.f, 2500.f, 5000.f})
        if (s * zoom_ >= 60.0f) {
            step = s;
            break;
        }
    QFont small = p.font();
    small.setPointSizeF(small.pointSizeF() * 0.78);
    p.save();
    p.setFont(small);
    char label[16];
    // Top ruler (x).
    d.pushClip(Pt(kRuler, 0), Pt(W, kRuler));
    const float x0 = view.toComp(Vec2(kRuler, 0)).x, x1 = view.toComp(Vec2(W, 0)).x;
    for (float x = std::floor(x0 / step) * step; x <= x1; x += step) {
        const float sx = view.toScreen({x, 0}).x;
        d.line(Pt(sx, kRuler - 8), Pt(sx, kRuler), theme::kTextFaint);
        for (int k = 1; k < 5; ++k) {
            const float mx = view.toScreen({x + step * k / 5.0f, 0}).x;
            d.line(Pt(mx, kRuler - 3), Pt(mx, kRuler), theme::kBorder);
        }
        std::snprintf(label, sizeof(label), "%.0f", x);
        d.text(Pt(sx + 3, 2), theme::kTextFaint, std::string(label));
    }
    d.popClip();
    // Left ruler (y).
    d.pushClip(Pt(0, kRuler), Pt(kRuler, H));
    const float y0 = view.toComp(Vec2(0, kRuler)).y, y1 = view.toComp(Vec2(0, H)).y;
    for (float y = std::floor(y0 / step) * step; y <= y1; y += step) {
        const float sy = view.toScreen({0, y}).y;
        d.line(Pt(kRuler - 8, sy), Pt(kRuler, sy), theme::kTextFaint);
        for (int k = 1; k < 5; ++k) {
            const float my = view.toScreen({0, y + step * k / 5.0f}).y;
            d.line(Pt(kRuler - 3, my), Pt(kRuler, my), theme::kBorder);
        }
        std::snprintf(label, sizeof(label), "%.0f", y);
        d.text(Pt(2, sy + 2), theme::kTextFaint, std::string(label));
    }
    d.popClip();
    p.restore();

    // Mouse position markers.
    if (hovered_ && mouse_.x > kRuler && mouse_.x < W && mouse_.y > 0 && mouse_.y < H) {
        d.line(Pt(mouse_.x, 0), Pt(mouse_.x, kRuler), theme::kAccent);
        d.line(Pt(0, mouse_.y), Pt(kRuler, mouse_.y), theme::kAccent);
    }
}

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
    const render::View view = parentView(sceneView(), *l, ctx_.time);
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

// --- input ------------------------------------------------------------------------------------

void ViewportPanel::updateCursor(Vec2 mouse, Qt::KeyboardModifiers mods) {
    Qt::CursorShape shape = Qt::ArrowCursor;
    const bool overRulerTop = ctx_.showRulers && mouse.y < kRuler && mouse.x > kRuler;
    const bool overRulerLeft = ctx_.showRulers && mouse.x < kRuler && mouse.y > kRuler;
    if (drag_ == Drag::Pan) shape = Qt::ClosedHandCursor;
    else if (drag_ == Drag::Guide && guideDrag_ >= 0 && guideDrag_ < static_cast<int>(ctx_.comp().guides.size()))
        shape = ctx_.comp().guides[guideDrag_].vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    else if (drag_ == Drag::None) {
        if (spaceHeld_ || (mods & Qt::AltModifier)) shape = Qt::OpenHandCursor;
        else if (overRulerTop && ctx_.showGuides) shape = Qt::SizeVerCursor;
        else if (overRulerLeft && ctx_.showGuides) shape = Qt::SizeHorCursor;
        else if (const int g = guideAt(mouse); g >= 0 && !ctx_.lockGuides)
            shape = ctx_.comp().guides[g].vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor;
        else {
            const double t = ctx_.time;
            Layer* single = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
            if (single && (single->locked || !single->activeAt(t) || (single->type == LayerType::Camera && throughCamera_))) single = nullptr;
            Handles h{};
            if (single && computeHandles(*single, t, sceneView(), h)) {
                for (int i = 0; i < 4; ++i)
                    if (single->type != LayerType::Camera && near(mouse, h.screen[i], kHandleRadius + 3))
                        shape = i % 2 == 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
                if (near(mouse, h.rotate, kHandleRadius + 4)) shape = Qt::PointingHandCursor;
            }
            int key = -1, part = 0;
            if (pathHit(mouse, key, part)) shape = Qt::PointingHandCursor;
        }
    }
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

    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) { // pan
        drag_ = Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton) return;

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

    // Scale/rotate handles exist only for a single editable layer.
    Layer* single = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
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
            const Vec2 scenePoint = view.screenToScene(mouse);
            // With a compound open (timeline), clicks pick among its contents.
            if (ctx_.scope && !comp.findLayer(ctx_.scope)) ctx_.scope = 0;
            uint64_t hit = ctx_.scope ? render::hitTestInside(comp, t, scenePoint, ctx_.scope) : render::hitTest(comp, t, scenePoint, !throughCamera_);
            // Inside a compound, clicks pick among its layers; a double-click on a
            // compound goes one level in (like After Effects / Figma groups).
            if (const Layer* cur = ctx_.selected(); cur && !additive) {
                uint64_t inner = 0;
                if (cur->parent) inner = render::hitTestInside(comp, t, scenePoint, cur->parent);
                if (!inner && cur->type == LayerType::Compound && doubleClick) {
                    inner = render::hitTestInside(comp, t, scenePoint, cur->id);
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
                        if (l->activeAt(t) && compBox(*l, t, world_, mn, mx)) {
                            dragBoxMin_ = {std::min(dragBoxMin_.x, mn.x), std::min(dragBoxMin_.y, mn.y)};
                            dragBoxMax_ = {std::max(dragBoxMax_.x, mx.x), std::max(dragBoxMax_.y, mx.y)};
                        }
                    }
                    collectSnapTargets(moving);
                }
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
    case Drag::Pan:
        pan_ += mouse - lastMouse_;
        autoFit_ = false;
        updateCursor(mouse, mods);
        return;
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

    if (!dragMoved_) return;
    switch (drag_) {
    case Drag::Move: {
        Vec2 delta = mouseComp - dragStartMouse_;
        if (mods & Qt::ShiftModifier) (std::fabs(delta.x) > std::fabs(delta.y) ? delta.y : delta.x) = 0.0f;
        if (ctx_.snapping && !(mods & Qt::ControlModifier)) delta = delta + snapMove(dragBoxMin_, dragBoxMax_, delta);
        const Vec2 sceneDelta = world_.inverse().applyVector(delta); // the camera may zoom or rotate the view
        for (const auto& [id, start] : dragStartPositions_)
            if (Layer* l = ctx_.comp().findLayer(id)) // inside a compound: in its (scaled, rotated) space
                l->transform.position.set(t, start + parentOf(*l, t).inverse().applyVector(sceneDelta));
        break;
    }
    case Drag::Scale: {
        Layer* layer = ctx_.comp().findLayer(dragLayer_);
        if (!layer) break;
        auto& tr = layer->transform;
        const Affine pre = parentOf(*layer, t) * Affine::translate(dragStartPosition_) * Affine::rotateDeg(dragStartRotation_);
        const Vec2 q = pre.inverse().apply(view.screenToScene(mouse));
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
        const Vec2 q = parentView(view, *layer, t).screenToScene(mouse);
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
    // A plain click inside a multi-selection narrows it to the clicked layer.
    if (drag_ == Drag::Move && !dragMoved_ && !additive_) ctx_.select(dragLayer_);
    drag_ = Drag::None;
    snapLinesX_.clear();
    snapLinesY_.clear();
    updateCursor(mouse, e->modifiers());
    update();
}

void ViewportPanel::wheelEvent(QWheelEvent* e) {
    const float steps = e->angleDelta().y() / 120.0f;
    if (steps != 0.0f) zoomAround(vec(e->position()), zoom_ * std::pow(1.15f, steps));
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
    if (e->mimeData()->hasFormat(ui::kToolbarMime)) return e->acceptProposedAction();
    if (updateDrop(e->mimeData(), vec(e->position()), e->modifiers())) e->acceptProposedAction();
    else e->ignore();
}

void ViewportPanel::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(ui::kToolbarMime)) return e->acceptProposedAction();
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
    if (e->mimeData()->hasFormat(ui::kToolbarMime)) {
        // Where the grip was grabbed (x in the bar): the bar lands under the mouse as it was held.
        const auto grab = e->mimeData()->data(ui::kToolbarMime).split(',');
        const QPoint offset(grab[0].toInt(), grab.size() > 1 ? grab[1].toInt() : 14);
        e->acceptProposedAction();
        emit toolbarDropped(e->position().toPoint() - offset);
        return;
    }
    if (!updateDrop(e->mimeData(), vec(e->position()), e->modifiers())) return;
    const Vec2 scenePos = world_.inverse().apply(dropPos_); // where the camera shows that point
    if (dropMedia_) ctx_.addMedia(dropMedia_, scenePos);
    else if (dropKind_ == ui::kDropCamera) ctx_.addCamera();
    else if (dropKind_ == ui::kDropBackground) ctx_.addBackground();
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
        if (!compBox(l, ctx_.time, world_, mn, mx)) continue;
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
    menu.addSection(guides[index].vertical ? "Vertical guide" : "Horizontal guide");
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
    menu.addAction("Delete Guide", this, [this, index] {
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
        textHint_ = new QLabel("Enter to finish  ·  Ctrl+Enter for a new line  ·  Esc to cancel", this);
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
    if (!layer || !layer->activeAt(ctx_.time) || !compBox(*layer, ctx_.time, world_, mn, mx)) {
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
