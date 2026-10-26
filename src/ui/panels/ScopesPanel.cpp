#include "ui/panels/ScopesPanel.h"

#include "media/MediaManager.h"
#include "render/SceneRenderer.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/Language.h"
#include "ui/Theme.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bemotion {

namespace {

constexpr int kScopeWidth = 384;   // the frame is rendered this wide for the scopes
constexpr int kRefreshMs = 150;    // at most this often while playing or dragging
// The channels' own colors (what they mean, not the theme's).
constexpr ui::Rgba kChannel[3] = {ui::rgba(235, 87, 87), ui::rgba(80, 200, 120), ui::rgba(86, 145, 240)};

float cbOf(float r, float g, float b) { return -0.1687f * r - 0.3313f * g + 0.5f * b; }
float crOf(float r, float g, float b) { return 0.5f * r - 0.4187f * g - 0.0813f * b; }

// Counts as a trace: a few hits already show, many saturate (like a phosphor scope).
QImage traceImage(const std::vector<uint32_t>& counts, int w, int h, ui::Rgba color, float gain) {
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float k = 1.0f - std::exp(-static_cast<float>(counts[static_cast<size_t>(y) * w + x]) * gain);
            line[x] = qRgba(ui::red(color), ui::green(color), ui::blue(color), static_cast<int>(k * 255.0f));
        }
    }
    return img;
}

} // namespace

ScopesPanel::ScopesPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(8, 8, 8, 8);
    auto* top = new QHBoxLayout;
    mode_ = new QComboBox;
    for (const char* name : {TR_NOOP("Waveform"), TR_NOOP("RGB Parade"), TR_NOOP("Vectorscope"), TR_NOOP("Histogram")}) mode_->addItem(tr(name));
    mode_->setToolTip(tr("Waveform: brightness across the frame. RGB Parade: each channel. Vectorscope: hue and "
                         "saturation. Histogram: how many pixels at each level."));
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { update(); });
    top->addWidget(mode_);
    top->addStretch();
    col->addLayout(top);
    col->addStretch();
    later_ = new QTimer(this);
    later_->setSingleShot(true);
    connect(later_, &QTimer::timeout, this, [this] {
        renderFrame();
        update();
    });
    setMinimumSize(200, 180);
}

void ScopesPanel::sync() {
    if (!isVisible()) return;
    if (valid_ && ctx_.revision() == revision_ && ctx_.time == time_ && media::MediaManager::get().arrivals() == arrivals_) return;
    if (!sinceRender_.isValid() || sinceRender_.elapsed() >= kRefreshMs) {
        renderFrame();
        update();
    } else if (!later_->isActive()) {
        later_->start(kRefreshMs);
    }
}

void ScopesPanel::renderFrame() {
    const Composition& c = ctx_.comp();
    const double t = ctx_.time;
    const float scale = std::min(1.0f, static_cast<float>(kScopeWidth) / std::max(1, c.width));
    width_ = std::max(1, static_cast<int>(std::lround(c.width * scale)));
    height_ = std::max(1, static_cast<int>(std::lround(c.height * scale)));
    rgba_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
    const SkImageInfo info = SkImageInfo::Make(width_, height_, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    if (sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, rgba_.data(), static_cast<size_t>(width_) * 4))
        render::drawComposition(surface->getCanvas(), c, t, render::View{{0, 0}, scale, c.cameraAt(t)}, true);
    revision_ = ctx_.revision();
    time_ = t;
    arrivals_ = media::MediaManager::get().arrivals();
    valid_ = true;
    sinceRender_.restart();
}

QRectF ScopesPanel::scopeRect() const {
    const QRectF all(8, mode_->geometry().bottom() + 8, width() - 16, height() - mode_->geometry().bottom() - 16);
    if (static_cast<Mode>(mode_->currentIndex()) != Mode::Vectorscope) return all;
    const qreal side = std::min(all.width(), all.height());
    return {all.center().x() - side / 2, all.top(), side, side};
}

void ScopesPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    ui::Draw d(p);
    const QRectF r = scopeRect();
    if (r.width() < 20 || r.height() < 20) return;
    const Mode mode = static_cast<Mode>(mode_->currentIndex());
    const ui::Pt a(r.topLeft()), b(r.bottomRight());
    d.rectFilled(a, b, theme::kField, 3.0f);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const int n = width_ * height_;
    const bool have = valid_ && n > 0;

    // Levels 0..100% across the trace (waveform, parade).
    auto levels = [&](const QRectF& area) {
        for (int pct = 0; pct <= 100; pct += 25) {
            const float y = static_cast<float>(area.bottom() - area.height() * pct / 100.0);
            d.line(ui::Pt(static_cast<float>(area.left()), y), ui::Pt(static_cast<float>(area.right()), y), theme::kBorder);
            d.text(ui::Pt(static_cast<float>(area.left()) + 3, y - d.textHeight() - 1), theme::kTextFaint, QString::number(pct));
        }
    };

    if (mode == Mode::Waveform || mode == Mode::Parade) {
        const QRectF area = r.adjusted(4, 6, -4, -4);
        levels(area);
        if (have) {
            const int channels = mode == Mode::Parade ? 3 : 1;
            for (int ch = 0; ch < channels; ++ch) {
                std::vector<uint32_t> counts(static_cast<size_t>(width_) * 256, 0);
                for (int y = 0; y < height_; ++y)
                    for (int x = 0; x < width_; ++x) {
                        const uint8_t* px = &rgba_[(static_cast<size_t>(y) * width_ + x) * 4];
                        const int v = channels == 3 ? px[ch]
                                                    : std::clamp(static_cast<int>(0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2] + 0.5f), 0, 255);
                        ++counts[static_cast<size_t>(255 - v) * width_ + x];
                    }
                const float gain = 24.0f / std::max(1, height_);
                const QImage img = traceImage(counts, width_, 256, channels == 3 ? kChannel[ch] : theme::kText, gain);
                const qreal w = area.width() / channels;
                p.drawImage(QRectF(area.left() + w * ch, area.top(), w, area.height()), img);
            }
        }
    } else if (mode == Mode::Histogram) {
        const QRectF area = r.adjusted(4, 6, -4, -4);
        if (have) {
            std::vector<uint32_t> bins(4 * 256, 0);
            for (int i = 0; i < n; ++i) {
                const uint8_t* px = &rgba_[static_cast<size_t>(i) * 4];
                for (int ch = 0; ch < 3; ++ch) ++bins[ch * 256 + px[ch]];
                ++bins[3 * 256 + std::clamp(static_cast<int>(0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2] + 0.5f), 0, 255)];
            }
            // Scaled to the tallest bar away from the very ends (pure black / white would flatten the rest).
            uint32_t top = 1;
            for (int ch = 0; ch < 4; ++ch)
                for (int i = 1; i < 255; ++i) top = std::max(top, bins[ch * 256 + i]);
            auto path = [&](int ch) {
                QPainterPath shape;
                shape.moveTo(area.bottomLeft());
                for (int i = 0; i < 256; ++i) {
                    const qreal h = std::min(1.0, std::sqrt(static_cast<double>(bins[ch * 256 + i]) / top));
                    shape.lineTo(area.left() + area.width() * i / 255.0, area.bottom() - area.height() * h);
                }
                shape.lineTo(area.bottomRight());
                shape.closeSubpath();
                return shape;
            };
            for (int ch = 0; ch < 3; ++ch) {
                QColor c = theme::q(kChannel[ch]);
                c.setAlpha(70);
                p.fillPath(path(ch), c);
            }
            p.setPen(QPen(theme::q(theme::kText), 1.2));
            p.drawPath(path(3));
        }
        for (int pct = 0; pct <= 100; pct += 25) {
            const float x = static_cast<float>(area.left() + area.width() * pct / 100.0);
            d.line(ui::Pt(x, static_cast<float>(area.top())), ui::Pt(x, static_cast<float>(area.bottom())), theme::kBorder);
        }
    } else { // vectorscope
        const ui::Pt c(r.center());
        const float radius = static_cast<float>(r.width()) * 0.46f;
        d.circle(c, radius, theme::kBorder);
        d.circle(c, radius * 0.5f, theme::kBorder);
        d.line(ui::Pt(c.x - radius, c.y), ui::Pt(c.x + radius, c.y), theme::kBorder);
        d.line(ui::Pt(c.x, c.y - radius), ui::Pt(c.x, c.y + radius), theme::kBorder);
        // Full saturation (|Cb|, |Cr| up to 0.5) reaches the outer circle.
        auto at = [&](float cb, float cr) { return ui::Pt(c.x + cb * 2.0f * radius, c.y - cr * 2.0f * radius); };
        // The skin tone line, through a typical skin color.
        {
            const float sr = 0.86f, sg = 0.62f, sb = 0.5f;
            const float cb = cbOf(sr, sg, sb), cr = crOf(sr, sg, sb), len = std::hypot(cb, cr);
            d.line(c, at(cb / len * 0.5f, cr / len * 0.5f), theme::kTextFaint);
        }
        // The 75% color bars' targets.
        const struct {
            const char* name;
            float r, g, b;
        } targets[] = {{"R", 0.75f, 0, 0}, {"Mg", 0.75f, 0, 0.75f}, {"B", 0, 0, 0.75f},
                       {"Cy", 0, 0.75f, 0.75f}, {"G", 0, 0.75f, 0}, {"Yl", 0.75f, 0.75f, 0}};
        for (const auto& tg : targets) {
            const ui::Pt q = at(cbOf(tg.r, tg.g, tg.b), crOf(tg.r, tg.g, tg.b));
            d.rect(q - ui::Pt(4, 4), q + ui::Pt(4, 4), theme::kTextDim);
            d.text(q + ui::Pt(6, -d.textHeight() * 0.5f), theme::kTextFaint, QString(tg.name));
        }
        if (have) {
            constexpr int kSize = 256;
            std::vector<uint32_t> counts(kSize * kSize, 0);
            for (int i = 0; i < n; ++i) {
                const uint8_t* px = &rgba_[static_cast<size_t>(i) * 4];
                const float rr = px[0] / 255.0f, gg = px[1] / 255.0f, bb = px[2] / 255.0f;
                const int x = std::clamp(static_cast<int>((cbOf(rr, gg, bb) + 0.5f) * (kSize - 1) + 0.5f), 0, kSize - 1);
                const int y = std::clamp(static_cast<int>((0.5f - crOf(rr, gg, bb)) * (kSize - 1) + 0.5f), 0, kSize - 1);
                ++counts[static_cast<size_t>(y) * kSize + x];
            }
            const QImage img = traceImage(counts, kSize, kSize, theme::kText, 0.6f);
            p.drawImage(QRectF(c.x - radius, c.y - radius, radius * 2, radius * 2), img);
        }
    }
    d.rect(a, b, theme::kFieldBorder, 3.0f);
}

} // namespace bemotion
