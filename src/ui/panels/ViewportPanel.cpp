#include "ui/panels/ViewportPanel.h"

#include "app/PluginHost.h"
#include "app/Settings.h"
#include "core/Geometry.h"
#include "core/Snapping.h"
#include "media/MediaManager.h"
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
#include <QPainterPath>
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
bool g_throughCamera = true; // the view looks through the camera (else the free view)
const Lens* g_lens = nullptr; // the 3D view's eye (null: the camera or the free view)

// The camera the layer is seen through (scene -> composition): one per layer, since 3D layers
// are seen at their own depth. False when the camera does not see the layer (behind it).
// `tilted` = false: the plane a 3D layer's position moves in (unturned, at its Z).
bool cameraOf(const Layer& layer, double t, Projective& out, bool tilted = true) {
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
void axesOf(const Lens& l, V3& right, V3& down, V3& forward) {
    float r[3], d[3], f[3];
    lensAxes(l, r, d, f);
    right = {r[0], r[1], r[2]}, down = {d[0], d[1], d[2]}, forward = {f[0], f[1], f[2]};
}

const Rgba kAxisColors[3] = {ui::rgba(235, 86, 86), ui::rgba(96, 200, 110), ui::rgba(92, 146, 245)}; // X, Y, Z

// `view` with the layer's camera and enclosing compounds: maps the layer's
// parent space (where its position lives) to the screen.
render::View parentView(const render::View& view, const Layer& layer, double t, bool tilted = true) {
    render::View v = view;
    Projective camera;
    if (g_comp && cameraOf(layer, t, camera, tilted)) v.camera = camera * g_comp->parentMatrix(layer, t);
    return v;
}

Affine parentOf(const Layer& layer, double t) { return g_comp ? g_comp->parentMatrix(layer, t) : Affine{}; }

bool computeHandles(const Layer& layer, double t, const render::View& sceneView, Handles& h) {
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

// Axis-aligned bounds of a layer in composition pixels (as the view shows it).
bool compBox(const Layer& layer, double t, Vec2& mn, Vec2& mx) {
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
        if (orbit_) orbit_ = false, throughCamera_ = true; // from the 3D view: back through the camera
        else throughCamera_ = !throughCamera_;
        update();
    });
    orbitButton_ = hud(ui::Icon::Cube, tr("3D view: the layers and the camera in space\n"
                                        "Drag: turn around  ·  Right drag: pan  ·  Wheel: closer / farther  ·  Fit: start over"));
    connect(orbitButton_, &QAbstractButton::clicked, this, [this] {
        orbit_ = !orbit_;
        update();
    });
    const char* toolNames[3] = {TR_NOOP("Orbit"), TR_NOOP("Pan"), TR_NOOP("Dolly")};
    const char* toolTips[3] = {TR_NOOP("Drag to turn the camera around what it looks at"), TR_NOOP("Drag to move the camera sideways and up / down"),
                               TR_NOOP("Drag up / down to move the camera forward / back")};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(tr(toolNames[i]), this);
        b->setCheckable(true);
        b->setFixedHeight(26);
        b->setToolTip(tr("Camera tool: %1 (keyframes at the playhead when animated)").arg(tr(toolTips[i])));
        connect(b, &QPushButton::clicked, this, [this, i](bool on) {
            cameraTool_ = on ? static_cast<CameraTool>(i + 1) : CameraTool::None;
            for (int k = 0; k < 3; ++k) cameraTools_[k]->setChecked(cameraTool_ == static_cast<CameraTool>(k + 1));
            updateCursor(mouse_, QApplication::keyboardModifiers());
        });
        cameraTools_[i] = b;
    }
    snapButton_ = hud(ui::Icon::Magnet, tr("Snapping (S) - hold Ctrl while dragging to bypass"));
    connect(snapButton_, &QAbstractButton::clicked, this, [this] { ctx_.snapping = !ctx_.snapping; });
    zoomOutButton_ = hud(ui::Icon::ZoomOut, tr("Zoom out (Ctrl+-)"));
    connect(zoomOutButton_, &QAbstractButton::clicked, this, [this] { zoomBy(1.0f / 1.25f); });
    zoomInButton_ = hud(ui::Icon::ZoomIn, tr("Zoom in (Ctrl+=)"));
    connect(zoomInButton_, &QAbstractButton::clicked, this, [this] { zoomBy(1.25f); });
    zoomLevel_ = new QToolButton(this);
    zoomLevel_->setPopupMode(QToolButton::InstantPopup);
    zoomLevel_->setFixedSize(70, 26);
    auto* menu = new QMenu(zoomLevel_);
    menu->addAction(tr("Fit"), this, [this] { requestFit(); });
    for (float z : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f})
        menu->addAction(QString("%1%").arg(static_cast<int>(z * 100)), this, [this, z] { setZoom(z); });
    zoomLevel_->setMenu(menu);
    fitButton_ = new QPushButton(tr("Fit"), this);
    fitButton_->setFixedHeight(26);
    fitButton_->setMinimumWidth(46); // as wide as its word needs (translations)
    fitButton_->setToolTip(tr("Fit composition to view (Ctrl+0)\nWheel: zoom  ·  Middle/right drag or Space+drag: pan"));
    connect(fitButton_, &QPushButton::clicked, this, [this] {
        if (orbit_) resetOrbit();
        requestFit();
    });
    qualityButton_ = new QToolButton(this);
    qualityButton_->setPopupMode(QToolButton::InstantPopup);
    qualityButton_->setFixedSize(70, 26);
    qualityButton_->setText(tr("Full"));
    qualityButton_->setToolTip(tr("Preview resolution (export quality is unchanged)"));
    auto* qualityMenu = new QMenu(qualityButton_);
    auto* qualityGroup = new QActionGroup(qualityMenu);
    for (const auto& [label, divisor] : {std::pair{TR_NOOP("Full"), 1}, {TR_NOOP("Half"), 2}, {"Quarter", 4}}) {
        auto* action = qualityMenu->addAction(tr(label));
        action->setCheckable(true);
        action->setChecked(divisor == previewDivisor_);
        action->setData(divisor);
        qualityGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, divisor] { setPreviewQuality(divisor); });
    }
    qualityMenu->addSeparator();
    QAction* proxies = qualityMenu->addAction(tr("Use Proxies"));
    proxies->setCheckable(true);
    proxies->setToolTip(tr("Videos with a proxy (Media panel › Create Proxy) play from it; exports use the originals"));
    connect(qualityMenu, &QMenu::aboutToShow, proxies, [proxies] { proxies->setChecked(media::MediaManager::get().useProxies()); });
    connect(proxies, &QAction::triggered, this, [](bool on) {
        settings().useProxies = on;
        media::MediaManager::get().setUseProxies(on);
        saveSettings();
    });
    qualityMenu->setToolTipsVisible(true);
    qualityButton_->setMenu(qualityMenu);
    fullscreenButton_ = new ui::IconButton(ui::Icon::Fullscreen, "", 26, this);
    fullscreenButton_->setToolTip(tr("Full screen preview: only the picture, playing (Esc to return)"));
    connect(fullscreenButton_, &QPushButton::clicked, this, &ViewportPanel::viewportFullscreenRequested);
    snapshotButton_ = new ui::IconButton(ui::Icon::Snapshot, tr("Export Frame: this frame as a picture, at full size (Shift+E)"), 26, this);
    connect(snapshotButton_, &QPushButton::clicked, this, &ViewportPanel::snapshotRequested);
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
    // Views that need what was deleted fall back (their buttons are gone): without a camera, the
    // default one (3D layers in perspective, as rendered); without anything 3D, no 3D view.
    const bool hasCamera = std::any_of(c.layers.begin(), c.layers.end(), [](const Layer& l) { return l.type == LayerType::Camera; });
    if (!hasCamera) throughCamera_ = true;
    if (orbit_ && !hasCamera && std::none_of(c.layers.begin(), c.layers.end(), [](const Layer& l) { return l.threeD; })) orbit_ = false;
    const float w = static_cast<float>(width()), h = static_cast<float>(height());
    if (autoFit_) {
        zoom_ = std::clamp(std::min((w - 64.0f) / c.width, (h - 64.0f) / c.height), kMinZoom, kMaxZoom);
        pan_ = {0.0f, 0.0f};
    }
    origin_ = Vec2{w * 0.5f, h * 0.5f} + pan_ - compSize_ * (zoom_ * 0.5f);
    world_ = throughCamera_ && !orbit_ ? c.cameraAt(ctx_.time) : Affine{};
    g_throughCamera = throughCamera_;
    if (orbitView_.distance <= 0.0f && orbitView_.tx == 960.0f && orbitView_.ty == 540.0f) resetOrbit(); // first time
    orbitLens_ = orbitLens();
    g_lens = orbit_ ? &orbitLens_ : nullptr;
}

Lens ViewportPanel::orbitLens() const {
    const Composition& c = ctx_.comp();
    Lens l;
    l.rx = orbitView_.pitch;
    l.ry = orbitView_.yaw;
    l.focal = c.focalLength();
    l.center = c.center();
    V3 right, down, forward;
    axesOf(l, right, down, forward);
    const float dist = orbitView_.distance > 0.0f ? orbitView_.distance : c.focalLength() * 3.2f;
    const V3 eye = V3{orbitView_.tx, orbitView_.ty, orbitView_.tz} - forward * dist;
    l.x = eye.x, l.y = eye.y, l.z = eye.z;
    return l;
}

void ViewportPanel::resetOrbit() {
    const Composition& c = ctx_.comp();
    // From the front, a little to the right and above, back far enough to see the camera too.
    orbitView_ = {};
    orbitView_.tx = c.center().x;
    orbitView_.ty = c.center().y;
    orbitView_.tz = -c.focalLength() * 0.35f;
    orbitView_.distance = c.focalLength() * 3.2f;
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
    qualityButton_->setText(divisor == 1 ? tr("Full") : divisor == 2 ? tr("Half") : tr("Quarter"));
    for (QAction* action : qualityButton_->menu()->actions())
        if (action->data().toInt() > 0) action->setChecked(action->data().toInt() == divisor); // not Use Proxies
    frameValid_ = false;
    update();
    emit previewQualityChanged(divisor);
}

void ViewportPanel::setViewportFullscreen(bool fullscreen) {
    fullscreenButton_->setActive(fullscreen);
    fullscreenButton_->setToolTip(fullscreen ? tr("Exit fullscreen (Esc)") : tr("Full screen preview: only the picture, playing (Esc to return)"));
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
    // The pen stops drawing when its tool, its layer or the selection goes.
    if (penDrawing_ && (ctx_.tool != Tool::Pen || !pathOf(penTarget_) || ctx_.selection.size() != 1 || ctx_.primaryId() != penTarget_.layer))
        finishPen();
    Signature s{ctx_.revision(), ctx_.time,       ctx_.selection, ctx_.scope,  ctx_.snapping,
                ctx_.showGuides, ctx_.showRulers, ctx_.playing,   static_cast<int>(ctx_.tool)};
    if (s == drawn_) return;
    drawn_ = std::move(s);
    update();
}

void ViewportPanel::resizeEvent(QResizeEvent*) { positionHud(); }

void ViewportPanel::positionHud() {
    const Composition& c = ctx_.comp();
    const int y = height() - 26 - 10;
    fitButton_->adjustSize(); // its word decides its width (translations)
    int x = width() - 12 - (26 * 3 + 70 + fitButton_->width() + 4 * 3 + 12);
    const bool hasCamera = std::any_of(c.layers.begin(), c.layers.end(), [](const Layer& l) { return l.type == LayerType::Camera; });
    const bool has3D = hasCamera || std::any_of(c.layers.begin(), c.layers.end(), [](const Layer& l) { return l.threeD; });
    cameraButton_->setVisible(hasCamera);
    cameraButton_->move(x - 26 - 6, y);
    orbitButton_->setVisible(has3D);
    orbitButton_->move(x - (26 + 6) * (hasCamera ? 2 : 1), y);
    // Camera tools: a camera selected, looking through it.
    const Layer* sel = ctx_.selection.size() == 1 ? const_cast<EditorContext&>(ctx_).selected() : nullptr;
    const bool tools = sel && sel->type == LayerType::Camera && throughCamera_ && !orbit_;
    if (!tools && cameraTool_ != CameraTool::None) {
        cameraTool_ = CameraTool::None;
        for (QPushButton* b : cameraTools_) b->setChecked(false);
    }
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
    const bool snapshot = settings().showSnapshotButton; // Settings › Interface › Show
    snapshotButton_->setVisible(snapshot);
    snapshotButton_->move(88 + 26 + 4, controlsY);
    // Next to them, off the picture: it would hide the top of the frame.
    int tx = 88 + (snapshot ? 2 * 26 + 4 : 26) + 12;
    for (QPushButton* b : cameraTools_) {
        b->setVisible(tools);
        b->adjustSize();
        b->move(tx, controlsY);
        tx += b->width() + 4;
    }
}

// --- painting ---------------------------------------------------------------------------------

void ViewportPanel::clearCache() {
    cache_.clear();
    cacheBytes_ = 0;
    ctx_.cachedFrames.clear();
}

void ViewportPanel::paintEvent(QPaintEvent*) {
    layout();
    positionHud();
    const Composition& c = ctx_.comp();
    const double t = ctx_.time;
    QPainter p(this);
    Draw d(p);
    d.rectFilled(Pt(0, 0), Pt(width(), height()), theme::kCanvas);

    // Cache the scene independently of hover/selection overlays. Media may finish decoding
    // without a document revision, so compositions with imported media are rendered again on
    // every paint, unless the frame is in the cache below.
    const qreal rasterScale = devicePixelRatioF() / previewDivisor_;
    const QSize px(std::max(1, static_cast<int>(std::ceil(width() * rasterScale))),
                   std::max(1, static_cast<int>(std::ceil(height() * rasterScale))));
    const Layer* cropping = cropLayer();
    const FrameKey key{ctx_.revision(), t, px, origin_, zoom_, rasterScale, throughCamera_, orbit_, orbitView_, cropping ? cropping->id : 0};
    // The RAM preview: frames rendered at their own time are kept while nothing else changes
    // (the document, the view, a proxy). While playing, the scene is the frame's (as exported).
    FrameKey look = key;
    look.time = -1.0;
    const uint64_t generation = media::MediaManager::get().generation();
    if (look != cacheLook_ || generation != cacheGeneration_) {
        clearCache();
        cacheLook_ = look;
        cacheGeneration_ = generation;
    }
    const int frameNumber = c.frameAt(t);
    const double sceneTime = ctx_.playing ? c.snapToFrame(t) : t;
    const bool onFrame = std::fabs(sceneTime - c.snapToFrame(sceneTime)) < 1e-6 && !cropping;
    if (const auto hit = cache_.find(frameNumber); onFrame && hit != cache_.end() && (!frameValid_ || key != frameKey_ || !c.media.empty())) {
        frame_ = hit->second;
        frameKey_ = key;
        frameValid_ = true;
    } else if (!frameValid_ || key != frameKey_ || !c.media.empty()) {
        if (frame_.size() != px) frame_ = QImage(px, QImage::Format_ARGB32_Premultiplied);
        frame_.fill(Qt::transparent);
        const SkImageInfo info = qtImageInfo(px.width(), px.height());
        sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, frame_.bits(), static_cast<size_t>(frame_.bytesPerLine()));
        frameValid_ = static_cast<bool>(surface);
        if (surface) {
            const float z = static_cast<float>(rasterScale);
            // Cropping: the layer's whole picture, the part cut away dimmed over it (drawCrop).
            Composition uncropped;
            if (cropping) {
                uncropped = c;
                if (Layer* l = uncropped.findLayer(cropping->id))
                    l->media.cropLeft = l->media.cropTop = l->media.cropRight = l->media.cropBottom = AnimFloat{0.0f};
            }
            render::drawComposition(surface->getCanvas(), cropping ? uncropped : c, sceneTime, render::View{origin_ * z, zoom_ * z, world_}, true,
                                    throughCamera_, orbit_ ? &orbitLens_ : nullptr);
            frameKey_ = key;
            // Kept (shared until the next render writes a new picture), the frames farthest from
            // the playhead going first when the memory for it is full.
            if (onFrame) {
                const size_t budget = static_cast<size_t>(std::max(0, settings().cacheMegabytes / 2)) << 20;
                const size_t bytes = static_cast<size_t>(frame_.sizeInBytes());
                if (bytes <= budget) {
                    cache_[frameNumber] = frame_;
                    cacheBytes_ += bytes;
                    while (cacheBytes_ > budget && cache_.size() > 1) {
                        auto far = cache_.begin();
                        if (std::abs(std::prev(cache_.end())->first - frameNumber) > std::abs(far->first - frameNumber)) far = std::prev(cache_.end());
                        cacheBytes_ -= static_cast<size_t>(far->second.sizeInBytes());
                        cache_.erase(far);
                    }
                    ctx_.cachedFrames.clear();
                    for (const auto& [n, image] : cache_) ctx_.cachedFrames.insert(n);
                }
            }
        }
    }
    const Vec2 compMax = sceneView().toScreen(compSize_);
    if (!orbit_) d.rectFilled(Pt(origin_.x + 4, origin_.y + 6), Pt(compMax.x + 4, compMax.y + 6), theme::shade(90), 2.0f);
    // Upscale only the scene; handles, rulers, guides and HUD retain native resolution.
    p.setRenderHint(QPainter::SmoothPixmapTransform, previewDivisor_ > 1);
    p.drawImage(rect(), frame_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (orbit_) draw3D(p);
    else d.rect(pt(origin_), pt(compMax), theme::kBorder);
    drawGuides(p);
    drawOverlay(p);
    drawRulers(p);
    drawHudBackground(p);

    char label[16];
    std::snprintf(label, sizeof(label), "%.0f%%", zoom_ * 100.0f);
    zoomLevel_->setText(label);
    snapButton_->setActive(ctx_.snapping);
    cameraButton_->setActive(throughCamera_ && !orbit_);
    orbitButton_->setActive(orbit_);
    cameraButton_->setToolTip(throughCamera_ ? tr("Looking through the camera (click for the free view)")
                                             : tr("Free view: camera frames shown (click to look through the camera)"));
    if (textEdit_) placeTextEditor();
}

void ViewportPanel::draw3D(QPainter& p) {
    Draw d(p);
    const Composition& c = ctx_.comp();
    const double t = ctx_.time;
    const render::View view = sceneView();
    auto onScreen = [&](V3 w, Vec2& out) {
        Vec2 q;
        if (!c.project(orbitLens_, w.x, w.y, w.z, q)) return false;
        out = view.toScreen(q);
        return true;
    };
    auto polygon = [&](const V3* pts, int n, Rgba col, float width) {
        for (int i = 0; i < n; ++i) {
            Vec2 a, b;
            if (onScreen(pts[i], a) && onScreen(pts[(i + 1) % n], b)) d.line(pt(a), pt(b), col, width);
        }
    };
    // The composition frame where 2D layers live (Z 0).
    const float W = compSize_.x, H = compSize_.y;
    const V3 frame[4] = {{0, 0, 0}, {W, 0, 0}, {W, H, 0}, {0, H, 0}};
    polygon(frame, 4, theme::kBorder, 1.0f);
    // Each camera: its body, and what it sees at its Z position (where layers show at their size).
    for (const Layer& cam : c.layers) {
        if (cam.type != LayerType::Camera || !cam.activeAt(t)) continue;
        const Lens l = c.lensOf(cam, t);
        V3 right, down, forward;
        axesOf(l, right, down, forward);
        const V3 body{l.x, l.y, l.z};
        const float dist = c.focalLength();
        V3 corners[4];
        for (int i = 0; i < 4; ++i) {
            const float sx = (frame[i].x - l.center.x) / l.focal * dist, sy = (frame[i].y - l.center.y) / l.focal * dist;
            corners[i] = body + right * sx + down * sy + forward * dist;
        }
        const bool selected = ctx_.isSelected(cam.id);
        const Rgba col = selected ? theme::kAccent : theme::withAlpha(theme::kGuide, 230);
        polygon(corners, 4, col, selected ? 1.6f : 1.2f);
        Vec2 b;
        if (!onScreen(body, b)) continue;
        for (const V3& corner : corners)
            if (Vec2 q; onScreen(corner, q)) d.line(pt(b), pt(q), theme::withAlpha(col, 150), 1.0f);
        // The point it looks at, and the body.
        if (Vec2 q; onScreen(body + forward * dist, q)) {
            d.line(pt(b), pt(q), theme::withAlpha(col, 90), 1.0f);
            d.circle(pt(q), 4.0f, col, 1.2f);
        }
        d.rectFilled(Pt(b.x - 7, b.y - 5), Pt(b.x + 7, b.y + 5), theme::withAlpha(theme::kBgDeep, 230), 2.0f);
        d.rect(Pt(b.x - 7, b.y - 5), Pt(b.x + 7, b.y + 5), col, 2.0f, 1.5f);
        d.text(Pt(b.x + 12, b.y - d.textHeight() * 0.5f), col, cam.name);
    }
}

uint64_t ViewportPanel::cameraAt(Vec2 screen) const {
    if (!orbit_) return 0;
    const Composition& c = ctx_.comp();
    for (const Layer& cam : c.layers) {
        if (cam.type != LayerType::Camera || !cam.activeAt(ctx_.time) || cam.locked) continue;
        const Lens l = c.lensOf(cam, ctx_.time);
        Vec2 q;
        if (c.project(orbitLens_, l.x, l.y, l.z, q) && near(screen, sceneView().toScreen(q), 12.0f)) return cam.id;
    }
    return 0;
}

ViewportPanel::Gizmo ViewportPanel::gizmo() const {
    Gizmo g;
    const double t = ctx_.time;
    const Layer* l = ctx_.selection.size() == 1 ? const_cast<EditorContext&>(ctx_).selected() : nullptr;
    if (!l || l->locked || !l->activeAt(t) || l->parent) return g;
    const bool cam = l->type == LayerType::Camera;
    // A 3D layer, through the camera or in the 3D view; a camera, in the 3D view.
    if (cam ? !orbit_ : (!l->threeD || !(throughCamera_ || orbit_))) return g;
    const Composition& c = ctx_.comp();
    const Lens lens = orbit_ ? orbitLens_ : c.lensAt(t);
    V3 origin;
    if (cam) {
        const Lens own = c.lensOf(*l, t);
        origin = {own.x, own.y, own.z};
    } else {
        const Vec2 p = l->transform.position.valueAt(t);
        origin = {p.x, p.y, l->transform.depth.valueAt(t)};
    }
    const render::View view = sceneView();
    Vec2 base;
    if (!c.project(lens, origin.x, origin.y, origin.z, base)) return g;
    g.base = view.toScreen(base);
    constexpr float kLength = 70.0f, kStep = 10.0f;
    const V3 units[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) {
        const V3 w = origin + units[i] * kStep;
        Vec2 q;
        float pxPerUnit = 0.0f;
        Vec2 dir{0.7071f, 0.7071f};
        if (c.project(lens, w.x, w.y, w.z, q)) {
            const Vec2 v = (view.toScreen(q) - g.base);
            pxPerUnit = v.length() / kStep;
            if (pxPerUnit > 0.03f) dir = v / v.length();
        }
        if (pxPerUnit <= 0.03f) { // pointing at the eye (Z, looking straight): drag along a diagonal instead
            dir = i == 2 ? Vec2{0.7071f, -0.7071f} : Vec2{0.7071f, 0.7071f};
            pxPerUnit = 0.5f;
            if (i == 2) dir = dir * -1.0f; // down-left: up-right pushes it away
        }
        g.dir[i] = dir;
        g.tip[i] = g.base + dir * kLength;
        g.perPx[i] = 1.0f / pxPerUnit;
    }
    // Pushed away along Z when the mouse goes along its arrow (it points to larger Z).
    g.valid = true;
    g.layer = l->id;
    return g;
}

int ViewportPanel::gizmoAxis(Vec2 screen) const {
    const Gizmo g = gizmo();
    if (!g.valid) return -1;
    int best = -1;
    float bestDist = 7.0f;
    for (int i = 0; i < 3; ++i) {
        const Vec2 a = g.base + g.dir[i] * 12.0f, b = g.tip[i]; // not the very center: that moves the layer
        const Vec2 ab = b - a, ap = screen - a;
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        const float u = len2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
        const float dist = (screen - (a + ab * u)).length();
        if (dist < bestDist) bestDist = dist, best = i;
    }
    return best;
}

void ViewportPanel::drawGizmo(QPainter& p) {
    const Gizmo g = gizmo();
    if (!g.valid) return;
    Draw d(p);
    const char* names[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        const bool hot = axis_ == i || (drag_ == Drag::None && hoverAxis_ == i);
        const Rgba col = hot ? ui::rgba(255, 255, 255) : kAxisColors[i];
        d.line(pt(g.base), pt(g.tip[i]), col, hot ? 3.0f : 2.2f);
        // The head: a small triangle.
        const Vec2 dir = g.dir[i], side{-dir.y, dir.x};
        const Vec2 tip = g.tip[i] + dir * 8.0f;
        const Pt head[3] = {pt(tip), pt(g.tip[i] + side * 5.0f), pt(g.tip[i] - side * 5.0f)};
        d.polyline(head, 3, col, true, 2.0f);
        d.text(Pt(tip.x + dir.x * 6 - 4, tip.y + dir.y * 6 - d.textHeight() * 0.5f), col, QString(names[i]));
    }
    d.circleFilled(pt(g.base), 3.5f, ui::rgba(255, 255, 255, 220));
    if (axis_ >= 0 && drag_ == Drag::Axis)
        if (const Layer* l = ctx_.comp().findLayer(g.layer)) {
            const Vec2 pos = l->transform.position.valueAt(ctx_.time);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "X %.0f   Y %.0f   Z %.0f", pos.x, pos.y, l->transform.depth.valueAt(ctx_.time));
            const QString s = QString::fromUtf8(buf);
            const Pt at(mouse_.x + 16, mouse_.y + 18);
            d.rectFilled(Pt(at.x - 6, at.y - 3), Pt(at.x + d.textWidth(s) + 6, at.y + d.textHeight() + 3), theme::withAlpha(theme::kBgDeep, 220), 4);
            d.text(at, theme::kText, s);
        }
}

// The camera tools: orbit around the point it looks at, pan across, dolly along its axis.
void ViewportPanel::moveCamera(Vec2 mouse) {
    Layer* cam = ctx_.comp().findLayer(dragLayer_);
    if (!cam) return;
    const double t = ctx_.time;
    const Composition& c = ctx_.comp();
    const float focus = c.focalLength(); // the camera stands this far in front of its Z position
    const Vec2 d = mouse - dragStartScreen_;
    const Lens& l0 = toolStartLens_;
    V3 right, down, forward;
    axesOf(l0, right, down, forward);
    V3 body{l0.x, l0.y, l0.z};
    if (cameraTool_ == CameraTool::Orbit) {
        // Keeps looking at the same point while going around it.
        const V3 target = body + forward * focus;
        const float rx = std::clamp(toolStartRx_ - d.y * 0.25f, -89.0f, 89.0f), ry = toolStartRy_ + d.x * 0.25f;
        Lens turned = l0;
        turned.rx = rx, turned.ry = ry;
        axesOf(turned, right, down, forward);
        body = target - forward * focus;
        cam->transform.rotationX.set(t, rx);
        cam->transform.rotationY.set(t, ry);
    } else if (cameraTool_ == CameraTool::Pan) {
        // The scene follows the mouse at the camera's focus distance.
        const float perPx = focus / l0.focal / zoom_;
        body = body - right * (d.x * perPx) - down * (d.y * perPx);
    } else if (cameraTool_ == CameraTool::Dolly) {
        body = body + forward * (-d.y / zoom_ * 4.0f); // up: forward
    }
    cam->transform.position.set(t, {body.x, body.y});
    cam->transform.depth.set(t, body.z + focus);
}

void ViewportPanel::drawHudBackground(QPainter&) {
    // Nothing: the composition size is in Properties (with nothing selected), not on the canvas.
}

void ViewportPanel::drawMotionPath(QPainter& p) {
    Layer* l = pathLayer();
    if (!l) return;
    Draw d(p);
    const render::View view = parentView(sceneView(), *l, ctx_.time, false); // the plane the path lies in
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
        const uint64_t hover = render::hitTest(comp, t, view.toComp(mouse_), throughCamera_, !throughCamera_ && !orbit_, g_lens);
        Handles h{};
        if (hover && !ctx_.isSelected(hover) && computeHandles(*comp.findLayer(hover), t, view, h)) outline(d, h, theme::accent(110), 1.0f);
    }

    // Free view: show what each camera sees.
    if (!throughCamera_ && !orbit_)
        for (const Layer& l : comp.layers) {
            Handles h{};
            if (l.type != LayerType::Camera || !l.activeAt(t) || !computeHandles(l, t, view, h)) continue;
            outline(d, h, theme::withAlpha(theme::kGuide, 220), 1.5f);
            const Pt at(std::max(h.screen[0].x + 6, kRuler + 6), std::max(h.screen[0].y + 4, kRuler + 4));
            d.text(at, theme::withAlpha(theme::kGuide, 255), l.name); // kept clear of the rulers
        }

    drawMotionPath(p);

    // Selection outlines.
    drawPaths(p);
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
    const bool cameraView = selected.size() == 1 && selected[0]->type == LayerType::Camera && (throughCamera_ || orbit_);
    if (cropLayer()) drawCrop(p); // its handles instead of the transform's
    else if (selected.size() == 1 && selected[0]->activeAt(t) && !cameraView) {
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

    drawGizmo(p);

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
                      : dropKind_ == ui::kDropSubtitles ? makeSubtitleLayer(compSize_, comp.duration)
                      : dropKind_ == ui::kDropCounter ? makeCounterLayer(scenePos, comp.duration)
                      : type                       ? makePluginLayer(type->name, type->prototype, scenePos, comp.duration)
                      : dropKind_ < 0              ? makeTextLayer(tr("Your text").toStdString(), scenePos, comp.duration)
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
            const Projective m = Affine::scale(static_cast<float>(dpr)) * view.matrix() * ghost.matrixAt(t);
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
    if (!ctx_.showGuides || orbit_) return; // guides are on the frame, not in space
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
    if (!ctx_.showRulers || orbit_) return;
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
    return r.mask < static_cast<int>(l->masks.size()) ? &l->masks[r.mask].path : nullptr;
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
    if (l->type == LayerType::Shape && l->shape.kind == ShapeKind::Path) out.push_back({l->id, -1});
    for (int i = 0; i < static_cast<int>(l->masks.size()); ++i) out.push_back({l->id, i});
    return out;
}

bool ViewportPanel::pathPointAt(Vec2 s, PathPoint& out) const {
    for (const PathRef& r : editablePaths()) {
        const BezierPath* path = pathOf(r);
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
    if (mask) {
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
        if (penTarget_.mask >= 0 && path->vertices.size() < 3) l->masks.erase(l->masks.begin() + penTarget_.mask); // nothing to cut with
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
        BezierPath* path = pathOf(r);
        Projective m;
        if (!path || !pathMatrix(r.layer, m)) continue;
        const Vec2 local = m.inverse().apply(mouse);
        const float scale = std::max(1e-4f, m.localScale(local));
        size_t segment = 0;
        float u = 0.0f;
        BezierPath outline = *path;
        if (r.mask >= 0) outline.closed = true; // masks are always closed
        if (nearestSegment(outline, local, 6.0f / scale, segment, u)) {
            path->closed = outline.closed;
            insertVertex(*path, segment, u);
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
        const BezierPath* path = pathOf(r);
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
        else if (ctx_.tool == Tool::Pen && !orbit_)
            shape = Qt::CrossCursor;
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
