// The viewport's creative tools (ui/EditorContext.h Tool): Brush and Eraser paint on a layer,
// Puppet places and drags pins, Warp drags the points of a Mesh Warp, Track follows a detail of
// a video (ui/Tracking). Part of ViewportPanel, kept apart from its selection and path code.
// Their options live in Settings::tools (the Tool Settings panel).

#include "ui/panels/ViewportPanel.h"

#include "app/Settings.h"
#include "core/Deform.h"
#include "render/SceneRenderer.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/Language.h"
#include "ui/Theme.h"
#include "ui/Tracking.h"
#include "ui/Widgets.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <string>

namespace bemotion {

namespace {
constexpr float kPinRadius = 6.0f;
bool hasPicture(const Layer& l) { return l.type != LayerType::Camera && !(l.type == LayerType::Media && !l.media.hasVideo); }
Color brushColor() {
    const ToolSettings& t = settings().tools;
    return {t.brushColor[0], t.brushColor[1], t.brushColor[2], t.brushColor[3]};
}
float& toolSize(Tool tool) { return tool == Tool::Eraser ? settings().tools.eraserSize : settings().tools.brushSize; }
} // namespace

Layer* ViewportPanel::toolLayer() const {
    Layer* l = ctx_.selection.size() == 1 ? const_cast<EditorContext&>(ctx_).selected() : nullptr;
    return l && hasPicture(*l) && !l->locked ? l : nullptr;
}

void ViewportPanel::buildToolHud() {
    // The brush's color and size, beside the preview buttons while painting (the rest of its
    // options: the Tool Settings panel).
    brushColor_ = new ui::ColorButton(this);
    brushColor_->setToolTip(tr("Brush color"));
    brushColor_->setColor(brushColor());
    brushColor_->setFixedSize(28, 26);
    connect(brushColor_, &ui::ColorButton::colorChanged, this, [](Color c) {
        float* to = settings().tools.brushColor;
        to[0] = c.r, to[1] = c.g, to[2] = c.b, to[3] = c.a;
    });
    connect(brushColor_, &ui::ColorButton::editFinished, this, [] { saveSettings(); });
    brushSize_ = new ui::DragValue(this);
    brushSize_->setRange(1.0f, 500.0f);
    brushSize_->setSpeed(0.5f);
    brushSize_->setFormat("%.0f px");
    brushSize_->setFixedSize(76, 26);
    brushSize_->setToolTip(tr("Brush size (Ctrl+wheel over the picture)"));
    connect(brushSize_, &ui::DragValue::valueChanged, this, [this](float v) { toolSize(ctx_.tool) = v; });
    connect(brushSize_, &ui::DragValue::editFinished, this, [] { saveSettings(); });
    brushColor_->hide();
    brushSize_->hide();
}

void ViewportPanel::positionToolHud(int x, int y) {
    const bool painting = ctx_.tool == Tool::Brush || ctx_.tool == Tool::Eraser;
    brushColor_->setVisible(ctx_.tool == Tool::Brush && !orbit_);
    brushSize_->setVisible(painting && !orbit_);
    if (!painting) return;
    if (ctx_.tool == Tool::Brush) {
        if (brushColor_->color() != brushColor()) brushColor_->setColor(brushColor());
        brushColor_->move(x, y);
        x += 30;
    }
    brushSize_->move(x, y);
    if (brushSize_->value() != toolSize(ctx_.tool)) brushSize_->setValue(toolSize(ctx_.tool));
}

bool ViewportPanel::toolPress(Vec2 mouse, Qt::KeyboardModifiers mods) {
    if (orbit_) return false;
    const double t = ctx_.time;
    const ToolSettings& opts = settings().tools;
    switch (ctx_.tool) {
    case Tool::Brush:
    case Tool::Eraser: {
        Layer* l = toolLayer();
        if (!l && ctx_.tool == Tool::Eraser) return true; // nothing to erase
        if (!l) { // a new layer to paint on, the size of the frame
            const Vec2 center = ctx_.comp().center();
            Layer& made = ctx_.addShape(ShapeKind::Rectangle, center);
            made.name = tr("Paint").toStdString();
            made.shape.fillEnabled = false;
            made.shape.strokeEnabled = false;
            made.shape.roundness = AnimFloat{0.0f};
            made.shape.size = AnimVec2{Vec2{static_cast<float>(ctx_.comp().width), static_cast<float>(ctx_.comp().height)}};
            ctx_.select(made.id);
            l = &made;
        }
        PaintStroke s;
        s.erase = ctx_.tool == Tool::Eraser;
        s.color = brushColor();
        s.color.a *= std::clamp(opts.brushOpacity * 0.01f, 0.0f, 1.0f);
        s.width = s.erase ? opts.eraserSize : opts.brushSize;
        s.softness = s.erase ? opts.eraserSoftness : opts.brushSoftness;
        s.points.push_back(pathLocal(l->id, mouse));
        l->paint.push_back(std::move(s));
        dragLayer_ = l->id;
        drag_ = Drag::Paint;
        ctx_.touch();
        return true;
    }
    case Tool::Puppet: {
        Layer* l = toolLayer();
        if (!l) return false;
        Projective m;
        if (!pathMatrix(l->id, m)) return false;
        Vec2 lo, hi;
        if (!render::pictureBounds(*l, t, lo, hi, &ctx_.comp())) lo = hi = pathLocal(l->id, mouse);
        for (size_t i = 0; i < l->pins.size(); ++i) {
            // A starch pin shows where it rests on the bent picture; a pin, where it went.
            const Vec2 at = l->pins[i].starch ? deformPoint(*l, t, l->pins[i].rest, lo, hi) : l->pins[i].position.valueAt(t);
            if ((m.apply(at) - mouse).length() > kPinRadius + 3.0f) continue;
            if (mods & Qt::AltModifier) { // Alt+click: the pin goes
                l->pins.erase(l->pins.begin() + static_cast<std::ptrdiff_t>(i));
                ctx_.touch();
                return true;
            }
            if (l->pins[i].starch) return true; // starch pins stay where they were put
            dragLayer_ = l->id;
            toolIndex_ = static_cast<int>(i);
            drag_ = Drag::Pin;
            recordingPin_ = mods & Qt::ControlModifier;
            if (recordingPin_) {
                recordFrom_ = t;
                recordClock_.start();
            }
            return true;
        }
        // A new pin where the picture is under the mouse: found back on the picture at rest,
        // when it is already bent (a few steps of the inverse).
        const Vec2 local = pathLocal(l->id, mouse);
        Vec2 rest = local;
        for (int k = 0; k < 8 && deforms(*l, t); ++k) rest = rest + (local - deformPoint(*l, t, rest, lo, hi));
        if (!puppetOnMesh(*l, rest, lo, hi) || l->pins.size() >= 64) return true; // pins go on the picture only (its mesh)
        PuppetPin pin;
        pin.rest = rest;
        pin.position = AnimVec2{local};
        pin.starch = opts.pinStarch;
        if (pin.starch) {
            l->pins.push_back(std::move(pin));
            ctx_.touch();
            return true;
        }
        l->pins.push_back(std::move(pin));
        dragLayer_ = l->id;
        toolIndex_ = static_cast<int>(l->pins.size()) - 1;
        drag_ = Drag::Pin;
        ctx_.touch();
        return true;
    }
    case Tool::Warp: {
        Layer* l = toolLayer();
        if (!l) return false;
        const int i = warpPointAt(*l, mouse);
        if (i < 0) return false;
        if (!l->warp) {
            l->warp = true;
            ctx_.touch();
        }
        dragLayer_ = l->id;
        toolIndex_ = i;
        drag_ = Drag::WarpPoint;
        return true;
    }
    case Tool::Track: {
        // A box around the detail (dragged), or a click: a box of the Tool Settings' size.
        Layer* l = ctx_.selection.size() == 1 ? ctx_.selected() : nullptr;
        if (!l || l->type != LayerType::Media || !l->media.hasVideo) return false;
        dragLayer_ = l->id;
        trackBoxStart_ = trackBoxEnd_ = mouse;
        drag_ = Drag::TrackBox;
        return true;
    }
    default: return false;
    }
}

void ViewportPanel::toolDrag(Vec2 mouse) {
    Layer* l = ctx_.comp().findLayer(dragLayer_);
    if (!l) return;
    const Vec2 local = pathLocal(l->id, mouse);
    auto set = [&](const std::string& id, std::vector<float> value, bool key) {
        auto props = l->properties();
        PropertyRef* p = l->findProperty(props, id.c_str());
        if (!p) return;
        if (key && !p->anim->isAnimated()) p->anim->addKeyAt(ctx_.time); // keyed from the first drag
        setPropertyValue(*p, ctx_.time, value);
    };
    switch (drag_) {
    case Drag::Paint: {
        if (l->paint.empty()) return;
        std::vector<Vec2>& pts = l->paint.back().points;
        // A point every few screen pixels: smooth enough, and light.
        Projective m;
        if (!pts.empty() && pathMatrix(l->id, m) && (m.apply(pts.back()) - mouse).length() < 2.5f) return;
        pts.push_back(local);
        ctx_.touch();
        break;
    }
    case Drag::Pin:
        if (toolIndex_ < 0 || toolIndex_ >= static_cast<int>(l->pins.size())) return;
        if (recordingPin_) { // the motion recorded: the playhead follows the clock, a key at every move
            ctx_.setTime(recordFrom_ + static_cast<double>(recordClock_.elapsed()) / 1000.0);
            auto props = l->properties();
            if (PropertyRef* p = l->findProperty(props, ("pin" + std::to_string(toolIndex_)).c_str())) {
                if (!p->anim->isAnimated()) p->anim->addKeyAt(recordFrom_);
                p->anim->addKeyAt(ctx_.time);
                setPropertyValue(*p, ctx_.time, {local.x, local.y});
            }
        } else {
            set("pin" + std::to_string(toolIndex_), {local.x, local.y}, settings().tools.pinAutoKey);
        }
        ctx_.touch();
        break;
    case Drag::WarpPoint: {
        Vec2 lo, hi;
        if (toolIndex_ < 0 || !render::pictureBounds(*l, ctx_.time, lo, hi, &ctx_.comp())) return;
        const Vec2 rest = warpRest(toolIndex_, lo, hi);
        set("warp" + std::to_string(toolIndex_), {local.x - rest.x, local.y - rest.y}, settings().tools.pinAutoKey);
        ctx_.touch();
        break;
    }
    case Drag::TrackBox: trackBoxEnd_ = mouse; break;
    default: break;
    }
}

void ViewportPanel::toolRelease() {
    recordingPin_ = false;
    if (drag_ != Drag::TrackBox) return;
    const Layer* l = ctx_.comp().findLayer(dragLayer_);
    if (!l) return;
    // The box in the clip's own space: its center is followed, its size is the detail's.
    const Vec2 a = pathLocal(l->id, trackBoxStart_), b = pathLocal(l->id, trackBoxEnd_);
    const bool box = (trackBoxEnd_ - trackBoxStart_).length() > 6.0f;
    const Vec2 center = box ? (a + b) * 0.5f : b;
    const float radius = box ? std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)) * 0.5f : settings().tools.trackFeature * 0.5f;
    const uint64_t id = l->id;
    QMetaObject::invokeMethod(this, [this, id, center, radius] { ui::trackPoint(ctx_, id, center, radius, this); }, Qt::QueuedConnection);
}

Vec2 ViewportPanel::warpRest(int i, Vec2 lo, Vec2 hi) {
    const int x = i % kWarpSide, y = i / kWarpSide;
    return {lo.x + (hi.x - lo.x) * x / (kWarpSide - 1), lo.y + (hi.y - lo.y) * y / (kWarpSide - 1)};
}

int ViewportPanel::warpPointAt(const Layer& l, Vec2 mouse) const {
    Projective m;
    Vec2 lo, hi;
    if (!pathMatrix(l.id, m) || !render::pictureBounds(l, ctx_.time, lo, hi, &const_cast<EditorContext&>(ctx_).comp())) return -1;
    for (int i = 0; i < kWarpPoints; ++i) {
        const Vec2 rest = warpRest(i, lo, hi);
        const Vec2 at = rest + (l.warp ? l.warpPoints[static_cast<size_t>(i)].valueAt(ctx_.time) : Vec2{});
        if ((m.apply(at) - mouse).length() <= kPinRadius + 4.0f) return i;
    }
    return -1;
}

void ViewportPanel::drawToolOverlay(QPainter& p) {
    if (orbit_) return;
    ui::Draw d(p);
    const double t = ctx_.time;
    const Layer* l = toolLayer();
    auto pt = [](Vec2 v) { return ui::Pt(v.x, v.y); };
    // The brush: a ring as big as what it paints, where it would paint.
    if ((ctx_.tool == Tool::Brush || ctx_.tool == Tool::Eraser) && hovered_) {
        const float size = toolSize(ctx_.tool);
        float r = size * 0.5f * zoom_;
        if (l) {
            Projective m;
            if (pathMatrix(l->id, m)) {
                const Vec2 c = pathLocal(l->id, mouse_);
                r = (m.apply(c + Vec2{size * 0.5f, 0}) - m.apply(c)).length();
            }
        }
        d.circle(pt(mouse_), std::max(1.5f, r), theme::shade(160), 3.0f);
        d.circle(pt(mouse_), std::max(1.5f, r), ctx_.tool == Tool::Eraser ? theme::kText : theme::kAccent, 1.2f);
    }
    // The detail being boxed for tracking.
    if (drag_ == Drag::TrackBox) {
        const Vec2 a = trackBoxStart_, b = trackBoxEnd_;
        const bool box = (b - a).length() > 6.0f;
        const float half = settings().tools.trackFeature * 0.5f * zoom_;
        const ui::Pt lo = box ? ui::Pt(std::min(a.x, b.x), std::min(a.y, b.y)) : ui::Pt(b.x - half, b.y - half);
        const ui::Pt hi = box ? ui::Pt(std::max(a.x, b.x), std::max(a.y, b.y)) : ui::Pt(b.x + half, b.y + half);
        d.rect(lo, hi, theme::shade(160), 0.0f, 3.0f);
        d.rect(lo, hi, theme::kAccent, 0.0f, 1.5f);
        const ui::Pt c((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
        d.line(ui::Pt(c.x - 5, c.y), ui::Pt(c.x + 5, c.y), theme::kAccent, 1.5f);
        d.line(ui::Pt(c.x, c.y - 5), ui::Pt(c.x, c.y + 5), theme::kAccent, 1.5f);
    }
    if (!l) return;
    Projective m;
    if (!pathMatrix(l->id, m)) return;
    // The Puppet mesh (After Effects shows it while the tool is on): over what the picture shows.
    Vec2 plo, phi;
    const bool bounded = render::pictureBounds(*l, t, plo, phi, &ctx_.comp());
    if (ctx_.tool == Tool::Puppet && settings().tools.showMesh && bounded && !l->pins.empty()) {
        const ui::Rgba line = theme::withAlpha(theme::kGuide, 90);
        for (const auto& [a, b] : puppetMeshLines(*l, t, plo, phi)) d.line(pt(m.apply(a)), pt(m.apply(b)), line, 1.0f);
    }
    // Pins: shown with the Puppet tool (and whenever the layer has some while it is picked).
    // Starch pins: rings, where they rest on the bent picture.
    if (ctx_.tool == Tool::Puppet || (!l->pins.empty() && ctx_.tool == Tool::Select)) {
        for (const PuppetPin& pin : l->pins) {
            const Vec2 at = m.apply(pin.starch ? (bounded ? deformPoint(*l, t, pin.rest, plo, phi) : pin.rest) : pin.position.valueAt(t));
            if (pin.starch) {
                d.circle(pt(at), kPinRadius, theme::kBgDeep, 3.5f);
                d.circle(pt(at), kPinRadius, theme::kTextDim, 1.8f);
                continue;
            }
            d.circleFilled(pt(at), kPinRadius, theme::kAccent);
            d.circle(pt(at), kPinRadius, theme::kBgDeep, 1.5f);
            d.circleFilled(pt(at), 2.0f, theme::kBgDeep);
        }
    }
    // The warp grid: its lines and points, where they are now.
    if (ctx_.tool == Tool::Warp) {
        Vec2 lo, hi;
        if (!render::pictureBounds(*l, t, lo, hi, &ctx_.comp())) return;
        Vec2 at[kWarpPoints];
        for (int i = 0; i < kWarpPoints; ++i)
            at[i] = m.apply(warpRest(i, lo, hi) + (l->warp ? l->warpPoints[static_cast<size_t>(i)].valueAt(t) : Vec2{}));
        const ui::Rgba line = theme::withAlpha(theme::kGuide, 200);
        for (int y = 0; y < kWarpSide; ++y)
            for (int x = 0; x < kWarpSide; ++x) {
                const int i = y * kWarpSide + x;
                if (x + 1 < kWarpSide) d.line(pt(at[i]), pt(at[i + 1]), line, 1.0f);
                if (y + 1 < kWarpSide) d.line(pt(at[i]), pt(at[i + kWarpSide]), line, 1.0f);
            }
        for (const Vec2& a : at) {
            d.rectFilled(ui::Pt(a.x - 4, a.y - 4), ui::Pt(a.x + 4, a.y + 4), theme::kBgPanel);
            d.rect(ui::Pt(a.x - 4, a.y - 4), ui::Pt(a.x + 4, a.y + 4), theme::kAccent, 0.0f, 1.5f);
        }
    }
}

} // namespace bemotion
