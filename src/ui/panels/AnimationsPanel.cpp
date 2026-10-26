#include "ui/panels/AnimationsPanel.h"

#include "app/Platform.h"
#include "app/PresetLibrary.h"
#include "core/Presets.h"
#include "core/Transitions.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace bemotion {

namespace {

// A picture for a Quick Animation, from its name (imported ones too).
ui::Icon presetIcon(const std::string& name) {
    auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
    if (has("Type") || has("Still")) return ui::Icon::Text;
    if (has("Blur")) return ui::Icon::FxBlur;
    if (has("Wave")) return ui::Icon::Wave;
    if (has("Jitter")) return ui::Icon::Zigzag;
    if (has("Random")) return ui::Icon::Fade;
    if (has("Jelly")) return ui::Icon::Bounce;
    if (has("Swing")) return ui::Icon::Reset;
    if (has("Glide")) return ui::Icon::ArrowDown;
    if (has("Grow")) return ui::Icon::ZoomIn;
    if (has("Shrink") || has("Zoom Out")) return ui::Icon::ZoomOut;
    if (has("Letters Out")) return ui::Icon::Fade;
    if (has("Draw")) return ui::Icon::Line;
    if (has("Spin") || has("Wobble")) return ui::Icon::Reset;
    if (has("Pop")) return ui::Icon::Burst;
    if (has("Pulse")) return ui::Icon::Ripple;
    if (has("Heartbeat")) return ui::Icon::Heartbeat;
    if (has("Shake")) return ui::Icon::Zigzag;
    if (has("Float")) return ui::Icon::Wave;
    if (has("Bounce")) return ui::Icon::Bounce;
    if (has("Zoom")) return ui::Icon::ZoomIn;
    if (has("Drop") || has("Sink")) return ui::Icon::ArrowDown;
    if (has("Up") || has("Rise")) return ui::Icon::ArrowUp;
    if (has("Slide")) return ui::Icon::ArrowRight;
    if (has("Blink")) return ui::Icon::Eye;
    if (has("Fade")) return ui::Icon::Fade;
    return ui::Icon::Ease;
}

// ... and for a transition, from its id.
ui::Icon transitionIcon(const std::string& id) {
    if (id == "fade") return ui::Icon::Fade;
    if (id == "fade_up") return ui::Icon::ArrowUp;
    if (id == "slide_left" || id == "whip_left") return ui::Icon::ArrowLeft;
    if (id == "slide_right" || id == "whip_right") return ui::Icon::ArrowRight;
    if (id == "slide_up") return ui::Icon::ArrowUp;
    if (id == "slide_down" || id == "fade_down" || id == "drop") return ui::Icon::ArrowDown;
    if (id == "zoom") return ui::Icon::ZoomIn;
    if (id == "zoom_out") return ui::Icon::ZoomOut;
    if (id == "pop" || id == "elastic") return ui::Icon::Burst;
    if (id == "stretch" || id == "squash") return ui::Icon::Ungroup;
    if (id == "spin" || id == "swing" || id == "roll") return ui::Icon::Reset;
    if (id == "turn" || id == "tilt" || id == "fly") return ui::Icon::Cube;
    return ui::Icon::Transition;
}

QLabel* faint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

bool matches(const QString& text, const QString& filter) { return filter.isEmpty() || text.contains(filter, Qt::CaseInsensitive); }

} // namespace

AnimationsPanel::AnimationsPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search animations"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    auto* top = new QHBoxLayout;
    top->setContentsMargins(8, 0, 8, 0);
    top->addWidget(search_);
    col->addLayout(top);
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col->addWidget(scroll_, 1);
    rebuild();
}

void AnimationsPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (std::max(2, (scroll_->viewport()->width() - 16) / 92) != columns_) rebuild();
}

std::vector<Layer*> AnimationsPanel::shownLayers() const {
    auto& ctx = const_cast<EditorContext&>(ctx_);
    if (ctx.selection.empty()) // a transition or an animation picked on the timeline: its clip's
        for (const uint64_t id : {ctx.transitionLayer, ctx.animationLayer})
            if (Layer* l = id ? ctx.comp().findLayer(id) : nullptr) return {l};
    return ctx.selectedLayers();
}

std::string AnimationsPanel::appliedKey() const {
    std::string key = std::to_string(ctx_.transitionLayer) + (ctx_.transitionIncoming ? "i" : "o") + std::to_string(ctx_.animationLayer) +
                      "/" + std::to_string(ctx_.animationIndex) + ":";
    for (const Layer* l : shownLayers()) {
        key += std::to_string(l->id) + l->name + ":" + l->transitionIn.id + (l->transitionIn.joined ? "+" : "") + ":" + l->transitionOut.id +
               (l->transitionOut.joined ? "+" : "") + ":";
        for (const AppliedAnimation& a : l->animations) key += "|" + a.name + std::to_string(a.start);
        key += ",";
    }
    return key;
}

void AnimationsPanel::applyTransition(const std::string& id) {
    Composition& comp = ctx_.comp();
    std::vector<uint64_t> ids;
    for (Layer* l : ctx_.selectedLayers()) ids.push_back(l->id);
    auto selected = [&](const Layer* l) { return l && std::find(ids.begin(), ids.end(), l->id) != ids.end() && canTakeTransition(*l, id); };
    bool any = false;
    for (uint64_t layerId : ids) { // looked up each time: a cut transition reorders the stack
        Layer* l = comp.findLayer(layerId);
        if (!l || !canTakeTransition(*l, id)) continue;
        // Two selected clips that touch on a track: one transition across their cut (they
        // overlap, a cross dissolve), made from the first one; a free edge gets its own.
        const bool cutBefore = selected(transitionPartner(comp, *l, TransitionEdge::In)) || [&] {
            for (Layer& other : comp.layers)
                if (&other != l && other.track == l->track && other.parent == l->parent &&
                    std::fabs(other.outPoint - l->inPoint) < comp.frameDuration() * 0.5 && selected(&other))
                    return true;
            return false;
        }();
        const bool cutAfter = selected(nextOnTrack(comp, *l));
        if (!cutBefore) any = bemotion::applyTransition(*l, id, TransitionEdge::In, 0.5) || any;
        if (cutAfter) any = applyCutTransition(comp, *l, id, 1.0) || any;
        else any = bemotion::applyTransition(*l, id, TransitionEdge::Out, 0.5) || any;
    }
    if (any) ctx_.touch();
}

void AnimationsPanel::buildApplied(QVBoxLayout* col) {
    appliedKey_ = appliedKey();
    lengths_.clear();
    const auto shown = shownLayers();
    if (shown.size() > 1) {
        col->addWidget(new QLabel(tr("Animate  ·  %1 clips").arg(shown.size())));
        col->addWidget(faint(tr("What you click below goes to each of them.")));
        col->addSpacing(6);
        return;
    }
    if (shown.empty()) return;
    Layer* layer = shown.front();
    const uint64_t id = layer->id;
    col->addWidget(new QLabel(tr("Animate  ·  %1").arg(QString::fromStdString(layer->name))));
    auto* card = new ui::SettingsCard;
    // A row: its length (drag it, or its band's end on the timeline) and a way to take it off.
    auto addRow = [&](const QString& name, bool picked, const QString& about, float length, int which, std::function<void()> remove) {
        auto* controls = new QWidget;
        auto* row = new QHBoxLayout(controls);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* value = new ui::DragValue;
        value->setRange(0.05f, 60.0f);
        value->setSpeed(0.01f);
        value->setFormat("%.2f s");
        value->setValue(length);
        value->setFixedWidth(84);
        value->setToolTip(tr("How long it lasts (or drag the end of its band on the timeline)"));
        connect(value, &ui::DragValue::valueChanged, this, [this, id, which](float v) {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l) return;
            bool done = false;
            if (which == -1) done = resizeTransition(ctx_.comp(), *l, TransitionEdge::In, v);
            else if (which == -2) done = resizeTransition(ctx_.comp(), *l, TransitionEdge::Out, v);
            else if (which < static_cast<int>(l->animations.size()))
                done = retimeQuickAnimation(*l, static_cast<size_t>(which), l->animations[static_cast<size_t>(which)].start, v);
            if (done) ctx_.touch();
        });
        lengths_.emplace_back(value, which);
        row->addWidget(value);
        auto* trash = new ui::IconButton(ui::Icon::Trash, tr("Take it off (its keyframes go too)"), 26);
        connect(trash, &QAbstractButton::clicked, this, [this, remove] {
            remove();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        });
        row->addWidget(trash);
        card->addRow((picked ? QStringLiteral("▸ ") : QString()) + name, about, controls);
    };
    if (!canTakeTransition(*layer) && layer->animations.empty()) {
        card->addRow(tr("Nothing yet"), layer->type == LayerType::Camera ? tr("A camera takes Quick Animations (from the playhead), not ins and outs")
                                                                          : tr("Sound without a picture: fade its volume instead"),
                     nullptr);
    } else if (!layer->transitionIn.active() && !layer->transitionOut.active() && layer->animations.empty()) {
        card->addRow(tr("Nothing yet"), tr("Click one below, or drag it onto the clip"), nullptr);
    }
    auto transitionRow = [&](TransitionEdge edge) {
        const bool in = edge == TransitionEdge::In;
        const ClipTransition& clip = in ? layer->transitionIn : layer->transitionOut;
        if (!clip.active()) return;
        const TransitionInfo* info = findTransition(clip.id);
        const QString name = info ? tr(info->name) : QString::fromStdString(clip.id);
        QString about = in ? tr("In: at the clip's start") : tr("Out: at the clip's end");
        if (clip.joined) about = in ? tr("In: across the cut with the clip before") : tr("Out: across the cut with the clip after");
        addRow(name, ctx_.transitionLayer == id && ctx_.transitionIncoming == in, about, clip.duration, in ? -1 : -2, [this, id, edge] {
            if (Layer* l = ctx_.comp().findLayer(id); l && removeTransition(ctx_.comp(), *l, edge)) {
                if (ctx_.transitionLayer == id) ctx_.transitionLayer = 0;
                ctx_.touch();
            }
        });
    };
    // In time order: its in, the Quick Animations, its out.
    transitionRow(TransitionEdge::In);
    for (size_t i = 0; i < layer->animations.size(); ++i) {
        const AppliedAnimation& a = layer->animations[i];
        addRow(trs(a.name), ctx_.animationLayer == id && ctx_.animationIndex == static_cast<int>(i),
               tr("From %1 s (drag its band on the timeline)").arg(a.start - layer->inPoint, 0, 'f', 2), static_cast<float>(a.duration),
               static_cast<int>(i), [this, id, i] {
                   if (Layer* l = ctx_.comp().findLayer(id); l && removeQuickAnimation(*l, i)) {
                       if (ctx_.animationLayer == id) ctx_.animationLayer = 0;
                       ctx_.touch();
                   }
               });
    }
    transitionRow(TransitionEdge::Out);
    col->addWidget(card);
    col->addSpacing(6);
}

void AnimationsPanel::rebuild() {
    QWidget* viewport = scroll_->viewport();
    columns_ = std::max(2, (viewport->width() - 16) / 92);
    const int tileW = std::max(56, (viewport->width() - 16 - 8 * (columns_ - 1)) / columns_);
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.78f), 50, 66));
    presetCount_ = PresetLibrary::all().size();
    const QString filter = search_->text().trimmed();

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 4, 8, 8);
    col->setSpacing(6);
    tiles_.clear();
    buildApplied(col);
    col->addWidget(faint(tr("Click one for the selected clips, or drag it onto a clip. In and out go to the clip's start and end (or "
                            "across the cut between two clips); the others start at the playhead. A dot marks what the clip has.")));

    QGridLayout* grid = nullptr;
    int n = 0;
    auto section = [&](const QString& title) {
        grid = nullptr;
        n = 0;
        return [&, title] {
            if (grid) return;
            col->addSpacing(4);
            col->addWidget(ui::sectionLabel(title));
            grid = new QGridLayout;
            grid->setSpacing(8);
            grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            col->addLayout(grid);
        };
    };
    auto place = [&](ui::Tile* t) {
        t->setTileSize(tile);
        grid->addWidget(t, n / columns_, n % columns_);
        ++n;
    };

    // In and out: the transitions, by kind.
    {
        auto open = section(tr("In and out"));
        for (const TransitionInfo& info : transitions()) {
            const QString name = tr(info.name);
            if (!matches(name, filter) && !matches(info.name, filter) && !matches(tr(info.category), filter)) continue;
            open();
            auto* t = new ui::Tile(transitionIcon(info.id), name);
            t->setToolTip(tr("%1 · %2\n\nClick: the selected clips, in and out (across the cut between two selected clips that touch). "
                             "Or drag it onto the start or end of a clip, or onto the cut between two clips.")
                              .arg(tr(info.about), tr(info.category)));
            const std::string id = info.id;
            connect(t, &QAbstractButton::clicked, this, [this, id] { applyTransition(id); });
            t->setDragData([id] { return ui::makeTextMime(ui::kTransitionMime, id); });
            place(t);
            tiles_.push_back({t, id, true});
        }
    }
    // The Quick Animations: built-in categories first, then whatever imported ones use.
    std::vector<std::string> categories = {TR_NOOP("Entrance"), TR_NOOP("Emphasis"), TR_NOOP("Exit"), TR_NOOP("Letters")};
    for (const auto& lp : PresetLibrary::all())
        if (std::find(categories.begin(), categories.end(), lp.preset.category) == categories.end()) categories.push_back(lp.preset.category);
    for (const std::string& category : categories) {
        auto open = section(trs(category));
        for (const auto& lp : PresetLibrary::all()) {
            if (lp.preset.category != category) continue;
            const AnimationPreset preset = lp.preset;
            if (!matches(trs(preset.name), filter) && !matches(QString::fromStdString(preset.name), filter) && !matches(trs(category), filter))
                continue;
            open();
            auto* t = new ui::Tile(presetIcon(preset.name), trs(preset.name));
            t->setToolTip(tr("%1 · %2 s%3\n\nClick: the selected layers, from the playhead. Or drag it onto a clip.")
                              .arg(trs(preset.name))
                              .arg(preset.duration, 0, 'g', 2)
                              .arg(preset.appliesTo == "text" ? tr(" · text") : preset.appliesTo == "shape" ? tr(" · shapes") : QString()));
            connect(t, &QAbstractButton::clicked, this, [this, preset] {
                bool any = false;
                for (Layer* l : ctx_.selectedLayers())
                    if (presetAppliesTo(preset, *l)) applyQuickAnimation(preset, *l, ctx_.time), any = true;
                if (any) ctx_.touch();
            });
            t->setDragData([preset] { return ui::makeTextMime(ui::kAnimationMime, presetToJson(preset)); });
            if (!lp.file.empty()) { // the user's own: shown, deleted from here
                const std::filesystem::path file = lp.file;
                t->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(t, &QWidget::customContextMenuRequested, this, [this, t, file](QPoint p) {
                    QMenu menu;
                    menu.addAction(tr("Show File"), [file] { platform::revealInFileManager(file); });
                    menu.addAction(ui::menuIcon(ui::Icon::Trash, theme::kDanger), tr("Delete Preset"), [this, file] {
                        PresetLibrary::remove(file);
                        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
                    });
                    menu.exec(t->mapToGlobal(p));
                });
            }
            place(t);
            tiles_.push_back({t, preset.name, false});
        }
    }
    if (tiles_.empty() && !filter.isEmpty()) col->addWidget(faint(tr("Nothing matches your search.")));

    auto* row = new QHBoxLayout;
    auto* import = new QPushButton(tr("Import..."));
    import->setToolTip(tr("Add a Quick Animation from a .json preset file"));
    connect(import, &QPushButton::clicked, this, [this] {
        if (auto file = platform::openFileDialog(TR_NOOP("Import Quick Animation"), TR_NOOP("Quick Animation preset"), ".json")) {
            std::string err;
            messageText_ = PresetLibrary::importFile(*file, &err) ? "" : "Import failed: " + err;
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        }
    });
    auto* save = new QPushButton(tr("Save Selected..."));
    save->setToolTip(tr("Turn the selected layer's keyframes into a Quick Animation of your own"));
    connect(save, &QPushButton::clicked, this, [this] {
        Layer* l = ctx_.selected();
        if (!l) {
            messageText_ = "Select a layer with keyframes first.";
            message_->setText(QString::fromStdString(messageText_));
            message_->show();
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Quick Animation"), tr("Preset name"), QLineEdit::Normal,
                                                   QString::fromStdString(l->name + " animation"), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        AnimationPreset preset = presetFromLayer(*l, l->inPoint, l->outPoint, name.toStdString());
        std::string err;
        if (preset.tracks.empty()) messageText_ = "This layer has no keyframes to save.";
        else if (PresetLibrary::save(preset, &err)) messageText_ = "Saved \"" + name.toStdString() + "\" (Custom).";
        else messageText_ = err;
        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
    });
    row->addWidget(import);
    row->addWidget(save);
    col->addSpacing(4);
    col->addLayout(row);
    message_ = new QLabel(QString::fromStdString(messageText_));
    message_->setWordWrap(true);
    ui::setTone(message_, ui::Tone::TextDim);
    col->addWidget(message_);
    message_->setVisible(!messageText_.empty());
    col->addStretch();
    const int scrolled = scroll_->verticalScrollBar() ? scroll_->verticalScrollBar()->value() : 0;
    scroll_->setWidget(content);
    scroll_->verticalScrollBar()->setValue(scrolled);
    sync();
}

void AnimationsPanel::sync() {
    if (!isVisible()) return;
    if (PresetLibrary::all().size() != presetCount_ || appliedKey() != appliedKey_) { // imported or saved elsewhere; the clip's list
        rebuild();
        return;
    }
    const auto shown = shownLayers();
    // Lengths changed elsewhere (dragged on the timeline, undone) show here.
    if (shown.size() == 1)
        for (auto& [value, which] : lengths_) {
            if (value->editing()) continue;
            const Layer* l = shown.front();
            if (which == -1) value->setValue(l->transitionIn.duration);
            else if (which == -2) value->setValue(l->transitionOut.duration);
            else if (which < static_cast<int>(l->animations.size())) value->setValue(static_cast<float>(l->animations[static_cast<size_t>(which)].duration));
        }
    // Dimmed when no selected clip can take it (nothing selected: they can still be dragged); a
    // dot on what the clips have.
    for (const TileInfo& info : tiles_) {
        bool fits = shown.empty(), has = false;
        const LibraryPreset* lp = nullptr;
        if (!info.transition)
            for (const auto& p : PresetLibrary::all())
                if (p.preset.name == info.id) lp = &p;
        for (const Layer* l : shown) {
            if (info.transition) {
                fits = fits || canTakeTransition(*l, info.id);
                has = has || l->transitionIn.id == info.id || l->transitionOut.id == info.id;
            } else {
                fits = fits || (lp && presetAppliesTo(lp->preset, *l));
                for (const AppliedAnimation& a : l->animations) has = has || a.name == info.id;
            }
        }
        info.tile->setDimmed(!fits);
        info.tile->setMarked(has);
    }
}

} // namespace bemotion
