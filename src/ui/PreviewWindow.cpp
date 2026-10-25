#include "ui/PreviewWindow.h"

#include "render/SceneRenderer.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/SkiaQt.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace bemotion::ui {

namespace {
constexpr int kBarHeight = 64;
constexpr qint64 kBarMs = 2200; // how long the bar stays after the mouse stops
} // namespace

PreviewWindow::PreviewWindow(EditorContext& ctx, QWidget* parent) : QWidget(parent, Qt::Window), ctx_(ctx) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Preview");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    sinceMouse_.start();
}

bool PreviewWindow::barShown() const { return !ctx_.playing || sinceMouse_.elapsed() < kBarMs; }

QRectF PreviewWindow::barTrack() const { return QRectF(150, height() - kBarHeight * 0.5 - 2, std::max(40, width() - 300), 4); }

void PreviewWindow::wake() {
    sinceMouse_.restart();
    unsetCursor();
    update();
}

void PreviewWindow::sync() {
    const bool bar = barShown();
    if (!bar && cursor().shape() != Qt::BlankCursor) setCursor(Qt::BlankCursor);
    if (ctx_.revision() == drawnRevision_ && ctx_.time == drawnTime_ && bar == drawnBar_ && ctx_.comp().media.empty()) return;
    update();
}

void PreviewWindow::paintEvent(QPaintEvent*) {
    const Composition& c = ctx_.comp();
    const double t = ctx_.time;
    QPainter p(this);
    Draw d(p);
    d.rectFilled(Pt(0, 0), Pt(width(), height()), rgba(0, 0, 0)); // a screen, not the editor's canvas

    // The frame, as large as the screen allows, through the camera (as exported).
    const float zoom = std::min(width() / static_cast<float>(c.width), height() / static_cast<float>(c.height));
    const Vec2 size{c.width * zoom, c.height * zoom};
    const Vec2 origin{(width() - size.x) * 0.5f, (height() - size.y) * 0.5f};
    const qreal scale = devicePixelRatioF();
    const QSize px(std::max(1, static_cast<int>(std::ceil(width() * scale))), std::max(1, static_cast<int>(std::ceil(height() * scale))));
    if (frame_.size() != px) frame_ = QImage(px, QImage::Format_ARGB32_Premultiplied);
    frame_.fill(Qt::transparent);
    const SkImageInfo info = qtImageInfo(px.width(), px.height());
    if (sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, frame_.bits(), static_cast<size_t>(frame_.bytesPerLine()))) {
        const float z = static_cast<float>(scale);
        SkCanvas* canvas = surface->getCanvas();
        canvas->clipRect(SkRect::MakeXYWH(origin.x * z, origin.y * z, size.x * z, size.y * z)); // nothing past the frame
        render::drawComposition(canvas, c, t, render::View{origin * z, zoom * z, c.cameraAt(t)}, true, true, nullptr);
    }
    frame_.setDevicePixelRatio(scale);
    p.drawImage(QPointF(0, 0), frame_);
    drawnRevision_ = ctx_.revision();
    drawnTime_ = t;
    drawnBar_ = barShown();
    if (!drawnBar_) return;

    // The bar: play / pause, the time, the progress (click to seek), how to leave.
    const float top = static_cast<float>(height() - kBarHeight);
    for (int i = 0; i < kBarHeight; ++i) // fades up from the bottom
        d.rectFilled(Pt(0, top + i), Pt(width(), top + i + 1), rgba(0, 0, 0, static_cast<int>(200.0f * i / kBarHeight)));
    const float mid = height() - kBarHeight * 0.5f;
    drawIcon(d, ctx_.playing ? Icon::Pause : Icon::Play, Pt(40, mid), 20, theme::kText);
    const QString now = QString::fromStdString(formatTimecode(t, c.fps));
    p.setFont(font());
    d.text(Pt(66, mid - d.textHeight() * 0.5f), theme::kText, now);
    const QRectF track = barTrack();
    const double end = std::max(1e-6, c.duration);
    const float done = static_cast<float>(std::clamp(t / end, 0.0, 1.0));
    d.rectFilled(Pt(track.left(), track.top()), Pt(track.right(), track.bottom()), rgba(255, 255, 255, 60), 2.0f);
    d.rectFilled(Pt(track.left(), track.top()), Pt(track.left() + track.width() * done, track.bottom()), theme::kAccent, 2.0f);
    d.circleFilled(Pt(track.left() + track.width() * done, track.center().y()), 6.0f, theme::kAccent);
    const QString hint = "Esc";
    d.text(Pt(width() - 24 - d.textWidth(hint), mid - d.textHeight() * 0.5f), theme::kTextDim, hint);
    const QString total = QString::fromStdString(formatTimecode(c.duration, c.fps));
    d.text(Pt(track.right() + 14, mid - d.textHeight() * 0.5f), theme::kTextDim, total);
}

void PreviewWindow::seek(double x) {
    const QRectF track = barTrack();
    const double u = std::clamp((x - track.left()) / track.width(), 0.0, 1.0);
    ctx_.setTime(u * ctx_.comp().duration);
}

void PreviewWindow::keyPressEvent(QKeyEvent* e) {
    wake();
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Escape:
    case Qt::Key_F11: close(); return;
    case Qt::Key_Space:
    case Qt::Key_K:
        if (!e->isAutoRepeat()) {
            // At the end, playing starts over.
            if (!ctx_.playing && ctx_.time >= ctx_.comp().timeOfFrame(ctx_.comp().frameCount() - 1) - 1e-6) ctx_.setTime(0.0);
            ctx_.playing = !ctx_.playing;
        }
        return;
    case Qt::Key_Left:
        ctx_.playing = false;
        ctx_.stepFrames(shift ? -10 : -1);
        return;
    case Qt::Key_Right:
        ctx_.playing = false;
        ctx_.stepFrames(shift ? 10 : 1);
        return;
    case Qt::Key_Home: ctx_.setTime(0.0); return;
    case Qt::Key_End: ctx_.setTime(ctx_.comp().timeOfFrame(ctx_.comp().frameCount() - 1)); return;
    default: QWidget::keyPressEvent(e);
    }
}

void PreviewWindow::mouseMoveEvent(QMouseEvent* e) {
    wake();
    if (e->buttons() & Qt::LeftButton && e->position().y() > height() - kBarHeight) seek(e->position().x());
}

void PreviewWindow::mousePressEvent(QMouseEvent* e) {
    wake();
    if (e->button() != Qt::LeftButton) return;
    if (e->position().y() > height() - kBarHeight) seek(e->position().x()); // the bar: seek
    else ctx_.playing = !ctx_.playing;                                     // the picture: play / pause
}

void PreviewWindow::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->position().y() <= height() - kBarHeight) close();
}

void PreviewWindow::closeEvent(QCloseEvent* e) {
    ctx_.playing = false; // back to editing where the preview stopped
    emit closed();
    QWidget::closeEvent(e);
}

} // namespace bemotion::ui
