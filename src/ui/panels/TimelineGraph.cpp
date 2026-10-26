// Graph editor of the timeline (see TimelinePanel.h).

#include "ui/panels/TimelinePanel.h"

#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bemotion {

using ui::Pt;
using ui::Rgba;

namespace {

constexpr float kHit = 7.0f;      // pick radius (px)
constexpr float kListRow = 24.0f; // property list row height

int components(const PropertyRef& p) {
    return p.kind == PropertyKind::Vec2 ? 2 : p.kind == PropertyKind::Float ? 1 : 0; // colors are not graphed
}

float keyValue(const PropertyRef& p, int i, int k) {
    if (p.kind == PropertyKind::Float) return p.asFloat().keys[i].value;
    const Vec2 v = p.asVec2().keys[i].value;
    return k == 0 ? v.x : v.y;
}

void setKeyValue(const PropertyRef& p, int i, int k, float value) {
    if (p.kind == PropertyKind::Float) {
        p.asFloat().keys[i].value = value;
    } else {
        Vec2& v = p.asVec2().keys[i].value;
        (k == 0 ? v.x : v.y) = value;
    }
}

float valueAt(const PropertyRef& p, double t, int k) {
    if (p.kind == PropertyKind::Float) return p.asFloat().valueAt(t);
    const Vec2 v = p.asVec2().valueAt(t);
    return k == 0 ? v.x : v.y;
}

// X / Y like After Effects (red / green); single values get a color per property.
Rgba curveColor(int propIndex, int comp, int comps) {
    if (comps == 2) return comp == 0 ? ui::rgba(236, 96, 96) : ui::rgba(96, 204, 120);
    static constexpr Rgba kColors[] = {ui::rgba(232, 185, 49), ui::rgba(79, 142, 247), ui::rgba(167, 123, 243),
                                       ui::rgba(60, 198, 214), ui::rgba(242, 140, 56), ui::rgba(217, 101, 168)};
    return kColors[propIndex % std::size(kColors)];
}

// The segment's timing as a cubic-bezier (built-in easings start from their look-alike).
BezierEase curveOf(const AnimatedBase& anim, int i) {
    const Easing e = anim.keyEasing(i);
    if (e == Easing::Custom) return anim.keyCurve(i);
    if (e == Easing::Linear || e == Easing::Hold) return {1.0f / 3, 1.0f / 3, 2.0f / 3, 2.0f / 3};
    return bezierFor(e);
}

void setCurve(AnimatedBase& anim, int i, const BezierEase& c) {
    anim.setKeyEasing(i, Easing::Custom);
    anim.setKeyCurve(i, c);
}

std::string formatValue(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, std::fabs(v) >= 100.0f ? "%.0f" : "%.1f", v);
    return buf;
}

// Animated (graphable) properties of `layer`, in order.
std::vector<PropertyRef> animatedProps(Layer& layer) {
    std::vector<PropertyRef> out;
    for (const PropertyRef& p : layer.properties())
        if (components(p) > 0 && p.anim->keyCount() > 0) out.push_back(p);
    return out;
}

} // namespace

void TimelinePanel::applyEase(EditorContext& ctx, Ease ease) {
    for (const KeySelection& sel : ctx.selectedKeys) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str());
        if (!p) continue;
        AnimatedBase& anim = *p->anim;
        const int i = anim.findKeyByUid(sel.keyUid);
        if (i < 0) continue;
        const bool hasOut = i + 1 < anim.keyCount(), hasIn = i > 0;
        auto outgoing = [&](float x1, float y1) { // this key's side of the next segment
            if (!hasOut) return;
            BezierEase c = curveOf(anim, i);
            c.x1 = x1, c.y1 = y1;
            setCurve(anim, i, c);
        };
        auto incoming = [&](float x2, float y2) { // this key's side of the previous segment
            if (!hasIn) return;
            BezierEase c = curveOf(anim, i - 1);
            c.x2 = x2, c.y2 = y2;
            setCurve(anim, i - 1, c);
        };
        switch (ease) {
        case Ease::EasyEase: outgoing(1.0f / 3, 0.0f), incoming(2.0f / 3, 1.0f); break;
        case Ease::EaseIn: incoming(2.0f / 3, 1.0f); break;
        case Ease::EaseOut: outgoing(1.0f / 3, 0.0f); break;
        case Ease::Linear: outgoing(1.0f / 3, 1.0f / 3), incoming(2.0f / 3, 2.0f / 3); break;
        case Ease::Hold:
            if (hasOut) anim.setKeyEasing(i, Easing::Hold);
            break;
        }
    }
    if (!ctx.selectedKeys.empty()) ctx.touch();
}

float TimelinePanel::graphY(float v) const {
    return graphBottom_ - (v - graphMin_) / (graphMax_ - graphMin_) * (graphBottom_ - graphTop_);
}

float TimelinePanel::graphValue(float y) const {
    return graphMin_ + (graphBottom_ - y) / (graphBottom_ - graphTop_) * (graphMax_ - graphMin_);
}

std::vector<TimelinePanel::GraphHit> TimelinePanel::graphHits() {
    std::vector<GraphHit> hits;
    Layer* layer = ctx_.selected();
    if (!layer) return hits;
    const auto animated = animatedProps(*layer);
    const float gx0 = trackX0(), gx1 = trackX0() + trackW();
    graphTop_ = 14.0f;
    graphBottom_ = std::max(graphTop_ + 20.0f, static_cast<float>(rowsWidget()->height()) - 14.0f);
    // Vertical range: fit what is visible (frozen while dragging so nothing jumps).
    if (graphDrag_.kind == GraphHit::None) {
        float lo = 1e30f, hi = -1e30f;
        for (const PropertyRef& p : animated) {
            if (graphHidden_.count(p.id)) continue;
            for (int k = 0; k < components(p); ++k) {
                for (int i = 0; i < p.anim->keyCount(); ++i) lo = std::min(lo, keyValue(p, i, k)), hi = std::max(hi, keyValue(p, i, k));
                for (float x = gx0; x <= gx1; x += 6.0f) {
                    const float v = valueAt(p, xToTime(x), k);
                    lo = std::min(lo, v), hi = std::max(hi, v);
                }
            }
        }
        if (lo > hi) lo = 0.0f, hi = 1.0f;
        if (hi - lo < 1e-3f) lo -= 1.0f, hi += 1.0f;
        const float pad = (hi - lo) * 0.12f;
        graphMin_ = lo - pad, graphMax_ = hi + pad;
    }
    for (const PropertyRef& p : animated) {
        if (graphHidden_.count(p.id)) continue;
        AnimatedBase& anim = *p.anim;
        for (int k = 0; k < components(p); ++k) {
            // Easing handles of each segment (only for selected keys' segments, like After Effects).
            for (int i = 0; i + 1 < anim.keyCount(); ++i) {
                if (anim.keyEasing(i) == Easing::Hold) continue;
                const bool selA = ctx_.isKeySelected({layer->id, p.id, anim.keyUid(i)});
                const bool selB = ctx_.isKeySelected({layer->id, p.id, anim.keyUid(i + 1)});
                if (!selA && !selB) continue;
                const double ta = anim.keyTime(i), tb = anim.keyTime(i + 1);
                const float va = keyValue(p, i, k), vb = keyValue(p, i + 1, k);
                if (std::fabs(vb - va) < 1e-5f) continue; // flat: the timing curve has no height here
                const BezierEase c = curveOf(anim, i);
                if (selA)
                    hits.push_back({GraphHit::Handle, p.id, k, anim.keyUid(i), 1,
                                    QPointF(timeToX(ta + c.x1 * (tb - ta)), graphY(va + c.y1 * (vb - va))), ""});
                if (selB)
                    hits.push_back({GraphHit::Handle, p.id, k, anim.keyUid(i), 2,
                                    QPointF(timeToX(ta + c.x2 * (tb - ta)), graphY(va + c.y2 * (vb - va))), ""});
            }
            for (int i = 0; i < anim.keyCount(); ++i) {
                char label[160];
                std::snprintf(label, sizeof label, "%s  %s\n%.2f s  ·  %s", p.label, components(p) == 2 ? (k ? "Y" : "X") : "",
                              anim.keyTime(i), formatValue(keyValue(p, i, k)).c_str());
                hits.push_back({GraphHit::Key, p.id, k, anim.keyUid(i), 0, QPointF(timeToX(anim.keyTime(i)), graphY(keyValue(p, i, k))), label});
            }
        }
    }
    return hits;
}

const TimelinePanel::GraphHit* TimelinePanel::graphHitAt(const std::vector<GraphHit>& hits, QPointF m) const {
    // Handles win over keys when they overlap.
    const GraphHit* best = nullptr;
    float bestD = kHit;
    for (const GraphHit& h : hits) {
        const float d = static_cast<float>(std::hypot(h.pos.x() - m.x(), h.pos.y() - m.y())) - (h.kind == GraphHit::Handle ? 1.5f : 0.0f);
        if (d < bestD) bestD = d, best = &h;
    }
    return best;
}

void TimelinePanel::paintGraph(QPainter& p, QSize size) {
    ui::Draw d(p);
    const float height = static_cast<float>(size.height());
    Layer* layer = ctx_.selected();
    graphEmpty_ = {};
    // Nothing to draw: the list column stays, the graph says what it needs (drawn over the
    // playhead by paintRows, see paintGraphEmpty).
    auto empty = [&](const char* title, const char* text) {
        d.rectFilled({0, 0}, {trackX0() - 1, height}, theme::shade(30));
        graphEmpty_ = {title, text};
    };
    if (!layer) {
        // The layers to pick from, right here (their rows are hidden while the graph shows).
        empty(TR_NOOP("Select a layer"), TR_NOOP("Pick one on the left (or in the viewport): its animated properties show here as curves."));
        const auto layers = graphLayers();
        const QPoint mouse = rowsWidget()->mapFromGlobal(QCursor::pos());
        d.text({16, 8 + (kListRow - d.textHeight()) * 0.5f}, theme::kTextFaint, tr("Layers"));
        for (size_t i = 0; i < layers.size(); ++i) {
            const Layer* l = ctx_.comp().findLayer(layers[i]);
            if (!l) continue;
            const float y = 8 + kListRow * static_cast<float>(i + 1);
            if (rowsWidget()->underMouse() && mouse.x() >= 8 && mouse.x() < leftWidth_ - 8 && mouse.y() >= y && mouse.y() < y + kListRow)
                d.rectFilled({8, y}, {leftWidth_ - 8, y + kListRow}, theme::kBgHover, 4.0f);
            const Color lc = labelColor(l->label);
            d.rectFilled({16, y + 6}, {19, y + kListRow - 6}, ui::rgba(static_cast<int>(lc.r * 255), static_cast<int>(lc.g * 255), static_cast<int>(lc.b * 255)), 1.5f);
            ui::drawIcon(d, ui::iconForLayer(*l), {34, y + kListRow * 0.5f}, 12, theme::kTextDim);
            d.text({48, y + (kListRow - d.textHeight()) * 0.5f}, theme::kTextDim, l->name);
        }
        return;
    }
    const auto animated = animatedProps(*layer);
    if (animated.empty()) {
        empty(TR_NOOP("Nothing animated yet"), TR_NOOP("Add keyframes with the diamond beside a property in Properties: their curves show here."));
        return;
    }
    const auto hits = graphHits(); // fits the range too

    // --- left: property list (click to show / hide a curve) -------------------------
    d.rectFilled({0, 0}, {trackX0() - 1, height}, theme::shade(30));
    const QPoint mouse = rowsWidget()->mapFromGlobal(QCursor::pos());
    for (size_t pi = 0; pi < animated.size(); ++pi) {
        const PropertyRef& pr = animated[pi];
        const float y = 8 + kListRow * static_cast<float>(pi);
        const bool hidden = graphHidden_.count(pr.id) > 0;
        if (rowsWidget()->underMouse() && mouse.x() >= 8 && mouse.x() < leftWidth_ - 8 && mouse.y() >= y && mouse.y() < y + kListRow)
            d.rectFilled({8, y}, {leftWidth_ - 8, y + kListRow}, theme::kBgHover, 4.0f);
        const int comps = components(pr);
        for (int k = 0; k < comps; ++k) {
            const Rgba c = curveColor(static_cast<int>(pi), k, comps);
            const Pt sw(18 + k * 10.0f, y + kListRow * 0.5f);
            if (hidden) d.circle(sw, 4.0f, c, 1.5f);
            else d.circleFilled(sw, 4.0f, c);
        }
        d.text({46, y + (kListRow - d.textHeight()) * 0.5f}, hidden ? theme::kTextFaint : theme::kTextDim,
               QString("%1  ·  %2").arg(pr.group, pr.label));
    }

    // --- right: the graph ---------------------------------------------------------------
    const float gx0 = trackX0(), gx1 = trackX0() + trackW();
    if (graphBottom_ - graphTop_ < 20.0f) return;
    d.pushClip({gx0, 0}, {gx1, height});
    for (int g = 0; g <= 4; ++g) {
        const float v = graphMin_ + (graphMax_ - graphMin_) * (0.1f + 0.8f * g / 4.0f);
        const float y = graphY(v);
        d.line({gx0, y}, {gx1, y}, theme::ink(14));
        d.text({gx0 + 4, y - d.textHeight() - 1}, theme::kTextFaint, formatValue(v));
    }
    for (size_t pi = 0; pi < animated.size(); ++pi) {
        const PropertyRef& pr = animated[pi];
        if (graphHidden_.count(pr.id)) continue;
        const AnimatedBase& anim = *pr.anim;
        const int comps = components(pr);
        for (int k = 0; k < comps; ++k) {
            const Rgba col = curveColor(static_cast<int>(pi), k, comps);
            std::vector<Pt> pts;
            for (float x = gx0; x <= gx1 + 2.0f; x += 2.0f) pts.emplace_back(x, graphY(valueAt(pr, xToTime(x), k)));
            d.polyline(pts.data(), static_cast<int>(pts.size()), col, false, 1.8f);
            for (int i = 0; i < anim.keyCount(); ++i) {
                const Pt kp(timeToX(anim.keyTime(i)), graphY(keyValue(pr, i, k)));
                const bool sel = ctx_.isKeySelected({layer->id, pr.id, anim.keyUid(i)});
                const float r = 4.5f;
                d.rectFilled({kp.x - r, kp.y - r}, {kp.x + r, kp.y + r}, sel ? theme::kAccent : theme::kBgDeep);
                d.rect({kp.x - r, kp.y - r}, {kp.x + r, kp.y + r}, sel ? theme::kAccentHover : col, 0, 1.5f);
            }
        }
    }
    // Handles with their lines to the key they belong to.
    for (const GraphHit& h : hits) {
        if (h.kind != GraphHit::Handle) continue;
        auto ref = findProperty(ctx_, layer->id, h.prop, -1);
        if (!ref) {
            for (const PropertyRef& pr : animated)
                if (pr.id == h.prop) ref = pr;
        }
        if (!ref) continue;
        const int i = ref->anim->findKeyByUid(h.uid);
        if (i < 0 || i + 1 >= ref->anim->keyCount()) continue;
        const int ki = h.which == 1 ? i : i + 1;
        const Pt key(timeToX(ref->anim->keyTime(ki)), graphY(keyValue(*ref, ki, h.comp)));
        d.line(key, Pt(h.pos), theme::ink(110));
        d.circleFilled(Pt(h.pos), 4.0f, theme::kAccent);
    }
    d.popClip();
}

void TimelinePanel::paintGraphEmpty(QPainter& p, QSize size) {
    if (graphEmpty_.first.isEmpty()) return;
    ui::Draw d(p);
    const float gx0 = trackX0(), gx1 = trackX0() + trackW();
    const QFont base = p.font();
    QFont bold = base;
    bold.setBold(true);
    p.setFont(bold);
    const float titleW = d.textWidth(graphEmpty_.first), lineH = d.textHeight();
    p.setFont(base);
    const float textW = std::min(d.textWidth(graphEmpty_.second), gx1 - gx0 - 80.0f);
    const float w = std::max(titleW, textW) + 40.0f, h = lineH * 2 + 70.0f;
    const float cx = (gx0 + gx1) * 0.5f, top = std::max(16.0f, (size.height() - h) * 0.4f);
    d.rectFilled({cx - w * 0.5f, top}, {cx + w * 0.5f, top + h}, theme::kBgRaised, theme::current().rounding + 2.0f);
    d.rect({cx - w * 0.5f, top}, {cx + w * 0.5f, top + h}, theme::kBorder, theme::current().rounding + 2.0f);
    ui::drawIcon(d, ui::Icon::Graph, {cx, top + 26}, 20, theme::kTextDim);
    p.setFont(bold);
    d.text({cx - titleW * 0.5f, top + 44}, theme::kText, graphEmpty_.first);
    p.setFont(base);
    const QRectF box(cx - textW * 0.5f, top + 46 + lineH, textW, lineH + 4);
    p.setPen(ui::qcolor(theme::kTextDim));
    p.drawText(box, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
               p.fontMetrics().elidedText(graphEmpty_.second, Qt::ElideRight, static_cast<int>(textW)));
}

std::vector<uint64_t> TimelinePanel::graphLayers() const {
    // Every clip of the timeline in view (the top level or the compound opened), top to bottom.
    std::vector<uint64_t> out;
    const Composition& comp = ctx_.comp();
    for (const TrackRow& row : comp.trackRows())
        for (int i = row.first; i < row.first + row.count; ++i) {
            const Layer& l = comp.layers[i];
            const uint64_t owner = comp.findLayer(l.parent) ? l.parent : 0;
            if (owner == ctx_.scope) out.push_back(l.id);
        }
    return out;
}

void TimelinePanel::graphPress(QMouseEvent* e) {
    Layer* layer = ctx_.selected();
    const QPointF m = e->position();
    if (!layer) { // the layer list
        const auto layers = graphLayers();
        const int row = static_cast<int>(std::floor((m.y() - 8) / kListRow)) - 1;
        if (m.x() < trackX0() && e->button() == Qt::LeftButton && row >= 0 && row < static_cast<int>(layers.size())) ctx_.select(layers[row]);
        rowsWidget()->update();
        return;
    }
    const bool right = e->button() == Qt::RightButton;
    // Property list: show / hide curves.
    if (m.x() < trackX0()) {
        const auto animated = animatedProps(*layer);
        const int row = static_cast<int>(std::floor((m.y() - 8) / kListRow));
        if (!right && row >= 0 && row < static_cast<int>(animated.size())) {
            const std::string id = animated[row].id;
            if (graphHidden_.count(id)) graphHidden_.erase(id);
            else graphHidden_.insert(id);
        }
        rowsWidget()->update();
        return;
    }
    const auto hits = graphHits();
    const GraphHit* hit = graphHitAt(hits, m);
    const bool additive = QGuiApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
    if (hit) {
        const KeySelection sel{layer->id, hit->prop, hit->uid};
        if (hit->kind == GraphHit::Key) {
            if (additive) {
                if (ctx_.isKeySelected(sel) && !right) std::erase(ctx_.selectedKeys, sel);
                else if (!ctx_.isKeySelected(sel)) ctx_.selectedKeys.push_back(sel);
            } else if (!ctx_.isKeySelected(sel)) {
                ctx_.selectedKeys = {sel};
            }
        }
        if (right) {
            if (hit->kind == GraphHit::Key) showKeyMenu(e->globalPosition().toPoint());
            return;
        }
        graphDrag_ = {};
        graphDrag_.kind = hit->kind;
        graphDrag_.prop = hit->prop;
        graphDrag_.comp = hit->comp;
        graphDrag_.uid = hit->uid;
        graphDrag_.which = hit->which;
        graphDrag_.startMouse = m;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, hit->prop.c_str()))
            if (const int i = p->anim->findKeyByUid(hit->uid); i >= 0) {
                graphDrag_.startTime = p->anim->keyTime(i);
                graphDrag_.startValue = keyValue(*p, i, hit->comp);
            }
    } else if (right) {
        showGraphMenu(e->globalPosition().toPoint(), m);
        return;
    } else if (!additive) {
        ctx_.clearKeySelection();
    }
    rowsWidget()->update();
}

// The curve (property and component) closest to a point of the graph, if one passes near it.
std::optional<std::pair<std::string, int>> TimelinePanel::curveNear(QPointF m) {
    Layer* layer = ctx_.selected();
    if (!layer) return std::nullopt;
    graphHits(); // the vertical range
    const double t = xToTime(static_cast<float>(m.x()));
    std::optional<std::pair<std::string, int>> best;
    float bestD = 12.0f;
    for (const PropertyRef& p : animatedProps(*layer)) {
        if (graphHidden_.count(p.id)) continue;
        for (int k = 0; k < components(p); ++k)
            if (const float d = std::fabs(graphY(valueAt(p, t, k)) - static_cast<float>(m.y())); d < bestD) bestD = d, best = {{p.id, k}};
    }
    return best;
}

void TimelinePanel::addGraphKeys(const std::vector<std::string>& propIds, double t) {
    Layer* layer = ctx_.selected();
    if (!layer) return;
    t = ctx_.comp().snapToFrame(std::clamp(t, layer->inPoint, layer->outPoint));
    auto props = layer->properties();
    std::vector<KeySelection> added;
    for (const std::string& id : propIds)
        if (PropertyRef* p = layer->findProperty(props, id.c_str())) {
            p->anim->addKeyAt(t); // keeps the curve's value there
            if (const int i = p->anim->findKey(t); i >= 0) added.push_back({layer->id, id, p->anim->keyUid(i)});
        }
    if (added.empty()) return;
    ctx_.selectedKeys = added;
    ctx_.touch();
    rowsWidget()->update();
}

void TimelinePanel::addKeysAtPlayhead() {
    // Every curve shown; with none animated yet, position (the usual start).
    Layer* layer = ctx_.selected();
    if (!layer) return;
    std::vector<std::string> ids;
    for (const PropertyRef& p : animatedProps(*layer))
        if (!graphHidden_.count(p.id)) ids.push_back(p.id);
    if (ids.empty()) ids.push_back("position");
    addGraphKeys(ids, ctx_.time);
}

void TimelinePanel::showGraphMenu(QPoint global, QPointF at) {
    Layer* layer = ctx_.selected();
    if (!layer) return;
    const double t = xToTime(static_cast<float>(at.x()));
    QMenu menu;
    char when[32];
    std::snprintf(when, sizeof when, "%.2f s", ctx_.comp().snapToFrame(std::max(0.0, t)));
    if (const auto curve = curveNear(at)) {
        const std::string id = curve->first;
        auto props = layer->properties();
        const PropertyRef* p = layer->findProperty(props, id.c_str());
        menu.addAction(ui::menuIcon(ui::Icon::Diamond, theme::kAccent), tr("Add Keyframe to %1 at %2").arg(p ? p->label : id.c_str(), when),
                       [this, id, t] { addGraphKeys({id}, t); });
    }
    menu.addAction(ui::menuIcon(ui::Icon::Diamond, theme::kTextDim), tr("Add Keyframes Here (shown curves, %1)").arg(when), [this, t] {
        std::vector<std::string> ids;
        if (Layer* l = ctx_.selected())
            for (const PropertyRef& p : animatedProps(*l))
                if (!graphHidden_.count(p.id)) ids.push_back(p.id);
        addGraphKeys(ids, t);
    });
    // Properties without keys yet: their first keyframe makes them appear here.
    QMenu* animate = menu.addMenu(tr("Animate Property"));
    for (const PropertyRef& p : layer->properties())
        if (components(p) > 0 && p.anim->keyCount() == 0) {
            const std::string id = p.id;
            animate->addAction(QString("%1  ·  %2").arg(p.group, p.label), [this, id, t] { addGraphKeys({id}, t); });
        }
    animate->setEnabled(!animate->isEmpty());
    if (!ctx_.selectedKeys.empty()) {
        menu.addSeparator();
        menu.addAction(tr("Keyframe Options..."), [this, global] { showKeyMenu(global); });
    }
    menu.exec(global);
    rowsWidget()->update();
}

void TimelinePanel::graphMove(QMouseEvent* e) {
    const QPointF m = e->position();
    Layer* layer = ctx_.selected();
    if (graphDrag_.kind == GraphHit::None || !layer || !(e->buttons() & Qt::LeftButton)) {
        // Hover: tooltips for keys and handles.
        if (layer && m.x() >= trackX0()) {
            const auto hits = graphHits();
            if (const GraphHit* hit = graphHitAt(hits, m)) {
                rowsWidget()->setCursor(Qt::PointingHandCursor);
                QToolTip::showText(e->globalPosition().toPoint(),
                                   hit->kind == GraphHit::Key ? QString::fromUtf8(hit->label.c_str())
                                                              : tr("Drag to shape the easing (influence and speed)"),
                                   rowsWidget());
            } else {
                rowsWidget()->setCursor(Qt::ArrowCursor);
                QToolTip::hideText();
            }
        }
        rowsWidget()->update();
        return;
    }
    if (std::hypot(m.x() - graphDrag_.startMouse.x(), m.y() - graphDrag_.startMouse.y()) > 3.0) graphDrag_.moved = true;
    if (!graphDrag_.moved) return; // waiting for the drag threshold
    auto props = layer->properties();
    PropertyRef* p = layer->findProperty(props, graphDrag_.prop.c_str());
    if (!p) return;
    AnimatedBase& anim = *p->anim;
    const int i = anim.findKeyByUid(graphDrag_.uid);
    const Composition& comp = ctx_.comp();
    if (i >= 0 && graphDrag_.kind == GraphHit::Key) {
        // Time stays between the neighbors, on whole frames; value follows the mouse.
        const double fd = comp.frameDuration();
        const double lo = i > 0 ? anim.keyTime(i - 1) + fd : 0.0;
        const double hi = i + 1 < anim.keyCount() ? anim.keyTime(i + 1) - fd : 1e9;
        // Relative to the grab point; Shift locks the time (value only).
        const double t = graphDrag_.startTime + (m.x() - graphDrag_.startMouse.x()) / pxPerSec_;
        if (!(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier))
            anim.setKeyTime(i, std::clamp(comp.snapToFrame(t), lo, std::max(lo, hi)));
        setKeyValue(*p, i, graphDrag_.comp,
                    graphDrag_.startValue + graphValue(static_cast<float>(m.y())) - graphValue(static_cast<float>(graphDrag_.startMouse.y())));
        ctx_.touch();
    } else if (i >= 0 && i + 1 < anim.keyCount() && graphDrag_.kind == GraphHit::Handle) {
        const double ta = anim.keyTime(i), tb = anim.keyTime(i + 1);
        const float va = keyValue(*p, i, graphDrag_.comp), vb = keyValue(*p, i + 1, graphDrag_.comp);
        BezierEase c = curveOf(anim, i);
        const float fx = static_cast<float>(std::clamp((xToTime(static_cast<float>(m.x())) - ta) / (tb - ta), 0.0, 1.0));
        const float fy = std::clamp((graphValue(static_cast<float>(m.y())) - va) / (vb - va), -1.0f, 2.0f);
        if (graphDrag_.which == 1) c.x1 = fx, c.y1 = fy;
        else c.x2 = fx, c.y2 = fy;
        setCurve(anim, i, c);
        ctx_.touch();
    }
    rowsWidget()->update();
}

void TimelinePanel::graphRelease(QMouseEvent*) {
    graphDrag_ = {};
    rowsWidget()->update();
}

} // namespace bemotion
