#pragma once

// What the timeline's source files share (TimelinePanel.cpp: the panel, its widgets and layout;
// TimelinePaint.cpp: painting; TimelineInput.cpp: mouse, menus and drags). Internal to them.

#include "app/Settings.h"
#include "core/Layer.h"
#include "core/Project.h"
#include "ui/Draw.h"
#include "ui/Language.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace bemotion::timeline_internal {

using ui::Pt;
using ui::Rgba;


// A transition or a Quick Animation on its clip: the same band for both (they are the same
// thing, keyframes made for you). `stretchRight`: the end its length is dragged by.
inline void drawMotionBand(ui::Draw& d, float left, float right, float y, float h, bool picked, bool stretchRight, bool withIcon,
                    ui::Icon icon, const QString& name, bool bothEnds = false) {
    const Rgba tone = theme::kAccent;
    d.rectFilled({left, y + 5}, {right, y + h - 5}, theme::withAlpha(theme::kBgPanel, 225), 5.0f); // readable over a video's frames
    d.rectFilled({left, y + 5}, {right, y + h - 5}, theme::withAlpha(tone, picked ? 80 : 45), 5.0f);
    d.rect({left, y + 5}, {right, y + h - 5}, picked ? theme::kText : tone, 5.0f, picked ? 2.0f : 1.2f);
    // Where it is stretched from (a Quick Animation: either end).
    const float handle = stretchRight ? right : left;
    d.line({handle, y + 8}, {handle, y + h - 8}, tone, 2.0f);
    if (bothEnds) d.line({left + 1.0f, y + 8}, {left + 1.0f, y + h - 8}, tone, 2.0f);
    const float iconX = left + (bothEnds ? 13.0f : 10.0f);
    if (withIcon) ui::drawIcon(d, icon, {iconX, y + h * 0.5f}, 12.0f, tone);
    if (right - left > 44.0f && !name.isEmpty()) { // its name, when there is room
        d.pushClip({iconX + 8.0f, y}, {right - 3.0f, y + h});
        d.text({iconX + 10.0f, y + (h - d.textHeight()) * 0.5f}, theme::kText, name);
        d.popClip();
    }
}

inline constexpr float kRulerHeight = 26.0f;
// Row sizes come from Settings > Interface.
inline float layerRowHeight() { return settings().timelineLayerRow; }
inline float propRowHeight() { return std::max(26.0f, settings().timelinePropertyRow); }
inline float groupRowHeight() { return std::max(18.0f, settings().timelinePropertyRow * 0.8f); }
inline constexpr float kEdgeGrab = 6.0f;
inline float keyRadius() { return settings().keyframeSize; }
// Left column of a track row.
inline constexpr float kButton = 20.0f;
inline constexpr float kExpandX = 6.0f, kVisX = 30.0f, kLockX = 54.0f, kSoloX = 78.0f, kMuteX = 100.0f, kChipX = 124.0f, kNameX = 156.0f;

inline Qt::KeyboardModifiers mods() { return QGuiApplication::keyboardModifiers(); }
inline bool ctrlHeld() { return mods() & Qt::ControlModifier; }
inline bool shiftHeld() { return mods() & Qt::ShiftModifier; }

inline Rgba toRgba(Color c, float alpha = 1.0f) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return ui::rgba(byte(c.r), byte(c.g), byte(c.b), byte(c.a * alpha));
}

inline double snapDelta(double seconds, double fps) { return std::round(seconds * fps) / fps; }

// Keyframe glyph: the shape hints at the easing of the outgoing segment.
inline void drawKeyGlyph(ui::Draw& d, Pt c, float r, Easing easing, Rgba fill, Rgba outline) {
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

inline std::vector<uint64_t> clipsOfTrack(const Composition& comp, uint64_t track) {
    std::vector<uint64_t> ids;
    for (const Layer& l : comp.layers)
        if (l.track == track) ids.push_back(l.id);
    return ids;
}

inline bool trackHasSound(const Composition& comp, uint64_t track) {
    return std::any_of(comp.layers.begin(), comp.layers.end(),
                       [&](const Layer& l) { return l.track == track && l.type == LayerType::Media && l.media.hasAudio; });
}

// What a clip bar says after its name: how it plays and how it is composited.
inline std::string clipNotes(const Composition& comp, const Layer& l) {
    std::string s;
    auto add = [&](const std::string& note) { s += "  ·  " + note; };
    auto t = [](const char* text) { return tr(text).toStdString(); };
    if (l.type == LayerType::Media && l.media.timeRemap) add(l.media.remap.isAnimated() ? t("Time Remap") : t("Frozen"));
    else if (l.type == LayerType::Media && (l.media.speed != 1.0 || l.media.reversed)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.4g%%", l.media.speed * 100.0);
        add((l.media.reversed ? t("Reversed") + (l.media.speed != 1.0 ? " " : "") : std::string()) + (l.media.speed != 1.0 ? buf : ""));
    }
    if (comp.isMatte(l.id)) add(t("Matte"));
    if (l.matte != MatteMode::None)
        if (const Layer* m = comp.findLayer(l.matteLayer)) add(tr("Matte: %1").arg(QString::fromStdString(m->name)).toStdString());
    if (l.blend != BlendMode::Normal) add(t(kBlendModeLabels[static_cast<int>(l.blend)]));
    if (!l.masks.empty()) add(l.masks.size() == 1 ? t("1 mask") : tr("%1 masks").arg(l.masks.size()).toStdString());
    return s;
}

inline std::string rowKey(uint64_t layer, const PropertyRef& p) {
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

} // namespace bemotion::timeline_internal
