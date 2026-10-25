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
constexpr int kBarHeight = 72;
constexpr qint64 kBarMs = 2200;  // how long the bar stays after the mouse stops
constexpr float kButton = 18.0f; // a round button's radius
} // namespace

PreviewWindow::PreviewWindow(EditorContext& ctx, QWidget* parent) : QWidget(parent, Qt::Window), ctx_(ctx) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Preview");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    sinceMouse_.start();
}

bool PreviewWindow::barShown() const { return !ctx_.playing || sinceMouse_.elapsed() < kBarMs; }

// The bar, left to right: step back, play, step forward, the time, the progress, the length,
// loop, exit.
QPointF PreviewWindow::partCenter(Part part) const {
    const double mid = height() - kBarHeight * 0.5;
    switch (part) {
    case Part::StepBack: return {36, mid};
    case Part::Play: return {78, mid};
    case Part::StepForward: return {120, mid};
    case Part::Loop: return {width() - 78.0, mid};
    case Part::Exit: return {width() - 36.0, mid};
    default: return {};
    }
}

QRectF PreviewWindow::barTrack() const { return QRectF(262, height() - kBarHeight * 0.5 - 2, std::max(40, width() - 524), 4); }

PreviewWindow::Part PreviewWindow::partAt(QPointF pos) const {
    if (pos.y() < height() - kBarHeight) return Part::None;
    for (Part part : {Part::StepBack, Part::Play, Part::StepForward, Part::Loop, Part::Exit}) {
        const QPointF d = pos - partCenter(part);
        if (d.x() * d.x() + d.y() * d.y() <= (kButton + 3) * (kButton + 3)) return part;
    }
    const QRectF track = barTrack();
    if (pos.x() >= track.left() - 8 && pos.x() <= track.right() + 8) return Part::Track;
    return Part::None;
}

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

    // The bar: buttons, the time, the progress (click or drag to seek), the length.
    const int fade = static_cast<int>(kBarHeight * 1.6f);
    const float top = static_cast<float>(height() - fade);
    for (int i = 0; i < fade; ++i) // fades up from the bottom
        d.rectFilled(Pt(0, top + i), Pt(width(), top + i + 1), rgba(0, 0, 0, static_cast<int>(210.0f * i / fade)));
    const float mid = height() - kBarHeight * 0.5f;
    auto button = [&](Part part, Icon icon, float size, bool on = false) {
        const QPointF at = partCenter(part);
        const Pt center(static_cast<float>(at.x()), static_cast<float>(at.y()));
        if (part == Part::Play) d.circleFilled(center, kButton + 2, hover_ == part ? theme::kAccentHover : theme::kAccent);
        else if (hover_ == part) d.circleFilled(center, kButton, rgba(255, 255, 255, 34));
        const Rgba color = part == Part::Play ? theme::kOnAccent : on ? theme::kAccent : hover_ == part ? theme::kText : theme::kTextDim;
        drawIcon(d, icon, center, size, color);
    };
    button(Part::StepBack, Icon::StepBack, 16);
    button(Part::Play, ctx_.playing ? Icon::Pause : Icon::Play, 18);
    button(Part::StepForward, Icon::StepForward, 16);
    button(Part::Loop, Icon::Loop, 16, ctx_.loop);
    button(Part::Exit, Icon::ExitFullscreen, 16);
    p.setFont(font());
    const QString now = QString::fromStdString(formatTimecode(t, c.fps));
    d.text(Pt(158, mid - d.textHeight() * 0.5f), theme::kText, now);
    const QRectF track = barTrack();
    const double end = std::max(1e-6, c.duration);
    const float done = static_cast<float>(std::clamp(t / end, 0.0, 1.0));
    const bool active = hover_ == Part::Track || seeking_;
    const float grow = active ? 1.5f : 0.0f; // the line thickens under the mouse
    const float l = static_cast<float>(track.left()), r = static_cast<float>(track.right());
    const float y0 = static_cast<float>(track.top()) - grow, y1 = static_cast<float>(track.bottom()) + grow;
    d.rectFilled(Pt(l, y0), Pt(r, y1), rgba(255, 255, 255, 60), 3.0f);
    d.rectFilled(Pt(l, y0), Pt(l + (r - l) * done, y1), theme::kAccent, 3.0f);
    d.circleFilled(Pt(l + (r - l) * done, static_cast<float>(track.center().y())), active ? 8.0f : 6.0f, theme::kAccent);
    const QString total = QString::fromStdString(formatTimecode(c.duration, c.fps));
    d.text(Pt(r + 16, mid - d.textHeight() * 0.5f), theme::kTextDim, total);
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
    case Qt::Key_L:
        ctx_.loop = !ctx_.loop;
        update();
        return;
    case Qt::Key_Home: ctx_.setTime(0.0); return;
    case Qt::Key_End: ctx_.setTime(ctx_.comp().timeOfFrame(ctx_.comp().frameCount() - 1)); return;
    default: QWidget::keyPressEvent(e);
    }
}

void PreviewWindow::mouseMoveEvent(QMouseEvent* e) {
    wake();
    if (seeking_) seek(e->position().x());
    const Part part = seeking_ ? Part::Track : partAt(e->position());
    if (part != hover_) {
        hover_ = part;
        update();
    }
    if (part != Part::None) setCursor(Qt::PointingHandCursor);
}

void PreviewWindow::mousePressEvent(QMouseEvent* e) {
    wake();
    if (e->button() != Qt::LeftButton) return;
    switch (partAt(e->position())) {
    case Part::StepBack:
        ctx_.playing = false;
        ctx_.stepFrames(-1);
        break;
    case Part::StepForward:
        ctx_.playing = false;
        ctx_.stepFrames(1);
        break;
    case Part::Play:
        if (!ctx_.playing && ctx_.time >= ctx_.comp().timeOfFrame(ctx_.comp().frameCount() - 1) - 1e-6) ctx_.setTime(0.0);
        ctx_.playing = !ctx_.playing;
        break;
    case Part::Track:
        seeking_ = true;
        ctx_.playing = false;
        seek(e->position().x());
        break;
    case Part::Loop: ctx_.loop = !ctx_.loop; break;
    case Part::Exit: close(); return;
    case Part::None:
        if (e->position().y() < height() - kBarHeight) ctx_.playing = !ctx_.playing; // the picture: play / pause
        break;
    }
    update();
}

void PreviewWindow::mouseReleaseEvent(QMouseEvent*) {
    seeking_ = false;
    update();
}

void PreviewWindow::leaveEvent(QEvent*) {
    hover_ = Part::None;
    update();
}

void PreviewWindow::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->position().y() < height() - kBarHeight) close();
    else mousePressEvent(e); // quick clicks on the buttons each count
}

void PreviewWindow::closeEvent(QCloseEvent* e) {
    ctx_.playing = false; // back to editing where the preview stopped
    emit closed();
    QWidget::closeEvent(e);
}

} // namespace bemotion::ui
