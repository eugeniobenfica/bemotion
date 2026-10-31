#include "ui/panels/PropertiesPanel.h"

// The Properties panel: a layer's media, sound, shape, time, blending and mask sections (the rest:
// PropertiesPanel.cpp, PropertiesData.cpp).

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Automation.h"
#include "app/PresetLibrary.h"
#include "core/Transitions.h"
#include "app/Settings.h"
#include "core/Data.h"
#include "core/Editing.h"
#include "core/Geometry.h"
#include "core/Ops.h"
#include "core/Presets.h"
#include "media/MediaManager.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/LayerMenus.h"
#include "ui/Modules.h"
#include "ui/PluginInstall.h"
#include "ui/PropertyRow.h"
#include "ui/Transcribe.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include "ui/panels/PropertiesInternal.h"

namespace bemotion {

using namespace properties_internal;

void PropertiesPanel::buildMediaSource(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Source"));
    const MediaItem* item = ctx_.comp().findMedia(layer.media.mediaId);
    if (!item) {
        auto* gone = new QLabel(tr("The media of this layer was removed."));
        ui::setTone(gone, ui::Tone::Danger);
        s->body()->addWidget(gone);
        col->addWidget(s);
        return;
    }
    const bool missing = media::MediaManager::get().fileMissing(*item);
    s->body()->addWidget(new ui::ElidedLabel(QString::fromStdString(item->name)));
    char info[128];
    if (item->kind == MediaKind::Image) std::snprintf(info, sizeof info, "%s  ·  %d×%d", tr("Image").toUtf8().constData(), item->width, item->height);
    else if (item->kind == MediaKind::Audio) std::snprintf(info, sizeof info, "%s  ·  %.2f s", tr("Audio").toUtf8().constData(), item->duration);
    else
        std::snprintf(info, sizeof info, "%s  ·  %d×%d  ·  %.2f s  ·  %.4g fps%s", tr("Video").toUtf8().constData(), item->width, item->height,
                      item->duration, item->fps, item->hasAudio ? tr("  ·  with audio").toUtf8().constData() : "");
    auto* about = dim(QString::fromUtf8(info));
    about->setWordWrap(true);
    s->body()->addWidget(about);
    // The path, shortened in its middle (the folder and the file name stay readable).
    auto* path = new ui::ElidedLabel(missing ? tr("Missing: %1").arg(QString::fromStdString(item->path)) : QString::fromStdString(item->path), Qt::ElideMiddle);
    path->setToolTip(QString::fromStdString(item->path));
    ui::setTone(path, missing ? ui::Tone::Danger : ui::Tone::TextFaint);
    s->body()->addWidget(path);
    if (missing) s->body()->addWidget(faint(tr("Relink it from the Media panel (right-click the item).")));
    auto* row = new ui::FlowLayout; // wraps when the panel is narrow
    const uint64_t id = layer.id;
    if (layer.media.hasVideo) {
        auto* fit = new QPushButton(tr("Fit to Composition"));
        fit->setToolTip(tr("Scale it to fill the composition, keeping its proportions"));
        connect(fit, &QPushButton::clicked, this, [this, id] {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l || l->media.size.x <= 0 || l->media.size.y <= 0) return;
            // Like After Effects' Fit to Comp (keeping the aspect ratio): centered, as large as fits.
            const Composition& c = ctx_.comp();
            const float f = std::min(c.width / l->media.size.x, c.height / l->media.size.y) * 100.0f;
            l->transform.scale.base = {f, f};
            l->transform.position.base = c.center();
            if (const Layer* parent = c.findLayer(l->parent))
                l->transform.position.base = c.worldMatrix(*parent, ctx_.time).inverse().apply(c.center());
            ctx_.touch();
        });
        row->addWidget(fit);
    }
    auto* reveal = new QPushButton(tr("Show in Folder"));
    reveal->setEnabled(!missing);
    const std::string file = item->path;
    connect(reveal, &QPushButton::clicked, this, [file] { platform::revealInFileManager(platform::fromUtf8(file)); });
    row->addWidget(reveal);
    s->body()->addLayout(row);
    col->addWidget(s);
    if (hasTimeline(ctx_.comp(), layer)) buildMediaTime(col, layer);
}

// The automatic sound fixes, on the selected clips with sound: Auto Level and Remove Silence
// (whose sample of the silence is picked on the timeline). Their options are kept in
// Settings::tools between sessions.
void PropertiesPanel::buildSoundFixes(QVBoxLayout* col, Layer&) {
    ToolSettings& t = settings().tools;
    auto number = [](float& value, float lo, float hi, float speed, const char* format) {
        auto* v = new ui::DragValue;
        v->setRange(lo, hi);
        v->setSpeed(speed);
        v->setFormat(format);
        v->setValue(value);
        connect(v, &ui::DragValue::valueChanged, v, [&value](float x) { value = x; });
        connect(v, &ui::DragValue::editFinished, v, [] { saveSettings(); });
        return v;
    };
    auto* level = new CollapsibleSection(tr("Auto Level"));
    level->addPlainRow(tr("Target"), number(t.levelTarget, -40, -6, 0.1f, "%.1f dB"));
    level->addPlainRow(tr("Even Out"), number(t.levelEvenOut, 0, 100, 0.25f, "%.0f%%"));
    auto* levelRun = new QPushButton(tr("Level Selected Clips"));
    connect(levelRun, &QPushButton::clicked, this, [this] { ui::autoLevelSelected(ctx_); });
    level->body()->addWidget(levelRun);
    col->addWidget(level);

    auto* silence = new CollapsibleSection(tr("Remove Silence"));
    silence->addPlainRow(tr("Quieter than"), number(t.silenceThreshold, -80, -6, 0.25f, "%.0f dB"));
    silence->addPlainRow(tr("For at least"), number(t.silenceMin, 0.1f, 10, 0.01f, "%.2f s"));
    silence->addPlainRow(tr("Keep around words"), number(t.silencePadding, 0, 2, 0.005f, "%.2f s"));
    auto* ripple = new ui::Switch;
    ripple->setChecked(t.silenceRipple);
    connect(ripple, &QAbstractButton::toggled, this, [](bool on) {
        settings().tools.silenceRipple = on;
        saveSettings();
    });
    silence->addPlainRow(tr("Close the gaps"), ripple);
    // The sample: a stretch of the timeline with only silence or background noise, dragged over
    // on the timeline; what is that quiet counts as silence (instead of the level above).
    auto* sampleRow = new QWidget;
    auto* sr = new QHBoxLayout(sampleRow);
    sr->setContentsMargins(0, 0, 0, 0);
    sr->setSpacing(4);
    auto* pick = new QPushButton(ui::makeIcon(ui::Icon::Pointer, theme::kTextDim, 16), tr("Pick a Sample"));
    pick->setCheckable(true);
    pick->setToolTip(tr("Then drag on the timeline over a pause: only silence or background noise"));
    auto* clearSample = new ui::IconButton(ui::Icon::Close, tr("No sample: use the level above"), 24);
    sr->addWidget(pick, 1);
    sr->addWidget(clearSample);
    silence->addPlainRow(tr("Sample"), sampleRow);
    auto* sampleInfo = faint({});
    sampleInfo->setWordWrap(true);
    silence->body()->addWidget(sampleInfo);
    connect(pick, &QPushButton::clicked, this, [this](bool on) {
        if (!on) {
            ctx_.samplePick = nullptr;
            return;
        }
        ctx_.samplePick = [](double from, double to) {
            settings().tools.silenceSampleFrom = from;
            settings().tools.silenceSampleTo = to;
        };
        emit panelRequested(QStringLiteral("Timeline"));
    });
    connect(clearSample, &QAbstractButton::clicked, this, [] { settings().tools.silenceSampleFrom = settings().tools.silenceSampleTo = -1.0; });
    refreshers_.push_back([this, sampleInfo, clearSample, pick] {
        const ToolSettings& o = settings().tools;
        const bool on = o.silenceSampleFrom >= 0.0 && o.silenceSampleTo > o.silenceSampleFrom;
        char span[64];
        std::snprintf(span, sizeof span, "%.2f s – %.2f s", o.silenceSampleFrom, o.silenceSampleTo);
        const QString text = on ? tr("Sample of the silence: %1 (the level above is not used)").arg(QString::fromUtf8(span))
                                : tr("No sample: stretches quieter than the level above count as silence.");
        if (sampleInfo->text() != text) sampleInfo->setText(text);
        clearSample->setEnabled(on);
        if (pick->isChecked() != ctx_.pickingSample()) pick->setChecked(ctx_.pickingSample());
    });
    auto* silenceRun = new QPushButton(tr("Remove from Selected Clips"));
    connect(silenceRun, &QPushButton::clicked, this, [this] { ui::removeSilence(ctx_, this); });
    silence->body()->addWidget(silenceRun);
    col->addWidget(silence);
}

// A shape's kind (a path keeps the outline it had), its path's points, the repeater.
void PropertiesPanel::buildShapeRows(CollapsibleSection* section, Layer& layer) {
    const uint64_t id = layer.id;
    auto* kind = newCombo();
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) kind->addItem(tr(shapeKindLabel(static_cast<ShapeKind>(i))));
    kind->setCurrentIndex(static_cast<int>(layer.shape.kind));
    connect(kind, &QComboBox::currentIndexChanged, this, [this, id](int i) {
        if (Layer* l = ctx_.comp().findLayer(id); l && i >= 0) {
            // A shape becoming a path keeps its outline, as points to edit.
            if (static_cast<ShapeKind>(i) == ShapeKind::Path && l->shape.kind != ShapeKind::Path) {
                l->shape.path = shapeToBezier(l->shape, ctx_.time);
                if (l->shape.kind == ShapeKind::Line) l->shape.path.closed = false;
            }
            l->shape.kind = static_cast<ShapeKind>(i);
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Type"), kind);
    if (layer.shape.kind == ShapeKind::Path) {
        auto* edit = new QPushButton(ui::makeIcon(ui::Icon::Pen, theme::kTextDim, 16), tr("Edit Points"));
        edit->setCheckable(true);
        edit->setToolTip(tr("The Pen (G): drag points and handles; click the outline to add a point, "
                         "Ctrl+click a point to remove it, Alt+click to make it a corner or smooth"));
        connect(edit, &QPushButton::clicked, this, [this](bool on) { ctx_.tool = on ? Tool::Pen : Tool::Select; });
        section->addPlainRow(tr("Path"), edit);
        refreshers_.push_back([this, edit] {
            const QSignalBlocker b(edit);
            edit->setChecked(ctx_.tool == Tool::Pen);
        });
        auto* closed = new ui::Switch;
        closed->setToolTip(tr("The last point joins the first"));
        connect(closed, &QAbstractButton::toggled, this, [this, id](bool v) {
            if (Layer* l = ctx_.comp().findLayer(id)) {
                l->shape.path.closed = v;
                ctx_.touch();
            }
        });
        section->addPlainRow(tr("Closed"), closed);
        refreshers_.push_back([this, id, closed] {
            if (const Layer* l = ctx_.comp().findLayer(id)) {
                const QSignalBlocker b(closed);
                closed->setChecked(l->shape.path.closed);
            }
        });
    }
    auto* repeater = new ui::Switch;
    repeater->setToolTip(tr("Copies of the shape, each moved, turned and scaled from the last (After Effects' Repeater)"));
    connect(repeater, &QAbstractButton::toggled, this, [this](bool v) {
        for (Layer* l : ctx_.selectedLayers())
            if (l->type == LayerType::Shape) l->shape.repeater = v;
        ctx_.touch(); // its rows come and go (the panel rebuilds)
    });
    section->addPlainRow(tr("Repeater"), repeater);
    refreshers_.push_back([this, id, repeater] {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            const QSignalBlocker b(repeater);
            repeater->setChecked(l->shape.repeater);
        }
    });
}

// A clip's time: its speed, direction, time remap, a frozen frame.
void PropertiesPanel::buildMediaTime(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Speed"));
    const uint64_t id = layer.id;
    auto* speed = new ui::DragValue;
    speed->setRange(1.0f, 10000.0f);
    speed->setSpeed(1.0f);
    speed->setFormat("%.0f%%");
    speed->setToolTip(tr("100%: as recorded; 200%: twice as fast (half as long). The first frame stays where it is"));
    connect(speed, &ui::DragValue::valueChanged, this, [this, id](float v) {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            ctx_.comp().duration += kMaxOverhang; // room to grow (fitted again when the edit is done)
            setClipSpeed(ctx_.comp(), id, v * 0.01, l->media.reversed);
            ctx_.comp().duration -= kMaxOverhang;
            ctx_.touch();
        }
    });
    s->addPlainRow(tr("Speed"), speed);
    auto* reverse = new ui::Switch;
    reverse->setToolTip(tr("Plays the same footage backwards"));
    connect(reverse, &QAbstractButton::toggled, this, [this, id](bool v) {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            setClipSpeed(ctx_.comp(), id, l->media.speed, v);
            ctx_.touch();
        }
    });
    s->addPlainRow(tr("Reverse"), reverse);
    auto* remap = new ui::Switch;
    remap->setToolTip(tr("Keyframe which moment of the footage shows at each moment (Time Remap): slow motion, ramps, holds"));
    connect(remap, &QAbstractButton::toggled, this, [this, id](bool v) {
        if (setTimeRemap(ctx_.comp(), id, v)) ctx_.touch(); // its row comes and goes (the panel rebuilds)
    });
    s->addPlainRow(tr("Time Remap"), remap);
    auto* buttons = new ui::FlowLayout; // wraps when the panel is narrow
    auto* freeze = new QPushButton(tr("Freeze Frame"));
    freeze->setToolTip(tr("The whole clip holds the frame under the playhead"));
    connect(freeze, &QPushButton::clicked, this, [this, id] {
        if (freezeFrame(ctx_.comp(), id, ctx_.time)) ctx_.touch();
    });
    buttons->addWidget(freeze);
    auto* more = new QPushButton(tr("Speed / Duration..."));
    connect(more, &QPushButton::clicked, this, [this, id] {
        ctx_.select(id);
        ui::showSpeedDialog(ctx_, this);
    });
    buttons->addWidget(more);
    s->body()->addLayout(buttons);
    col->addWidget(s);
    refreshers_.push_back([this, id, speed, reverse, remap] {
        const Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        speed->setValue(static_cast<float>(l->media.speed * 100.0));
        speed->setEnabled(!l->media.timeRemap);
        const QSignalBlocker a(reverse), b(remap);
        reverse->setChecked(l->media.reversed);
        reverse->setEnabled(!l->media.timeRemap);
        remap->setChecked(l->media.timeRemap);
    });
}

// How the layer mixes with what is under it, and its track matte.
void PropertiesPanel::buildBlending(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Blending"));
    const uint64_t id = layer.id;
    auto* mode = ui::blendModeCombo(ctx_);
    ui::showBlendMode(mode, layer.blend);
    mode->setToolTip(tr("How its colors mix with the layers under it (the one under the mouse shows on the picture)"));
    s->addPlainRow(tr("Mode"), mode);
    // The track matte: a mode, and the layer beside it whose picture decides.
    std::vector<uint64_t> candidates;
    for (int i = 0; i < static_cast<int>(ctx_.comp().layers.size()); ++i) {
        const Layer& other = ctx_.comp().layers[i];
        if (other.id != id && other.parent == layer.parent && other.type != LayerType::Camera) candidates.push_back(other.id);
    }
    auto* matte = choiceCombo(kMatteModeLabels, static_cast<int>(layer.matte), [this, id, candidates](int i) {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        if (i == 0 || candidates.empty()) {
            l->matte = MatteMode::None;
            l->matteLayer = 0;
        } else {
            if (!l->matteLayer) { // the layer right above it, as After Effects does
                const int at = ctx_.comp().indexOf(id);
                uint64_t above = candidates.front();
                for (uint64_t c : candidates)
                    if (ctx_.comp().indexOf(c) < at) above = c;
                l->matteLayer = above;
            }
            l->matte = static_cast<MatteMode>(i);
        }
        ctx_.touch();
    });
    matte->setToolTip(tr("Shows this layer only where another one is opaque (Alpha) or bright (Luma); that one is then not drawn"));
    matte->setEnabled(!candidates.empty());
    s->addPlainRow(tr("Track Matte"), matte);
    auto* source = newCombo();
    for (uint64_t c : candidates)
        if (const Layer* other = ctx_.comp().findLayer(c))
            source->addItem(ui::makeIcon(ui::iconForLayer(*other), theme::kTextDim, 16), QString::fromStdString(other->name), QVariant::fromValue<qulonglong>(c));
    connect(source, &QComboBox::activated, this, [this, id, source](int i) {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l || i < 0) return;
        l->matteLayer = source->itemData(i).toULongLong();
        if (l->matte == MatteMode::None) l->matte = MatteMode::Alpha;
        ctx_.touch();
    });
    s->addPlainRow(tr("Matte Layer"), source);
    // Masks: added here (fitted to it, or drawn with the Pen); each gets a section above.
    auto* masks = new QPushButton(ui::makeIcon(ui::Icon::Mask, theme::kTextDim, 16), tr("Add Mask"));
    masks->setToolTip(tr("Cut the layer to a shape: a rectangle or an ellipse over it, or one drawn with the Pen (G)"));
    connect(masks, &QPushButton::clicked, this, [this, masks] {
        QMenu menu;
        ui::fillMaskMenu(&menu, ctx_);
        menu.exec(masks->mapToGlobal(QPoint(0, masks->height())));
    });
    s->addPlainRow(tr("Masks"), masks);
    col->addWidget(s);
    refreshers_.push_back([this, id, mode, matte, source] {
        const Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        const QSignalBlocker a(mode), b(matte), c(source);
        if (!mode->view()->isVisible()) ui::showBlendMode(mode, l->blend);
        if (!matte->view()->isVisible()) matte->setCurrentIndex(static_cast<int>(l->matte));
        source->setEnabled(l->matte != MatteMode::None);
        if (!source->view()->isVisible()) source->setCurrentIndex(l->matte == MatteMode::None ? -1 : source->findData(QVariant::fromValue<qulonglong>(l->matteLayer)));
    });
}

// A mask's settings above its feather, opacity and expansion: how it combines, inverted, on.
void PropertiesPanel::buildMaskRows(CollapsibleSection* section, Layer& layer, int index) {
    const uint64_t id = layer.id;
    auto mask = [this, id, index]() -> Mask* {
        Layer* l = ctx_.comp().findLayer(id);
        return l && index < static_cast<int>(l->masks.size()) ? &l->masks[index] : nullptr;
    };
    auto* mode = choiceCombo(kMaskModeLabels, static_cast<int>(layer.masks[index].mode), [this, mask](int i) {
        if (Mask* m = mask()) {
            m->mode = static_cast<MaskMode>(i);
            ctx_.touch();
        }
    });
    mode->setToolTip(tr("Add: shows its inside; Subtract: takes it away; Intersect: only where it overlaps the masks above; Difference: where only one is"));
    section->addPlainRow(tr("Mode"), mode);
    auto* inverted = new ui::Switch;
    connect(inverted, &QAbstractButton::toggled, this, [this, mask](bool v) {
        if (Mask* m = mask()) {
            m->inverted = v;
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Inverted"), inverted);
    auto* enabled = new ui::Switch;
    enabled->setToolTip(tr("Turn the mask off without losing it"));
    connect(enabled, &QAbstractButton::toggled, this, [this, mask](bool v) {
        if (Mask* m = mask()) {
            m->enabled = v;
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Enabled"), enabled);
    // Mask Path keyframes (After Effects' stopwatch on Mask Path): a key of the outline at the
    // playhead; with keys, editing the points at another moment makes one there.
    auto* pathKeys = new QWidget;
    auto* keyRow = new QHBoxLayout(pathKeys);
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->setSpacing(4);
    auto* prevKey = new ui::IconButton(ui::Icon::KeyPrev, tr("Previous path keyframe"), 22);
    auto* key = new ui::KeyframeButton;
    key->setToolTip(tr("A keyframe of the outline at the playhead: the mask moves from one to the next"));
    auto* nextKey = new ui::IconButton(ui::Icon::KeyNext, tr("Next path keyframe"), 22);
    auto* keyInfo = new QLabel;
    ui::setTone(keyInfo, ui::Tone::TextDim);
    keyRow->addWidget(prevKey);
    keyRow->addWidget(key);
    keyRow->addWidget(nextKey);
    keyRow->addWidget(keyInfo, 1);
    section->addPlainRow(tr("Path"), pathKeys);
    connect(key, &QAbstractButton::clicked, this, [this, mask] {
        Mask* m = mask();
        if (!m) return;
        if (const int k = m->pathKeyAt(ctx_.time); k >= 0) {
            m->path = m->pathAt(ctx_.time);
            m->pathKeys.erase(m->pathKeys.begin() + k);
        } else {
            m->setPathKey(ctx_.time, m->pathAt(ctx_.time));
        }
        ctx_.touch();
    });
    auto jump = [this, mask](int direction) {
        const Mask* m = mask();
        if (!m) return;
        double best = direction > 0 ? DBL_MAX : -DBL_MAX;
        for (const Mask::PathKey& k : m->pathKeys) {
            if (direction > 0 && k.time > ctx_.time + kTimeEpsilon) best = std::min(best, k.time);
            if (direction < 0 && k.time < ctx_.time - kTimeEpsilon) best = std::max(best, k.time);
        }
        if (best != DBL_MAX && best != -DBL_MAX) ctx_.setTime(best);
    };
    connect(prevKey, &QAbstractButton::clicked, this, [jump] { jump(-1); });
    connect(nextKey, &QAbstractButton::clicked, this, [jump] { jump(1); });
    refreshers_.push_back([this, mask, key, prevKey, nextKey, keyInfo] {
        const Mask* m = mask();
        if (!m) return;
        key->setState(m->pathKeyAt(ctx_.time) >= 0, !m->pathKeys.empty());
        prevKey->setEnabled(!m->pathKeys.empty() && m->pathKeys.front().time < ctx_.time - kTimeEpsilon);
        nextKey->setEnabled(!m->pathKeys.empty() && m->pathKeys.back().time > ctx_.time + kTimeEpsilon);
        const QString info = m->pathKeys.empty() ? tr("Not animated") : tr("%1 keyframes").arg(m->pathKeys.size());
        if (keyInfo->text() != info) keyInfo->setText(info);
    });
    // Its points (the Pen), its name, removing it: small buttons on the right.
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* edit = new ui::IconButton(ui::Icon::Pen, tr("Edit its points with the Pen (G): drag points and handles in the viewport"), 26);
    connect(edit, &QAbstractButton::clicked, this, [this] { ctx_.tool = Tool::Pen; });
    row->addWidget(edit);
    auto* rename = new ui::IconButton(ui::Icon::Edit, tr("Rename this mask"), 26);
    connect(rename, &QAbstractButton::clicked, this, [this, mask] {
        Mask* m = mask();
        if (!m) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Mask"), tr("Name:"), QLineEdit::Normal, QString::fromStdString(m->name), &ok);
        if (ok && !name.trimmed().isEmpty() && (m = mask())) {
            m->name = name.trimmed().toStdString();
            ctx_.touch();
        }
    });
    row->addWidget(rename);
    auto* duplicate = new ui::IconButton(ui::Icon::Duplicate, tr("Duplicate this mask"), 26);
    connect(duplicate, &QAbstractButton::clicked, this, [this, id, index] {
        const OpsResult r = applyOps(ctx_.project, {{"op", "mask.duplicate"}, {"layer", id}, {"index", index}});
        if (r.ok) ctx_.touch();
    });
    row->addWidget(duplicate);
    auto* remove = new ui::IconButton(ui::Icon::Trash, tr("Delete this mask"), 26);
    connect(remove, &QAbstractButton::clicked, this, [this, id, index] {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l || index >= static_cast<int>(l->masks.size())) return;
        l->masks.erase(l->masks.begin() + index);
        ctx_.touch();
    });
    row->addWidget(remove);
    section->body()->addLayout(row);
    refreshers_.push_back([mask, mode, inverted, enabled] {
        const Mask* m = mask();
        if (!m) return;
        const QSignalBlocker a(mode), b(inverted), c(enabled);
        if (!mode->view()->isVisible()) mode->setCurrentIndex(static_cast<int>(m->mode));
        inverted->setChecked(m->inverted);
        enabled->setChecked(m->enabled);
    });
}

} // namespace bemotion
