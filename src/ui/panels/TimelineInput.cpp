#include "ui/panels/TimelinePanel.h"

// The timeline panel: mouse, menus, markers and drags (the rest: TimelinePanel.cpp, TimelinePaint.cpp).

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

// --- input ------------------------------------------------------------------------------------

const TimelinePanel::Item* TimelinePanel::itemAt(float y) const {
    for (const Item& item : items_)
        if (y >= item.top && y < item.top + item.h) return &item;
    return nullptr;
}

int TimelinePanel::clipHit(const Item& track, float mx, Drag& mode) const {
    // Edges win over bodies so adjacent clips can be trimmed; at a cut, the edge of the clip the
    // mouse is over (left of the cut: the first clip's end; right of it: the next one's start).
    const auto& layers = ctx_.comp().layers;
    int hit = -1, body = -1;
    float best = 1e9f;
    mode = Drag::None;
    for (int i = track.first; i < track.first + track.count && i < static_cast<int>(layers.size()); ++i) {
        const float bx1 = timeToX(layers[i].inPoint), bx2 = timeToX(layers[i].outPoint);
        auto edge = [&](float x, bool over, Drag m) {
            const float d = std::fabs(mx - x);
            if (d >= kEdgeGrab) return;
            if (const float score = d + (over ? 0.0f : kEdgeGrab); score < best) best = score, hit = i, mode = m;
        };
        edge(bx1, mx >= bx1, Drag::LayerIn);
        edge(bx2, mx <= bx2, Drag::LayerOut);
        if (mx > bx1 && mx < bx2) body = i;
    }
    if (hit < 0 && body >= 0) hit = body, mode = Drag::LayerMove;
    return hit;
}

TimelinePanel::MotionDrop TimelinePanel::motionDropAt(QPointF pos, const Motion& motion) const {
    constexpr float kCutGrab = 10.0f; // px around a cut that mean "between the two clips"
    const Item* item = itemAt(static_cast<float>(pos.y()));
    if (!item || item->kind != Item::Track) return {};
    const auto& layers = ctx_.comp().layers;
    const float x = static_cast<float>(pos.x());
    const bool transition = !motion.preset;
    auto takes = [&](const Layer& l) {
        return transition ? canTakeTransition(l, motion.transition) : l.type != LayerType::Camera && presetAppliesTo(*motion.preset, l);
    };
    auto lengthOn = [&](const Layer& l) { return transition ? 0.5 : presetDuration(*motion.preset, l); };
    // Between two clips that touch.
    const double eps = ctx_.comp().frameDuration() * 0.5;
    const int end = std::min(item->first + item->count, static_cast<int>(layers.size()));
    for (int i = item->first; i < end; ++i)
        for (int j = item->first; j < end; ++j)
            if (i != j && std::fabs(layers[i].outPoint - layers[j].inPoint) < eps && std::fabs(x - timeToX(layers[i].outPoint)) <= kCutGrab) {
                MotionDrop d{layers[i].id, MotionDrop::Spot::Cut, layers[i].outPoint, transition ? 1.0 : lengthOn(layers[j]), false};
                d.ok = transition ? takes(layers[i]) && takes(layers[j]) : takes(layers[j]);
                if (transition) d.time -= d.length * 0.5;
                return d;
            }
    Drag mode;
    const int index = clipHit(*item, x, mode);
    if (index < 0 || index >= static_cast<int>(layers.size())) return {};
    const Layer& l = layers[index];
    const double length = std::min(lengthOn(l), std::max(0.05, l.outPoint - l.inPoint));
    // Near an end (the first or last part of the clip it would cover): on that end; else from
    // the mouse, wherever along the clip.
    const float xi = timeToX(l.inPoint), xo = timeToX(l.outPoint);
    const float zone = std::clamp(timeToX(l.inPoint + length) - xi, 12.0f, (xo - xi) * 0.3f);
    MotionDrop d{l.id, MotionDrop::Spot::At, 0.0, length, takes(l)};
    if (x - xi < zone) d.spot = MotionDrop::Spot::In, d.time = l.inPoint;
    else if (xo - x < zone) d.spot = MotionDrop::Spot::Out, d.time = l.outPoint - length;
    else d.time = std::clamp(ctx_.comp().snapToFrame(xToTime(x) - length * 0.5), l.inPoint, std::max(l.inPoint, l.outPoint - length));
    return d;
}

bool TimelinePanel::placeMotion(const MotionDrop& drop, const Motion& motion) {
    Layer* layer = ctx_.comp().findLayer(drop.layer);
    if (!layer) return false;
    static constexpr MotionSpot kSpots[] = {MotionSpot::In, MotionSpot::Out, MotionSpot::Cut, MotionSpot::At};
    Layer* placed = bemotion::placeMotion(ctx_.comp(), *layer, motion.transition, motion.preset ? &*motion.preset : nullptr,
                                          kSpots[static_cast<int>(drop.spot)], drop.time, drop.length);
    if (!placed) return false;
    ctx_.select(placed->id);
    ctx_.touch();
    return true;
}

void TimelinePanel::paintTransitionDrop(ui::Draw& d) {
    const MotionDrop& drop = motionDrop_;
    const Layer* layer = drop.layer ? ctx_.comp().findLayer(drop.layer) : nullptr;
    if (!layer) return;
    for (const Item& r : items_) {
        if (r.kind != Item::Track || r.track != layer->track) continue;
        using Spot = MotionDrop::Spot;
        const float y = r.top, h = r.h;
        const float x0 = timeToX(drop.time), x1 = std::max(timeToX(drop.time + drop.length), x0 + 16.0f);
        const Rgba line = drop.ok ? theme::kAccent : theme::kDanger;
        d.rectFilled({x0, y + 3}, {x1, y + h - 3}, drop.ok ? theme::accent(110) : theme::withAlpha(theme::kDanger, 60), 5.0f);
        d.rect({x0, y + 3}, {x1, y + h - 3}, line, 5.0f, 2.0f);
        // What it becomes there.
        const QString where = drop.spot == Spot::Cut ? tr("Between the clips") : drop.spot == Spot::In ? tr("In") : drop.spot == Spot::Out ? tr("Out") : QString();
        if (!drop.ok) {
            const QString why = layer->type == LayerType::Camera ? tr("Cameras take no transitions")
                                : (layer->type == LayerType::Media && layer->media.hasAudio && !layer->media.hasVideo)
                                    ? tr("Sound clips take no transitions (use Fade In / Out in Effects)")
                                    : tr("This clip cannot take this one");
            d.text({x1 + 8, y + (h - d.textHeight()) * 0.5f}, theme::kDanger, why);
        } else if (!where.isEmpty()) {
            d.text({x1 + 8, y + (h - d.textHeight()) * 0.5f}, theme::kAccent, where);
        }
        return;
    }
}

TimelinePanel::AnimationHit TimelinePanel::animationHit(const Item& track, float mx) const {
    const auto& layers = ctx_.comp().layers;
    for (int i = track.first; i < track.first + track.count && i < static_cast<int>(layers.size()); ++i) {
        const Layer& layer = layers[i];
        for (int k = 0; k < static_cast<int>(layer.animations.size()); ++k) {
            const AppliedAnimation& a = layer.animations[static_cast<size_t>(k)];
            const double from = std::max(a.start, layer.inPoint), until = std::min(a.start + a.duration, layer.outPoint);
            if (until <= from) continue;
            const float left = timeToX(from) + 2.0f, right = std::max(left + 16.0f, timeToX(until) - 2.0f);
            if (mx < left || mx > right) continue;
            const bool end = std::fabs(mx - right) <= kEdgeGrab, start = !end && std::fabs(mx - left) <= kEdgeGrab;
            return {layer.id, k, end || start, start};
        }
    }
    return {};
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

void TimelinePanel::clickLayer(uint64_t id) {
    ctx_.transitionLayer = 0;
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
    menu.addAction(tr("Rename"), [this, focus] {
        computeItems();
        for (const Item& it : items_)
            if (it.kind == Item::Track && it.layer == focus) startRename(it);
    });
    menu.addAction(tr("Duplicate\tCtrl+D"), [this] { ctx_.duplicateSelected(); });
    menu.addAction(tr("Split at Playhead\tCtrl+K"), [this] { ctx_.splitSelectedAtPlayhead(); });
    menu.addAction(tr("Ripple Delete\tShift+Del"), [this] { ctx_.rippleDeleteSelected(); })->setToolTip(tr("Delete and close the gap: the clips after it move back"));
    ui::fillTimeMenu(menu.addMenu(ui::menuIcon(ui::Icon::Speed, theme::kTextDim), tr("Time")), ctx_, this);
    ui::addSeparateAudio(menu, ctx_);
    ui::addAudioFixes(menu, ctx_, this);
    if (ui::canTranscribe(ctx_, focus)) {
        QAction* t = menu.addAction(ui::menuIcon(ui::Icon::Subtitles, theme::kTextDim), tr("Transcribe to Subtitles..."),
                                    [this, focus] { ui::transcribeClip(ctx_, focus, 0, this); });
        t->setToolTip(tr("The words spoken in this clip become a Subtitles layer, at their time"));
    }
    // 3D Layer, like the Properties checkbox: on for all when any is off.
    std::vector<Layer*> spatial;
    for (Layer* l : ctx_.selectedLayers())
        if (l->type != LayerType::Camera && l->type != LayerType::Background && !l->parent) spatial.push_back(l);
    if (!spatial.empty()) {
        const bool all = std::all_of(spatial.begin(), spatial.end(), [](const Layer* l) { return l->threeD; });
        QAction* threeD = menu.addAction(tr("3D Layer"), [this, all] {
            for (Layer* l : ctx_.selectedLayers())
                if (l->type != LayerType::Camera && l->type != LayerType::Background && !l->parent) l->threeD = !all;
            ctx_.touch();
        });
        threeD->setCheckable(true);
        threeD->setChecked(all);
        threeD->setToolTip(tr("Seen in perspective by the camera at its Z position"));
    }
    // How it is composited: blending, a track matte, masks.
    menu.addSeparator();
    ui::fillBlendMenu(menu.addMenu(tr("Blending Mode")), ctx_);
    ui::fillMatteMenu(menu.addMenu(tr("Track Matte")), ctx_);
    ui::fillMaskMenu(menu.addMenu(ui::menuIcon(ui::Icon::Mask, theme::kTextDim), tr("Mask")), ctx_);
    {
        const auto sel = ctx_.selectedLayers();
        if (std::any_of(sel.begin(), sel.end(), [](const Layer* l) { return l->type == LayerType::Shape && l->shape.kind != ShapeKind::Path; }))
            menu.addAction(ui::menuIcon(ui::Icon::PathShape, theme::kTextDim), tr("Convert to Bézier Path"), [this] { ui::convertSelectedToPaths(ctx_); })
                ->setToolTip(tr("Its outline becomes points and handles to edit with the Pen"));
    }
    menu.addSeparator();
    menu.addAction(tr("Create Compound\tCtrl+G"), [this] { ctx_.createCompound(); });
    menu.addAction(tr("Release Compound\tCtrl+Shift+G"), [this] { ctx_.releaseSelectedCompounds(); })->setEnabled(ctx_.selectionHasCompound());
    fillEffectMenu(menu.addMenu(tr("Effect")), ctx_);
    menu.addSeparator();
    menu.addAction(tr("Bring to Front\tCtrl+Shift+]"), [this] { ctx_.shiftSelected(-1000); });
    menu.addAction(tr("Bring Forward\tCtrl+]"), [this] { ctx_.shiftSelected(-1); });
    menu.addAction(tr("Send Backward\tCtrl+["), [this] { ctx_.shiftSelected(1); });
    menu.addAction(tr("Send to Back\tCtrl+Shift+["), [this] { ctx_.shiftSelected(1000); });
    menu.addSeparator();
    menu.addAction(tr("Delete\tDel"), [this] { ctx_.deleteSelected(); });
    menu.exec(global);
    relayout();
}

// --- markers and gaps -----------------------------------------------------------------------

int TimelinePanel::markerAt(float x, float y) const {
    if (y > 14.0f) return -1; // the flags are along the top of the ruler
    const auto& markers = ctx_.comp().markers;
    int best = -1;
    float distance = 7.0f;
    for (size_t i = 0; i < markers.size(); ++i)
        if (const float dx = std::fabs(x - timeToX(markers[i].time)); dx <= distance) best = static_cast<int>(i), distance = dx;
    return best;
}

void TimelinePanel::dragMarkerTo(float x) {
    Composition& comp = ctx_.comp();
    if (dragMarker_ < 0 || dragMarker_ >= static_cast<int>(comp.markers.size())) return;
    const double raw = (x - dragStart_.x()) / pxPerSec_;
    const double t = std::max(0.0, dragOrigIn_ + snappedDelta({dragOrigIn_}, raw));
    if (std::fabs(comp.markers[dragMarker_].time - t) < 1e-9) return;
    dragMarker_ = moveMarker(comp, dragMarker_, t);
    ctx_.touch();
    relayout();
}

void TimelinePanel::renameMarker(int index) {
    Composition& comp = ctx_.comp();
    if (index < 0 || index >= static_cast<int>(comp.markers.size())) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Marker"), tr("Name:"), QLineEdit::Normal, QString::fromStdString(comp.markers[index].name), &ok);
    if (!ok || index >= static_cast<int>(ctx_.comp().markers.size())) return;
    ctx_.comp().markers[index].name = name.trimmed().toStdString();
    ctx_.touch();
    relayout();
}

void TimelinePanel::showMarkerMenu(QPoint global, int index, double t) {
    QMenu menu;
    if (index < 0) {
        menu.addAction(ui::menuIcon(ui::Icon::Marker, theme::kTextDim), tr("Add Marker Here"), [this, t] {
            addMarker(ctx_.comp(), t, "");
            ctx_.touch();
        });
        if (!ctx_.comp().markers.empty())
            menu.addAction(tr("Remove All Markers"), [this] {
                ctx_.comp().markers.clear();
                ctx_.touch();
            });
    } else {
        menu.addAction(tr("Rename..."), [this, index] { renameMarker(index); });
        menu.addAction(tr("Go to Marker"), [this, index] { ctx_.setTime(ctx_.comp().markers[index].time); });
        QMenu* colors = menu.addMenu(tr("Color"));
        for (int i = 0; i < kLabelColorCount; ++i) {
            QPixmap swatch(14, 14);
            swatch.fill(Qt::transparent);
            QPainter sp(&swatch);
            sp.setRenderHint(QPainter::Antialiasing);
            const Color c = labelColor(i);
            sp.setBrush(QColor::fromRgbF(c.r, c.g, c.b));
            sp.setPen(Qt::NoPen);
            sp.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
            sp.end();
            QAction* a = colors->addAction(QIcon(swatch), tr("Color %1").arg(i + 1), [this, index, i] {
                ctx_.comp().markers[index].color = i;
                ctx_.touch();
            });
            a->setCheckable(true);
            a->setChecked(ctx_.comp().markers[index].color == i);
        }
        menu.addSeparator();
        menu.addAction(tr("Delete Marker"), [this, index] {
            ctx_.comp().markers.erase(ctx_.comp().markers.begin() + index);
            ctx_.touch();
        });
    }
    menu.exec(global);
    relayout();
}

void TimelinePanel::showGapMenu(QPoint global, uint64_t track, double t) {
    QMenu menu;
    menu.setToolTipsVisible(true);
    Composition probe = ctx_.comp(); // whether there is a gap to close here
    QAction* close = menu.addAction(tr("Close Gap"), [this, track, t] {
        if (closeGap(ctx_.comp(), track, t)) ctx_.touch();
    });
    close->setEnabled(closeGap(probe, track, t));
    close->setToolTip(tr("The clips after this gap move back to close it (Premiere's Ripple Delete on a gap)"));
    menu.addAction(ui::menuIcon(ui::Icon::Marker, theme::kTextDim), tr("Add Marker Here"), [this, t] {
        addMarker(ctx_.comp(), t, "");
        ctx_.touch();
    });
    menu.exec(global);
    relayout();
}

// The cut under x between a clip of the row and the one right after it: the first clip's id.
uint64_t TimelinePanel::cutAt(const Item& row, float x) const {
    const auto& layers = ctx_.comp().layers;
    const int end = std::min(row.first + row.count, static_cast<int>(layers.size()));
    for (int i = row.first; i < end; ++i)
        if (std::fabs(x - timeToX(layers[i].outPoint)) < kEdgeGrab && clipAfterCut(ctx_.comp(), layers[i].id)) return layers[i].id;
    return 0;
}

// Ripple, roll and slip drags redo their edit from where the drag started, each move.
void TimelinePanel::beginEditDrag(Drag mode, uint64_t id, QPointF pos) {
    const Layer* layer = ctx_.comp().findLayer(id);
    if (!layer) return;
    drag_ = mode;
    dragLayer_ = id;
    dragStart_ = pos;
    dragOrigIn_ = layer->inPoint;
    dragOrigOut_ = layer->outPoint;
    dragBase_ = ctx_.comp();
    std::vector<uint64_t> moving{id};
    if (const uint64_t next = clipAfterCut(ctx_.comp(), id); mode == Drag::Roll && next) moving.push_back(next);
    collectTimeSnapTargets(moving, false);
}

void TimelinePanel::updateEditDrag(QPointF pos) {
    Composition& comp = ctx_.comp();
    const double raw = (pos.x() - dragStart_.x()) / pxPerSec_;
    comp = dragBase_;
    switch (drag_) {
    case Drag::RippleIn: rippleTrim(comp, dragLayer_, ClipEdge::In, dragOrigIn_ + snappedDelta({dragOrigIn_}, raw)); break;
    case Drag::RippleOut: rippleTrim(comp, dragLayer_, ClipEdge::Out, dragOrigOut_ + snappedDelta({dragOrigOut_}, raw)); break;
    case Drag::Roll: rollEdit(comp, dragLayer_, dragOrigOut_ + snappedDelta({dragOrigOut_}, raw)); break;
    case Drag::Slip: slipClip(comp, dragLayer_, snapDelta(raw, comp.fps)); break;
    default: break;
    }
    rowsWidget()->setCursor(drag_ == Drag::Roll ? Qt::SplitHCursor : Qt::SizeHorCursor);
    ctx_.touch();
    relayout();
}

void TimelinePanel::showKeyMenu(QPoint global) {
    QMenu menu;
    const int n = static_cast<int>(ctx_.selectedKeys.size());
    menu.addSection(tr("%1 keyframe%2").arg(n).arg(n == 1 ? "" : "s"));
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        if (e == Easing::Custom) continue;
        menu.addAction(easingLabel(e), [this, e] { ctx_.setSelectedKeysEasing(e); });
    }
    // Custom Curve: the bezier editor, its four numbers and some starting points.
    QMenu* custom = menu.addMenu(tr("Custom Curve"));
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
    auto* startLabel = new QLabel(tr("Start from:"));
    ui::setTone(startLabel, ui::Tone::TextFaint);
    col->addWidget(startLabel);
    auto* starts = new QHBoxLayout;
    const std::pair<Easing, const char*> presets[] = {
        {Easing::Linear, TR_NOOP("Lin")}, {Easing::EaseIn, TR_NOOP("In")}, {Easing::EaseOut, TR_NOOP("Out")}, {Easing::EaseInOut, TR_NOOP("In Out")}, {Easing::BackOut, TR_NOOP("Back")}};
    for (const auto& [e, label] : presets) {
        auto* b = new QPushButton(tr(label));
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
        menu.addSection(tr("Motion path"));
        menu.addAction(tr("Auto Bezier"), [this] { ctx_.setSelectedKeysPath(PathMode::Auto); });
        menu.addAction(tr("Linear (corner)"), [this] { ctx_.setSelectedKeysPath(PathMode::Linear); });
        menu.addAction(tr("Custom Handles"), [this] { ctx_.setSelectedKeysPath(PathMode::Custom); });
    }
    menu.addSeparator();
    menu.addAction(tr("Delete\tDel"), [this] { ctx_.deleteSelectedKeys(); });
    menu.exec(global);
    relayout();
}

void TimelinePanel::startRename(const Item& track) {
    Layer* layer = ctx_.comp().findLayer(track.layer);
    if (!layer) return;
    delete rename_;
    renaming_ = layer->id;
    rename_ = new QLineEdit(QString::fromStdString(layer->name), rowsWidget());
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
        rowsWidget()->update();
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
    if (ctx_.pickingSample() && left && x >= trackX0()) {
        beginSamplePick(x);
        return;
    }
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
            } else if (inButton(kSoloX)) { // solo and mute act on the whole track too
                const bool solo = !focus->solo;
                for (int i = row.first; i < row.first + row.count; ++i) layers[i].solo = solo;
                ctx_.touch();
            } else if (inButton(kMuteX) && trackHasSound(ctx_.comp(), row.track)) {
                const bool muted = !focus->muted;
                for (int i = row.first; i < row.first + row.count; ++i) layers[i].muted = muted;
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
                ctx_.selectTransition(transition.layer, transition.incoming); // the transition alone, not its clip
                if (!ctrlHeld() && !shiftHeld()) {
                    if (transition.resize) beginLayerDrag(transition.incoming ? Drag::TransitionIn : Drag::TransitionOut, transition.layer, pos);
                    else { // its body: to the clip's other end
                        beginLayerDrag(Drag::TransitionMove, transition.layer, pos);
                        dragTransitionIn_ = transition.incoming;
                    }
                }
                relayout();
                return;
            }
            if (const AnimationHit band = animationHit(row, x)) { // a Quick Animation: picked alone, moved, stretched
                ctx_.selectAnimation(band.layer, band.index);
                if (const Layer* l = ctx_.comp().findLayer(band.layer); l && !ctrlHeld() && !shiftHeld()) {
                    const AppliedAnimation& a = l->animations[static_cast<size_t>(band.index)];
                    beginLayerDrag(band.resize ? Drag::AnimationResize : Drag::AnimationMove, band.layer, pos);
                    dragAnimStart_ = a.start;
                    dragAnimDuration_ = a.duration;
                    dragAnimFromStart_ = band.start;
                }
                relayout();
                return;
            }
        }
        Drag mode = Drag::None;
        const int hit = clipHit(row, x, mode);
        const uint64_t cut = ctx_.tool == Tool::Roll ? cutAt(row, x) : 0;
        if (ctx_.tool == Tool::Razor) {
            if (left && hit >= 0) ctx_.splitLayerAt(layers[hit].id, xToTime(x));
        } else if (right) {
            if (hit >= 0 && !ctx_.isSelected(layers[hit].id)) ctx_.select(layers[hit].id);
            if (hit >= 0) showTrackMenu(e->globalPosition().toPoint());
            else showGapMenu(e->globalPosition().toPoint(), row.track, xToTime(x));
            return;
        } else if (left && cut) { // Rolling Edit: the cut between two clips
            ctx_.select(cut);
            beginEditDrag(Drag::Roll, cut, pos);
        } else if (left && hit >= 0 && ((ctx_.tool == Tool::Ripple && mode != Drag::LayerMove) || (ctx_.tool == Tool::Slip && mode == Drag::LayerMove))) {
            const uint64_t id = layers[hit].id;
            ctx_.select(id);
            beginEditDrag(ctx_.tool == Tool::Slip ? Drag::Slip : mode == Drag::LayerIn ? Drag::RippleIn : Drag::RippleOut, id, pos);
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
    if (item->kind == Item::Audio) { // the videos' sound: mute it, pick its clip, Separate / Link Audio
        auto& all = ctx_.comp().layers;
        if (left && x >= kVisX && x < kVisX + kButton) { // sound on / off for the track's videos
            bool anyOn = false;
            for (int i = item->first; i < item->first + item->count; ++i)
                if (all[i].media.hasAudio && all[i].media.hasVideo) anyOn = anyOn || !all[i].muted;
            for (int i = item->first; i < item->first + item->count; ++i)
                if (all[i].media.hasAudio && all[i].media.hasVideo) all[i].muted = anyOn;
            ctx_.touch();
        } else if (x >= trackX0()) {
            for (int i = item->first; i < item->first + item->count; ++i)
                if (all[i].media.hasAudio && all[i].media.hasVideo && x >= timeToX(all[i].inPoint) && x <= timeToX(all[i].outPoint)) {
                    clickLayer(all[i].id);
                    if (right) {
                        QMenu menu;
                        ui::addSeparateAudio(menu, ctx_);
                        menu.exec(e->globalPosition().toPoint());
                    }
                    break;
                }
        }
        relayout();
        return;
    }
    if (item->kind == Item::Mask) { // a mask as an object: picked, shown / hidden, its menu
        const Layer* layer = ctx_.comp().findLayer(item->layer);
        if (!layer || item->effectIndex >= static_cast<int>(layer->masks.size())) return;
        if (left && x >= kVisX && x < kVisX + kButton) {
            ctx_.comp().findLayer(item->layer)->masks[static_cast<size_t>(item->effectIndex)].enabled ^= true;
            ctx_.touch();
        } else if (left || right) {
            ctx_.selectMask(item->layer, item->effectIndex);
            if (right) showMaskMenu(e->globalPosition().toPoint(), item->layer, item->effectIndex);
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
    } else if (left) { // empty space on a keyframe row: drag a box around keyframes (Shift / Ctrl adds)
        if (!shiftHeld() && !ctrlHeld()) {
            ctx_.clearKeySelection();
            ctx_.select(layer->id);
        }
        beginKeyMarquee(pos);
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
    if (sampling_) {
        updateSamplePick(static_cast<float>(pos.x()));
        return;
    }
    if (drag_ == Drag::LayerMove || drag_ == Drag::LayerIn || drag_ == Drag::LayerOut || drag_ == Drag::LayerName ||
        drag_ == Drag::TransitionIn || drag_ == Drag::TransitionOut || drag_ == Drag::TransitionMove || drag_ == Drag::AnimationMove ||
        drag_ == Drag::AnimationResize) {
        updateLayerDrag(pos);
        return;
    }
    if (drag_ == Drag::RippleIn || drag_ == Drag::RippleOut || drag_ == Drag::Roll || drag_ == Drag::Slip) {
        updateEditDrag(pos);
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
    const bool clipTool = ctx_.tool == Tool::Razor || ctx_.tool == Tool::Ripple || ctx_.tool == Tool::Roll || ctx_.tool == Tool::Slip;
    if (item && item->kind == Item::Track && pos.x() >= trackX0()) {
        Drag mode;
        clipHit(*item, static_cast<float>(pos.x()), mode);
        if (clipTool) cursor = Qt::BitmapCursor; // the tool's icon (below)
        else if (transitionHit(*item, static_cast<float>(pos.x())).resize || animationHit(*item, static_cast<float>(pos.x())).resize ||
                 mode == Drag::LayerIn || mode == Drag::LayerOut)
            cursor = Qt::SizeHorCursor;
        else if (transitionHit(*item, static_cast<float>(pos.x())) || animationHit(*item, static_cast<float>(pos.x())))
            cursor = Qt::OpenHandCursor;
    }
    if (cursor == Qt::BitmapCursor) rowsWidget()->setCursor(ui::toolCursor(ctx_.tool));
    else rowsWidget()->setCursor(cursor);
    if (item && item->kind == Item::Prop && pos.x() >= trackX0()) {
        if (auto ref = findProperty(ctx_, item->layer, item->prop, item->effectIndex))
            for (int k = 0; k < ref->anim->keyCount(); ++k)
                if (std::fabs(pos.x() - timeToX(ref->anim->keyTime(k))) <= keyRadius() + 2)
                    QToolTip::showText(e->globalPosition().toPoint(), easingLabel(ref->anim->keyEasing(k)), rowsWidget());
    }
    rowsWidget()->update();
}

void TimelinePanel::rowsRelease(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (graphMode_) {
        graphRelease(e);
        return;
    }
    if (sampling_) endSamplePick();
    if (drag_ == Drag::Keys) endKeyDrag();
    else if (drag_ == Drag::LayerMove || drag_ == Drag::LayerIn || drag_ == Drag::LayerOut || drag_ == Drag::LayerName ||
             drag_ == Drag::TransitionIn || drag_ == Drag::TransitionOut || drag_ == Drag::TransitionMove || drag_ == Drag::AnimationMove ||
             drag_ == Drag::AnimationResize)
        endLayerDrag(pos);
    else if (drag_ == Drag::RippleIn || drag_ == Drag::RippleOut || drag_ == Drag::Roll || drag_ == Drag::Slip) {
        drag_ = Drag::None;
        dragBase_ = Composition{}; // (the copy is only needed while dragging)
    }
    if (marquee_) marquee_ = keyMarquee_ = false;
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
    for (const Marker& m : comp.markers) timeTargets_.push_back(m.time);
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
    rowsWidget()->setCursor(restacking_ ? Qt::SizeVerCursor : drag_ == Drag::LayerMove ? Qt::ArrowCursor : Qt::SizeHorCursor);
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
    case Drag::TransitionMove: // lands on release
        rowsWidget()->setCursor(Qt::ClosedHandCursor);
        break;
    case Drag::AnimationMove:
    case Drag::AnimationResize: {
        rowsWidget()->setCursor(drag_ == Drag::AnimationMove ? Qt::ClosedHandCursor : Qt::SizeHorCursor);
        const int index = ctx_.animationIndex;
        if (index < 0 || index >= static_cast<int>(layer->animations.size())) break;
        double start = drag_ == Drag::AnimationMove ? dragAnimStart_ + raw : dragAnimStart_;
        double duration = drag_ == Drag::AnimationResize ? std::max(0.05, dragAnimDuration_ + raw) : dragAnimDuration_;
        if (drag_ == Drag::AnimationResize && dragAnimFromStart_) { // its start moves, its end stays
            const double end = dragAnimStart_ + dragAnimDuration_;
            start = std::clamp(dragAnimStart_ + raw, layer->inPoint, end - 0.05);
            duration = end - start;
        }
        const AppliedAnimation before = layer->animations[static_cast<size_t>(index)];
        if (retimeQuickAnimation(*layer, static_cast<size_t>(index), start, duration)) {
            // Its index may change (they stay in time order): follow it by what it is.
            for (size_t i = 0; i < layer->animations.size(); ++i)
                if (layer->animations[i].name == before.name && layer->animations[i].properties == before.properties &&
                    (i == static_cast<size_t>(index) || std::fabs(layer->animations[i].start - before.start) > 1e-9))
                    ctx_.animationIndex = static_cast<int>(i);
            ctx_.touch();
        }
        break;
    }
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
    if (drag_ == Drag::TransitionMove) { // to the clip's other end, or anywhere along it (an animation there)
        if (Layer* layer = ctx_.comp().findLayer(dragLayer_)) {
            const TransitionEdge from = dragTransitionIn_ ? TransitionEdge::In : TransitionEdge::Out;
            const ClipTransition clip = dragTransitionIn_ ? layer->transitionIn : layer->transitionOut;
            const float x = static_cast<float>(pos.x()), xi = timeToX(layer->inPoint), xo = timeToX(layer->outPoint);
            const float zone = std::clamp(timeToX(layer->inPoint + clip.duration) - xi, 12.0f, (xo - xi) * 0.3f);
            const bool atIn = x - xi < zone, atOut = xo - x < zone;
            if ((dragTransitionIn_ && atOut) || (!dragTransitionIn_ && atIn)) {
                if (moveTransition(*layer, from)) {
                    ctx_.selectTransition(layer->id, !dragTransitionIn_);
                    ctx_.touch();
                }
            } else if (!atIn && !atOut && !clip.joined) {
                // Along the clip: the same motion, played there as an animation.
                const AnimationPreset p = transitionPreset(clip.id, from, clip.duration);
                const double at = std::clamp(ctx_.comp().snapToFrame(xToTime(x) - clip.duration * 0.5), layer->inPoint,
                                             std::max(layer->inPoint, layer->outPoint - clip.duration));
                if (!p.tracks.empty() && removeTransition(ctx_.comp(), *layer, from)) {
                    applyQuickAnimation(p, *layer, at);
                    for (size_t i = 0; i < layer->animations.size(); ++i)
                        if (layer->animations[i].name == p.name && std::fabs(layer->animations[i].start - at) < 1e-6) ctx_.selectAnimation(layer->id, static_cast<int>(i));
                    ctx_.touch();
                }
            }
        }
    } else if (drag_ == Drag::TransitionIn || drag_ == Drag::TransitionOut || drag_ == Drag::AnimationMove || drag_ == Drag::AnimationResize) {
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
    rowsWidget()->setCursor(Qt::ArrowCursor);
}

uint64_t TimelinePanel::freeTrackAt(float y, double t, double length) const {
    constexpr float kEdge = 7.0f; // a row's edges make a new track, as when moving clips
    for (const Item& r : items_) {
        if (r.kind != Item::Track || y < r.top + kEdge || y > r.top + r.h - kEdge) continue;
        const double end = t + std::max(length, ctx_.comp().frameDuration());
        for (const Layer& l : ctx_.comp().layers)
            if (l.track == r.track && !l.parent && l.inPoint < end - kTimeEpsilon && l.outPoint > t + kTimeEpsilon) return 0;
        return r.track;
    }
    return 0;
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
    keyMarquee_ = false;
    marqueeStart_ = marqueeEnd_ = pos;
    marqueeBase_ = additive ? ctx_.selection : std::vector<uint64_t>{};
    if (!additive) ctx_.select(0);
    ctx_.clearKeySelection();
}

void TimelinePanel::beginSamplePick(float x) {
    sampling_ = true;
    sampleFrom_ = sampleTo_ = std::clamp(xToTime(x), 0.0, ctx_.comp().duration);
    ctx_.playing = false;
    ctx_.setTime(sampleFrom_);
    relayout();
}

void TimelinePanel::updateSamplePick(float x) {
    sampleTo_ = std::clamp(xToTime(x), 0.0, ctx_.comp().duration);
    ctx_.setTime(sampleTo_); // the needle rides along: what is under it can be seen and found
    relayout();
}

void TimelinePanel::endSamplePick() {
    sampling_ = false;
    const double from = std::min(sampleFrom_, sampleTo_), to = std::max(sampleFrom_, sampleTo_);
    // A click (no stretch) leaves the pick on: the next drag counts.
    if (to - from >= 1.0 / std::max(1.0, ctx_.comp().fps) && ctx_.samplePick) {
        const auto pick = std::move(ctx_.samplePick);
        ctx_.samplePick = nullptr;
        pick(from, to);
    }
    relayout();
}

// While a sample is being picked: the stretch dragged so far, and a word on what to do.
void TimelinePanel::paintSamplePick(ui::Draw& d, float top, float bottom) {
    if (!ctx_.pickingSample()) return;
    const float x0 = trackX0(), x1 = x0 + trackW();
    if (sampling_) {
        const float a = std::clamp(timeToX(std::min(sampleFrom_, sampleTo_)), x0, x1), b = std::clamp(timeToX(std::max(sampleFrom_, sampleTo_)), x0, x1);
        d.rectFilled({a, top}, {b, bottom}, theme::accent(40));
        d.line({a, top}, {a, bottom}, theme::kAccent, 1.5f);
        d.line({b, top}, {b, bottom}, theme::kAccent, 1.5f);
    }
    const QString hint = tr("Drag over a stretch with only the noise or silence to sample it  ·  Esc: cancel");
    const float w = static_cast<float>(QFontMetricsF(font()).horizontalAdvance(hint)) + 24.0f;
    const float cx = std::max(x0 + 6.0f, (x0 + x1 - w) * 0.5f);
    d.rectFilled({cx, top + 6}, {cx + w, top + 30}, theme::kAccent, 12.0f);
    d.text({cx + 12, top + 10}, theme::kOnAccent, hint);
}

void TimelinePanel::beginKeyMarquee(QPointF pos) {
    marquee_ = true;
    keyMarquee_ = true;
    marqueeStart_ = marqueeEnd_ = pos;
    marqueeKeyBase_ = ctx_.selectedKeys;
}

void TimelinePanel::updateKeyMarquee(QPointF pos) {
    marqueeEnd_ = pos;
    const QRectF r = QRectF(marqueeStart_, marqueeEnd_).normalized();
    if (r.width() < 3 && r.height() < 3) return; // still a click
    computeItems();
    // The keyframes of every property row the box touches, inside its time span; their layers
    // join the selection (the one the box started on stays the primary layer).
    ctx_.selectedKeys = marqueeKeyBase_;
    const uint64_t primary = ctx_.primaryId();
    for (const Item& row : items_) {
        if (row.kind != Item::Prop || row.top + row.h < r.top() || row.top > r.bottom()) continue;
        const auto ref = findProperty(ctx_, row.layer, row.prop, row.effectIndex);
        if (!ref) continue;
        for (int k = 0; k < ref->anim->keyCount(); ++k) {
            const float kx = timeToX(ref->anim->keyTime(k));
            if (kx < r.left() - keyRadius() || kx > r.right() + keyRadius()) continue;
            const KeySelection ks{row.layer, row.prop, ref->anim->keyUid(k)};
            if (ctx_.isKeySelected(ks)) continue;
            ctx_.selectedKeys.push_back(ks);
            if (!ctx_.isSelected(row.layer)) ctx_.selection.insert(ctx_.selection.begin(), row.layer);
        }
    }
    if (primary && ctx_.isSelected(primary)) { // keep the primary layer last
        std::erase(ctx_.selection, primary);
        ctx_.selection.push_back(primary);
    }
    relayout();
}

void TimelinePanel::updateMarquee(QPointF pos) {
    if (keyMarquee_) {
        updateKeyMarquee(pos);
        return;
    }
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
