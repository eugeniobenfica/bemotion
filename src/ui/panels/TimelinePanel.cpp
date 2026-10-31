#include "ui/panels/TimelinePanel.h"

#include "app/PluginHost.h"
#include "app/Settings.h"
#include "core/Editing.h"
#include "core/Snapping.h"
#include "core/Presets.h"
#include "core/Transitions.h"
#include "ui/Transcribe.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "ui/AppSignals.h"
#include "ui/Draw.h"
#include "ui/LayerMenus.h"
#include "ui/Mime.h"
#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/TimelineInternal.h"

#include <QApplication>
#include <QDrag>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace bemotion {

using namespace timeline_internal;

// --- body widgets ------------------------------------------------------------------------------

class TimelineRuler : public QWidget {
public:
    explicit TimelineRuler(TimelinePanel* panel) : panel_(panel) {
        setFixedHeight(static_cast<int>(kRulerHeight));
        setMouseTracking(true);
        edgeTimer_.setInterval(16);
        connect(&edgeTimer_, &QTimer::timeout, this, [this] { edgeTick(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        panel_->paintRuler(p, width());
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->position().x() < panel_->trackX0()) return;
        const float mx = static_cast<float>(e->position().x()), my = static_cast<float>(e->position().y());
        if (panel_->ctx_.pickingSample() && e->button() == Qt::LeftButton) {
            panel_->beginSamplePick(mx);
            return;
        }
        const int marker = panel_->markerAt(mx, my);
        if (e->button() == Qt::RightButton) {
            panel_->showMarkerMenu(e->globalPosition().toPoint(), marker, panel_->xToTime(mx));
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        EditorContext& ctx = panel_->ctx_;
        if (marker >= 0) { // a marker's flag: drag it along (it snaps like everything else)
            panel_->collectTimeSnapTargets({}, false);
            panel_->drag_ = TimelinePanel::Drag::Marker;
            panel_->dragMarker_ = marker;
            panel_->dragStart_ = e->position();
            panel_->dragOrigIn_ = ctx.comp().markers[marker].time;
            return;
        }
        panel_->collectTimeSnapTargets({}, false);
        panel_->timeTargets_.erase(panel_->timeTargets_.begin() + 2); // the playhead itself (see collectTimeSnapTargets)
        // Grabbing the playhead keeps the grab point: it moves with the mouse instead of
        // jumping to it, and snapping tests where the playhead is, not the cursor.
        panel_->playheadGrab_ = std::fabs(mx - panel_->timeToX(ctx.time)) <= 8.0f ? ctx.time - panel_->xToTime(mx) : 0.0;
        panel_->drag_ = TimelinePanel::Drag::Playhead;
        mouseMoveEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        const int marker = panel_->markerAt(static_cast<float>(e->position().x()), static_cast<float>(e->position().y()));
        if (marker >= 0) panel_->renameMarker(marker);
        else mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const float mx = static_cast<float>(e->position().x()), my = static_cast<float>(e->position().y());
        if (panel_->sampling_) {
            panel_->updateSamplePick(mx);
            return;
        }
        if (panel_->drag_ == TimelinePanel::Drag::Marker) {
            panel_->dragMarkerTo(mx);
            return;
        }
        const int marker = panel_->drag_ == TimelinePanel::Drag::None ? panel_->markerAt(mx, my) : -1;
        setCursor(marker >= 0 ? Qt::PointingHandCursor : mx >= panel_->trackX0() ? Qt::SizeHorCursor : Qt::ArrowCursor);
        if (marker >= 0) {
            const Marker& m = panel_->ctx_.comp().markers[marker];
            const QString tip = (m.name.empty() ? tr("Marker") : QString::fromStdString(m.name)) + "  ·  " +
                                QString::fromStdString(ui::formatTimecode(m.time, panel_->ctx_.comp().fps)) +
                                "\n" + tr("Drag to move  ·  Double-click to rename  ·  Right-click for more");
            QToolTip::showText(e->globalPosition().toPoint(), tip, this);
        }
        if (panel_->drag_ != TimelinePanel::Drag::Playhead) return;
        mouseX_ = static_cast<float>(e->position().x());
        dragTo(mouseX_);
        // Near or past an edge the timeline scrolls on its own, the playhead riding along.
        if (edgeSpeed() != 0.0f && !edgeTimer_.isActive()) edgeTimer_.start();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        edgeTimer_.stop();
        if (panel_->sampling_) panel_->endSamplePick();
        if (panel_->drag_ == TimelinePanel::Drag::Marker) panel_->dragMarker_ = -1;
        panel_->drag_ = TimelinePanel::Drag::None;
        panel_->snapLineTime_ = -1.0;
        panel_->relayout();
    }
    void wheelEvent(QWheelEvent* e) override { panel_->wheel(e); }

private:
    static constexpr float kEdge = 28.0f; // px from an edge of the track area where scrolling starts

    // Pixels to scroll per tick: faster the further past the edge (0 inside).
    float edgeSpeed() const {
        const float left = panel_->trackX0() + kEdge, right = panel_->trackX0() + panel_->trackW() - kEdge;
        const float over = mouseX_ < left ? mouseX_ - left : mouseX_ > right ? mouseX_ - right : 0.0f;
        if (over == 0.0f) return 0.0f;
        return std::copysign(std::clamp(std::fabs(over) * 0.4f, 2.0f, 48.0f), over);
    }
    void edgeTick() {
        const float speed = panel_->drag_ == TimelinePanel::Drag::Playhead ? edgeSpeed() : 0.0f;
        if (speed == 0.0f || panel_->pxPerSec_ <= 0.0) {
            edgeTimer_.stop();
            return;
        }
        panel_->scroll_ = std::max(0.0, panel_->scroll_ + speed / panel_->pxPerSec_);
        panel_->clampView();
        // The playhead stays under the mouse, kept inside the track area.
        dragTo(std::clamp(mouseX_, panel_->trackX0(), panel_->trackX0() + panel_->trackW()));
        // The playhead lands on a frame, maybe just left of the view when zoomed in: keep it in sight.
        if (panel_->ctx_.time < panel_->scroll_) {
            panel_->scroll_ = panel_->ctx_.time;
            panel_->relayout();
        }
    }
    void dragTo(float x) {
        EditorContext& ctx = panel_->ctx_;
        ctx.playing = false;
        double t = panel_->xToTime(x) + panel_->playheadGrab_;
        // With the magnet on (or Shift held) the playhead snaps to clip edges and keyframes.
        if ((ctx.snapping || shiftHeld()) && !ctrlHeld()) t = panel_->snappedDelta({0.0}, t, true);
        ctx.setTime(t);
        panel_->relayout();
    }

    TimelinePanel* panel_;
    QTimer edgeTimer_;
    float mouseX_ = 0.0f;
};

// The horizontal scrollbar under the tracks: the thumb is the time in view. Drag it to move,
// drag its ends to zoom (as in Premiere), click beside it to jump there.
class TimelineScrollbar : public QWidget {
public:
    explicit TimelineScrollbar(TimelinePanel* panel) : panel_(panel) {
        setFixedHeight(12);
        setMouseTracking(true);
    }

protected:
    enum class Part { None, Thumb, Start, End };
    // The thumb in pixels, over the track area only.
    void thumb(float& x0, float& x1) const {
        const float left = panel_->trackX0(), w = panel_->trackW();
        const double total = std::max(panel_->scrollEnd(), 1e-3), span = w / panel_->pxPerSec_;
        x0 = left + static_cast<float>(panel_->scroll_ / total) * w;
        x1 = left + static_cast<float>(std::min(1.0, (panel_->scroll_ + span) / total)) * w;
        if (x1 - x0 < 24.0f) x1 = std::min(left + w, x0 + 24.0f), x0 = std::min(x0, x1 - 24.0f);
    }
    Part partAt(float x) const {
        float x0, x1;
        thumb(x0, x1);
        if (x < x0 - 2 || x > x1 + 2) return Part::None;
        if (x - x0 < 7) return Part::Start;
        if (x1 - x < 7) return Part::End;
        return Part::Thumb;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        const float left = panel_->trackX0(), w = panel_->trackW(), h = static_cast<float>(height());
        d.rectFilled({left, 2}, {left + w, h - 2}, theme::kBgDeep, (h - 4) * 0.5f);
        float x0, x1;
        thumb(x0, x1);
        const bool hot = drag_ != Part::None || hover_ != Part::None;
        d.rectFilled({x0, 2}, {x1, h - 2}, hot ? theme::kTextFaint : theme::kBgHover, (h - 4) * 0.5f);
        // Grips at the ends: they zoom.
        if (hot)
            for (float gx : {x0 + 4.5f, x1 - 4.5f}) d.line({gx, 4.5f}, {gx, h - 4.5f}, theme::kTextDim, 1.5f);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        const float x = static_cast<float>(e->position().x());
        drag_ = partAt(x);
        grabX_ = x;
        grabScroll_ = panel_->scroll_;
        grabPx_ = panel_->pxPerSec_;
        if (drag_ == Part::None && x >= panel_->trackX0()) { // beside the thumb: center the view there
            const double total = panel_->scrollEnd(), span = panel_->trackW() / panel_->pxPerSec_;
            const double at = (x - panel_->trackX0()) / panel_->trackW() * total;
            panel_->scroll_ = std::max(0.0, at - span * 0.5);
            panel_->relayout();
            drag_ = Part::Thumb;
            grabScroll_ = panel_->scroll_;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const float x = static_cast<float>(e->position().x());
        if (drag_ == Part::None) {
            const Part over = partAt(x);
            if (over != hover_) hover_ = over, update();
            setCursor(over == Part::Start || over == Part::End ? Qt::SizeHorCursor : Qt::ArrowCursor);
            return;
        }
        const double total = panel_->scrollEnd(), perPixel = total / panel_->trackW(); // seconds per scrollbar pixel
        const double dt = (x - grabX_) * perPixel;
        const double span = panel_->trackW() / grabPx_;
        if (drag_ == Part::Thumb) {
            panel_->scroll_ = std::max(0.0, grabScroll_ + dt);
        } else { // an end: the other one stays, the span changes (the zoom)
            const double start = drag_ == Part::Start ? std::clamp(grabScroll_ + dt, 0.0, grabScroll_ + span - 0.05) : grabScroll_;
            const double end = drag_ == Part::End ? std::max(grabScroll_ + span + dt, grabScroll_ + 0.05) : grabScroll_ + span;
            panel_->pxPerSec_ = panel_->trackW() / (end - start);
            panel_->scroll_ = start;
        }
        panel_->relayout();
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        drag_ = Part::None;
        update();
    }
    void leaveEvent(QEvent*) override {
        hover_ = Part::None;
        update();
    }
    void wheelEvent(QWheelEvent* e) override { panel_->wheel(e); }

private:
    TimelinePanel* panel_;
    Part drag_ = Part::None, hover_ = Part::None;
    float grabX_ = 0;
    double grabScroll_ = 0, grabPx_ = 1;
};

class TimelineRows : public QWidget {
public:
    explicit TimelineRows(TimelinePanel* panel) : panel_(panel) {
        setMouseTracking(true);
        setAcceptDrops(true);
        setFocusPolicy(Qt::ClickFocus);
        // Dragging near the left or right edge scrolls the view, smoothly and faster the further
        // out (as the ruler does for the playhead): clips, trims and things dragged in alike.
        edgeTimer_.setInterval(16);
        connect(&edgeTimer_, &QTimer::timeout, this, [this] { edgeTick(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        panel_->paintRows(p, size());
    }
    void resizeEvent(QResizeEvent*) override { panel_->relayout(); }
    void mousePressEvent(QMouseEvent* e) override { panel_->rowsPress(e); }
    void mouseMoveEvent(QMouseEvent* e) override {
        panel_->rowsMove(e);
        lastPos_ = e->position();
        lastButtons_ = e->buttons();
        lastMods_ = e->modifiers();
        followEdge(panel_->drag_ != TimelinePanel::Drag::None && (e->buttons() & Qt::LeftButton));
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        edgeTimer_.stop();
        panel_->rowsRelease(e);
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override { panel_->rowsDoubleClick(e); }
    void wheelEvent(QWheelEvent* e) override { panel_->wheel(e); }
    void leaveEvent(QEvent*) override {
        panel_->hoverX_ = -1.0f;
        update();
    }
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(ui::kObjectMime) || e->mimeData()->hasFormat(ui::kMediaMime) || e->mimeData()->hasFormat(ui::kTransitionMime) ||
            e->mimeData()->hasFormat(ui::kAnimationMime))
            e->acceptProposedAction();
    }
    // A transition or a Quick Animation from Animate, if that is what is dragged.
    static std::optional<TimelinePanel::Motion> motionOf(const QMimeData* data) {
        TimelinePanel::Motion m;
        if (ui::readTextMime(data, ui::kTransitionMime, m.transition)) return m;
        std::string json;
        AnimationPreset preset;
        if (ui::readTextMime(data, ui::kAnimationMime, json) && parsePreset(json, preset)) {
            m.preset = preset;
            return m;
        }
        return std::nullopt;
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (const auto motion = motionOf(e->mimeData())) {
            // Shows where it goes; a clip that cannot take it refuses the drop.
            panel_->motionDrop_ = panel_->motionDropAt(e->position(), *motion);
            if (panel_->motionDrop_.ok) e->acceptProposedAction();
            else e->ignore();
            update();
            return;
        }
        lastPos_ = e->position();
        externalDrag_ = true;
        followEdge(true);
        double t = 0;
        int gap = 0;
        if (!panel_->dropTimeAndGap(e->position(), t, gap)) return;
        uint64_t mediaId = 0;
        const MediaItem* item = ui::readMime(e->mimeData(), ui::kMediaMime, mediaId) ? panel_->ctx_.comp().findMedia(mediaId) : nullptr;
        panel_->dropActive_ = true;
        panel_->dropTime_ = t;
        panel_->dropLength_ = item && item->kind != MediaKind::Image ? item->duration : kDefaultLayerLength;
        panel_->dropTrack_ = panel_->freeTrackAt(static_cast<float>(e->position().y()), t, panel_->dropLength_);
        e->acceptProposedAction();
        update();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override {
        edgeTimer_.stop();
        externalDrag_ = false;
        panel_->dropTrack_ = 0;
        panel_->dropActive_ = false;
        panel_->motionDrop_ = {};
        panel_->snapLineTime_ = -1.0;
        update();
    }
    void dropEvent(QDropEvent* e) override {
        EditorContext& ctx = panel_->ctx_;
        double t = 0;
        int gap = 0;
        edgeTimer_.stop();
        externalDrag_ = false;
        panel_->dropActive_ = false;
        panel_->snapLineTime_ = -1.0;
        if (const auto motion = motionOf(e->mimeData())) {
            const TimelinePanel::MotionDrop drop = panel_->motionDropAt(e->position(), *motion);
            panel_->motionDrop_ = {};
            if (drop.ok && panel_->placeMotion(drop, *motion)) e->acceptProposedAction();
            panel_->relayout();
            return;
        }
        if (!panel_->dropTimeAndGap(e->position(), t, gap)) return;
        uint64_t mediaId = 0;
        int kind = 0;
        Layer* l = nullptr;
        if (ui::readMime(e->mimeData(), ui::kMediaMime, mediaId)) l = ctx.addMedia(mediaId, std::nullopt, t);
        else if (ui::readMime(e->mimeData(), ui::kObjectMime, kind)) l = ctx.addObject(kind, ctx.comp().center(), t);
        if (l) {
            // Onto the track it was dropped on when there is room there, else a new track at the gap.
            const uint64_t id = l->id;
            const uint64_t onto = panel_->freeTrackAt(static_cast<float>(e->position().y()), t, l->outPoint - l->inPoint);
            if (!onto || !ctx.comp().moveClipsToTrack({id}, onto)) {
                if (const Layer* added = ctx.comp().findLayer(id)) ctx.comp().moveTracks({added->track}, gap);
            }
            ctx.touch();
        }
        panel_->dropTrack_ = 0;
        e->acceptProposedAction();
        panel_->relayout();
    }

private:
    float edgeSpeed() const {
        constexpr float kEdge = 28.0f;
        const float x = static_cast<float>(lastPos_.x());
        const float left = panel_->trackX0() + kEdge, right = panel_->trackX0() + panel_->trackW() - kEdge;
        const float over = x < left ? x - left : x > right ? x - right : 0.0f;
        if (over == 0.0f || x < panel_->trackX0() - 4.0f) return 0.0f; // over the names: no scrolling
        return std::copysign(std::clamp(std::fabs(over) * 0.35f, 2.0f, 40.0f), over);
    }
    void followEdge(bool dragging) {
        if (dragging && edgeSpeed() != 0.0f) {
            if (!edgeTimer_.isActive()) edgeTimer_.start();
        } else {
            edgeTimer_.stop();
        }
    }
    void edgeTick() {
        const float speed = edgeSpeed();
        const bool dragging = externalDrag_ || (panel_->drag_ != TimelinePanel::Drag::None && (lastButtons_ & Qt::LeftButton));
        if (!dragging || speed == 0.0f || panel_->pxPerSec_ <= 0.0) {
            edgeTimer_.stop();
            return;
        }
        panel_->scroll_ = std::max(0.0, panel_->scroll_ + speed / panel_->pxPerSec_);
        panel_->clampView();
        if (!externalDrag_) { // the dragged clip stays under the mouse as the view moves
            QMouseEvent move(QEvent::MouseMove, lastPos_, mapToGlobal(lastPos_), Qt::NoButton, lastButtons_, lastMods_);
            panel_->rowsMove(&move);
        } else if (panel_->dropActive_) {
            double t = 0;
            int gap = 0;
            if (panel_->dropTimeAndGap(lastPos_, t, gap)) {
                panel_->dropTime_ = t;
                panel_->dropTrack_ = panel_->freeTrackAt(static_cast<float>(lastPos_.y()), t, panel_->dropLength_);
            }
        }
        panel_->relayout();
    }

    TimelinePanel* panel_;
    QTimer edgeTimer_;
    QPointF lastPos_;
    Qt::MouseButtons lastButtons_;
    Qt::KeyboardModifiers lastMods_;
    bool externalDrag_ = false;
};

// The user's tools (Settings::toolbar, filled from the Tools popover) on a pill of their own.
// Drag a tool along the bar to reorder it, out of the bar to remove it. The grip on the left
// moves the whole bar: into the viewport (it floats there) or back into the timeline.
class ToolPill : public QWidget {
public:
    explicit ToolPill(TimelinePanel* panel) : panel_(panel) {
        setMouseTracking(true);
        setAcceptDrops(true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override {
        const int n = static_cast<int>(tools().size());
        const QSize size(static_cast<int>(kGrip + kPad * 2 + n * (kSize + kGap) + kSize * 0.8f + 8), static_cast<int>(kSize + 2));
        return vertical_ ? size.transposed() : size;
    }

    void setVertical(bool vertical) {
        if (vertical_ == vertical) return;
        vertical_ = vertical;
        hover_ = -1;
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (dragging_) p.setOpacity(0.35); // its picture follows the mouse: this is where it was
        ui::Draw d(p);
        d.rectFilled({0, 0}, {float(width()), float(height())}, theme::kBgDeep, 7.0f);
        if (parentWidget() != panel_) d.rect({0.5f, 0.5f}, {width() - 0.5f, height() - 0.5f}, theme::kBorder, 7.0f); // floating
        if (hover_ != -1) // the grip is a hover affordance, not an active tool
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 2; ++c) {
                    const float across = (vertical_ ? width() : height()) * 0.5f + (r - 1) * 5.0f;
                    const float along = 5.0f + c * 4.0f;
                    d.circleFilled(vertical_ ? Pt(across, along) : Pt(along, across), 1.2f,
                                   hover_ == -3 ? theme::kText : theme::kTextFaint);
                }
        const auto ts = tools();
        EditorContext& ctx = panel_->ctx_;
        const auto& catalog = ui::toolCatalog();
        for (int i = 0; i < static_cast<int>(ts.size()); ++i) {
            const ui::ToolInfo& tool = catalog[ts[i]];
            const QRectF r = buttonRect(i);
            const bool enabled = ui::toolEnabled(ctx, tool), active = ui::toolActive(ctx, tool);
            if (active) d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), theme::accent(50), 5.0f);
            else if (hover_ == i && enabled) d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), theme::kBgHover, 5.0f);
            ui::drawIcon(d, tool.icon, Pt(r.center()), kSize * 0.6f,
                         !enabled ? theme::kTextFaint : active ? theme::kAccent : hover_ == i ? theme::kText : theme::kTextDim);
        }
        const QRectF plus = plusRect();
        if (hover_ == -2) d.rectFilled(Pt(plus.topLeft()), Pt(plus.bottomRight()), theme::kBgHover, 5.0f);
        ui::drawIcon(d, ui::Icon::Plus, Pt(plus.center()), kSize * 0.45f, hover_ == -2 ? theme::kText : theme::kTextFaint);
        if (dropAt_ >= 0) {
            const QRectF next = dropAt_ < static_cast<int>(ts.size()) ? buttonRect(dropAt_) : plusRect();
            const float at = static_cast<float>(vertical_ ? next.top() : next.left()) - 1.0f;
            d.line(vertical_ ? Pt(1, at) : Pt(at, 1),
                   vertical_ ? Pt(float(width()) - 1, at) : Pt(at, float(height()) - 1), theme::kAccent, 2.0f);
        }
    }
    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(e);
            const int i = indexAt(he->pos());
            const auto ts = tools();
            if (i >= 0) QToolTip::showText(he->globalPos(), QString::fromStdString(ui::toolTooltip(ui::toolCatalog()[ts[i]])), this);
            else if (i == -3) QToolTip::showText(he->globalPos(), tr("Drag to move the toolbar: along a window edge, over the viewport or back to the timeline (right-click for positions)"), this);
            else if (i == -2) QToolTip::showText(he->globalPos(), tr("All tools: click to open them, then drag the ones you want here"), this);
            else QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (dragging_) { // another button while moving the bar: cancelled
            endDrag(e->globalPosition().toPoint(), false);
            return;
        }
        pressed_ = indexAt(e->position());
        pressPos_ = e->position();
        if (e->button() == Qt::RightButton && pressed_ == -3) {
            QMenu menu;
            const bool floating = parentWidget() != panel_;
            menu.addAction(floating ? tr("Back to the Timeline") : tr("Move to the Viewport"),
                           [this, floating] { emit panel_->toolbarMoveRequested(!floating, QPoint(-1, -1)); });
            menu.addSeparator();
            const char* labels[] = {TR_NOOP("Float at top"), TR_NOOP("Float at bottom"), TR_NOOP("Float at left"), TR_NOOP("Float at right")};
            const QPointF positions[] = {{0.5, 0}, {0.5, 1}, {0, 0.5}, {1, 0.5}};
            for (int i = 0; i < 4; ++i)
                menu.addAction(tr(labels[i]), [this, position = positions[i]] { emit panel_->toolbarDockRequested(position); });
            menu.exec(e->globalPosition().toPoint());
            pressed_ = -1;
            hover_ = -1;
            unsetCursor();
            update();
        }
        if (e->button() == Qt::RightButton && pressed_ >= 0) {
            const int at = pressed_;
            QMenu menu;
            menu.addAction(tr("Remove from Toolbar"), [this, at] { removeAt(at); });
            menu.addAction(tr("Customize..."), [this] { emit panel_->toolsPanelRequested(); });
            menu.exec(e->globalPosition().toPoint());
            pressed_ = -1;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) {
            emit panel_->toolbarDragMoved(e->globalPosition().toPoint());
            return;
        }
        if ((e->buttons() & Qt::LeftButton) && pressed_ == -3 && (e->position() - pressPos_).manhattanLength() > 4) {
            pressed_ = -1; // the grip: the whole bar moves with the mouse, the window shows where it can go
            emit panel_->toolbarDragStarted(pressPos_.toPoint()); // (pictures it as it looks)
            dragging_ = true;
            setCursor(Qt::ClosedHandCursor);
            update();
            emit panel_->toolbarDragMoved(e->globalPosition().toPoint());
            return;
        }
        if ((e->buttons() & Qt::LeftButton) && pressed_ >= 0 && (e->position() - pressPos_).manhattanLength() > 6) {
            const int from = pressed_;
            pressed_ = -1;
            const int index = tools()[from];
            auto* drag = new QDrag(this);
            drag->setMimeData(ui::makeMime(ui::kToolMime, index));
            drag->setPixmap(ui::makeIcon(ui::toolCatalog()[index].icon, theme::kAccent, 24).pixmap(24, 24));
            // Dropped anywhere but a toolbar: it leaves the bar.
            if (drag->exec(Qt::MoveAction | Qt::CopyAction) == Qt::IgnoreAction) {
                const std::string id = ui::toolCatalog()[index].id;
                auto& bar = settings().toolbar;
                if (auto it = std::find(bar.begin(), bar.end(), id); it != bar.end()) {
                    bar.erase(it);
                    changed();
                }
            }
            return;
        }
        const int h = indexAt(e->position());
        if (h != hover_) {
            hover_ = h;
            setCursor(h == -3 ? Qt::OpenHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        if (dragging_) {
            endDrag(e->globalPosition().toPoint(), true);
            return;
        }
        const int i = indexAt(e->position());
        if (i == pressed_ && i >= 0) {
            const ui::ToolInfo& tool = ui::toolCatalog()[tools()[i]];
            if (ui::toolEnabled(panel_->ctx_, tool)) ui::runTool(panel_->ctx_, tool);
            panel_->relayout();
        } else if (i == -2 && pressed_ == -2) {
            emit panel_->toolsPanelRequested();
        }
        pressed_ = -1;
        update();
    }
    void leaveEvent(QEvent*) override {
        if (dragging_) return;
        hover_ = -1;
        unsetCursor();
        update();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (dragging_ && e->key() == Qt::Key_Escape) endDrag(QCursor::pos(), false);
        else QWidget::keyPressEvent(e);
    }
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(ui::kToolMime)) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        const auto ts = tools();
        int at = 0;
        while (at < static_cast<int>(ts.size()) &&
               (vertical_ ? e->position().y() > buttonRect(at).center().y() : e->position().x() > buttonRect(at).center().x())) ++at;
        dropAt_ = at;
        e->acceptProposedAction();
        update();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override {
        dropAt_ = -1;
        update();
    }
    void dropEvent(QDropEvent* e) override {
        int index = -1;
        int at = dropAt_;
        dropAt_ = -1;
        if (!ui::readMime(e->mimeData(), ui::kToolMime, index) || index < 0 || index >= static_cast<int>(ui::toolCatalog().size()))
            return;
        auto& bar = settings().toolbar;
        const std::string id = ui::toolCatalog()[index].id;
        // Already there (a move, or the same tool dropped again): take it out first.
        if (auto it = std::find(bar.begin(), bar.end(), id); it != bar.end()) {
            if (static_cast<int>(it - bar.begin()) < at) --at;
            bar.erase(it);
        }
        bar.insert(bar.begin() + std::clamp(at, 0, static_cast<int>(bar.size())), id);
        e->setDropAction(Qt::MoveAction);
        e->accept();
        changed();
    }

private:
    static constexpr float kSize = 26.0f, kPad = 3.0f, kGap = 2.0f, kGrip = 12.0f;
    // Catalog indices of the bar's tools (unknown ids skipped).
    static std::vector<int> tools() {
        std::vector<int> out;
        for (const std::string& id : settings().toolbar)
            if (const int i = ui::toolIndex(id); i >= 0 && ui::toolAvailable(ui::toolCatalog()[i])) out.push_back(i);
        return out;
    }
    QRectF oriented(QRectF r) const { return vertical_ ? QRectF(r.y(), r.x(), r.height(), r.width()) : r; }
    QRectF buttonRect(int i) const { return oriented({kGrip + kPad + i * (kSize + kGap), 1.0, kSize, kSize}); }
    QRectF plusRect() const {
        const float x = kGrip + kPad + static_cast<float>(tools().size()) * (kSize + kGap) + 2.0f;
        return oriented({x, 1.0 + kSize * 0.1, kSize * 0.8, kSize * 0.8});
    }
    int indexAt(QPointF p) const { // a tool, -2 the "+", -3 the grip, -1 nothing
        if ((vertical_ ? p.y() : p.x()) < kGrip) return -3;
        const int n = static_cast<int>(tools().size());
        for (int i = 0; i < n; ++i)
            if (buttonRect(i).contains(p)) return i;
        return plusRect().adjusted(-2, -3, 2, 3).contains(p) ? -2 : -1;
    }
    void removeAt(int i) {
        auto& bar = settings().toolbar;
        const auto ts = tools();
        if (i < 0 || i >= static_cast<int>(ts.size())) return;
        if (auto it = std::find(bar.begin(), bar.end(), std::string(ui::toolCatalog()[ts[i]].id)); it != bar.end()) bar.erase(it);
        changed();
    }
    void changed() {
        saveSettings();
        emit ui::AppSignals::get().toolbarChanged();
    }
    void endDrag(QPoint global, bool dropped) {
        dragging_ = false;
        hover_ = -1;
        unsetCursor();
        update();
        emit panel_->toolbarDragFinished(global, dropped); // may move this bar elsewhere
    }

    TimelinePanel* panel_;
    bool vertical_ = false, dragging_ = false;
    int hover_ = -1, pressed_ = -1, dropAt_ = -1;
    QPointF pressPos_;
};

// --- panel -------------------------------------------------------------------------------------

TimelinePanel::TimelinePanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(6, 6, 6, 4);
    col->setSpacing(4);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(2);
    buildToolbar(bar);
    col->addLayout(bar);
    breadcrumb_ = new QWidget;
    breadcrumb_->hide();
    col->addWidget(breadcrumb_);
    ruler_ = new TimelineRuler(this);
    col->addWidget(ruler_);
    rows_ = new TimelineRows(this);
    col->addWidget(rows_, 1);
    hscroll_ = new TimelineScrollbar(this);
    col->addWidget(hscroll_); // parented first: shown on its own, it would flash up as a window
    hscroll_->setVisible(settings().showTimelineScrollbar);
    for (auto signal : {&ui::AppSignals::toolbarChanged, &ui::AppSignals::modulesChanged})
        connect(&ui::AppSignals::get(), signal, this, [this] {
            pill_->updateGeometry();
            pill_->update();
        });
    connect(&ui::AppSignals::get(), &ui::AppSignals::appearanceChanged, this, [this] {
        hscroll_->setVisible(settings().showTimelineScrollbar);
        relayout();
    });
    connect(&ui::AppSignals::get(), &ui::AppSignals::pluginsChanged, this, [this] {
        QTimer::singleShot(300, this, [this] { rebuildPluginButtons(); }); // after the reload
    });
}

void TimelinePanel::buildToolbar(QHBoxLayout* bar) {
    const int s = 26;
    bar_ = bar;
    pill_ = new ToolPill(this);
    bar->addWidget(pill_);
    // Timeline functions, set apart from the tools by a divider.
    auto* divider = new QFrame;
    divider_ = divider;
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedHeight(s - 8);
    divider->setContentsMargins(8, 0, 8, 0);
    divider->setFixedWidth(17);
    ui::setTone(divider, ui::Tone::Border);
    bar->addWidget(divider);
    bar->addSpacing(8);
    auto button = [&](ui::Icon icon, const QString& tip, std::function<void()> click) {
        auto* b = new ui::IconButton(icon, tip, s);
        connect(b, &QAbstractButton::clicked, this, [this, click] {
            click();
            sync();
        });
        bar->addWidget(b);
        return b;
    };
    snap_ = button(ui::Icon::Magnet, tr("Snapping (S): clips, keyframes and objects snap. Hold Ctrl to bypass"),
                   [this] { ctx_.snapping = !ctx_.snapping; });
    bar->addSpacing(12);
    button(ui::Icon::ToStart, tr("Go to start (Home)"), [this] {
        ctx_.playing = false;
        ctx_.setTime(0.0);
    });
    button(ui::Icon::KeyPrev, tr("Previous keyframe (Shift+J): of the selected layer, or of any"), [this] { ctx_.jumpToKeyframe(-1); });
    button(ui::Icon::StepBack, tr("Previous frame (Left)"), [this] { ctx_.stepFrames(-1); });
    play_ = button(ui::Icon::Play, tr("Play / Pause (Space)"), [this] { ctx_.playing = !ctx_.playing; });
    play_->setActive(true);
    button(ui::Icon::StepForward, tr("Next frame (Right)"), [this] { ctx_.stepFrames(1); });
    button(ui::Icon::KeyNext, tr("Next keyframe (Shift+K): of the selected layer, or of any"), [this] { ctx_.jumpToKeyframe(1); });
    button(ui::Icon::ToEnd, tr("Go to end (End)"), [this] {
        ctx_.playing = false;
        ctx_.setTime(ctx_.comp().duration);
    });
    loop_ = button(ui::Icon::Loop, tr("Loop playback"), [this] { ctx_.loop = !ctx_.loop; });

    bar->addSpacing(14);
    timecode_ = new QLabel;
    QFont f = timecode_->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    f.setWeight(QFont::DemiBold);
    timecode_->setFont(f);
    ui::setTone(timecode_, ui::Tone::Accent);
    bar->addWidget(timecode_);
    bar->addSpacing(6);
    frameInfo_ = new QLabel;
    ui::setTone(frameInfo_, ui::Tone::TextFaint);
    bar->addWidget(frameInfo_);

    // Graph editor easing buttons (for the selected keyframes).
    bar->addSpacing(16);
    const std::pair<const char*, const char*> eases[] = {
        {TR_NOOP("Easy Ease"), TR_NOOP("Smooth in and out of the selected keyframes (F9)")},
        {TR_NOOP("Ease In"), TR_NOOP("Slow down into the selected keyframes (Shift+F9)")},
        {TR_NOOP("Ease Out"), TR_NOOP("Speed up out of the selected keyframes (Ctrl+Shift+F9)")},
        {TR_NOOP("Linear"), TR_NOOP("Constant speed")},
        {TR_NOOP("Hold"), TR_NOOP("Jump to the next keyframe's value")},
    };
    const Ease kinds[] = {Ease::EasyEase, Ease::EaseIn, Ease::EaseOut, Ease::Linear, Ease::Hold};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QPushButton(tr(eases[i].first));
        b->setToolTip(tr(eases[i].second));
        const Ease e = kinds[i];
        connect(b, &QPushButton::clicked, this, [this, e] {
            applyEase(ctx_, e);
            relayout();
        });
        b->hide();
        bar->addWidget(b);
        easeButtons_.push_back(b);
    }
    bar->addStretch(1);

    // Right-aligned controls: icons only. Plugin buttons (recording, ...) come first.
    pluginBar_ = new QHBoxLayout;
    pluginBar_->setSpacing(2);
    bar->addLayout(pluginBar_);
    rebuildPluginButtons();
    graph_ = button(ui::Icon::Graph, tr("Graph Editor: edit the curves of the selected layer's keyframes"), [this] {
        graphMode_ = !graphMode_;
        graphDrag_ = {};
        relayout();
    });
    addKeys_ = button(ui::Icon::Plus, tr("Add keyframes at the playhead to the curves shown (right-click the graph for more)"),
                      [this] { addKeysAtPlayhead(); });
    animOnly_ = button(ui::Icon::Diamond, tr("Show only animated properties"), [this] {
        animatedOnly_ = !animatedOnly_;
        relayout();
    });
    bar->addSpacing(8);
    button(ui::Icon::ZoomOut, tr("Zoom out (Ctrl+Wheel)"), [this] { zoomAround(trackX0() + trackW() * 0.5f, 1.0 / 1.5); });
    button(ui::Icon::ZoomIn, tr("Zoom in (Ctrl+Wheel)"), [this] { zoomAround(trackX0() + trackW() * 0.5f, 1.5); });
    button(ui::Icon::Reset, tr("Fit the whole composition"), [this] {
        pxPerSec_ = 0.0;
        relayout();
    });
}

QWidget* TimelinePanel::rowsWidget() const { return rows_; }

QWidget* TimelinePanel::toolPill() const { return pill_; }

void TimelinePanel::rebuildPluginButtons() {
    std::map<std::string, bool> active;
    for (const auto& [id, b] : pluginButtons_) active[id] = b->active();
    while (auto* item = pluginBar_->takeAt(0)) {
        if (auto* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    pluginButtons_.clear();
    for (const PluginCommand* c : PluginHost::get().commands()) {
        if (c->place != "timeline") continue;
        auto* b = new ui::IconButton(ui::iconForPlugin(c->icon), QString::fromStdString(c->name + (c->description.empty() ? "" : "\n" + c->description)), 26);
        if (c->icon == "mic" || c->icon == "microphone") b->setActiveColor(theme::kDanger);
        const std::string id = c->id;
        const bool toggle = c->toggle;
        if (toggle) b->setActive(active[id]);
        connect(b, &QAbstractButton::clicked, this, [this, b, id, toggle] {
            const bool pressed = toggle && !b->active();
            if (toggle) b->setActive(pressed);
            emit pluginButtonClicked(id, pressed);
        });
        pluginBar_->addWidget(b);
        pluginButtons_.emplace_back(id, b);
    }
    if (!pluginButtons_.empty()) pluginBar_->addSpacing(8);
}

void TimelinePanel::setPluginButtonPressed(const std::string& commandId, bool pressed) {
    for (auto& [id, b] : pluginButtons_)
        if (id == commandId) b->setActive(pressed);
}

void TimelinePanel::releaseToolPill() {
    if (pill_->parentWidget() != this) return;
    bar_->removeWidget(pill_);
    divider_->hide();
}

void TimelinePanel::setToolPillVertical(bool vertical) { pill_->setVertical(vertical); }

void TimelinePanel::dockToolPill() {
    pill_->setVertical(false);
    if (pill_->parentWidget() == this) return;
    pill_->setParent(this);
    bar_->insertWidget(0, pill_);
    divider_->show();
    pill_->show();
}

float TimelinePanel::trackW() const { return std::max(50.0f, static_cast<float>(rows_->width()) - leftWidth_); }

void TimelinePanel::zoomAround(float x, double factor) {
    const double t = xToTime(x);
    pxPerSec_ *= factor;
    clampView();
    scroll_ = std::max(0.0, t - (x - trackX0()) / pxPerSec_);
    relayout();
}

void TimelinePanel::setRecordingPreview(double start, uint64_t scope, media::RecordingPreview preview) {
    if (!recordingPreviewActive_) {
        graphMode_ = false;
        scrollY_ = 0.0f;
    }
    recordingPreviewActive_ = true;
    recordingStart_ = start;
    recordingScope_ = scope;
    recordingPreview_ = std::move(preview);
    const double end = recordingStart_ + recordingPreview_.seconds;
    if (pxPerSec_ > 0.0 && timeToX(end) > trackX0() + trackW() - 20)
        scroll_ = std::max(0.0, end - (trackW() - 40) / pxPerSec_);
    relayout();
}

void TimelinePanel::clearRecordingPreview() {
    if (!recordingPreviewActive_) return;
    recordingPreviewActive_ = false;
    recordingPreview_ = {};
    relayout();
}

double TimelinePanel::visibleEnd() const {
    return hasRecordingPreview() ? std::max(ctx_.comp().duration, recordingStart_ + recordingPreview_.seconds) : ctx_.comp().duration;
}

void TimelinePanel::clampView() {
    const Composition& comp = ctx_.comp();
    leftWidth_ = std::clamp(300.0f, 220.0f, std::max(220.0f, rows_->width() * 0.5f));
    const double w = trackW();
    // Clips may run past the composition end: keep them reachable.
    double contentEnd = visibleEnd();
    for (const Layer& l : comp.layers) contentEnd = std::max(contentEnd, l.outPoint);
    const double minPx = w / (std::max(contentEnd, 0.1) * 4.0);
    if (pxPerSec_ <= 0.0) {
        pxPerSec_ = (w - 24.0) / std::max(comp.duration, 2.0 * kDefaultLayerLength); // room to grow
        scroll_ = 0.0;
    }
    pxPerSec_ = std::clamp(pxPerSec_, std::min(minPx, 4000.0), 4000.0);
    // Some room past the end: the playhead can go there to add objects (the composition grows).
    scroll_ = std::clamp(scroll_, 0.0, std::max(0.0, scrollEnd() - (w - 24.0) / pxPerSec_));
}

double TimelinePanel::scrollEnd() const {
    double contentEnd = visibleEnd();
    for (const Layer& l : ctx_.comp().layers) contentEnd = std::max(contentEnd, l.outPoint);
    return contentEnd + kDefaultLayerLength;
}

void TimelinePanel::wheel(QWheelEvent* e) {
    const float steps = e->angleDelta().y() / 120.0f;
    if (e->modifiers() & Qt::ControlModifier) {
        zoomAround(static_cast<float>(e->position().x()), std::pow(1.2, steps));
    } else if (e->modifiers() & Qt::ShiftModifier || e->angleDelta().x() != 0) {
        const float s = e->angleDelta().x() != 0 ? e->angleDelta().x() / 120.0f : steps;
        scroll_ = std::max(0.0, scroll_ - s * 60.0 / pxPerSec_);
        relayout();
    } else {
        scrollY_ -= steps * 40.0f;
        relayout();
    }
    e->accept();
}

// --- layout -------------------------------------------------------------------------------------

void TimelinePanel::computeItems() {
    Composition& comp = ctx_.comp();
    if (ctx_.scope && !comp.findLayer(ctx_.scope)) ctx_.scope = 0; // closed by undo / delete
    clampView();
    items_.clear();
    clips_.clear();
    if (graphMode_) return;
    auto& layers = comp.layers;
    const auto trackRows = comp.trackRows();
    float y = -scrollY_;
    if (hasRecordingPreview()) {
        Item recording;
        recording.kind = Item::Recording;
        recording.top = y;
        recording.h = layerRowHeight();
        items_.push_back(recording);
        y += recording.h;
    }
    for (int r = 0; r < static_cast<int>(trackRows.size()); ++r) {
        const TrackRow& row = trackRows[r];
        // A compound's contents live in its own timeline (double-click it to open).
        const uint64_t owner = comp.findLayer(layers[row.first].parent) ? layers[row.first].parent : 0; // orphans: top level
        if (owner != ctx_.scope) continue;
        // The clip that represents the track: the primary selection if it is here, otherwise the first clip.
        Layer* focus = &layers[row.first];
        for (int i = row.first; i < row.first + row.count; ++i)
            if (layers[i].id == ctx_.primaryId()) focus = &layers[i];
        Item track;
        track.kind = Item::Track;
        track.top = y;
        track.h = layerRowHeight();
        track.rowIndex = r;
        track.track = row.track;
        track.first = row.first;
        track.count = row.count;
        track.layer = focus->id;
        items_.push_back(track);
        y += track.h;
        for (int i = row.first; i < row.first + row.count; ++i)
            clips_.push_back({layers[i].id, track.top, track.top + track.h, timeToX(layers[i].inPoint), timeToX(layers[i].outPoint)});
        // The sound of its videos: a row of its own, like After Effects / Premiere's linked audio.
        bool sound = false;
        for (int i = row.first; i < row.first + row.count; ++i)
            sound = sound || (layers[i].type == LayerType::Media && layers[i].media.hasVideo && layers[i].media.hasAudio);
        if (sound) {
            Item audio;
            audio.kind = Item::Audio;
            audio.top = y;
            audio.h = std::max(30.0f, layerRowHeight() * 0.7f);
            audio.track = row.track;
            audio.first = row.first;
            audio.count = row.count;
            audio.layer = focus->id;
            items_.push_back(audio);
            y += audio.h;
        }
        // Its masks, each a row of its own right under it: objects you see, pick, hide and delete.
        for (int m = 0; m < static_cast<int>(focus->masks.size()); ++m) {
            Item mask;
            mask.kind = Item::Mask;
            mask.top = y;
            mask.h = groupRowHeight() + 6.0f;
            mask.track = row.track;
            mask.layer = focus->id;
            mask.effectIndex = m;
            items_.push_back(mask);
            y += mask.h;
        }
        if (!expanded_.count(row.track)) continue;

        const char* group = nullptr;
        bool groupOpen = true;
        bool effectsHeader = false, effectsOpen = true; // effects nest under one "Effects" group, like After Effects
        auto addGroup = [&](const std::string& label, float indent) {
            Item g;
            g.kind = Item::Group;
            g.top = y;
            g.h = groupRowHeight();
            g.track = row.track;
            g.groupLabel = label;
            g.groupKey = std::to_string(row.track) + "/" + label;
            g.indent = indent;
            items_.push_back(g);
            y += g.h;
            return !collapsedGroups_.count(g.groupKey);
        };
        for (const PropertyRef& prop : focus->properties()) {
            if ((animatedOnly_ || prop.whileAnimated) && !prop.anim->isAnimated()) continue;
            if (prop.effectIndex >= 0 && !effectsHeader) {
                effectsHeader = true;
                effectsOpen = addGroup(TR_NOOP("Effects"), 0.0f);
            }
            if (prop.effectIndex >= 0 && !effectsOpen) continue;
            if (!group || std::strcmp(group, prop.group) != 0) {
                group = prop.group;
                groupOpen = addGroup(group, prop.effectIndex >= 0 ? 16.0f : 0.0f);
            }
            if (!groupOpen) continue;
            Item p;
            p.kind = Item::Prop;
            p.top = y;
            p.h = propRowHeight();
            p.track = row.track;
            p.layer = focus->id;
            p.prop = prop.id;
            p.effectIndex = prop.effectIndex;
            items_.push_back(p);
            y += p.h;
        }
    }
    rowsBottom_ = y;
    contentHeight_ = y + scrollY_;
    // Keep the rows on screen when the content shrinks.
    const float maxScroll = std::max(0.0f, contentHeight_ + layerRowHeight() - static_cast<float>(rows_->height()));
    if (scrollY_ > maxScroll || scrollY_ < 0.0f) {
        scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll);
        computeItems();
    }
}

void TimelinePanel::placeRowWidgets() {
    std::set<std::string> used;
    for (const Item& item : items_) {
        if (item.kind != Item::Prop) continue;
        auto ref = findProperty(ctx_, item.layer, item.prop, item.effectIndex);
        if (!ref) continue;
        const std::string key = rowKey(item.layer, *ref);
        used.insert(key);
        auto& row = rowWidgets_[key];
        if (!row) row = std::make_unique<PropertyRow>(ctx_, item.layer, *ref, rows_);
        QWidget* w = row->widget();
        const int h = 24;
        w->setGeometry(30, static_cast<int>(item.top + (item.h - h) * 0.5f), static_cast<int>(leftWidth_) - 30 - 8, h);
        const bool onScreen = item.top + item.h > 0 && item.top < rows_->height();
        if (w->isVisible() != onScreen) w->setVisible(onScreen);
    }
    for (auto it = rowWidgets_.begin(); it != rowWidgets_.end();) {
        if (used.count(it->first)) {
            ++it;
            continue;
        }
        QWidget* w = it->second->widget();
        // Never pull a widget away from under an edit in progress.
        if (w->isAncestorOf(QApplication::focusWidget())) {
            w->hide();
            ++it;
            continue;
        }
        it = rowWidgets_.erase(it);
    }
}

void TimelinePanel::relayout() {
    computeItems();
    placeRowWidgets();
    rows_->update();
    ruler_->update();
    hscroll_->update();
}

void TimelinePanel::syncBreadcrumb() {
    // Composition > Compound > ...: where this timeline is; click a level to go back up.
    std::vector<const Layer*> path;
    for (const Layer* l = ctx_.comp().findLayer(ctx_.scope); l && path.size() < 32; l = ctx_.comp().findLayer(l->parent))
        path.insert(path.begin(), l);
    std::string key;
    for (const Layer* l : path) key += std::to_string(l->id) + l->name + "/";
    if (key == breadcrumbKey_) return;
    breadcrumbKey_ = key;
    delete breadcrumb_->layout();
    for (QWidget* w : breadcrumb_->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) delete w;
    breadcrumb_->setVisible(!path.empty());
    if (path.empty()) return;
    auto* row = new QHBoxLayout(breadcrumb_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto* top = new QPushButton(tr("Composition"));
    const uint64_t first = path.front()->id;
    connect(top, &QPushButton::clicked, this, [this, first] {
        ctx_.scope = 0;
        ctx_.select(first);
        relayout();
    });
    row->addWidget(top);
    for (size_t i = 0; i < path.size(); ++i) {
        auto* slash = new QLabel("/");
        ui::setTone(slash, ui::Tone::TextFaint);
        row->addWidget(slash);
        const bool last = i + 1 == path.size();
        auto* b = new QPushButton(QString::fromStdString(path[i]->name));
        if (last) b->setProperty("accent", true);
        else {
            const uint64_t scope = path[i]->id, next = path[i + 1]->id;
            connect(b, &QPushButton::clicked, this, [this, scope, next] {
                ctx_.scope = scope;
                ctx_.select(next);
                relayout();
            });
        }
        row->addWidget(b);
    }
    row->addSpacing(12);
    auto* note = new QLabel(tr("Editing inside a compound  ·  Esc (with nothing selected) goes back"));
    ui::setTone(note, ui::Tone::TextFaint);
    row->addWidget(note);
    row->addStretch();
}

void TimelinePanel::sync() {
    if (!isVisible()) return;
    const Composition& comp = ctx_.comp();
    // Keep the playhead visible when it leaves the timeline: playing, or moved by keys (frame
    // steps, Home / End). Scrolling away by hand keeps the view; a playhead drag scrolls itself.
    const bool moved = ctx_.time != followedTime_;
    followedTime_ = ctx_.time;
    if ((ctx_.playing || moved) && drag_ == Drag::None && pxPerSec_ > 0.0) {
        const float left = trackX0() + 16.0f;
        const float right = trackX0() + trackW() - 16.0f;
        const float playhead = timeToX(ctx_.time);
        if (playhead < left || playhead > right) {
            // Going forward it lands on the left part of the view, going back on the right part.
            const double at = ctx_.playing || playhead > right ? 0.70 : 0.30;
            scroll_ = std::max(0.0, ctx_.time - trackW() * at / pxPerSec_);
            clampView();
        }
    }
    timecode_->setText(QString::fromStdString(ui::formatTimecode(ctx_.time, comp.fps)));
    frameInfo_->setText(tr("frame %1").arg(comp.frameAt(ctx_.time)));
    frameInfo_->setToolTip(tr("Frame %1 of %2  ·  %3 fps").arg(comp.frameAt(ctx_.time)).arg(comp.frameCount() - 1).arg(comp.fps, 0, 'g', 4));
    play_->setIcon(ctx_.playing ? ui::Icon::Pause : ui::Icon::Play);
    snap_->setActive(ctx_.snapping);
    loop_->setActive(ctx_.loop);
    graph_->setActive(graphMode_);
    graph_->setVisible(graphMode_ || settings().showGraphButton); // off (Friendly, or by choice): keyframes, without curves
    addKeys_->setVisible(graphMode_);
    addKeys_->setEnabled(ctx_.selected() != nullptr);
    animOnly_->setActive(animatedOnly_);
    for (QPushButton* b : easeButtons_) {
        b->setVisible(graphMode_);
        b->setEnabled(!ctx_.selectedKeys.empty());
    }
    syncBreadcrumb();

    // Repaint when anything the timeline shows changed (and now and then for audio waveforms
    // finishing their decode).
    std::string sig = std::to_string(ctx_.revision()) + ":" + std::to_string(ctx_.time) + ":" + std::to_string(ctx_.scope) + ":" +
                      std::to_string(static_cast<int>(ctx_.tool)) + ":" + std::to_string(ctx_.selectedKeys.size()) + ":" +
                      std::to_string(ctx_.transitionLayer) + (ctx_.transitionIncoming ? "i" : "o") + ":" +
                      std::to_string(ctx_.animationLayer) + "/" + std::to_string(ctx_.animationIndex) + ":" + std::to_string(ctx_.maskFocus) + ":" +
                      std::to_string(media::MediaManager::get().arrivals()) + ":"; // filmstrips made
    for (uint64_t id : ctx_.selection) sig += std::to_string(id) + ",";
    for (const KeySelection& k : ctx_.selectedKeys) sig += std::to_string(k.keyUid) + ",";
    if (sig != signature_ || ++idleSyncs_ > 30) {
        signature_ = sig;
        idleSyncs_ = 0;
        relayout();
        pill_->update();
    }
    for (auto& [key, row] : rowWidgets_)
        if (row->widget()->isVisible()) row->refresh();
}

} // namespace bemotion
