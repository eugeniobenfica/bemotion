#include "ui/panels/TimelinePanel.h"

// The timeline panel: painting (the rest: TimelinePanel.cpp, TimelineInput.cpp).

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
    d.text({12, 6}, theme::kTextFaint, tr("LAYERS"));
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
    // Markers: a flag in their color, the name beside it when there is room.
    for (size_t i = 0; i < comp.markers.size(); ++i) {
        const Marker& m = comp.markers[i];
        const float mx = timeToX(m.time);
        if (mx < x0 - 80 || mx > x1 + 8) continue;
        const Rgba col = toRgba(labelColor(m.color));
        const Pt flag[5] = {{mx - 5, 1}, {mx + 5, 1}, {mx + 5, 8}, {mx, 12}, {mx - 5, 8}};
        d.polyFilled(flag, 5, col);
        if (static_cast<int>(i) == dragMarker_) d.polyline(flag, 5, theme::kText, true, 1.2f);
        d.line({mx, 12}, {mx, h}, theme::withAlpha(col, 160), 1.0f);
        const float next = i + 1 < comp.markers.size() ? timeToX(comp.markers[i + 1].time) : x1;
        if (!m.name.empty() && next - mx > 40) {
            QFont small = p.font();
            small.setPointSizeF(ticks.pointSizeF() * 0.85);
            p.setFont(small);
            d.pushClip({mx + 6, 0}, {std::min(next - 4, x1), 13});
            d.rectFilled({mx + 6, 0}, {mx + 10 + d.textWidth(QString::fromStdString(m.name)), 13}, theme::kBgDeep, 3.0f);
            d.text({mx + 8, 0}, col, m.name);
            d.popClip();
            p.setFont(ticks);
        }
    }
    // Playhead handle.
    const float ph = timeToX(ctx_.time);
    const Pt head[5] = {{ph - 6, 3}, {ph + 6, 3}, {ph + 6, 14}, {ph, 20}, {ph - 6, 14}};
    d.polyFilled(head, 5, theme::kPlayhead);
    d.line({ph, 18}, {ph, h}, theme::kPlayhead, 1.5f);
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
            case Item::Mask: paintMask(p, item, width); break;
            case Item::Audio: paintAudio(p, item, width); break;
            }
        }
        if (ctx_.comp().layers.empty() && !hasRecordingPreview()) {
            const QString hint = tr("Add text or graphics from the Objects panel to start animating.");
            d.text({(width - d.textWidth(hint)) * 0.5f, rowsBottom_ + 24}, theme::kTextFaint, hint);
        }
    }

    // Column divider, area past the end of the composition, playhead.
    const float x0 = trackX0(), x1 = x0 + trackW();
    d.line({x0 - 1, 0}, {x0 - 1, bottom}, theme::kBorder);
    d.pushClip({x0, 0}, {x1, bottom});
    const float endX = timeToX(visibleEnd());
    if (endX < x1 && (!ctx_.comp().layers.empty() || hasRecordingPreview())) d.rectFilled({endX, 0}, {x1, bottom}, theme::shade(80));
    for (const Marker& m : ctx_.comp().markers) { // a faint line down from each marker
        const float mx = timeToX(m.time);
        if (mx >= x0 && mx <= x1) d.line({mx, 0}, {mx, bottom}, toRgba(labelColor(m.color), 0.35f), 1.0f);
    }
    const float ph = timeToX(ctx_.time);
    d.line({ph, 0}, {ph, bottom}, theme::kPlayhead, 1.5f);
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
    if (graphMode_) paintGraphEmpty(p, size);

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
    paintSamplePick(d, 0.0f, static_cast<float>(size.height()));
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
        const auto onto = std::find_if(items_.begin(), items_.end(), [this](const Item& r) { return r.kind == Item::Track && r.track == dropTrack_; });
        if (dropTrack_ && onto != items_.end()) { // into that track's free room: the clip as it will sit
            d.rectFilled({tx, onto->top + 3}, {std::max(tx + 60, timeToX(dropTime_ + dropLength_)), onto->top + onto->h - 3}, theme::accent(70), 5.0f);
            d.rect({tx, onto->top + 3}, {std::max(tx + 60, timeToX(dropTime_ + dropLength_)), onto->top + onto->h - 3}, theme::kAccent, 5.0f, 1.5f);
            gy = onto->top + 9;
        } else {
            d.line({0, gy}, {width, gy}, theme::kAccent, 2.0f);
            d.rectFilled({tx, gy - 9}, {std::max(tx + 60, timeToX(dropTime_ + dropLength_)), gy + 9}, theme::accent(60), 4.0f);
        }
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
    ui::drawIcon(d, ui::Icon::Solo, {kSoloX + kButton * 0.5f, cy}, kButton * 0.62f, focus->solo ? theme::kAccent : theme::kTextFaint);
    if (trackHasSound(ctx_.comp(), item.track))
        ui::drawIcon(d, focus->muted ? ui::Icon::SpeakerMuted : ui::Icon::Speaker, {kMuteX + kButton * 0.5f, cy}, kButton * 0.62f,
                     focus->muted ? theme::kDanger : theme::kTextFaint);
    const bool silenced = ctx_.comp().soloedOut(*focus); // another track is solo
    d.rectFilled({kChipX + 2, y + 8}, {kChipX + 5, y + h - 8}, toRgba(labelColor(focus->label)), 2.0f);
    ui::drawIcon(d, ui::iconForLayer(*focus), {kChipX + 18, cy}, 14.0f, anySelected ? theme::kAccent : theme::kTextDim);
    const float nameW = std::max(10.0f, leftWidth_ - kNameX - 8);
    if (renaming_ != focus->id) {
        const Rgba col = focus->visible && !silenced ? (anySelected ? theme::kText : theme::kTextDim) : theme::kTextFaint;
        d.pushClip({kNameX, y}, {kNameX + nameW, y + h});
        const float textY = y + (h - d.textHeight()) * 0.5f;
        d.text({kNameX, textY}, col, focus->name);
        if (item.count > 1) { // e.g. a track that was cut into pieces
            const QString badge = tr("%1 clips").arg(item.count);
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
        // A video shows its frames, its sound (if any) as a band along the bottom growing up; sound
        // alone fills the clip.
        const bool frames = layer.type == LayerType::Media && layer.media.hasVideo && paintFilmstrip(p, layer, {a.x, a.y}, {b.x, b.y});
        // Sound alone: its waveform here; a video's sound has a row of its own (paintAudio).
        if (layer.type == LayerType::Media && layer.media.hasAudio && !layer.media.hasVideo) paintWaveform(p, layer, a, b, toRgba(lc, 0.75f));
        if (selected && frames) d.rect(a, b, primary ? theme::kText : theme::ink(170), 5.0f, primary ? 1.8f : 1.3f); // over the pictures
        paintAnimations(p, layer, y, h);
        auto paintTransition = [&](const ClipTransition& transition, bool incoming) {
            if (!transition.active()) return;
            const float duration = std::max(16.0f, std::fabs(timeToX(layer.inPoint + transition.duration) - bx1));
            const float left = incoming ? bx1 + 2.0f : bx2 - duration - 2.0f;
            const float right = incoming ? bx1 + duration + 2.0f : bx2 - 2.0f;
            // Picked, here or (across a cut) on its other half: both halves light up.
            bool selectedTransition = ctx_.transitionLayer == layer.id && ctx_.transitionIncoming == incoming;
            if (!selectedTransition && transition.joined && ctx_.transitionLayer)
                if (const Layer* other = transitionPartner(ctx_.comp(), layer, incoming ? TransitionEdge::In : TransitionEdge::Out))
                    selectedTransition = ctx_.transitionLayer == other->id && ctx_.transitionIncoming == !incoming;
            // A transition across a cut shows one icon, on the cut (its two halves meet there).
            const TransitionInfo* info = findTransition(transition.id);
            drawMotionBand(d, left, right, y, h, selectedTransition, incoming, !transition.joined, ui::Icon::Transition,
                           transition.joined || !info ? QString() : tr(info->name));
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
            // After what covers the clip's start (its transition, a Quick Animation there).
            float nameX = std::max(bx1, x0) + 10;
            if (layer.transitionIn.active()) nameX = std::max(nameX, timeToX(layer.inPoint + layer.transitionIn.duration) + 8);
            for (const AppliedAnimation& an : layer.animations)
                if (an.start <= layer.inPoint + kTimeEpsilon) nameX = std::max(nameX, std::max(timeToX(an.start + an.duration), bx1 + 18.0f) + 6);
            const QString name = QString::fromStdString(layer.name + clipNotes(ctx_.comp(), layer));
            d.pushClip({std::max(bx1, x0), y}, {std::min(bx2, x1), y + h});
            if (frames) { // a dark pill: readable over the pictures
                const float top = y + 7, w = d.textWidth(name) + 12;
                d.rectFilled({nameX - 6, top}, {nameX + w - 6, top + d.textHeight() + 4}, theme::withAlpha(theme::kBgDeep, 200), 4.0f);
                d.text({nameX, top + 2}, theme::kText, name);
            } else {
                d.text({nameX, y + (h - d.textHeight()) * 0.5f}, theme::ink(150), name);
            }
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

void TimelinePanel::paintMask(QPainter& p, const Item& item, float width) {
    ui::Draw d(p);
    Layer* layer = ctx_.comp().findLayer(item.layer);
    if (!layer || item.effectIndex < 0 || item.effectIndex >= static_cast<int>(layer->masks.size())) return;
    const Mask& mask = layer->masks[static_cast<size_t>(item.effectIndex)];
    const float y = item.top, h = item.h, cy = y + h * 0.5f;
    const bool picked = ctx_.isSelected(layer->id) && ctx_.maskFocus == item.effectIndex;
    d.rectFilled({0, y}, {width, y + h}, picked ? theme::accent(40) : theme::shade(45));
    if (picked) d.rectFilled({0, y + 2}, {3, y + h - 2}, theme::kAccent, 1.5f);
    // Left: on / off (the eye column), a mask icon, its name, how it combines.
    const Rgba ink = mask.enabled ? theme::kText : theme::kTextFaint;
    ui::drawIcon(d, mask.enabled ? ui::Icon::Eye : ui::Icon::EyeOff, {kVisX + kButton * 0.5f, cy}, kButton * 0.6f, mask.enabled ? theme::kTextDim : theme::kTextFaint);
    ui::drawIcon(d, ui::Icon::Mask, {kChipX + 8.0f, cy}, 14.0f, picked ? theme::kAccent : theme::kTextDim);
    d.pushClip({kNameX - 8, y}, {leftWidth_ - 6, y + h});
    const QString how = tr(kMaskModeLabels[static_cast<int>(mask.mode)]) + (mask.inverted ? tr(" · Inverted") : QString());
    d.text({kNameX - 4, cy - d.textHeight() * 0.5f}, ink, trLabel(QString::fromStdString(mask.name)));
    d.text({leftWidth_ - 10 - d.textWidth(how), cy - d.textHeight() * 0.5f}, theme::kTextFaint, how);
    d.popClip();
    // Right: a bar along its clip.
    const float x0 = trackX0(), x1 = x0 + trackW();
    d.pushClip({x0, y}, {x1, y + h});
    const float a = timeToX(layer->inPoint) + 1, b = timeToX(layer->outPoint) - 1;
    d.rectFilled({a, y + 3}, {b, y + h - 3}, mask.enabled ? theme::ink(picked ? 60 : 30) : theme::ink(12), 4.0f);
    d.rect({a, y + 3}, {b, y + h - 3}, picked ? theme::kAccent : theme::ink(70), 4.0f, picked ? 1.5f : 1.0f);
    ui::drawIcon(d, ui::Icon::Mask, {std::max(a, x0) + 12.0f, cy}, 12.0f, theme::kTextDim);
    d.popClip();
}

void TimelinePanel::paintAudio(QPainter& p, const Item& item, float width) {
    ui::Draw d(p);
    const auto& layers = ctx_.comp().layers;
    const float y = item.top, h = item.h, cy = y + h * 0.5f;
    bool anySelected = false, anyOn = false;
    for (int i = item.first; i < item.first + item.count && i < static_cast<int>(layers.size()); ++i)
        if (layers[i].media.hasAudio && layers[i].media.hasVideo) {
            anySelected = anySelected || ctx_.isSelected(layers[i].id);
            anyOn = anyOn || !layers[i].muted;
        }
    d.rectFilled({0, y}, {width, y + h}, anySelected ? theme::accent(22) : theme::shade(45));
    // Left: sound on / off for the track's videos, a speaker, "Audio".
    ui::drawIcon(d, anyOn ? ui::Icon::Speaker : ui::Icon::SpeakerMuted, {kVisX + kButton * 0.5f, cy}, kButton * 0.6f,
                 anyOn ? theme::kTextDim : theme::kTextFaint);
    ui::drawIcon(d, ui::Icon::Audio, {kChipX + 8.0f, cy}, 14.0f, theme::kTextDim);
    d.pushClip({kNameX - 8, y}, {leftWidth_ - 6, y + h});
    d.text({kNameX - 4, cy - d.textHeight() * 0.5f}, anyOn ? theme::kText : theme::kTextFaint, tr("Audio"));
    d.popClip();
    // Right: each video's sound under it.
    const float x0 = trackX0(), x1 = x0 + trackW();
    d.pushClip({x0, y}, {x1, y + h});
    for (int i = item.first; i < item.first + item.count && i < static_cast<int>(layers.size()); ++i) {
        const Layer& l = layers[i];
        if (!l.media.hasAudio || !l.media.hasVideo) continue;
        const bool selected = ctx_.isSelected(l.id);
        const Color lc = labelColor(l.label);
        const Pt a(timeToX(l.inPoint) + 1, y + 3), b(timeToX(l.outPoint) - 1, y + h - 3);
        d.rectFilled(a, b, toRgba(lc, l.muted ? 0.08f : selected ? 0.32f : 0.2f), 4.0f);
        if (selected) d.rect(a, b, theme::ink(170), 4.0f, 1.2f);
        if (!l.muted) paintWaveform(p, l, {a.x, a.y}, {b.x, b.y}, toRgba(lc, 0.85f));
    }
    d.popClip();
}

void TimelinePanel::showMaskMenu(QPoint global, uint64_t layerId, int index) {
    Layer* layer = ctx_.comp().findLayer(layerId);
    if (!layer || index < 0 || index >= static_cast<int>(layer->masks.size())) return;
    QMenu menu;
    auto mask = [this, layerId, index]() -> Mask* {
        Layer* l = ctx_.comp().findLayer(layerId);
        return l && index < static_cast<int>(l->masks.size()) ? &l->masks[static_cast<size_t>(index)] : nullptr;
    };
    QMenu* mode = menu.addMenu(tr("Mode"));
    for (int i = 0; i < static_cast<int>(std::size(kMaskModeLabels)); ++i) {
        QAction* a = mode->addAction(tr(kMaskModeLabels[i]), [this, mask, i] {
            if (Mask* m = mask()) m->mode = static_cast<MaskMode>(i), ctx_.touch();
        });
        a->setCheckable(true);
        a->setChecked(static_cast<int>(layer->masks[static_cast<size_t>(index)].mode) == i);
    }
    QAction* inverted = menu.addAction(tr("Inverted"), [this, mask] {
        if (Mask* m = mask()) m->inverted = !m->inverted, ctx_.touch();
    });
    inverted->setCheckable(true);
    inverted->setChecked(layer->masks[static_cast<size_t>(index)].inverted);
    menu.addAction(tr("Rename..."), [this, mask] {
        Mask* m = mask();
        if (!m) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Mask"), tr("Name"), QLineEdit::Normal, QString::fromStdString(m->name), &ok);
        if (ok && !name.trimmed().isEmpty() && (m = mask())) m->name = name.trimmed().toStdString(), ctx_.touch();
    });
    menu.addSeparator();
    menu.addAction(tr("Delete Mask"), [this, layerId, index] {
        ctx_.selectMask(layerId, index);
        ctx_.deleteFocusedMask();
    });
    menu.exec(global);
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
    d.text({x0 + 48, y + (h - d.textHeight()) * 0.5f}, theme::kTextDim, trLabel(QString::fromStdString(item.groupLabel)));
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
        d.line({xa, cy}, {xb, cy}, anim.keyEasing(k) == Easing::Hold ? theme::ink(25) : theme::withAlpha(theme::kKeyframe, 70), 1.5f);
    }
    for (int k = 0; k < anim.keyCount(); ++k) {
        const bool sel = ctx_.isKeySelected({layer->id, item.prop, anim.keyUid(k)});
        const float kx = timeToX(anim.keyTime(k));
        const bool hot = hoverX_ >= 0 && std::fabs(hoverX_ - kx) <= keyRadius() + 2 && rowsWidget()->underMouse() &&
                         rowsWidget()->mapFromGlobal(QCursor::pos()).y() >= y && rowsWidget()->mapFromGlobal(QCursor::pos()).y() < y + h;
        const Rgba fill = sel ? theme::kKeyframe : (hot ? theme::kText : theme::kTextDim);
        drawKeyGlyph(d, {kx, cy}, keyRadius() + (sel ? 1.0f : 0.0f), anim.keyEasing(k), fill,
                     sel ? theme::mix(theme::kKeyframe, ui::rgba(255, 255, 255), 0.18f) : theme::kBgDeep);
    }
    d.popClip();
}

bool TimelinePanel::paintFilmstrip(QPainter& p, const Layer& layer, QPointF a, QPointF b) {
    const MediaItem* item = ctx_.comp().findMedia(layer.media.mediaId);
    if (!item || item->kind != MediaKind::Video || !settings().showFilmstrips) return false;
    const std::shared_ptr<const media::Filmstrip> strip = media::MediaManager::get().filmstrip(*item);
    if (!strip) return false;
    const QRectF area(a + QPointF(1, 1), b - QPointF(1, 1));
    if (area.width() < 2 || area.height() < 4 || strip->frames.empty()) return false;
    const qreal th = area.height(), tw = th * strip->width / std::max(1, strip->height);
    // Pictures already at the size drawn (scaled once, not at every paint), made as they arrive.
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const int ph = std::max(1, static_cast<int>(std::lround(th * dpr))), pw = std::max(1, static_cast<int>(std::lround(tw * dpr)));
    FilmImages& cached = filmImages_[item->id];
    if (cached.strip != strip || cached.height != ph) {
        if (cached.height != ph || cached.pictures.size() != strip->frames.size()) cached.pictures.assign(strip->frames.size(), QPixmap());
        cached.strip = strip;
        cached.height = ph;
    }
    auto picture = [&](size_t i) -> const QPixmap* {
        const auto& rgba = strip->frames[i];
        if (rgba.empty()) return nullptr;
        QPixmap& pm = cached.pictures[i];
        if (pm.isNull()) {
            const QImage image(rgba.data(), strip->width, strip->height, QImage::Format_RGBA8888);
            pm = QPixmap::fromImage(image.scaled(pw, ph, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            pm.setDevicePixelRatio(dpr);
        }
        return &pm;
    };
    p.save();
    p.setClipRect(area, Qt::IntersectClip);
    p.setOpacity(layer.visible ? 0.9 : 0.35);
    // Tiles from the clip's start, each showing the moment under its middle.
    const qreal from = std::max(area.left(), static_cast<qreal>(trackX0()) - tw), to = std::min(area.right(), static_cast<qreal>(trackX0() + trackW()));
    const qreal first = area.left() + std::floor((from - area.left()) / tw) * tw;
    for (qreal x = first; x < to; x += tw) {
        const double source = layer.media.sourceTime(xToTime(static_cast<float>(x + tw * 0.5)));
        const int n = static_cast<int>(strip->frames.size());
        const int i = std::clamp(static_cast<int>(source / strip->step), 0, n - 1);
        // Not made yet: the nearest one that is.
        const QPixmap* pm = nullptr;
        for (int k = 0; k < n && !pm; ++k) {
            if (i + k < n) pm = picture(static_cast<size_t>(i + k));
            if (!pm && i - k >= 0) pm = picture(static_cast<size_t>(i - k));
        }
        if (pm) p.drawPixmap(QPointF(x, area.top()), *pm);
    }
    p.restore();
    return true;
}

void TimelinePanel::paintAnimations(QPainter& p, const Layer& layer, float y, float h) {
    if (layer.animations.empty()) return;
    ui::Draw d(p);
    auto props = const_cast<Layer&>(layer).properties();
    for (const AppliedAnimation& a : layer.animations) {
        const double from = std::max(a.start, layer.inPoint), until = std::min(a.start + a.duration, layer.outPoint);
        if (until <= from) continue;
        // Still there? (its keys may have been moved or deleted since; only its own properties looked at)
        bool keyed = false;
        for (const PropertyRef& pr : props)
            if (a.properties.empty() || std::find(a.properties.begin(), a.properties.end(), pr.id) != a.properties.end())
                for (int k = 0; k < pr.anim->keyCount() && !keyed; ++k)
                keyed = pr.anim->keyTime(k) >= a.start - kTimeEpsilon && pr.anim->keyTime(k) <= a.start + a.duration + kTimeEpsilon;
        if (!keyed) continue;
        const float left = timeToX(from) + 2.0f, right = std::max(left + 16.0f, timeToX(until) - 2.0f);
        const bool picked = ctx_.animationLayer == layer.id && ctx_.animationIndex == static_cast<int>(&a - layer.animations.data());
        drawMotionBand(d, left, right, y, h, picked, true, true, ui::Icon::Ease, trs(a.name), true);
    }
}

void TimelinePanel::paintWaveform(QPainter& p, const Layer& layer, QPointF a, QPointF b, uint32_t color, bool fromBottom) const {
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
        double t0 = layer.media.sourceTime(xToTime(x)), t1 = layer.media.sourceTime(xToTime(x + kStep));
        if (t1 < t0) std::swap(t0, t1); // played backwards
        if (t1 <= 0) continue;
        const size_t i0 = static_cast<size_t>(std::max(0.0, t0) * media::kAudioRate);
        const size_t i1 = std::min(frames, static_cast<size_t>(std::max(0.0, t1) * media::kAudioRate) + 1);
        if (i0 >= frames) continue;
        const size_t stride = std::max<size_t>(1, (i1 - i0) / 48);
        float peak = 0.0f;
        for (size_t i = i0; i < i1; i += stride) peak = std::max({peak, std::fabs(s[i * 2]), std::fabs(s[i * 2 + 1])});
        const float gain = std::clamp(layer.media.volume.valueAt(xToTime(x)) * 0.01f, 0.0f, 2.0f);
        const float level = std::sqrt(std::min(1.0f, peak * gain)); // sqrt: quiet audio stays visible
        if (fromBottom) { // under a video's frames: one side, up from the bottom
            const float full = static_cast<float>(b.y() - a.y()) * 0.9f, hh = std::max(0.5f, level * full);
            path.addRect(QRectF(x, b.y() - hh, kStep - 0.6f, hh));
        } else {
            const float hh = std::max(0.5f, level * half);
            path.addRect(QRectF(x, mid - hh, kStep - 0.6f, hh * 2));
        }
    }
    p.fillPath(path, ui::qcolor(color));
}

} // namespace bemotion
