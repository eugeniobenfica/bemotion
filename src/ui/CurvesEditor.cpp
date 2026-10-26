#include "ui/CurvesEditor.h"

#include "core/Effects.h"
#include "ui/Draw.h"
#include "ui/Language.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace bemotion::ui {

namespace {
// The channels' own colors (what they mean, not the theme's): the RGB curve uses the text color.
constexpr Rgba kChannelColors[] = {0, rgba(235, 87, 87), rgba(80, 200, 120), rgba(86, 145, 240)};
constexpr const char* kChannelNames[] = {TR_NOOP("RGB"), TR_NOOP("Red"), TR_NOOP("Green"), TR_NOOP("Blue")};
constexpr float kHit = 7.0f;
} // namespace

CurvesEditor::CurvesEditor(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(160, 160 + kTabs + 8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void CurvesEditor::setCurves(const Curves& curves) {
    if (drag_ >= 0 || curves == curves_) return;
    curves_ = curves;
    update();
}

QRectF CurvesEditor::tabRect(int i) const {
    const float w = std::min<float>(56.0f, (width() - 8.0f) / 4.0f);
    return {4.0 + i * w, 2.0, w - 4.0, kTabs - 6.0};
}

QRectF CurvesEditor::plot() const {
    const float side = std::min<float>(width() - 8.0f, height() - kTabs - 8.0f);
    return {4.0, kTabs + 2.0, side, side};
}

QPointF CurvesEditor::toScreen(Vec2 p) const {
    const QRectF r = plot();
    return {r.left() + p.x * r.width(), r.bottom() - p.y * r.height()};
}

Vec2 CurvesEditor::toCurve(QPointF p) const {
    const QRectF r = plot();
    return {static_cast<float>((p.x() - r.left()) / r.width()), static_cast<float>((r.bottom() - p.y()) / r.height())};
}

int CurvesEditor::pointAt(QPointF p) const {
    const auto& pts = curves_[channel_];
    int best = -1;
    float bestD = kHit;
    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        const QPointF s = toScreen(pts[i]);
        const float d = static_cast<float>(std::hypot(s.x() - p.x(), s.y() - p.y()));
        if (d <= bestD) bestD = d, best = i;
    }
    return best;
}

void CurvesEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    // Tabs.
    for (int i = 0; i < 4; ++i) {
        const QRectF r = tabRect(i);
        const bool on = i == channel_;
        d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), on ? theme::kAccentSoft : theme::kButton, 4.0f);
        const bool edited = !curveIsIdentity(curves_[i]);
        const QString label = tr(kChannelNames[i]) + (edited ? QStringLiteral(" •") : QString());
        p.setPen(theme::q(on ? theme::kText : theme::kTextDim));
        p.drawText(r, Qt::AlignCenter, label);
    }
    // The square, a grid in quarters and the straight line (no change).
    const QRectF r = plot();
    d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), theme::kField, 3.0f);
    for (int i = 1; i < 4; ++i) {
        const float x = static_cast<float>(r.left() + r.width() * i / 4.0), y = static_cast<float>(r.top() + r.height() * i / 4.0);
        d.line(Pt(x, static_cast<float>(r.top())), Pt(x, static_cast<float>(r.bottom())), theme::kBorder);
        d.line(Pt(static_cast<float>(r.left()), y), Pt(static_cast<float>(r.right()), y), theme::kBorder);
    }
    d.line(Pt(toScreen({0, 0})), Pt(toScreen({1, 1})), theme::kTextFaint);
    d.rect(Pt(r.topLeft()), Pt(r.bottomRight()), theme::kFieldBorder, 3.0f);
    // The other curves faintly, the one being edited on top with its points.
    auto colorOf = [](int c) { return c == 0 ? theme::kText : kChannelColors[c]; };
    auto drawCurve = [&](int c, bool active) {
        std::vector<Pt> line;
        const int steps = std::max(32, static_cast<int>(r.width() / 2));
        for (int s = 0; s <= steps; ++s) {
            const float x = static_cast<float>(s) / steps;
            line.push_back(Pt(toScreen({x, curveValue(curves_[c], x)})));
        }
        Rgba col = colorOf(c);
        if (!active) col = (col & 0x00FFFFFFu) | (90u << 24);
        d.polyline(line.data(), static_cast<int>(line.size()), col, false, active ? 2.0f : 1.0f);
    };
    for (int c = 0; c < 4; ++c)
        if (c != channel_ && !curveIsIdentity(curves_[c])) drawCurve(c, false);
    drawCurve(channel_, true);
    const auto& pts = curves_[channel_];
    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        const Pt s(toScreen(pts[i]));
        const bool hot = i == drag_;
        if (hot && dragOff_) continue; // about to go
        d.rectFilled(s - Pt(4, 4), s + Pt(4, 4), hot ? theme::kAccent : theme::kBgPanel, 1.0f);
        d.rect(s - Pt(4, 4), s + Pt(4, 4), colorOf(channel_), 1.0f, 1.5f);
    }
}

void CurvesEditor::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    for (int i = 0; i < 4; ++i)
        if (tabRect(i).contains(e->position())) {
            channel_ = i;
            update();
            return;
        }
    if (!plot().adjusted(-kHit, -kHit, kHit, kHit).contains(e->position())) return;
    auto& pts = curves_[channel_];
    drag_ = pointAt(e->position());
    if (drag_ < 0) { // a new point on the curve, under the mouse's x
        if (static_cast<int>(pts.size()) >= kMaxCurvePoints) return;
        if (pts.empty()) pts = {{0, 0}, {1, 1}};
        Vec2 at = toCurve(e->position());
        at = {clamp01(at.x), clamp01(at.y)};
        pts.push_back(at);
        cleanCurve(pts);
        for (int i = 0; i < static_cast<int>(pts.size()); ++i)
            if (std::fabs(pts[i].x - at.x) < 1e-4f) drag_ = i;
        emit changed();
    }
    dragOff_ = false;
    update();
}

void CurvesEditor::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0) {
        setCursor(pointAt(e->position()) >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
        return;
    }
    auto& pts = curves_[channel_];
    if (drag_ >= static_cast<int>(pts.size())) return;
    const Vec2 at = toCurve(e->position());
    // Off the square (and not an end point): it will be removed on release.
    const QRectF r = plot().adjusted(-24, -24, 24, 24);
    dragOff_ = pts.size() > 2 && !r.contains(e->position());
    // Between its neighbors, so the points keep their order.
    const float lo = drag_ > 0 ? pts[drag_ - 1].x + 0.01f : 0.0f;
    const float hi = drag_ + 1 < static_cast<int>(pts.size()) ? pts[drag_ + 1].x - 0.01f : 1.0f;
    pts[drag_] = {std::clamp(at.x, lo, std::max(lo, hi)), clamp01(at.y)};
    emit changed();
    update();
}

void CurvesEditor::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ < 0) return;
    auto& pts = curves_[channel_];
    if (dragOff_ && drag_ < static_cast<int>(pts.size())) {
        pts.erase(pts.begin() + drag_);
        emit changed();
    }
    drag_ = -1;
    dragOff_ = false;
    update();
}

void CurvesEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    auto& pts = curves_[channel_];
    const int i = pointAt(e->position());
    if (i < 0 || pts.size() <= 2) return;
    pts.erase(pts.begin() + i);
    drag_ = -1;
    emit changed();
    update();
}

void CurvesEditor::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    menu.addAction(tr("Reset %1").arg(tr(kChannelNames[channel_])), this, [this] {
        curves_[channel_].clear();
        emit changed();
        update();
    });
    menu.addAction(tr("Reset All Curves"), this, [this] {
        for (auto& c : curves_) c.clear();
        emit changed();
        update();
    });
    menu.exec(e->globalPos());
}

} // namespace bemotion::ui
