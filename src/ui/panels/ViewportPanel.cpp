#include "ui/panels/ViewportPanel.h"

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
    fitButton_ = hud(ui::Icon::FitView, tr("Fit composition to view (Ctrl+0)\nWheel: zoom  ·  Middle/right drag or Space+drag: pan"));
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
    for (const auto& [label, divisor] : {std::pair{TR_NOOP("Full"), 1}, {TR_NOOP("Half"), 2}, {TR_NOOP("Quarter"), 4}}) {
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
    buildToolHud();
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
    if (penDrawing_ && (ctx_.tool != Tool::Pen || !shownPath(penTarget_) || ctx_.selection.size() != 1 || ctx_.primaryId() != penTarget_.layer))
        finishPen();
    // Playing, the picture changes with the frame (not at every tick between two frames).
    const double time = ctx_.playing ? ctx_.comp().snapToFrame(ctx_.time) : ctx_.time;
    Signature s{ctx_.revision(),  time,             ctx_.selection, ctx_.scope,  ctx_.snapping,
                ctx_.showGuides,  ctx_.showRulers,  ctx_.playing,   static_cast<int>(ctx_.tool), media::MediaManager::get().arrivals()};
    if (s == drawn_) return;
    drawn_ = std::move(s);
    update();
}

int ViewportPanel::shownDivisor() const {
    return ctx_.playing && settings().adaptivePlayback ? std::max(previewDivisor_, playbackDivisor_) : previewDivisor_;
}

void ViewportPanel::resizeEvent(QResizeEvent*) { positionHud(); }

void ViewportPanel::positionHud() {
    const Composition& c = ctx_.comp();
    const int y = height() - 26 - 10;
    int x = width() - 12 - (26 * 4 + 70 + 4 * 3 + 12);
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
    positionToolHud(tx, controlsY); // the brush's color and size
}

// --- painting ---------------------------------------------------------------------------------

void ViewportPanel::clearCache() {
    cache_.clear();
    cacheBytes_ = 0;
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
    // without a document revision: a frame that showed a stand-in, or older than the last
    // decoded picture (arrivals), is rendered again; else the same scene is kept (moving the
    // mouse over the viewport redraws only the overlays).
    const int divisor = shownDivisor();
    const qreal rasterScale = devicePixelRatioF() / divisor;
    const QSize px(std::max(1, static_cast<int>(std::ceil(width() * rasterScale))),
                   std::max(1, static_cast<int>(std::ceil(height() * rasterScale))));
    const Layer* cropping = cropLayer();
    // While playing, the scene is the frame's (as exported): drawn again only when the frame
    // changes, not at every tick between two frames.
    const double sceneTime = ctx_.playing ? c.snapToFrame(t) : t;
    const FrameKey key{ctx_.revision(), sceneTime, px, origin_, zoom_, rasterScale, throughCamera_, orbit_, orbitView_, cropping ? cropping->id : 0};
    // The RAM preview: frames rendered at their own time are kept while nothing else changes
    // (the document, the view, a proxy), at each resolution playing and pausing use.
    FrameKey look = key;
    look.time = -1.0;
    look.pixels = size();
    look.scale = devicePixelRatioF();
    const uint64_t generation = media::MediaManager::get().generation();
    if (look != cacheLook_ || generation != cacheGeneration_) {
        clearCache();
        frameValid_ = false;
        cacheLook_ = look;
        cacheGeneration_ = generation;
    }
    const int frameNumber = c.frameAt(t);
    notePlaybackFrame(frameNumber);
    const bool onFrame = std::fabs(sceneTime - c.snapToFrame(sceneTime)) < 1e-6 && !cropping;
    const uint64_t arrivals = media::MediaManager::get().arrivals();
    // (Playing, the decoder works ahead all the time: only a frame that showed a stand-in waits
    // for its picture.)
    const bool mediaChanged = !c.media.empty() && (frameStale_ || (!ctx_.playing && arrivals != frameArrivals_));
    // A kept frame at this resolution, or a sharper one.
    auto hit = cache_.end();
    for (int d = divisor; d >= 1 && hit == cache_.end(); d /= 2) hit = cache_.find({frameNumber, d});
    if (onFrame && hit != cache_.end() && (!frameValid_ || key != frameKey_ || mediaChanged)) {
        frame_ = hit->second;
        frameKey_ = key;
        frameValid_ = true;
        frameStale_ = false;
        frameArrivals_ = arrivals;
    } else if (!frameValid_ || key != frameKey_ || mediaChanged) {
        // Only the composition frame is drawn, read back and kept (layers are clipped to it):
        // around it is the empty canvas, often half of the viewport. The 3D view draws it all.
        QRect region(QPoint(0, 0), px);
        if (!orbit_) {
            const Vec2 a = sceneView().toScreen({0, 0}), b = sceneView().toScreen(compSize_);
            const QRect frameRect(QPoint(static_cast<int>(std::floor(std::min(a.x, b.x) * rasterScale)) - 1,
                                         static_cast<int>(std::floor(std::min(a.y, b.y) * rasterScale)) - 1),
                                  QPoint(static_cast<int>(std::ceil(std::max(a.x, b.x) * rasterScale)) + 1,
                                         static_cast<int>(std::ceil(std::max(a.y, b.y) * rasterScale)) + 1));
            region = region.intersected(frameRect);
        }
        if (region.isEmpty()) region = QRect(0, 0, 1, 1); // the frame is out of sight: a pixel, transparent
        if (frame_.size() != region.size()) frame_ = QImage(region.size(), QImage::Format_ARGB32_Premultiplied);
        frame_.setOffset(region.topLeft());       // where it goes, in raster pixels...
        frame_.setDevicePixelRatio(rasterScale);  // ... of this many per point
        const SkImageInfo info = qtImageInfo(region.width(), region.height());
        // On the graphics card when there is one (render/Gpu.h; its pixels read back into
        // frame_), else straight into frame_.
        if (!render::gpu()) gpuSurface_.reset();
        else if (!gpuSurface_ || gpuSurface_->width() != region.width() || gpuSurface_->height() != region.height())
            gpuSurface_ = render::makeGpuSurface(info);
        const bool onGpu = gpuSurface_ && render::gpu()->makeCurrent();
        if (onGpu) gpuSurface_->getCanvas()->clear(SK_ColorTRANSPARENT);
        else frame_.fill(Qt::transparent); // (read back, every pixel is written anyway)
        sk_sp<SkSurface> surface = onGpu ? gpuSurface_ : SkSurfaces::WrapPixels(info, frame_.bits(), static_cast<size_t>(frame_.bytesPerLine()));
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
            media::MediaManager::get().setPreviewScale(this, zoom_ * z); // videos decoded no bigger than shown here
            media::MediaManager::get().takeStale(); // from here on: did this frame use a stand-in?
            QElapsedTimer timer;
            timer.start();
            render::drawComposition(surface->getCanvas(), cropping ? uncropped : c, sceneTime, render::View{origin_ * z - Vec2{static_cast<float>(region.x()), static_cast<float>(region.y())}, zoom_ * z, world_}, true,
                                    throughCamera_, orbit_ ? &orbitLens_ : nullptr);
            if (onGpu) surface->readPixels(info, frame_.bits(), static_cast<size_t>(frame_.bytesPerLine()), 0, 0);
            const double ms = timer.nsecsElapsed() / 1e6;
            frameKey_ = key;
            const bool stale = media::MediaManager::get().takeStale(); // a video frame still on its way: not kept
            frameStale_ = stale;
            frameArrivals_ = arrivals;
            // Playing: a frame that takes longer than the composition's frame (most of it: the
            // rest of the tick needs time too) lowers the resolution; one that would still fit at
            // four times the pixels raises it back. (The next frame shows the change.)
            if (ctx_.playing && settings().adaptivePlayback && divisor == std::max(previewDivisor_, playbackDivisor_)) {
                renderMs_ = renderMs_ > 0.0 ? renderMs_ * 0.7 + ms * 0.3 : ms;
                const double budget = 1000.0 * c.frameDuration() * 0.7;
                if (renderMs_ > budget && playbackDivisor_ < 4) {
                    playbackDivisor_ *= 2;
                    renderMs_ /= 4.0; // a quarter of the pixels
                } else if (renderMs_ * 4.0 * 1.5 < budget && playbackDivisor_ > 1) {
                    playbackDivisor_ /= 2;
                    renderMs_ *= 4.0;
                }
            }
            // Kept (shared until the next render writes a new picture), the frames farthest from
            // the playhead going first when the memory for it is full.
            if (onFrame && !stale) {
                const size_t budget = static_cast<size_t>(std::max(0, settings().cacheMegabytes / 2)) << 20;
                const size_t bytes = static_cast<size_t>(frame_.sizeInBytes());
                if (bytes <= budget) {
                    if (const auto old = cache_.find({frameNumber, divisor}); old != cache_.end()) cacheBytes_ -= static_cast<size_t>(old->second.sizeInBytes());
                    cache_[{frameNumber, divisor}] = frame_;
                    cacheBytes_ += bytes;
                    while (cacheBytes_ > budget && cache_.size() > 1) {
                        auto far = cache_.begin();
                        if (std::abs(std::prev(cache_.end())->first.first - frameNumber) > std::abs(far->first.first - frameNumber)) far = std::prev(cache_.end());
                        cacheBytes_ -= static_cast<size_t>(far->second.sizeInBytes());
                        cache_.erase(far);
                    }
                }
            }
        }
    }
    const Vec2 compMax = sceneView().toScreen(compSize_);
    if (!orbit_) d.rectFilled(Pt(origin_.x + 4, origin_.y + 6), Pt(compMax.x + 4, compMax.y + 6), theme::shade(90), 2.0f);
    // Upscale only the scene; handles, rulers, guides and HUD retain native resolution.
    if (!frame_.isNull()) { // the frame's part of the view, at the scale it was drawn
        const qreal s = frame_.devicePixelRatio();
        const QRectF target(frame_.offset().x() / s, frame_.offset().y() / s, frame_.width() / s, frame_.height() / s);
        p.setRenderHint(QPainter::SmoothPixmapTransform, s < devicePixelRatioF());
        p.drawImage(target, frame_, QRectF(frame_.rect()));
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    if (orbit_) draw3D(p);
    else d.rect(pt(origin_), pt(compMax), theme::kBorder);
    drawGuides(p);
    drawOverlay(p);
    drawRulers(p);
    drawHudBackground(p);

    char label[16];
    std::snprintf(label, sizeof(label), "%.0f%%", zoom_ * 100.0f);
    zoomLevel_->setText(label);
    qualityButton_->setText(divisor == 1 ? tr("Full") : divisor == 2 ? tr("Half") : tr("Quarter")); // playing: maybe lowered (adaptivePlayback)
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

void ViewportPanel::drawHudBackground(QPainter& p) {
    // (The composition size is in Properties, with nothing selected, not on the canvas.)
    // Playback that cannot keep the composition's frame rate: a quiet note in the corner, as long
    // as it lasts and a moment after (frames shown per second, over the last second).
    const qint64 now = playClock_.isValid() ? playClock_.elapsed() : 0;
    if (!playClock_.isValid() || now > slowUntil_) return;
    Draw d(p);
    const QString text = tr("Playing at %1 of %2 fps").arg(static_cast<int>(std::lround(shownFps_))).arg(static_cast<int>(std::lround(ctx_.comp().fps)));
    const float x = (ctx_.showRulers ? kRuler : 0.0f) + 10.0f, y = (ctx_.showRulers ? kRuler : 0.0f) + 8.0f;
    const float w = d.textWidth(text) + 30.0f, h = d.textHeight() + 8.0f;
    d.rectFilled(Pt(x, y), Pt(x + w, y + h), theme::withAlpha(theme::kBgDeep, 200), h * 0.5f);
    ui::drawIcon(d, ui::Icon::Speed, Pt(x + 12, y + h * 0.5f), 12.0f, theme::kWarning);
    d.text(Pt(x + 22, y + 4), theme::kTextDim, text);
}

void ViewportPanel::notePlaybackFrame(int frame) {
    if (!playClock_.isValid()) playClock_.start();
    const qint64 now = playClock_.elapsed();
    if (!ctx_.playing) {
        shownFrames_.clear();
        lastShownFrame_ = -1;
        return;
    }
    if (frame == lastShownFrame_) return;
    lastShownFrame_ = frame;
    shownFrames_.push_back(now);
    while (!shownFrames_.empty() && now - shownFrames_.front() > 1000) shownFrames_.pop_front();
    if (shownFrames_.size() < 2 || now - shownFrames_.front() < 700) return; // a second's worth first
    shownFps_ = (shownFrames_.size() - 1) * 1000.0 / static_cast<double>(now - shownFrames_.front());
    if (shownFps_ < ctx_.comp().fps * 0.85) slowUntil_ = now + 2500; // shown a moment after it recovers
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
    drawToolOverlay(p); // pins, the warp grid, the brush
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

} // namespace bemotion
