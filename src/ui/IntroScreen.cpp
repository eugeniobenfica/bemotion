#include "ui/IntroScreen.h"

#include "core/Serialization.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "render/SceneRenderer.h"
#include "ui/SkiaQt.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace bemotion {

namespace {

constexpr double kRevealSeconds = 0.35; // the home screen fading in afterwards
constexpr double kSkipToEnd = 0.45;     // a skip jumps to the project's own fade-out

} // namespace

IntroScreen::IntroScreen(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &IntroScreen::step);
}

bool IntroScreen::start(const std::filesystem::path& project) {
    if (!loadProject(project, project_)) return false;
    project_.comp.fitDuration();
    time_ = reveal_ = 0.0;
    active_ = true;
    clock_.start();
    timer_->start(16);
    show();
    raise();
    setFocus();
    return true;
}

void IntroScreen::skip() { time_ = std::max(time_, project_.comp.duration - kSkipToEnd); }

void IntroScreen::step() {
    // A slow first frame must not skip the start.
    const double dt = std::min(clock_.restart() / 1000.0, 1.0 / 20.0);
    if (time_ < project_.comp.duration) time_ += dt;
    else reveal_ += dt;
    // Its sound (an ordinary audio layer of the project): the picture follows the sound's clock,
    // as the editor's playback does.
    const bool playing = time_ < project_.comp.duration;
    media::MediaManager::get().update(project_.comp);
    if (double audio = 0.0; playing && media::AudioEngine::get().clock(audio) && std::fabs(audio - time_) < 0.25) time_ = audio;
    media::AudioEngine::get().update(project_.comp, time_, playing);
    if (reveal_ >= kRevealSeconds) {
        active_ = false;
        timer_->stop();
        media::AudioEngine::get().update(Composition{}, 0.0, false);
        hide();
        emit finished();
        return;
    }
    update();
}

void IntroScreen::paintEvent(QPaintEvent*) {
    if (!active_) return;
    const Composition& comp = project_.comp;
    QPainter p(this);
    const float fade = static_cast<float>(1.0 - reveal_ / kRevealSeconds);
    const Color& bg = comp.background;
    p.fillRect(rect(), QColor::fromRgbF(bg.r, bg.g, bg.b, bg.a * fade));
    if (time_ >= comp.duration) return;
    // The whole composition, as large as fits, at device resolution.
    const qreal dpr = devicePixelRatioF();
    const QSize px(static_cast<int>(std::ceil(width() * dpr)), static_cast<int>(std::ceil(height() * dpr)));
    if (frame_.size() != px) frame_ = QImage(px, QImage::Format_ARGB32_Premultiplied);
    frame_.fill(Qt::transparent);
    const SkImageInfo info = qtImageInfo(px.width(), px.height());
    if (sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, frame_.bits(), static_cast<size_t>(frame_.bytesPerLine()))) {
        const float zoom = std::min(static_cast<float>(px.width()) / comp.width, static_cast<float>(px.height()) / comp.height);
        const Vec2 origin{(px.width() - comp.width * zoom) * 0.5f, (px.height() - comp.height * zoom) * 0.5f};
        render::drawComposition(surface->getCanvas(), comp, time_, render::View{origin, zoom, {}}, false);
    }
    frame_.setDevicePixelRatio(dpr);
    p.drawImage(QPointF(0, 0), frame_);
}

} // namespace bemotion
