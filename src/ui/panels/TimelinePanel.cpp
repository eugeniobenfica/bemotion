#include "ui/panels/TimelinePanel.h"

#include "app/PluginHost.h"
#include "app/Settings.h"
#include "core/Snapping.h"
#include "core/Presets.h"
#include "core/Transitions.h"
#include "ui/Transcribe.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "ui/AppSignals.h"
#include "ui/Mime.h"
#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/EffectsPanel.h"

#include <QApplication>
#include <QDrag>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
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

using ui::Pt;
using ui::Rgba;

namespace {

constexpr float kRulerHeight = 26.0f;
// Row sizes come from Settings > Interface.
float layerRowHeight() { return settings().timelineLayerRow; }
float propRowHeight() { return std::max(26.0f, settings().timelinePropertyRow); }
float groupRowHeight() { return std::max(18.0f, settings().timelinePropertyRow * 0.8f); }
constexpr float kEdgeGrab = 6.0f;
float keyRadius() { return settings().keyframeSize; }
// Left column of a track row.
constexpr float kButton = 20.0f;
constexpr float kExpandX = 6.0f, kVisX = 30.0f, kLockX = 54.0f, kChipX = 78.0f, kNameX = 110.0f;

Qt::KeyboardModifiers mods() { return QGuiApplication::keyboardModifiers(); }
bool ctrlHeld() { return mods() & Qt::ControlModifier; }
bool shiftHeld() { return mods() & Qt::ShiftModifier; }

Rgba toRgba(Color c, float alpha = 1.0f) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return ui::rgba(byte(c.r), byte(c.g), byte(c.b), byte(c.a * alpha));
}

double snapDelta(double seconds, double fps) { return std::round(seconds * fps) / fps; }

// Keyframe glyph: the shape hints at the easing of the outgoing segment.
void drawKeyGlyph(ui::Draw& d, Pt c, float r, Easing easing, Rgba fill, Rgba outline) {
    if (easing == Easing::Linear) {
        ui::drawDiamond(d, c, r, fill, outline);
    } else if (easing == Easing::Hold) {
        const Pt a(c.x - r * 0.8f, c.y - r * 0.8f), b(c.x + r * 0.8f, c.y + r * 0.8f);
        d.rectFilled(a, b, fill, 1.5f);
        d.rect(a, b, outline, 1.5f, 1.3f);
    } else {
        d.circleFilled(c, r * 0.85f, fill);
        d.circle(c, r * 0.85f, outline, 1.3f);
    }
}

std::vector<uint64_t> clipsOfTrack(const Composition& comp, uint64_t track) {
    std::vector<uint64_t> ids;
    for (const Layer& l : comp.layers)
        if (l.track == track) ids.push_back(l.id);
    return ids;
}

std::string rowKey(uint64_t layer, const PropertyRef& p) {
    return std::to_string(layer) + "/" + std::to_string(p.effectIndex) + "/" + p.id;
}

// Cubic-bezier timing editor: drag the two handles.
class CurveEditor : public QWidget {
public:
    CurveEditor(BezierEase curve, std::function<void(const BezierEase&)> changed)
        : curve_(curve), changed_(std::move(changed)) {
        setFixedSize(static_cast<int>(kSize + kPadX * 2), static_cast<int>(kSize + kPadY * 2));
        setMouseTracking(true);
    }
    void setCurve(const BezierEase& c) {
        curve_ = c;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        d.rectFilled({0, 0}, {float(width()), float(height())}, theme::kBgDeep, 4.0f);
        d.rect(at(0, 1), at(1, 0), theme::ink(30));
        for (int i = 1; i < 4; ++i) {
            const float g = i / 4.0f;
            d.line(at(g, 0), at(g, 1), theme::ink(12));
            d.line(at(0, g), at(1, g), theme::ink(12));
        }
        const Pt a = at(0, 0), b = at(1, 1), h1 = at(curve_.x1, curve_.y1), h2 = at(curve_.x2, curve_.y2);
        d.line(a, h1, theme::kTextFaint);
        d.line(b, h2, theme::kTextFaint);
        d.bezier(a, h1, h2, b, theme::kAccent, 2.5f);
        for (int i = 1; i <= 2; ++i) {
            const Pt h = i == 1 ? h1 : h2;
            const bool hot = grab_ == i || (grab_ == 0 && hover_ == i);
            d.circleFilled(h, hot ? 6.0f : 5.0f, hot ? theme::kText : theme::kAccent);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        grab_ = nearest(e->position());
        mouseMoveEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!grab_) {
            const int n = nearest(e->position());
            const Pt h = n == 1 ? at(curve_.x1, curve_.y1) : at(curve_.x2, curve_.y2);
            const int hv = std::hypot(h.x - e->position().x(), h.y - e->position().y()) < 9.0 ? n : 0;
            if (hv != hover_) hover_ = hv, update();
            return;
        }
        const float x = std::clamp(static_cast<float>(e->position().x() - kPadX) / kSize, 0.0f, 1.0f);
        const float y = std::clamp(static_cast<float>(kPadY + kSize - e->position().y()) / kSize, -0.5f, 1.5f);
        (grab_ == 1 ? curve_.x1 : curve_.x2) = x;
        (grab_ == 1 ? curve_.y1 : curve_.y2) = y;
        changed_(curve_);
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        grab_ = 0;
        update();
    }

private:
    static constexpr float kSize = 180.0f, kPadX = 10.0f, kPadY = 34.0f; // vertical room for overshoot
    Pt at(float x, float y) const { return {kPadX + x * kSize, kPadY + kSize - y * kSize}; }
    int nearest(QPointF m) const {
        const Pt h1 = at(curve_.x1, curve_.y1), h2 = at(curve_.x2, curve_.y2);
        auto dist = [&](Pt p) { return std::hypot(p.x - m.x(), p.y - m.y()); };
        return dist(h1) <= dist(h2) ? 1 : 2;
    }
    BezierEase curve_;
    std::function<void(const BezierEase&)> changed_;
    int grab_ = 0, hover_ = 0;
};

} // namespace

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
        if (e->button() != Qt::LeftButton || e->position().x() < panel_->trackX0()) return;
        EditorContext& ctx = panel_->ctx_;
        panel_->collectTimeSnapTargets({}, false);
        panel_->timeTargets_.erase(panel_->timeTargets_.begin() + 2); // the playhead itself (see collectTimeSnapTargets)
        // Grabbing the playhead keeps the grab point: it moves with the mouse instead of
        // jumping to it, and snapping tests where the playhead is, not the cursor.
        const float mx = static_cast<float>(e->position().x());
        panel_->playheadGrab_ = std::fabs(mx - panel_->timeToX(ctx.time)) <= 8.0f ? ctx.time - panel_->xToTime(mx) : 0.0;
        panel_->drag_ = TimelinePanel::Drag::Playhead;
        mouseMoveEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        setCursor(e->position().x() >= panel_->trackX0() ? Qt::SizeHorCursor : Qt::ArrowCursor);
        if (panel_->drag_ != TimelinePanel::Drag::Playhead) return;
        mouseX_ = static_cast<float>(e->position().x());
        dragTo(mouseX_);
        // Near or past an edge the timeline scrolls on its own, the playhead riding along.
        if (edgeSpeed() != 0.0f && !edgeTimer_.isActive()) edgeTimer_.start();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        edgeTimer_.stop();
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

class TimelineRows : public QWidget {
public:
    explicit TimelineRows(TimelinePanel* panel) : panel_(panel) {
        setMouseTracking(true);
        setAcceptDrops(true);
        setFocusPolicy(Qt::ClickFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        panel_->paintRows(p, size());
    }
    void resizeEvent(QResizeEvent*) override { panel_->relayout(); }
    void mousePressEvent(QMouseEvent* e) override { panel_->rowsPress(e); }
    void mouseMoveEvent(QMouseEvent* e) override { panel_->rowsMove(e); }
    void mouseReleaseEvent(QMouseEvent* e) override { panel_->rowsRelease(e); }
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
    void dragMoveEvent(QDragMoveEvent* e) override {
        std::string transition;
        if (ui::readTextMime(e->mimeData(), ui::kTransitionMime, transition)) {
            // Shows where it goes; a clip that cannot take it refuses the drop.
            panel_->transitionDrop_ = panel_->transitionDropAt(e->position(), transition);
            if (panel_->transitionDrop_.ok) e->acceptProposedAction();
            else e->ignore();
            update();
            return;
        }
        std::string preset;
        if (ui::readTextMime(e->mimeData(), ui::kAnimationMime, preset)) {
            panel_->presetDrop_ = panel_->presetDropAt(e->position(), preset);
            if (panel_->presetDrop_.ok) e->acceptProposedAction();
            else e->ignore();
            update();
            return;
        }
        double t = 0;
        int gap = 0;
        if (!panel_->dropTimeAndGap(e->position(), t, gap)) return;
        uint64_t mediaId = 0;
        const MediaItem* item = ui::readMime(e->mimeData(), ui::kMediaMime, mediaId) ? panel_->ctx_.comp().findMedia(mediaId) : nullptr;
        panel_->dropActive_ = true;
        panel_->dropTime_ = t;
        panel_->dropLength_ = item && item->kind != MediaKind::Image ? item->duration : kDefaultLayerLength;
        e->acceptProposedAction();
        update();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override {
        panel_->dropActive_ = false;
        panel_->transitionDrop_ = {};
        panel_->presetDrop_ = {};
        panel_->snapLineTime_ = -1.0;
        update();
    }
    void dropEvent(QDropEvent* e) override {
        EditorContext& ctx = panel_->ctx_;
        double t = 0;
        int gap = 0;
        panel_->dropActive_ = false;
        panel_->snapLineTime_ = -1.0;
        std::string transition;
        std::string presetJson;
        AnimationPreset preset;
        if (ui::readTextMime(e->mimeData(), ui::kAnimationMime, presetJson) && parsePreset(presetJson, preset)) {
            const TimelinePanel::PresetDrop drop = panel_->presetDropAt(e->position(), presetJson);
            panel_->presetDrop_ = {};
            if (Layer* layer = drop.ok ? ctx.comp().findLayer(drop.layer) : nullptr) {
                applyPreset(preset, *layer, drop.time);
                ctx.select(layer->id);
                ctx.touch();
                e->acceptProposedAction();
            }
            panel_->relayout();
            return;
        }
        if (ui::readTextMime(e->mimeData(), ui::kTransitionMime, transition)) {
            using Spot = TimelinePanel::TransitionDrop::Spot;
            const TimelinePanel::TransitionDrop drop = panel_->transitionDropAt(e->position(), transition);
            panel_->transitionDrop_ = {};
            if (Layer* layer = drop.ok ? ctx.comp().findLayer(drop.layer) : nullptr) {
                const bool done = drop.spot == Spot::Cut ? applyCutTransition(ctx.comp(), *layer, transition, 1.0)
                                                         : applyTransition(*layer, transition, drop.spot == Spot::In ? TransitionEdge::In : TransitionEdge::Out, 0.5);
                if (done) {
                    ctx.touch();
                    e->acceptProposedAction();
                }
            }
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
            ctx.comp().moveTracks({l->track}, gap);
            ctx.touch();
        }
        e->acceptProposedAction();
        panel_->relayout();
    }

private:
    TimelinePanel* panel_;
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
            else if (i == -3) QToolTip::showText(he->globalPos(), "Drag to a window edge or back to the timeline; right-click for positions", this);
            else if (i == -2) QToolTip::showText(he->globalPos(), "All tools: click to open them, then drag the ones you want here", this);
            else QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
        pressed_ = indexAt(e->position());
        pressPos_ = e->position();
        if (e->button() == Qt::RightButton && pressed_ == -3) {
            QMenu menu;
            const bool floating = parentWidget() != panel_;
            menu.addAction(floating ? "Back to the Timeline" : "Move to the Viewport",
                           [this, floating] { emit panel_->toolbarMoveRequested(!floating, QPoint(-1, -1)); });
            menu.addSeparator();
            const char* labels[] = {"Float at top", "Float at bottom", "Float at left", "Float at right"};
            const QPointF positions[] = {{0.5, 0}, {0.5, 1}, {0, 0.5}, {1, 0.5}};
            for (int i = 0; i < 4; ++i)
                menu.addAction(labels[i], [this, position = positions[i]] { emit panel_->toolbarDockRequested(position); });
            menu.exec(e->globalPosition().toPoint());
            pressed_ = -1;
            hover_ = -1;
            unsetCursor();
            update();
        }
        if (e->button() == Qt::RightButton && pressed_ >= 0) {
            const int at = pressed_;
            QMenu menu;
            menu.addAction("Remove from Toolbar", [this, at] { removeAt(at); });
            menu.addAction("Customize...", [this] { emit panel_->toolsPanelRequested(); });
            menu.exec(e->globalPosition().toPoint());
            pressed_ = -1;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if ((e->buttons() & Qt::LeftButton) && pressed_ == -3 && (e->position() - pressPos_).manhattanLength() > 4) {
            pressed_ = -1; // the grip: the whole bar goes where it is dropped
            auto* drag = new QDrag(this);
            auto* mime = new QMimeData;
            mime->setData(ui::kToolbarMime, QByteArray::number(static_cast<int>(pressPos_.x())) + "," +
                          QByteArray::number(static_cast<int>(pressPos_.y())));
            drag->setMimeData(mime);
            drag->setPixmap(grab());
            drag->setHotSpot(pressPos_.toPoint());
            emit panel_->toolbarDragActive(true);
            drag->exec(Qt::MoveAction);
            emit panel_->toolbarDragActive(false);
            hover_ = -1;
            unsetCursor();
            update();
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
        hover_ = -1;
        unsetCursor();
        update();
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
            if (const int i = ui::toolIndex(id); i >= 0) out.push_back(i);
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

    TimelinePanel* panel_;
    bool vertical_ = false;
    int hover_ = -1, pressed_ = -1, dropAt_ = -1;
    QPointF pressPos_;
};

// --- panel -------------------------------------------------------------------------------------

TimelinePanel::TimelinePanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setAcceptDrops(true); // the toolbar, dragged back here
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
    connect(&ui::AppSignals::get(), &ui::AppSignals::toolbarChanged, this, [this] {
        pill_->updateGeometry();
        pill_->update();
    });
    connect(&ui::AppSignals::get(), &ui::AppSignals::appearanceChanged, this, [this] { relayout(); });
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
    snap_ = button(ui::Icon::Magnet, "Snapping (S): clips, keyframes and objects snap. Hold Ctrl to bypass",
                   [this] { ctx_.snapping = !ctx_.snapping; });
    bar->addSpacing(12);
    button(ui::Icon::ToStart, "Go to start (Home)", [this] {
        ctx_.playing = false;
        ctx_.setTime(0.0);
    });
    button(ui::Icon::StepBack, "Previous frame (Left)", [this] { ctx_.stepFrames(-1); });
    play_ = button(ui::Icon::Play, "Play / Pause (Space)", [this] { ctx_.playing = !ctx_.playing; });
    play_->setActive(true);
    button(ui::Icon::StepForward, "Next frame (Right)", [this] { ctx_.stepFrames(1); });
    button(ui::Icon::ToEnd, "Go to end (End)", [this] {
        ctx_.playing = false;
        ctx_.setTime(ctx_.comp().duration);
    });
    loop_ = button(ui::Icon::Loop, "Loop playback", [this] { ctx_.loop = !ctx_.loop; });

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
        {"Easy Ease", "Smooth in and out of the selected keyframes (F9)"},
        {"Ease In", "Slow down into the selected keyframes (Shift+F9)"},
        {"Ease Out", "Speed up out of the selected keyframes (Ctrl+Shift+F9)"},
        {"Linear", "Constant speed"},
        {"Hold", "Jump to the next keyframe's value"},
    };
    const Ease kinds[] = {Ease::EasyEase, Ease::EaseIn, Ease::EaseOut, Ease::Linear, Ease::Hold};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QPushButton(eases[i].first);
        b->setToolTip(eases[i].second);
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
    graph_ = button(ui::Icon::Graph, "Graph Editor: edit the curves of the selected layer's keyframes", [this] {
        graphMode_ = !graphMode_;
        graphDrag_ = {};
        relayout();
    });
    addKeys_ = button(ui::Icon::Plus, "Add keyframes at the playhead to the curves shown (right-click the graph for more)",
                      [this] { addKeysAtPlayhead(); });
    animOnly_ = button(ui::Icon::Diamond, "Show only animated properties", [this] {
        animatedOnly_ = !animatedOnly_;
        relayout();
    });
    bar->addSpacing(8);
    button(ui::Icon::ZoomOut, "Zoom out (Ctrl+Wheel)", [this] { zoomAround(trackX0() + trackW() * 0.5f, 1.0 / 1.5); });
    button(ui::Icon::ZoomIn, "Zoom in (Ctrl+Wheel)", [this] { zoomAround(trackX0() + trackW() * 0.5f, 1.5); });
    button(ui::Icon::Reset, "Fit the whole composition", [this] {
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

void TimelinePanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(ui::kToolbarMime)) e->acceptProposedAction();
}

void TimelinePanel::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasFormat(ui::kToolbarMime)) return;
    e->acceptProposedAction();
    emit toolbarMoveRequested(false, QPoint());
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
    scroll_ = std::clamp(scroll_, 0.0, std::max(0.0, contentEnd + kDefaultLayerLength - (w - 24.0) / pxPerSec_));
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
            if (animatedOnly_ && !prop.anim->isAnimated()) continue;
            if (prop.effectIndex >= 0 && !effectsHeader) {
                effectsHeader = true;
                effectsOpen = addGroup("Effects", 0.0f);
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
    auto* top = new QPushButton("Composition");
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
    auto* note = new QLabel("Editing inside a compound  ·  Esc (with nothing selected) goes back");
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
    frameInfo_->setText(QString("frame %1").arg(comp.frameAt(ctx_.time)));
    frameInfo_->setToolTip(QString("Frame %1 of %2  ·  %3 fps").arg(comp.frameAt(ctx_.time)).arg(comp.frameCount() - 1).arg(comp.fps, 0, 'g', 4));
    play_->setIcon(ctx_.playing ? ui::Icon::Pause : ui::Icon::Play);
    snap_->setActive(ctx_.snapping);
    loop_->setActive(ctx_.loop);
    graph_->setActive(graphMode_);
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
                      std::to_string(static_cast<int>(ctx_.tool)) + ":" + std::to_string(ctx_.selectedKeys.size()) + ":";
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

// --- painting -------------------------------------------------------------------------------

void TimelinePanel::paintRuler(QPainter& p, int width) {
    const Composition& comp = ctx_.comp();
    ui::Draw d(p);
    const float w = static_cast<float>(width), h = kRulerHeight;
    d.rectFilled({0, 0}, {w, h}, theme::kBgDeep, 5.0f);
    const QFont base = p.font();
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.8);
    small.setWeight(QFont::DemiBold);
    p.setFont(small);
    d.text({12, 6}, theme::kTextFaint, QString("LAYERS"));
    QFont ticks = base;
    ticks.setPointSizeF(base.pointSizeF() * 0.9);
    p.setFont(ticks);

    const float x0 = trackX0(), x1 = x0 + trackW();
    d.pushClip({x0, 0}, {x1, h});
    // Pick a tick spacing that keeps labels readable.
    const double fd = comp.frameDuration();
    const double candidates[] = {fd, fd * 2, fd * 5, fd * 10, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300};
    double major = candidates[std::size(candidates) - 1];
    for (double c : candidates)
        if (c * pxPerSec_ >= 64.0) {
            major = c;
            break;
        }
    const double minor = major / 5.0 >= fd * 0.999 && (major / 5.0) * pxPerSec_ >= 6.0 ? major / 5.0 : 0.0;
    const double tStart = std::floor(scroll_ / major) * major;
    const double tEnd = xToTime(x1);
    for (double t = tStart; t <= tEnd + major; t += major) {
        const float x = timeToX(t);
        d.line({x, h - 10}, {x, h}, theme::kTextFaint);
        char label[32];
        const double whole = std::round(t);
        if (std::fabs(t - whole) < 1e-6) std::snprintf(label, sizeof(label), "%.0fs", whole);
        else std::snprintf(label, sizeof(label), "%df", static_cast<int>(std::lround((t - std::floor(t)) * comp.fps)));
        d.text({x + 4, 3}, theme::kTextDim, std::string(label));
        if (minor > 0.0)
            for (int i = 1; i < 5; ++i) {
                const float mx = timeToX(t + minor * i);
                d.line({mx, h - 5}, {mx, h}, theme::kBorder);
            }
    }
    // Past the end of the composition (nothing to mark while it is empty: it grows with its content).
    const float endX = timeToX(visibleEnd());
    if (!comp.layers.empty() || hasRecordingPreview()) d.rectFilled({endX, 0}, {x1, h}, theme::shade(90));
    // Playhead handle.
    const float ph = timeToX(ctx_.time);
    const Pt head[5] = {{ph - 6, 3}, {ph + 6, 3}, {ph + 6, 14}, {ph, 20}, {ph - 6, 14}};
    d.polyFilled(head, 5, theme::kAccent);
    d.line({ph, 18}, {ph, h}, theme::kAccent, 1.5f);
    d.popClip();
}

void TimelinePanel::paintRows(QPainter& p, QSize size) {
    computeItems();
    ui::Draw d(p);
    const float width = static_cast<float>(size.width()), bottom = static_cast<float>(size.height());
    if (graphMode_) {
        paintGraph(p, size);
    } else {
        for (const Item& item : items_) {
            if (item.top > bottom || item.top + item.h < 0) continue;
            switch (item.kind) {
            case Item::Track: paintTrack(p, item, width); break;
            case Item::Recording: paintRecording(p, item, width); break;
            case Item::Group: paintGroup(p, item, width); break;
            case Item::Prop: paintPropKeys(p, item, width); break;
            }
        }
        if (ctx_.comp().layers.empty() && !hasRecordingPreview()) {
            const QString hint = "Add text or graphics from the Objects panel to start animating.";
            d.text({(width - d.textWidth(hint)) * 0.5f, rowsBottom_ + 24}, theme::kTextFaint, hint);
        }
    }

    // Column divider, area past the end of the composition, playhead.
    const float x0 = trackX0(), x1 = x0 + trackW();
    d.line({x0 - 1, 0}, {x0 - 1, bottom}, theme::kBorder);
    d.pushClip({x0, 0}, {x1, bottom});
    const float endX = timeToX(visibleEnd());
    if (endX < x1 && (!ctx_.comp().layers.empty() || hasRecordingPreview())) d.rectFilled({endX, 0}, {x1, bottom}, theme::shade(80));
    const float ph = timeToX(ctx_.time);
    d.line({ph, 0}, {ph, bottom}, theme::kAccent, 1.5f);
    if (snapLineTime_ >= 0.0) { // what the dragged item snapped to
        const float sx = timeToX(snapLineTime_);
        d.line({sx, 0}, {sx, bottom}, theme::kSnap, 1.0f);
    }
    // Razor tool: show where the cut will happen.
    if (!graphMode_ && ctx_.tool == Tool::Razor && hoverX_ >= x0) {
        const float cx = timeToX(ctx_.comp().snapToFrame(xToTime(hoverX_)));
        for (float yy = 0; yy < rowsBottom_; yy += 8.0f) d.line({cx, yy}, {cx, std::min(yy + 4.0f, rowsBottom_)}, theme::kDanger, 1.5f);
    }
    d.popClip();

    // Where a vertical clip / track drag lands.
    if (restacking_ && (drag_ == Drag::LayerMove || drag_ == Drag::LayerName)) {
        const DropTarget target = dropTargetAt(dragY_, drag_);
        if (target.valid && !target.newTrack) { // joining an existing track: outline that row
            for (const Item& r : items_) {
                if (r.kind != Item::Track || r.track != target.track) continue;
                const Rgba col = target.fits ? theme::kAccent : theme::kDanger;
                d.rectFilled({x0, r.top + 1}, {width - 2, r.top + r.h - 1}, target.fits ? theme::accent(30) : theme::withAlpha(theme::kDanger, 30), 4.0f);
                d.rect({x0, r.top + 1}, {width - 2, r.top + r.h - 1}, col, 4.0f, 2.0f);
            }
        } else if (target.valid) {
            // The gap is in visible-track order (see dropGapAt).
            float y = rowsBottom_;
            int visible = 0;
            for (const Item& r : items_)
                if (r.kind == Item::Track) {
                    if (visible == target.gap) {
                        y = r.top;
                        break;
                    }
                    ++visible;
                }
            d.line({4, y}, {width - 4, y}, theme::kAccent, 2.0f);
            d.circleFilled({6, y}, 4.0f, theme::kAccent);
        }
    }
    // Box selection.
    if (marquee_) {
        const QRectF r = QRectF(marqueeStart_, marqueeEnd_).normalized();
        if (r.width() >= 3 || r.height() >= 3) {
            d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), theme::accent(22));
            d.rect(Pt(r.topLeft()), Pt(r.bottomRight()), theme::accent(160));
        }
    }
    paintTransitionDrop(d);
    // An object or media item dragged in: a new row at the gap, starting at the drop time.
    if (dropActive_) {
        float gy = rowsBottom_;
        int visible = 0;
        for (const Item& r : items_)
            if (r.kind == Item::Track) {
                if (visible == dropGap_) {
                    gy = r.top;
                    break;
                }
                ++visible;
            }
        if (items_.empty()) gy = 4;
        const float tx = timeToX(dropTime_);
        d.line({0, gy}, {width, gy}, theme::kAccent, 2.0f);
        d.rectFilled({tx, gy - 9}, {std::max(tx + 60, timeToX(dropTime_ + dropLength_)), gy + 9}, theme::accent(60), 4.0f);
        d.line({tx, 0}, {tx, bottom}, theme::kAccent, 1.0f);
        d.text({tx + 6, gy - 8}, theme::kText, ui::formatTimecode(dropTime_, ctx_.comp().fps));
    }
}

void TimelinePanel::paintRecording(QPainter& p, const Item& item, float width) {
    ui::Draw d(p);
    const float y = item.top, h = item.h, cy = y + h * 0.5f;
    d.rectFilled({0, y}, {width, y + h}, theme::accent(24));
    d.line({0, y + h - 1}, {width, y + h - 1}, theme::kBorder);
    ui::drawIcon(d, ui::Icon::Microphone, {kChipX + 18, cy}, 14.0f, theme::kDanger);
    d.pushClip({kNameX, y}, {leftWidth_ - 6, y + h});
    d.text({kNameX, cy - d.textHeight() * 0.5f}, theme::kText, QStringLiteral("Recording"));
    d.popClip();
    d.pushClip({trackX0(), y}, {trackX0() + trackW(), y + h});
    const float begin = timeToX(recordingStart_);
    const float end = std::max(begin + 4, timeToX(recordingStart_ + recordingPreview_.seconds));
    d.rectFilled({begin, y + 4}, {end, y + h - 4}, theme::accent(55), 4.0f);
    d.rect({begin, y + 4}, {end, y + h - 4}, theme::kAccent, 4.0f);
    const auto& peaks = recordingPreview_.peaks;
    const float first = std::max(begin + 1, trackX0()), last = std::min(end - 1, trackX0() + trackW());
    for (float x = first; x < last; x += 2) {
        const double t0 = std::max(0.0, xToTime(x) - recordingStart_);
        const double t1 = std::max(0.0, xToTime(x + 2) - recordingStart_);
        const size_t from = static_cast<size_t>(t0 * media::RecordingPreview::kPeaksPerSecond);
        const size_t to = std::min(peaks.size(), static_cast<size_t>(t1 * media::RecordingPreview::kPeaksPerSecond) + 1);
        float peak = 0.0f;
        for (size_t i = from; i < to; ++i) peak = std::max(peak, peaks[i]);
        const float amplitude = std::max(0.5f, std::sqrt(peak) * (h - 10) * 0.45f);
        d.line({x, cy - amplitude}, {x, cy + amplitude}, theme::kAccent, 1.4f);
    }
    d.popClip();
}

void TimelinePanel::paintTrack(QPainter& p, const Item& item, float width) {
    ui::Draw d(p);
    auto& layers = ctx_.comp().layers;
    if (item.first + item.count > static_cast<int>(layers.size())) return;
    const float y = item.top, h = item.h;
    const Layer* focus = ctx_.comp().findLayer(item.layer);
    if (!focus) return;
    bool anySelected = false;
    for (int i = item.first; i < item.first + item.count; ++i) anySelected |= ctx_.isSelected(layers[i].id);
    const bool hasPrimary = focus->id == ctx_.primaryId();
    if (anySelected) {
        d.rectFilled({0, y}, {width, y + h}, theme::accent(hasPrimary ? 34 : 24));
        d.rectFilled({0, y + 2}, {3, y + h - 2}, theme::kAccent, 1.5f);
    }
    d.line({0, y + h - 1}, {width, y + h - 1}, theme::ink(10));

    // --- left column: controls and name ---------------------------------------
    const float cy = y + h * 0.5f;
    ui::drawIcon(d, expanded_.count(item.track) ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, {kExpandX + kButton * 0.5f, cy},
                 kButton * 0.6f, theme::kTextDim);
    ui::drawIcon(d, focus->visible ? ui::Icon::Eye : ui::Icon::EyeOff, {kVisX + kButton * 0.5f, cy}, kButton * 0.65f,
                 focus->visible ? theme::kTextDim : theme::kTextFaint);
    ui::drawIcon(d, focus->locked ? ui::Icon::Lock : ui::Icon::Unlock, {kLockX + kButton * 0.5f, cy}, kButton * 0.65f,
                 focus->locked ? theme::kAccent : theme::kTextFaint);
    d.rectFilled({kChipX + 2, y + 8}, {kChipX + 5, y + h - 8}, toRgba(labelColor(focus->label)), 2.0f);
    ui::drawIcon(d, ui::iconForLayer(*focus), {kChipX + 18, cy}, 14.0f, anySelected ? theme::kAccent : theme::kTextDim);
    const float nameW = std::max(10.0f, leftWidth_ - kNameX - 8);
    if (renaming_ != focus->id) {
        const Rgba col = focus->visible ? (anySelected ? theme::kText : theme::kTextDim) : theme::kTextFaint;
        d.pushClip({kNameX, y}, {kNameX + nameW, y + h});
        const float textY = y + (h - d.textHeight()) * 0.5f;
        d.text({kNameX, textY}, col, focus->name);
        if (item.count > 1) { // e.g. a track that was cut into pieces
            const QString badge = QString("%1 clips").arg(item.count);
            d.text({kNameX + nameW - d.textWidth(badge), textY}, theme::kTextFaint, badge);
        }
        d.popClip();
    }

    // --- track: clip bars ------------------------------------------------------
    const float x0 = trackX0(), x1 = x0 + trackW();
    d.pushClip({x0, y}, {x1, y + h});
    const bool expanded = expanded_.count(item.track) > 0;
    for (int i = item.first; i < item.first + item.count; ++i) {
        Layer& layer = layers[i];
        const bool selected = ctx_.isSelected(layer.id);
        const bool primary = ctx_.primaryId() == layer.id;
        const float bx1 = timeToX(layer.inPoint), bx2 = timeToX(layer.outPoint);
        const Color lc = labelColor(layer.label);
        const Pt a(bx1 + 1, y + 4), b(bx2 - 1, y + h - 4); // 1px gap shows cut points
        d.rectFilled(a, b, toRgba(lc, layer.visible ? (selected ? 0.42f : 0.26f) : 0.1f), 5.0f);
        if (selected) d.rect(a, b, primary ? theme::kText : theme::ink(170), 5.0f, primary ? 1.8f : 1.3f);
        if (layer.type == LayerType::Media && layer.media.hasAudio) paintWaveform(p, layer, a, b, toRgba(lc, 0.75f));
        auto paintTransition = [&](const ClipTransition& transition, bool incoming) {
            if (!transition.active()) return;
            const float duration = std::max(16.0f, std::fabs(timeToX(layer.inPoint + transition.duration) - bx1));
            const float left = incoming ? bx1 + 2.0f : bx2 - duration - 2.0f;
            const float right = incoming ? bx1 + duration + 2.0f : bx2 - 2.0f;
            const bool selectedTransition = selectedTransitionLayer_ == layer.id && selectedTransitionIncoming_ == incoming;
            d.rectFilled({left, y + 5}, {right, y + h - 5}, theme::accent(85), 5.0f);
            d.rect({left, y + 5}, {right, y + h - 5}, selectedTransition ? theme::kText : theme::kAccent, 5.0f,
                   selectedTransition ? 2.0f : 1.0f);
            // A transition across a cut shows one icon, on the cut (its two halves meet there).
            if (!transition.joined) ui::drawIcon(d, ui::Icon::Transition, {(left + right) * 0.5f, y + h * 0.5f}, 13.0f, theme::kAccent);
            const float handle = incoming ? right : left;
            d.line({handle, y + 8}, {handle, y + h - 8}, theme::kAccent, 2.0f);
        };
        paintTransition(layer.transitionIn, true);
        paintTransition(layer.transitionOut, false);
        if (layer.transitionOut.joined) {
            d.circleFilled({bx2, y + h * 0.5f}, 9.0f, theme::kBgPanel);
            ui::drawIcon(d, ui::Icon::Transition, {bx2, y + h * 0.5f}, 13.0f, theme::kAccent);
        }
        d.rectFilled({bx1 + 3, y + 9}, {bx1 + 5, y + h - 9}, toRgba(lc, 0.8f), 1.0f);
        d.rectFilled({bx2 - 5, y + 9}, {bx2 - 3, y + h - 9}, toRgba(lc, 0.8f), 1.0f);
        if (layer.type == LayerType::Text && layer.text.subtitle && !layer.text.captions.empty()) {
            // Subtitles: each caption a block along the clip, with its words.
            d.pushClip({std::max(bx1, x0), y}, {std::min(bx2, x1), y + h});
            for (const Caption& c : layer.text.captions) {
                const float cx0 = timeToX(std::max(c.start, layer.inPoint)), cx1 = timeToX(std::min(c.end, layer.outPoint));
                if (cx1 < x0 || cx0 > x1 || cx1 - cx0 < 1.0f) continue;
                d.rectFilled({cx0 + 1, y + 7}, {cx1 - 1, y + h - 7}, toRgba(lc, 0.45f), 3.0f);
                if (cx1 - cx0 > 24) {
                    d.pushClip({cx0 + 4, y}, {cx1 - 4, y + h});
                    QString words = QString::fromStdString(c.text);
                    words.replace(QChar('\n'), QChar(' '));
                    d.text({cx0 + 6, y + (h - d.textHeight()) * 0.5f}, theme::ink(210), words);
                    d.popClip();
                }
            }
            d.popClip();
        } else {
            d.pushClip({std::max(bx1, x0), y}, {std::min(bx2, x1), y + h});
            d.text({std::max(bx1, x0) + 10, y + (h - d.textHeight()) * 0.5f}, theme::ink(150), layer.name);
            d.popClip();
        }
        // Summary of the clip's keyframes (within the clip only, e.g. after a cut).
        if (!expanded)
            for (const PropertyRef& pr : layer.properties())
                for (int k = 0; k < pr.anim->keyCount(); ++k) {
                    const double kt = pr.anim->keyTime(k);
                    if (kt < layer.inPoint - kTimeEpsilon || kt > layer.outPoint + kTimeEpsilon) continue;
                    ui::drawDiamond(d, {timeToX(kt), y + h - 7}, 3.0f, theme::ink(200), 0);
                }
    }
    d.popClip();
}

void TimelinePanel::paintGroup(QPainter& p, const Item& item, float width) {
    ui::Draw d(p);
    const float y = item.top, h = item.h, x0 = item.indent;
    d.rectFilled({0, y}, {width, y + h}, theme::shade(60));
    const bool open = !collapsedGroups_.count(item.groupKey);
    ui::drawIcon(d, open ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, {x0 + 36, y + h * 0.5f}, 12.0f, theme::kTextDim);
    p.save();
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    d.text({x0 + 48, y + (h - d.textHeight()) * 0.5f}, theme::kTextDim, item.groupLabel);
    p.restore();
}

void TimelinePanel::paintPropKeys(QPainter& p, const Item& item, float width) {
    ui::Draw d(p);
    Layer* layer = ctx_.comp().findLayer(item.layer);
    auto ref = findProperty(ctx_, item.layer, item.prop, item.effectIndex);
    if (!layer || !ref) return;
    const AnimatedBase& anim = *ref->anim;
    const float y = item.top, h = item.h;
    // Selection tint: light for rows of the selected layer, stronger where keys are selected.
    const bool hasSelectedKey = std::any_of(ctx_.selectedKeys.begin(), ctx_.selectedKeys.end(), [&](const KeySelection& k) {
        return k.layerId == layer->id && k.propertyId == item.prop;
    });
    const Rgba rowBg = hasSelectedKey ? theme::accent(40) : ctx_.isSelected(layer->id) ? theme::accent(14) : theme::shade(40);
    d.rectFilled({0, y}, {width, y + h}, rowBg);
    if (hasSelectedKey) d.rectFilled({0, y + 2}, {3, y + h - 2}, theme::kAccent, 1.5f);

    const float x0 = trackX0(), x1 = x0 + trackW(), cy = y + h * 0.5f;
    d.pushClip({x0, y}, {x1, y + h});
    // Shade time where the clip does not exist: keyframes can't go there.
    d.rectFilled({x0, y}, {timeToX(layer->inPoint), y + h}, theme::shade(70));
    d.rectFilled({timeToX(layer->outPoint), y}, {x1, y + h}, theme::shade(70));
    for (int k = 0; k + 1 < anim.keyCount(); ++k) {
        const float xa = timeToX(anim.keyTime(k)), xb = timeToX(anim.keyTime(k + 1));
        d.line({xa, cy}, {xb, cy}, anim.keyEasing(k) == Easing::Hold ? theme::ink(25) : theme::accent(70), 1.5f);
    }
    for (int k = 0; k < anim.keyCount(); ++k) {
        const bool sel = ctx_.isKeySelected({layer->id, item.prop, anim.keyUid(k)});
        const float kx = timeToX(anim.keyTime(k));
        const bool hot = hoverX_ >= 0 && std::fabs(hoverX_ - kx) <= keyRadius() + 2 && rows_->underMouse() &&
                         rows_->mapFromGlobal(QCursor::pos()).y() >= y && rows_->mapFromGlobal(QCursor::pos()).y() < y + h;
        const Rgba fill = sel ? theme::kAccent : (hot ? theme::kText : theme::kTextDim);
        drawKeyGlyph(d, {kx, cy}, keyRadius() + (sel ? 1.0f : 0.0f), anim.keyEasing(k), fill, sel ? theme::kAccentHover : theme::kBgDeep);
    }
    d.popClip();
}

void TimelinePanel::paintWaveform(QPainter& p, const Layer& layer, QPointF a, QPointF b, uint32_t color) const {
    const media::AudioData data = media::MediaManager::get().audio(layer.media.mediaId);
    if (!data || data->empty()) return;
    const std::vector<float>& s = *data;
    const size_t frames = s.size() / 2;
    const float x1 = std::max(static_cast<float>(a.x()) + 6, trackX0()), x2 = std::min(static_cast<float>(b.x()) - 6, trackX0() + trackW());
    const float mid = static_cast<float>(a.y() + b.y()) * 0.5f, half = static_cast<float>(b.y() - a.y()) * 0.42f;
    constexpr float kStep = 2.0f;
    QPainterPath path;
    for (float x = x1; x < x2; x += kStep) {
        // Peak of the source samples under this column (sparsely sampled: enough for a sketch).
        const double t0 = layer.media.sourceTime(xToTime(x)), t1 = layer.media.sourceTime(xToTime(x + kStep));
        if (t1 <= 0) continue;
        const size_t i0 = static_cast<size_t>(std::max(0.0, t0) * media::kAudioRate);
        const size_t i1 = std::min(frames, static_cast<size_t>(std::max(0.0, t1) * media::kAudioRate) + 1);
        if (i0 >= frames) break;
        const size_t stride = std::max<size_t>(1, (i1 - i0) / 48);
        float peak = 0.0f;
        for (size_t i = i0; i < i1; i += stride) peak = std::max({peak, std::fabs(s[i * 2]), std::fabs(s[i * 2 + 1])});
        const float gain = std::clamp(layer.media.volume.valueAt(xToTime(x)) * 0.01f, 0.0f, 2.0f);
        const float hh = std::max(0.5f, std::sqrt(std::min(1.0f, peak * gain)) * half); // sqrt: quiet audio stays visible
        path.addRect(QRectF(x, mid - hh, kStep - 0.6f, hh * 2));
    }
    p.fillPath(path, ui::qcolor(color));
}

// --- input ------------------------------------------------------------------------------------

const TimelinePanel::Item* TimelinePanel::itemAt(float y) const {
    for (const Item& item : items_)
        if (y >= item.top && y < item.top + item.h) return &item;
    return nullptr;
}

int TimelinePanel::clipHit(const Item& track, float mx, Drag& mode) const {
    // Edges win over bodies so adjacent clips can be trimmed.
    const auto& layers = ctx_.comp().layers;
    int hit = -1;
    mode = Drag::None;
    for (int i = track.first; i < track.first + track.count && i < static_cast<int>(layers.size()); ++i) {
        const float bx1 = timeToX(layers[i].inPoint), bx2 = timeToX(layers[i].outPoint);
        if (std::fabs(mx - bx1) < kEdgeGrab) hit = i, mode = Drag::LayerIn;
        else if (std::fabs(mx - bx2) < kEdgeGrab && mode != Drag::LayerIn) hit = i, mode = Drag::LayerOut;
        else if (mx > bx1 && mx < bx2 && hit < 0) hit = i, mode = Drag::LayerMove;
    }
    return hit;
}

TimelinePanel::TransitionDrop TimelinePanel::transitionDropAt(QPointF pos, const std::string& transition) const {
    constexpr float kCutGrab = 10.0f; // px around a cut that mean "between the two clips"
    const Item* item = itemAt(static_cast<float>(pos.y()));
    if (!item || item->kind != Item::Track) return {};
    const auto& layers = ctx_.comp().layers;
    const float x = static_cast<float>(pos.x());
    const double eps = ctx_.comp().frameDuration() * 0.5;
    const int end = std::min(item->first + item->count, static_cast<int>(layers.size()));
    for (int i = item->first; i < end; ++i)
        for (int j = item->first; j < end; ++j)
            if (i != j && std::fabs(layers[i].outPoint - layers[j].inPoint) < eps && std::fabs(x - timeToX(layers[i].outPoint)) <= kCutGrab)
                return {layers[i].id, TransitionDrop::Spot::Cut,
                        canTakeTransition(layers[i], transition) && canTakeTransition(layers[j], transition)};
    Drag mode;
    const int index = clipHit(*item, x, mode);
    if (index < 0 || index >= static_cast<int>(layers.size())) return {};
    const Layer& l = layers[index];
    const bool atStart = mode == Drag::LayerIn || (mode == Drag::LayerMove && x < (timeToX(l.inPoint) + timeToX(l.outPoint)) * 0.5f);
    return {l.id, atStart ? TransitionDrop::Spot::In : TransitionDrop::Spot::Out, canTakeTransition(l, transition)};
}

TimelinePanel::PresetDrop TimelinePanel::presetDropAt(QPointF pos, const std::string& presetJson) const {
    AnimationPreset preset;
    const Item* item = itemAt(static_cast<float>(pos.y()));
    if (!item || item->kind != Item::Track || !parsePreset(presetJson, preset)) return {};
    Drag mode;
    const int index = clipHit(*item, static_cast<float>(pos.x()), mode);
    if (index < 0 || index >= static_cast<int>(ctx_.comp().layers.size())) return {};
    const Layer& l = ctx_.comp().layers[index];
    const double length = presetDuration(preset, l);
    // From the mouse, kept inside the clip (an exit lands on its end, an entrance on its start).
    double t = ctx_.comp().snapToFrame(xToTime(static_cast<float>(pos.x())));
    t = std::clamp(t, l.inPoint, std::max(l.inPoint, l.outPoint - length));
    if (std::fabs(timeToX(t) - timeToX(l.inPoint)) < 12) t = l.inPoint;
    return {l.id, t, length, l.type != LayerType::Camera && presetAppliesTo(preset, l)};
}

void TimelinePanel::paintTransitionDrop(ui::Draw& d) {
    if (const Layer* layer = presetDrop_.layer ? ctx_.comp().findLayer(presetDrop_.layer) : nullptr)
        for (const Item& r : items_) {
            if (r.kind != Item::Track || r.track != layer->track) continue;
            const float x0 = timeToX(presetDrop_.time), x1 = std::max(timeToX(presetDrop_.time + presetDrop_.length), x0 + 12.0f);
            const Rgba line = presetDrop_.ok ? theme::kAccent : theme::kDanger;
            d.rectFilled({x0, r.top + 3}, {x1, r.top + r.h - 3}, presetDrop_.ok ? theme::accent(110) : theme::withAlpha(theme::kDanger, 60), 5.0f);
            d.rect({x0, r.top + 3}, {x1, r.top + r.h - 3}, line, 5.0f, 2.0f);
            if (!presetDrop_.ok)
                d.text({x1 + 8, r.top + (r.h - d.textHeight()) * 0.5f}, theme::kDanger, QStringLiteral("This animation is not for this kind of layer"));
            break;
        }
    const TransitionDrop& drop = transitionDrop_;
    const Layer* layer = drop.layer ? ctx_.comp().findLayer(drop.layer) : nullptr;
    if (!layer) return;
    for (const Item& r : items_) {
        if (r.kind != Item::Track || r.track != layer->track) continue;
        using Spot = TransitionDrop::Spot;
        const float y = r.top, h = r.h;
        const double len = drop.spot == Spot::Cut ? 0.5 : std::min(0.5, (layer->outPoint - layer->inPoint) * 0.5);
        const double t0 = drop.spot == Spot::In ? layer->inPoint : layer->outPoint - len;
        const double t1 = drop.spot == Spot::Out ? layer->outPoint : drop.spot == Spot::In ? layer->inPoint + len : layer->outPoint + len;
        const float x0 = timeToX(t0), x1 = std::max(timeToX(t1), x0 + 16.0f);
        const Rgba line = drop.ok ? theme::kAccent : theme::kDanger;
        d.rectFilled({x0, y + 3}, {x1, y + h - 3}, drop.ok ? theme::accent(110) : theme::withAlpha(theme::kDanger, 60), 5.0f);
        d.rect({x0, y + 3}, {x1, y + h - 3}, line, 5.0f, 2.0f);
        if (!drop.ok) {
            const char* why = layer->type == LayerType::Camera ? "Cameras take no transitions"
                              : (layer->type == LayerType::Media && layer->media.hasAudio && !layer->media.hasVideo)
                                  ? "Sound clips take no transitions (use Fade In / Out in Effects)"
                                  : "This clip cannot take this transition";
            d.text({x1 + 8, y + (h - d.textHeight()) * 0.5f}, theme::kDanger, QString::fromUtf8(why));
        }
        return;
    }
}

TimelinePanel::TransitionHit TimelinePanel::transitionHit(const Item& track, float mx) const {
    const auto& layers = ctx_.comp().layers;
    for (int i = track.first; i < track.first + track.count && i < static_cast<int>(layers.size()); ++i) {
        const Layer& layer = layers[i];
        const float begin = timeToX(layer.inPoint), end = timeToX(layer.outPoint);
        auto findHit = [&](const ClipTransition& transition, bool incoming) -> TransitionHit {
            if (!transition.active()) return {};
            const float width = std::max(16.0f, static_cast<float>(transition.duration * pxPerSec_));
            const float left = incoming ? begin + 2.0f : end - width - 2.0f;
            const float right = incoming ? begin + width + 2.0f : end - 2.0f;
            if (mx < left || mx > right) return {};
            const float handle = incoming ? right : left;
            return {layer.id, incoming, std::fabs(mx - handle) <= kEdgeGrab};
        };
        if (TransitionHit hit = findHit(layer.transitionIn, true)) return hit;
        if (TransitionHit hit = findHit(layer.transitionOut, false)) return hit;
    }
    return {};
}

bool TimelinePanel::deleteSelectedTransition() {
    if (!selectedTransitionLayer_) return false;
    Layer* layer = ctx_.comp().findLayer(selectedTransitionLayer_);
    const TransitionEdge edge = selectedTransitionIncoming_ ? TransitionEdge::In : TransitionEdge::Out;
    const bool removed = layer && removeTransition(ctx_.comp(), *layer, edge); // a cut's other half goes too
    selectedTransitionLayer_ = 0;
    if (removed) ctx_.touch();
    relayout();
    return removed;
}

void TimelinePanel::clickLayer(uint64_t id) {
    selectedTransitionLayer_ = 0;
    if (ctrlHeld()) ctx_.toggleSelected(id);
    else if (shiftHeld()) ctx_.selectRange(id);
    else if (!ctx_.isSelected(id)) ctx_.select(id);
    else if (ctx_.primaryId() != id) { // keep the group, make this one primary
        std::erase(ctx_.selection, id);
        ctx_.selection.push_back(id);
        ctx_.clearKeySelection();
    }
}

void TimelinePanel::clickTrack(const Item& row) {
    const auto ids = clipsOfTrack(ctx_.comp(), row.track);
    if (ids.empty()) return;
    const bool all = std::all_of(ids.begin(), ids.end(), [&](uint64_t id) { return ctx_.isSelected(id); });
    if (ctrlHeld()) {
        for (uint64_t id : ids)
            if (all) std::erase(ctx_.selection, id);
            else if (!ctx_.isSelected(id)) ctx_.selection.push_back(id);
        ctx_.clearKeySelection();
    } else if (shiftHeld()) {
        ctx_.selectRange(ids.front());
    } else if (!all) { // keep a multi-track selection when clicking one of its tracks
        ctx_.select(ids.front());
        for (size_t i = 1; i < ids.size(); ++i) ctx_.selection.push_back(ids[i]);
    }
}

void TimelinePanel::showTrackMenu(QPoint global) {
    QMenu menu;
    menu.setToolTipsVisible(true);
    const uint64_t focus = ctx_.primaryId();
    menu.addAction("Rename", [this, focus] {
        computeItems();
        for (const Item& it : items_)
            if (it.kind == Item::Track && it.layer == focus) startRename(it);
    });
    menu.addAction("Duplicate\tCtrl+D", [this] { ctx_.duplicateSelected(); });
    menu.addAction("Split at Playhead\tCtrl+K", [this] { ctx_.splitSelectedAtPlayhead(); });
    if (ui::canTranscribe(ctx_, focus)) {
        QAction* t = menu.addAction(ui::menuIcon(ui::Icon::Subtitles, theme::kTextDim), "Transcribe to Subtitles...",
                                    [this, focus] { ui::transcribeClip(ctx_, focus, 0, this); });
        t->setToolTip("The words spoken in this clip become a Subtitles layer, at their time");
    }
    // 3D Layer, like the Properties checkbox: on for all when any is off.
    std::vector<Layer*> spatial;
    for (Layer* l : ctx_.selectedLayers())
        if (l->type != LayerType::Camera && l->type != LayerType::Background && !l->parent) spatial.push_back(l);
    if (!spatial.empty()) {
        const bool all = std::all_of(spatial.begin(), spatial.end(), [](const Layer* l) { return l->threeD; });
        QAction* threeD = menu.addAction("3D Layer", [this, all] {
            for (Layer* l : ctx_.selectedLayers())
                if (l->type != LayerType::Camera && l->type != LayerType::Background && !l->parent) l->threeD = !all;
            ctx_.touch();
        });
        threeD->setCheckable(true);
        threeD->setChecked(all);
        threeD->setToolTip("Seen in perspective by the camera at its Z position");
    }
    menu.addSeparator();
    menu.addAction("Create Compound\tCtrl+G", [this] { ctx_.createCompound(); });
    menu.addAction("Release Compound\tCtrl+Shift+G", [this] { ctx_.releaseSelectedCompounds(); })->setEnabled(ctx_.selectionHasCompound());
    fillEffectMenu(menu.addMenu("Effect"), ctx_);
    menu.addSeparator();
    menu.addAction("Bring to Front\tCtrl+Shift+]", [this] { ctx_.shiftSelected(-1000); });
    menu.addAction("Bring Forward\tCtrl+]", [this] { ctx_.shiftSelected(-1); });
    menu.addAction("Send Backward\tCtrl+[", [this] { ctx_.shiftSelected(1); });
    menu.addAction("Send to Back\tCtrl+Shift+[", [this] { ctx_.shiftSelected(1000); });
    menu.addSeparator();
    menu.addAction("Delete\tDel", [this] { ctx_.deleteSelected(); });
    menu.exec(global);
    relayout();
}

void TimelinePanel::showKeyMenu(QPoint global) {
    QMenu menu;
    const int n = static_cast<int>(ctx_.selectedKeys.size());
    menu.addSection(QString("%1 keyframe%2").arg(n).arg(n == 1 ? "" : "s"));
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        if (e == Easing::Custom) continue;
        menu.addAction(easingLabel(e), [this, e] { ctx_.setSelectedKeysEasing(e); });
    }
    // Custom Curve: the bezier editor, its four numbers and some starting points.
    QMenu* custom = menu.addMenu("Custom Curve");
    auto* box = new QWidget;
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(8, 8, 8, 8);
    auto* editor = new CurveEditor(ctx_.selectedKeysCurve(), [this](const BezierEase& c) { ctx_.setSelectedKeysCurve(c); });
    col->addWidget(editor);
    auto* numbers = new QHBoxLayout;
    std::vector<ui::DragValue*> values;
    BezierEase start = ctx_.selectedKeysCurve();
    float* fields[4] = {&start.x1, &start.y1, &start.x2, &start.y2};
    auto current = std::make_shared<BezierEase>(start);
    auto apply = [this, editor, current] {
        current->x1 = std::clamp(current->x1, 0.0f, 1.0f);
        current->x2 = std::clamp(current->x2, 0.0f, 1.0f);
        ctx_.setSelectedKeysCurve(*current);
        editor->setCurve(*current);
    };
    for (int i = 0; i < 4; ++i) {
        auto* v = new ui::DragValue;
        v->setRange(i % 2 == 0 ? 0.0f : -1.0f, i % 2 == 0 ? 1.0f : 2.0f);
        v->setSpeed(0.005f);
        v->setFormat("%.2f");
        v->setValue(*fields[i]);
        connect(v, &ui::DragValue::valueChanged, &menu, [current, apply, i](float x) {
            float* f[4] = {&current->x1, &current->y1, &current->x2, &current->y2};
            *f[i] = x;
            apply();
        });
        numbers->addWidget(v);
        values.push_back(v);
    }
    col->addLayout(numbers);
    auto* startLabel = new QLabel("Start from:");
    ui::setTone(startLabel, ui::Tone::TextFaint);
    col->addWidget(startLabel);
    auto* starts = new QHBoxLayout;
    const std::pair<Easing, const char*> presets[] = {
        {Easing::Linear, "Lin"}, {Easing::EaseIn, "In"}, {Easing::EaseOut, "Out"}, {Easing::EaseInOut, "In Out"}, {Easing::BackOut, "Back"}};
    for (const auto& [e, label] : presets) {
        auto* b = new QPushButton(label);
        connect(b, &QPushButton::clicked, &menu, [current, apply, values, e] {
            *current = bezierFor(e);
            const float f[4] = {current->x1, current->y1, current->x2, current->y2};
            for (int i = 0; i < 4; ++i) values[i]->setValue(f[i]);
            apply();
        });
        starts->addWidget(b);
    }
    col->addLayout(starts);
    auto* action = new QWidgetAction(custom);
    action->setDefaultWidget(box);
    custom->addAction(action);
    if (ctx_.selectionHasPathKeys()) {
        menu.addSection("Motion path");
        menu.addAction("Auto Bezier", [this] { ctx_.setSelectedKeysPath(PathMode::Auto); });
        menu.addAction("Linear (corner)", [this] { ctx_.setSelectedKeysPath(PathMode::Linear); });
        menu.addAction("Custom Handles", [this] { ctx_.setSelectedKeysPath(PathMode::Custom); });
    }
    menu.addSeparator();
    menu.addAction("Delete\tDel", [this] { ctx_.deleteSelectedKeys(); });
    menu.exec(global);
    relayout();
}

void TimelinePanel::startRename(const Item& track) {
    Layer* layer = ctx_.comp().findLayer(track.layer);
    if (!layer) return;
    delete rename_;
    renaming_ = layer->id;
    rename_ = new QLineEdit(QString::fromStdString(layer->name), rows_);
    const int h = 24;
    rename_->setGeometry(static_cast<int>(kNameX) - 4, static_cast<int>(track.top + (track.h - h) * 0.5f),
                         static_cast<int>(leftWidth_ - kNameX - 4), h);
    rename_->selectAll();
    rename_->show();
    rename_->setFocus();
    const uint64_t id = layer->id;
    connect(rename_, &QLineEdit::textEdited, this, [this, id](const QString& t) {
        if (Layer* l = ctx_.comp().findLayer(id)) {
            l->name = t.toStdString();
            ctx_.touch();
        }
    });
    connect(rename_, &QLineEdit::editingFinished, this, [this] {
        renaming_ = 0;
        if (rename_) rename_->deleteLater();
        rename_ = nullptr;
        rows_->update();
    });
}

void TimelinePanel::rowsPress(QMouseEvent* e) {
    computeItems();
    const QPointF pos = e->position();
    const float x = static_cast<float>(pos.x()), y = static_cast<float>(pos.y());
    if (graphMode_) {
        graphPress(e);
        return;
    }
    const bool left = e->button() == Qt::LeftButton, right = e->button() == Qt::RightButton;
    const Item* item = itemAt(y);
    if (!item) { // empty space below the rows: click to deselect, drag to box-select
        if (left) beginMarquee(pos);
        relayout();
        return;
    }
    if (item->kind == Item::Recording) return;
    auto& layers = ctx_.comp().layers;
    if (item->kind == Item::Track) {
        const Item row = *item;
        if (x < leftWidth_) {
            auto inButton = [&](float bx) { return left && x >= bx && x < bx + kButton; };
            Layer* focus = ctx_.comp().findLayer(row.layer);
            if (!focus) return;
            if (inButton(kExpandX)) {
                if (expanded_.count(row.track)) expanded_.erase(row.track);
                else expanded_.insert(row.track);
            } else if (inButton(kVisX)) { // visibility and lock act on the whole track
                const bool visible = !focus->visible;
                for (int i = row.first; i < row.first + row.count; ++i) layers[i].visible = visible;
                ctx_.touch();
            } else if (inButton(kLockX)) {
                const bool locked = !focus->locked;
                for (int i = row.first; i < row.first + row.count; ++i) layers[i].locked = locked;
                ctx_.touch();
            } else if (right) {
                bool anySelected = false;
                for (int i = row.first; i < row.first + row.count; ++i) anySelected |= ctx_.isSelected(layers[i].id);
                if (!anySelected) clickTrack(row);
                showTrackMenu(e->globalPosition().toPoint());
                return;
            } else if (left && x >= kChipX) {
                clickTrack(row);
                if (!ctrlHeld() && !shiftHeld()) beginLayerDrag(Drag::LayerName, row.layer, pos);
            }
            relayout();
            return;
        }
        // Track area.
        if (left) {
            if (const TransitionHit transition = transitionHit(row, x)) {
                selectedTransitionLayer_ = transition.layer;
                selectedTransitionIncoming_ = transition.incoming;
                if (transition.resize && !ctrlHeld() && !shiftHeld())
                    beginLayerDrag(transition.incoming ? Drag::TransitionIn : Drag::TransitionOut, transition.layer, pos);
                relayout();
                return;
            }
        }
        Drag mode = Drag::None;
        const int hit = clipHit(row, x, mode);
        if (ctx_.tool == Tool::Razor) {
            if (left && hit >= 0) ctx_.splitLayerAt(layers[hit].id, xToTime(x));
        } else if (right) {
            if (hit >= 0 && !ctx_.isSelected(layers[hit].id)) ctx_.select(layers[hit].id);
            if (hit >= 0) showTrackMenu(e->globalPosition().toPoint());
            return;
        } else if (left) {
            if (hit >= 0) {
                const uint64_t id = layers[hit].id;
                clickLayer(id);
                if (!ctrlHeld() && !shiftHeld()) beginLayerDrag(mode, id, pos);
            } else {
                beginMarquee(pos); // empty track space: box selection
            }
        }
        relayout();
        return;
    }
    if (item->kind == Item::Group) {
        if (left && x < leftWidth_) {
            if (collapsedGroups_.count(item->groupKey)) collapsedGroups_.erase(item->groupKey);
            else collapsedGroups_.insert(item->groupKey);
        }
        relayout();
        return;
    }
    // Property row: keyframes.
    if (x < trackX0()) return;
    Layer* layer = ctx_.comp().findLayer(item->layer);
    auto ref = findProperty(ctx_, item->layer, item->prop, item->effectIndex);
    if (!layer || !ref) return;
    AnimatedBase& anim = *ref->anim;
    int hoveredKey = -1;
    for (int k = 0; k < anim.keyCount(); ++k)
        if (std::fabs(x - timeToX(anim.keyTime(k))) <= keyRadius() + 2) hoveredKey = k;
    if (hoveredKey >= 0) {
        ctx_.select(layer->id); // key selection is limited to one layer
        const KeySelection ks{layer->id, item->prop, anim.keyUid(hoveredKey)};
        const bool isSel = ctx_.isKeySelected(ks);
        if (shiftHeld() || ctrlHeld()) {
            if (isSel && !right) std::erase(ctx_.selectedKeys, ks);
            else if (!isSel) ctx_.selectedKeys.push_back(ks);
        } else if (!isSel) {
            ctx_.selectedKeys = {ks};
        }
        if (right) {
            showKeyMenu(e->globalPosition().toPoint());
            return;
        }
        if (left) beginKeyDrag(pos);
    } else if (left && !shiftHeld()) {
        ctx_.clearKeySelection();
        ctx_.select(layer->id);
    }
    relayout();
}

void TimelinePanel::rowsDoubleClick(QMouseEvent* e) {
    computeItems();
    if (graphMode_ && e->button() == Qt::LeftButton && e->position().x() >= trackX0()) { // a key on the curve under the mouse
        if (const auto curve = curveNear(e->position())) addGraphKeys({curve->first}, xToTime(static_cast<float>(e->position().x())));
        return;
    }
    if (graphMode_ || e->button() != Qt::LeftButton) return;
    const float x = static_cast<float>(e->position().x());
    const Item* item = itemAt(static_cast<float>(e->position().y()));
    if (!item) return;
    auto& layers = ctx_.comp().layers;
    if (item->kind == Item::Track) {
        if (x >= kNameX && x < leftWidth_) {
            const Layer* focus = ctx_.comp().findLayer(item->layer);
            if (focus && focus->type == LayerType::Compound) ctx_.enterScope(focus->id); // its timeline opens
            else startRename(*item);
        } else if (x >= trackX0()) {
            Drag mode;
            const int hit = clipHit(*item, x, mode);
            if (hit >= 0 && layers[hit].type == LayerType::Compound) ctx_.enterScope(layers[hit].id);
        }
    } else if (item->kind == Item::Prop && x >= trackX0()) {
        if (auto ref = findProperty(ctx_, item->layer, item->prop, item->effectIndex))
            for (int k = 0; k < ref->anim->keyCount(); ++k)
                if (std::fabs(x - timeToX(ref->anim->keyTime(k))) <= keyRadius() + 2) ctx_.setTime(ref->anim->keyTime(k));
    }
    drag_ = Drag::None;
    relayout();
}

void TimelinePanel::rowsMove(QMouseEvent* e) {
    const QPointF pos = e->position();
    hoverX_ = static_cast<float>(pos.x());
    if (graphMode_) {
        graphMove(e);
        return;
    }
    if (drag_ == Drag::Keys) {
        updateKeyDrag(pos);
        return;
    }
    if (drag_ == Drag::LayerMove || drag_ == Drag::LayerIn || drag_ == Drag::LayerOut || drag_ == Drag::LayerName ||
        drag_ == Drag::TransitionIn || drag_ == Drag::TransitionOut) {
        updateLayerDrag(pos);
        return;
    }
    if (marquee_) {
        updateMarquee(pos);
        return;
    }
    // Hover feedback: trim cursor on clip edges, razor, the easing of a keyframe.
    computeItems();
    const Item* item = itemAt(static_cast<float>(pos.y()));
    Qt::CursorShape cursor = Qt::ArrowCursor;
    if (item && item->kind == Item::Track && pos.x() >= trackX0()) {
        Drag mode;
        clipHit(*item, static_cast<float>(pos.x()), mode);
        if (ctx_.tool == Tool::Razor) cursor = Qt::PointingHandCursor;
        else if (transitionHit(*item, static_cast<float>(pos.x())).resize || mode == Drag::LayerIn || mode == Drag::LayerOut)
            cursor = Qt::SizeHorCursor;
    }
    rows_->setCursor(cursor);
    if (item && item->kind == Item::Prop && pos.x() >= trackX0()) {
        if (auto ref = findProperty(ctx_, item->layer, item->prop, item->effectIndex))
            for (int k = 0; k < ref->anim->keyCount(); ++k)
                if (std::fabs(pos.x() - timeToX(ref->anim->keyTime(k))) <= keyRadius() + 2)
                    QToolTip::showText(e->globalPosition().toPoint(), easingLabel(ref->anim->keyEasing(k)), rows_);
    }
    rows_->update();
}

void TimelinePanel::rowsRelease(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (graphMode_) {
        graphRelease(e);
        return;
    }
    if (drag_ == Drag::Keys) endKeyDrag();
    else if (drag_ == Drag::LayerMove || drag_ == Drag::LayerIn || drag_ == Drag::LayerOut || drag_ == Drag::LayerName ||
             drag_ == Drag::TransitionIn || drag_ == Drag::TransitionOut)
        endLayerDrag(pos);
    if (marquee_) marquee_ = false;
    snapLineTime_ = -1.0;
    relayout();
}

void TimelinePanel::beginLayerDrag(Drag mode, uint64_t id, QPointF pos) {
    const Layer* layer = ctx_.comp().findLayer(id);
    if (!layer) return;
    drag_ = mode;
    dragLayer_ = id;
    dragStart_ = pos;
    dragY_ = static_cast<float>(pos.y());
    dragOrigIn_ = layer->inPoint;
    dragOrigOut_ = layer->outPoint;
    const ClipTransition& transition = mode == Drag::TransitionIn ? layer->transitionIn : layer->transitionOut;
    dragOrigTransitionDuration_ = transition.duration;
    dragApplied_ = 0.0;
    restacking_ = false;
    dragClips_.clear();
    // A compound moving in time brings everything inside it along.
    dragMoving_ = mode == Drag::LayerMove ? ctx_.selectionWithContents() : std::vector<uint64_t>{id};
    for (uint64_t m : dragMoving_)
        if (const Layer* l = ctx_.comp().findLayer(m)) dragClips_.push_back({m, l->inPoint, l->outPoint});
    collectTimeSnapTargets(dragMoving_, false);
}

void TimelinePanel::collectTimeSnapTargets(const std::vector<uint64_t>& movingClips, bool movingKeys) {
    const Composition& comp = ctx_.comp();
    timeTargets_ = {0.0, comp.duration, ctx_.time}; // composition ends and the playhead
    auto isDraggedKey = [&](uint64_t layerId, const char* prop, uint32_t uid) {
        if (!movingKeys) return false;
        for (const auto& [sel, orig] : dragKeys_)
            if (sel.layerId == layerId && sel.propertyId == prop && sel.keyUid == uid) return true;
        return false;
    };
    for (const Layer& l : comp.layers) {
        if (std::find(movingClips.begin(), movingClips.end(), l.id) != movingClips.end()) continue;
        timeTargets_.push_back(l.inPoint);
        timeTargets_.push_back(l.outPoint);
        for (const PropertyRef& p : const_cast<Layer&>(l).properties())
            for (int k = 0; k < p.anim->keyCount(); ++k)
                if (!isDraggedKey(l.id, p.id, p.anim->keyUid(k))) timeTargets_.push_back(p.anim->keyTime(k));
    }
}

double TimelinePanel::snappedDelta(const std::vector<double>& base, double rawDelta, bool force) {
    // Test the raw (unrounded) position so a target is caught before frame rounding jumps past it.
    snapLineTime_ = -1.0;
    if ((ctx_.snapping || force) && !ctrlHeld()) {
        std::vector<double> edges;
        for (double b : base) edges.push_back(b + rawDelta);
        if (const SnapResult r = snap1D(edges, timeTargets_, settings().snapDistance / pxPerSec_); r.snapped) { // screen pixels
            snapLineTime_ = r.target;
            return rawDelta + r.offset;
        }
    }
    return snapDelta(rawDelta, ctx_.comp().fps);
}

void TimelinePanel::updateLayerDrag(QPointF pos) {
    Composition& comp = ctx_.comp();
    const float dx = static_cast<float>(pos.x() - dragStart_.x()), dy = static_cast<float>(pos.y() - dragStart_.y());
    dragY_ = static_cast<float>(pos.y());
    if ((drag_ == Drag::LayerName || drag_ == Drag::LayerMove) && std::fabs(dy) > layerRowHeight() * 0.6f) restacking_ = true;
    rows_->setCursor(restacking_ ? Qt::SizeVerCursor : drag_ == Drag::LayerMove ? Qt::ArrowCursor : Qt::SizeHorCursor);
    Layer* layer = comp.findLayer(dragLayer_);
    if (!layer) return;
    const double raw = dx / pxPerSec_;
    const bool frozen = restacking_ && std::fabs(dx) < 12.0f; // vertical drags shouldn't nudge time
    switch (drag_) {
    case Drag::LayerMove: {
        std::vector<double> edges;
        for (const ClipStart& c : dragClips_) edges.insert(edges.end(), {c.in, c.out});
        const double delta = frozen ? 0.0 : snappedDelta(edges, raw);
        // Clips never overlap others on their track: stop at the neighbors.
        double lo = 0, hi = 0;
        comp.timeShiftLimits(dragMoving_, lo, hi);
        const double step = std::clamp(delta - dragApplied_, lo, hi);
        if (std::fabs(step) > 1e-9) {
            for (uint64_t id : dragMoving_)
                if (Layer* l = comp.findLayer(id)) l->shiftTime(step);
            dragApplied_ += step;
            ctx_.touch();
        }
        break;
    }
    case Drag::LayerIn:
        comp.setInPoint(layer->id, dragOrigIn_ + snappedDelta({dragOrigIn_}, raw));
        ctx_.touch();
        break;
    case Drag::LayerOut:
        comp.setOutPoint(layer->id, dragOrigOut_ + snappedDelta({dragOrigOut_}, raw));
        ctx_.touch();
        break;
    case Drag::TransitionIn:
    case Drag::TransitionOut: {
        const double sign = drag_ == Drag::TransitionIn ? 1.0 : -1.0;
        const float duration = static_cast<float>(std::clamp(dragOrigTransitionDuration_ + sign * raw, 0.05, layer->outPoint - layer->inPoint));
        ClipTransition& transition = drag_ == Drag::TransitionIn ? layer->transitionIn : layer->transitionOut;
        if (std::fabs(transition.duration - duration) > 1e-4f &&
            resizeTransition(comp, *layer, drag_ == Drag::TransitionIn ? TransitionEdge::In : TransitionEdge::Out, duration))
            ctx_.touch();
        break;
    }
    default: break;
    }
    relayout();
}

void TimelinePanel::endLayerDrag(QPointF pos) {
    computeItems();
    if (drag_ == Drag::TransitionIn || drag_ == Drag::TransitionOut) {
    } else if (restacking_) {
        const DropTarget t = dropTargetAt(static_cast<float>(pos.y()), drag_);
        if (t.valid) {
            if (drag_ == Drag::LayerName) {
                const auto tracks = ctx_.selectedTracks();
                int above = 0, visible = 0; // selected tracks above the gap leave the list first
                for (const Item& r : items_)
                    if (r.kind == Item::Track) {
                        if (visible < t.gap && std::find(tracks.begin(), tracks.end(), r.track) != tracks.end()) ++above;
                        ++visible;
                    }
                // The gap counts visible tracks; moveTracks counts every row.
                int target = static_cast<int>(ctx_.comp().trackRows().size());
                visible = 0;
                for (const Item& r : items_)
                    if (r.kind == Item::Track) {
                        if (visible == t.gap) {
                            target = r.rowIndex;
                            break;
                        }
                        ++visible;
                    }
                ctx_.moveSelectedTracksTo(target - above);
            } else if (t.newTrack) {
                int target = static_cast<int>(ctx_.comp().trackRows().size()), visible = 0;
                for (const Item& r : items_)
                    if (r.kind == Item::Track) {
                        if (visible == t.gap) {
                            target = r.rowIndex;
                            break;
                        }
                        ++visible;
                    }
                ctx_.moveSelectedClipsToNewTrack(target);
            } else if (t.fits) {
                ctx_.moveSelectedClipsToTrack(t.track);
            }
        }
    } else if (std::fabs(pos.x() - dragStart_.x()) < 3.0 && std::fabs(pos.y() - dragStart_.y()) < 3.0 && !ctrlHeld() && !shiftHeld()) {
        // A plain click inside a multi-selection keeps just this clip (or this track's clips).
        if (drag_ == Drag::LayerName) {
            if (const Layer* l = ctx_.comp().findLayer(dragLayer_)) {
                const auto ids = clipsOfTrack(ctx_.comp(), l->track);
                ctx_.select(ids.front());
                for (size_t i = 1; i < ids.size(); ++i) ctx_.selection.push_back(ids[i]);
            }
        } else {
            ctx_.select(dragLayer_);
        }
    }
    drag_ = Drag::None;
    restacking_ = false;
    rows_->setCursor(Qt::ArrowCursor);
}

int TimelinePanel::dropGapAt(float y) const {
    int gap = 0, visible = 0;
    for (const Item& r : items_)
        if (r.kind == Item::Track) {
            if (y > r.top + r.h * 0.5f) gap = visible + 1;
            ++visible;
        }
    return gap;
}

TimelinePanel::DropTarget TimelinePanel::dropTargetAt(float y, Drag mode) const {
    DropTarget t;
    const bool anyTrack = std::any_of(items_.begin(), items_.end(), [](const Item& i) { return i.kind == Item::Track; });
    if (!anyTrack) return t;
    t.valid = true;
    t.gap = dropGapAt(y);
    if (mode != Drag::LayerMove) return t; // name drags reorder whole tracks
    // Clips: the middle of a row joins that track, its edges (or the space between rows) create a new track.
    constexpr float kEdge = 7.0f;
    int visible = 0;
    for (const Item& r : items_) {
        if (r.kind != Item::Track) continue;
        const int index = visible++;
        if (y < r.top || y >= r.top + r.h) continue;
        if (y < r.top + kEdge) t.gap = index;
        else if (y > r.top + r.h - kEdge) t.gap = index + 1;
        else {
            t.newTrack = false;
            t.track = r.track;
            const auto& sel = ctx_.selection;
            const bool alreadyThere = std::all_of(sel.begin(), sel.end(), [&](uint64_t id) {
                const Layer* l = ctx_.comp().findLayer(id);
                return l && l->track == r.track;
            });
            if (alreadyThere) t.valid = false;
            else t.fits = ctx_.comp().clipsFitTrack(sel, r.track);
        }
        break;
    }
    return t;
}

void TimelinePanel::beginMarquee(QPointF pos) {
    const bool additive = ctrlHeld() || shiftHeld();
    marquee_ = true;
    marqueeStart_ = marqueeEnd_ = pos;
    marqueeBase_ = additive ? ctx_.selection : std::vector<uint64_t>{};
    if (!additive) ctx_.select(0);
    ctx_.clearKeySelection();
}

void TimelinePanel::updateMarquee(QPointF pos) {
    marqueeEnd_ = pos;
    const QRectF r = QRectF(marqueeStart_, marqueeEnd_).normalized();
    if (r.width() < 3 && r.height() < 3) return; // still a click
    computeItems();
    // Clips whose row the box touches; inside the track area the box must also touch the clip.
    ctx_.selection = marqueeBase_;
    for (const ClipRect& c : clips_) {
        const bool vertical = c.bottom > r.top() && c.top < r.bottom();
        const bool horizontal = r.left() < trackX0() || (c.x2 > r.left() && c.x1 < r.right());
        if (vertical && horizontal && !ctx_.isSelected(c.id)) ctx_.selection.push_back(c.id);
    }
    relayout();
}

void TimelinePanel::beginKeyDrag(QPointF pos) {
    drag_ = Drag::Keys;
    dragStart_ = pos;
    keysMoved_ = false;
    dragKeys_.clear();
    for (const auto& sel : ctx_.selectedKeys) {
        Layer* layer = ctx_.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) dragKeys_.emplace_back(sel, p->anim->keyTime(i));
    }
    collectTimeSnapTargets({}, true);
}

void TimelinePanel::updateKeyDrag(QPointF pos) {
    if (!keysMoved_ && std::fabs(pos.x() - dragStart_.x()) < 2.0) return;
    keysMoved_ = true;
    std::vector<double> edges;
    for (const auto& [sel, orig] : dragKeys_) edges.push_back(orig);
    const double delta = snappedDelta(edges, (pos.x() - dragStart_.x()) / pxPerSec_);
    for (const auto& [sel, orig] : dragKeys_) {
        Layer* layer = ctx_.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) {
                // Keys stay where the clip exists.
                p->anim->setKeyTime(i, std::clamp(orig + delta, layer->inPoint, layer->outPoint));
                p->anim->sortKeys();
            }
    }
    ctx_.touch();
    relayout();
}

void TimelinePanel::endKeyDrag() {
    drag_ = Drag::None;
    for (const auto& [sel, orig] : dragKeys_) {
        Layer* layer = ctx_.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str())) p->anim->removeDuplicateKeys();
    }
    // Drop selections whose key was merged away.
    std::erase_if(ctx_.selectedKeys, [&](const KeySelection& sel) {
        Layer* layer = ctx_.comp().findLayer(sel.layerId);
        if (!layer) return true;
        auto props = layer->properties();
        PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str());
        return !p || p->anim->findKeyByUid(sel.keyUid) < 0;
    });
    dragKeys_.clear();
}

bool TimelinePanel::dropTimeAndGap(QPointF pos, double& t, int& gap) {
    computeItems();
    t = std::max(0.0, xToTime(std::max(static_cast<float>(pos.x()), trackX0())));
    if (ctx_.snapping && !ctrlHeld()) { // snaps to clips, keys and the playhead like everything else
        collectTimeSnapTargets({}, false);
        t = snappedDelta({0.0}, t);
    } else {
        t = ctx_.comp().snapToFrame(t);
    }
    gap = dropGapAt(static_cast<float>(pos.y()));
    // The gap counts visible tracks; moveTracks counts every row.
    int visible = 0;
    int target = static_cast<int>(ctx_.comp().trackRows().size());
    for (const Item& r : items_)
        if (r.kind == Item::Track) {
            if (visible == gap) {
                target = r.rowIndex;
                break;
            }
            ++visible;
        }
    dropGap_ = gap;
    gap = target;
    return true;
}

} // namespace bemotion
