#include "ui/LayerMenus.h"

#include "core/Editing.h"
#include "core/Geometry.h"
#include "render/SceneRenderer.h"
#include "ui/EditorContext.h"
#include "ui/Keymap.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bemotion::ui {

namespace {

std::vector<Layer*> footage(EditorContext& ctx) {
    std::vector<Layer*> out;
    for (Layer* l : ctx.selectedLayers())
        if (hasTimeline(ctx.comp(), *l)) out.push_back(l);
    return out;
}

bool takesMatte(const Layer& l) { return l.type != LayerType::Camera; }

} // namespace

void fillTimeMenu(QMenu* menu, EditorContext& ctx, QWidget* parent) {
    menu->setToolTipsVisible(true);
    const std::vector<Layer*> clips = footage(ctx);
    const bool any = !clips.empty();
    QAction* speed = menu->addAction(menuIcon(Icon::Speed, theme::kTextDim), tr("Speed / Duration..."), [&ctx, parent] { showSpeedDialog(ctx, parent); });
    speed->setShortcut(sequenceOf(Action::SpeedDuration));
    speed->setEnabled(any);
    const bool reversed = any && std::all_of(clips.begin(), clips.end(), [](const Layer* l) { return l->media.reversed; });
    QAction* reverse = menu->addAction(tr("Reverse"), [&ctx, reversed] {
        for (Layer* l : footage(ctx)) setClipSpeed(ctx.comp(), l->id, l->media.speed, !reversed);
        ctx.touch();
    });
    reverse->setCheckable(true);
    reverse->setChecked(reversed);
    reverse->setEnabled(any);
    reverse->setToolTip(tr("Plays the clip backwards: the same footage, from its end"));
    const bool remapped = any && std::all_of(clips.begin(), clips.end(), [](const Layer* l) { return l->media.timeRemap; });
    QAction* remap = menu->addAction(tr("Time Remap"), [&ctx, remapped] { ctx.setSelectedTimeRemap(!remapped); });
    remap->setCheckable(true);
    remap->setChecked(remapped);
    remap->setEnabled(any);
    remap->setToolTip(tr("Keyframe which moment of the footage is shown: slow motion, speed ramps, holds"));
    QAction* freeze = menu->addAction(tr("Freeze Frame at Playhead"), [&ctx] { ctx.freezeSelectedAtPlayhead(); });
    freeze->setEnabled(any);
    freeze->setToolTip(tr("The whole clip holds the frame under the playhead"));
    if (!any) menu->addSection(tr("For video and audio clips"));
}

void fillBlendMenu(QMenu* menu, EditorContext& ctx) {
    std::vector<Layer*> layers;
    for (Layer* l : ctx.selectedLayers())
        if (takesMatte(*l)) layers.push_back(l);
    const int current = layers.empty() ? -1 : static_cast<int>(layers.front()->blend);
    for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i) {
        if (i == 1 || i == 3 || i == 5 || i == 10 || i == 12 || i == 16) menu->addSeparator(); // as After Effects groups them
        QAction* a = menu->addAction(tr(kBlendModeLabels[i]), [&ctx, i] {
            for (Layer* l : ctx.selectedLayers())
                if (takesMatte(*l)) l->blend = static_cast<BlendMode>(i);
            ctx.touch();
        });
        a->setCheckable(true);
        a->setChecked(i == current);
        a->setEnabled(!layers.empty());
    }
}

void fillMatteMenu(QMenu* menu, EditorContext& ctx) {
    menu->setToolTipsVisible(true);
    Layer* layer = ctx.selected();
    if (!layer || !takesMatte(*layer)) {
        menu->addAction(tr("Select a layer"))->setEnabled(false);
        return;
    }
    const uint64_t id = layer->id;
    Composition& comp = ctx.comp();
    // The matte: the layer it uses, or the one right above it (After Effects' default).
    uint64_t matte = layer->matteLayer;
    for (int i = comp.indexOf(id) - 1; i >= 0 && !matte; --i)
        if (comp.layers[i].parent == layer->parent && takesMatte(comp.layers[i])) matte = comp.layers[i].id;
    for (int i = 0; i < static_cast<int>(MatteMode::Count); ++i) {
        QAction* a = menu->addAction(tr(kMatteModeLabels[i]), [&ctx, id, i] {
            Layer* l = ctx.comp().findLayer(id);
            if (!l) return;
            uint64_t m = l->matteLayer;
            Composition& c = ctx.comp();
            for (int k = c.indexOf(id) - 1; k >= 0 && !m; --k)
                if (c.layers[k].parent == l->parent && takesMatte(c.layers[k])) m = c.layers[k].id;
            l->matte = i == 0 || !m ? MatteMode::None : static_cast<MatteMode>(i);
            l->matteLayer = l->matte == MatteMode::None ? 0 : m;
            ctx.touch();
        });
        a->setCheckable(true);
        a->setChecked(static_cast<int>(layer->matte) == i);
        if (i > 0 && !matte) a->setEnabled(false);
        if (i == 0) menu->addSeparator();
    }
    // Which layer is the matte: any layer beside it.
    QMenu* from = menu->addMenu(tr("Matte Layer"));
    for (const Layer& other : comp.layers) {
        if (other.id == id || other.parent != layer->parent || !takesMatte(other)) continue;
        const uint64_t oid = other.id;
        QAction* a = from->addAction(makeIcon(iconForLayer(other), theme::kTextDim, 16), QString::fromStdString(other.name), [&ctx, id, oid] {
            Layer* l = ctx.comp().findLayer(id);
            if (!l) return;
            l->matteLayer = oid;
            if (l->matte == MatteMode::None) l->matte = MatteMode::Alpha;
            ctx.touch();
        });
        a->setCheckable(true);
        a->setChecked(layer->matte != MatteMode::None && layer->matteLayer == oid);
    }
    if (from->isEmpty()) from->addAction(tr("No other layer beside it"))->setEnabled(false);
    menu->addSeparator();
    menu->addAction(tr("The matte layer is not drawn itself: it decides where this one shows"))->setEnabled(false);
}

void addFittedMask(EditorContext& ctx, bool ellipse) {
    bool added = false;
    for (Layer* l : ctx.selectedLayers()) {
        if (l->type == LayerType::Camera || (l->type == LayerType::Media && !l->media.hasVideo)) continue;
        Vec2 mn{-200, -200}, mx{200, 200};
        render::localBounds(*l, ctx.time, mn, mx, &ctx.comp());
        Mask m;
        m.name = tr("Mask %1").arg(l->masks.size() + 1).toStdString();
        m.path = ellipse ? ellipsePath(mn, mx) : rectanglePath(mn, mx);
        l->masks.push_back(std::move(m));
        added = true;
    }
    if (added) ctx.touch();
}

void fillMaskMenu(QMenu* menu, EditorContext& ctx) {
    menu->setToolTipsVisible(true);
    Layer* layer = ctx.selected();
    const bool can = layer && layer->type != LayerType::Camera && !(layer->type == LayerType::Media && !layer->media.hasVideo);
    menu->addAction(menuIcon(Icon::Rectangle, theme::kTextDim), tr("New Rectangle Mask"), [&ctx] { addFittedMask(ctx, false); })->setEnabled(can);
    menu->addAction(menuIcon(Icon::Ellipse, theme::kTextDim), tr("New Ellipse Mask"), [&ctx] { addFittedMask(ctx, true); })->setEnabled(can);
    QAction* draw = menu->addAction(menuIcon(Icon::Pen, theme::kTextDim), tr("Draw Mask with the Pen"), [&ctx] { ctx.tool = Tool::Pen; });
    draw->setEnabled(can && layer->type != LayerType::Shape);
    draw->setToolTip(tr("The Pen draws a mask on a selected layer that is not a shape"));
    if (can && !layer->masks.empty()) {
        menu->addSeparator();
        const uint64_t id = layer->id;
        menu->addAction(tr("Remove %1 Mask%2").arg(layer->masks.size()).arg(layer->masks.size() == 1 ? "" : "s"), [&ctx, id] {
            if (Layer* l = ctx.comp().findLayer(id)) l->masks.clear();
            ctx.touch();
        });
    }
}

void convertSelectedToPaths(EditorContext& ctx) {
    bool changed = false;
    for (Layer* l : ctx.selectedLayers()) {
        if (l->type != LayerType::Shape || l->shape.kind == ShapeKind::Path) continue;
        l->shape.path = shapeToBezier(l->shape, ctx.time);
        if (l->shape.kind == ShapeKind::Line) l->shape.path.closed = false;
        l->shape.kind = ShapeKind::Path;
        changed = true;
    }
    if (changed) ctx.touch();
}

void showSpeedDialog(EditorContext& ctx, QWidget* parent) {
    std::vector<Layer*> clips = footage(ctx);
    if (clips.empty()) return;
    const Layer& first = *clips.front();
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Speed / Duration"));
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    auto* speed = new DragValue;
    speed->setRange(1.0f, 10000.0f);
    speed->setSpeed(1.0f);
    speed->setFormat("%.0f%%");
    speed->setValue(static_cast<float>(first.media.speed * 100.0));
    form->addRow(tr("Speed"), speed);
    // The duration this speed gives the first clip (and the other way around).
    const double span = std::fabs(first.media.sourceTime(first.outPoint) - first.media.sourceTime(first.inPoint));
    auto* duration = new DragValue;
    duration->setRange(static_cast<float>(ctx.comp().frameDuration()), 100000.0f);
    duration->setSpeed(0.01f);
    duration->setFormat("%.2f s");
    duration->setValue(static_cast<float>(first.outPoint - first.inPoint));
    duration->setEnabled(clips.size() == 1 && span > 1e-6);
    form->addRow(tr("Duration"), duration);
    QObject::connect(speed, &DragValue::valueChanged, duration, [duration, span](float v) {
        if (span > 1e-6 && v > 0) duration->setValue(static_cast<float>(span / (v * 0.01)));
    });
    QObject::connect(duration, &DragValue::valueChanged, speed, [speed, span](float v) {
        if (span > 1e-6 && v > 0) speed->setValue(static_cast<float>(span / v * 100.0));
    });
    auto* reverse = new QCheckBox(tr("Play backwards"));
    reverse->setChecked(first.media.reversed);
    form->addRow(tr("Reverse"), reverse);
    auto* ripple = new QCheckBox(tr("Move the clips after it"));
    ripple->setToolTip(tr("Off: a longer clip stops at the next one on its track"));
    form->addRow(tr("Ripple"), ripple);
    auto* note = new QLabel(tr("Sound plays at the new speed too (its pitch changes)."));
    setTone(note, Tone::TextFaint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addSpacing(10);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    ctx.comp().duration += kMaxOverhang; // a slower clip may grow past the end (fitted again on commit)
    ctx.setSelectedSpeed(speed->value() * 0.01, reverse->isChecked(), ripple->isChecked());
    ctx.comp().duration -= kMaxOverhang;
    ctx.comp().fitDuration();
}

} // namespace bemotion::ui
