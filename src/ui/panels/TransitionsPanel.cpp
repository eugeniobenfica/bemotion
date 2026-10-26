#include "ui/panels/TransitionsPanel.h"

#include "core/Transitions.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace bemotion {

namespace {

ui::Icon categoryIcon(const std::string& c) {
    if (c == "Fade") return ui::Icon::Fade;
    if (c == "Slide") return ui::Icon::ArrowRight;
    if (c == "Scale") return ui::Icon::ZoomIn;
    if (c == "Rotate") return ui::Icon::Reset;
    if (c == "3D") return ui::Icon::Cube;
    return ui::Icon::Transition;
}

ui::Icon iconOf(const std::string& id) {
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

} // namespace

TransitionsPanel::TransitionsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

void TransitionsPanel::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    if (std::max(2, (viewport()->width() - 16) / 92) != columns_) rebuild();
}

void TransitionsPanel::rebuild() {
    columns_ = std::max(2, (viewport()->width() - 16) / 92);
    const int tileW = std::max(56, (viewport()->width() - 16 - 8 * (columns_ - 1)) / columns_);
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.78f), 50, 66));

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);

    buildApplied(col);

    // The categories, or the transitions of the open one.
    tiles_.clear();
    tileIds_.clear();
    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    int n = 0;
    auto* about = new QLabel(tr("Click one for the selected clips (in and out), or drag it onto the start or end of a clip, "
                             "or onto the cut between two clips. A dot marks what the clip has."));
    about->setWordWrap(true);
    ui::setTone(about, ui::Tone::TextFaint);
    if (category_.empty()) {
        col->addWidget(ui::sectionLabel(tr("Transitions")));
        col->addWidget(about);
        for (const char* c : transitionCategories()) {
            auto* t = new ui::Tile(categoryIcon(c), c);
            t->setTileSize(tile);
            tiles_.push_back(t);
            tileIds_.push_back(c);
            const std::string name = c;
            connect(t, &QAbstractButton::clicked, this, [this, name] {
                category_ = name;
                QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            });
            grid->addWidget(t, n / columns_, n % columns_);
            ++n;
        }
    } else {
        auto* head = new QHBoxLayout;
        auto* back = new ui::IconButton(ui::Icon::Back, tr("All transitions"), 24);
        connect(back, &QAbstractButton::clicked, this, [this] {
            category_.clear();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        });
        head->addWidget(back);
        head->addWidget(ui::sectionLabel(QString::fromStdString(category_)), 1);
        col->addLayout(head);
        col->addWidget(about);
        const auto& list = transitions();
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            if (category_ != list[i].category) continue;
            auto* t = new ui::Tile(iconOf(list[i].id), list[i].name);
            t->setTileSize(tile);
            t->setToolTip(tr("%1\n\nClick: the selected clips, in and out. Or drag it onto the start or end of a clip, "
                                  "or onto the cut between two clips. Drag its edge on the timeline to set its duration.")
                              .arg(tr(list[i].about)));
            const std::string id = list[i].id;
            connect(t, &QAbstractButton::clicked, this, [this, id] {
                bool any = false;
                for (Layer* l : ctx_.selectedLayers())
                    if (canTakeTransition(*l, id)) any = applyTransition(*l, id, TransitionEdge::Both, 0.5) || any;
                if (any) ctx_.touch();
            });
            t->setDragData([id] { return ui::makeTextMime(ui::kTransitionMime, id); });
            grid->addWidget(t, n / columns_, n % columns_);
            tiles_.push_back(t);
            tileIds_.push_back(id);
            ++n;
        }
    }
    col->addLayout(grid);
    col->addStretch();
    setWidget(content);
}

std::string TransitionsPanel::selectionKey() const {
    std::string key;
    for (const Layer* l : ctx_.selectedLayers())
        key += std::to_string(l->id) + ":" + l->name + ":" + l->transitionIn.id + (l->transitionIn.joined ? "+" : "") + ":" +
               l->transitionOut.id + (l->transitionOut.joined ? "+" : "") + ",";
    return key;
}

void TransitionsPanel::buildApplied(QVBoxLayout* col) {
    shownKey_ = selectionKey();
    durations_[0] = durations_[1] = nullptr;
    const auto selected = ctx_.selectedLayers();
    if (selected.size() > 1) {
        col->addWidget(new QLabel(tr("Transitions  ·  %1 clips").arg(selected.size())));
        col->addWidget(faint(tr("A transition clicked below goes to each of them, in and out.")));
        col->addSpacing(6);
        return;
    }
    if (selected.empty()) return;
    Layer* layer = selected.front();
    col->addWidget(new QLabel(tr("Transitions  ·  %1").arg(QString::fromStdString(layer->name))));
    if (!canTakeTransition(*layer)) {
        col->addWidget(faint(layer->type == LayerType::Camera ? tr("Cameras have no transitions: they show the others.")
                                                              : tr("Sound without a picture has no transitions: fade its volume instead.")));
        col->addSpacing(6);
        return;
    }
    auto* card = new ui::SettingsCard;
    const uint64_t id = layer->id;
    for (const TransitionEdge edge : {TransitionEdge::In, TransitionEdge::Out}) {
        const bool in = edge == TransitionEdge::In;
        const ClipTransition& clip = in ? layer->transitionIn : layer->transitionOut;
        if (!clip.active()) {
            card->addRow(in ? tr("In") : tr("Out"), in ? tr("None: click one below, or drag it onto the clip's start") : tr("None: click one below, or drag it onto the clip's end"), nullptr);
            continue;
        }
        const TransitionInfo* info = findTransition(clip.id);
        auto* controls = new QWidget;
        auto* row = new QHBoxLayout(controls);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* length = new ui::DragValue;
        length->setRange(0.05f, 60.0f);
        length->setSpeed(0.01f);
        length->setFormat("%.2f s");
        length->setValue(clip.duration);
        length->setFixedWidth(84);
        length->setToolTip(tr("How long it lasts (or drag its edge on the timeline)"));
        connect(length, &ui::DragValue::valueChanged, this, [this, id, edge](float v) {
            if (Layer* l = ctx_.comp().findLayer(id); l && resizeTransition(ctx_.comp(), *l, edge, v)) ctx_.touch();
        });
        durations_[in ? 0 : 1] = length;
        row->addWidget(length);
        auto* remove = new ui::IconButton(ui::Icon::Trash, tr("Remove this transition (its keyframes go too)"), 26);
        connect(remove, &QAbstractButton::clicked, this, [this, id, edge] {
            if (Layer* l = ctx_.comp().findLayer(id); l && removeTransition(ctx_.comp(), *l, edge)) ctx_.touch();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        });
        row->addWidget(remove);
        const QString name = info ? tr(info->name) : QString::fromStdString(clip.id);
        card->addRow(QString(in ? tr("In: %1") : tr("Out: %1")).arg(name), clip.joined ? (in ? tr("Shared with the clip before") : tr("Shared with the clip after")) : QString(), controls);
    }
    col->addWidget(card);
    col->addSpacing(8);
}

void TransitionsPanel::sync() {
    if (!isVisible()) return;
    if (selectionKey() != shownKey_) {
        rebuild();
        return;
    }
    // Durations changed elsewhere (dragged on the timeline, undone) show here.
    if (const auto selected = ctx_.selectedLayers(); selected.size() == 1) {
        const Layer* l = selected.front();
        if (durations_[0] && !durations_[0]->editing()) durations_[0]->setValue(l->transitionIn.duration);
        if (durations_[1] && !durations_[1]->editing()) durations_[1]->setValue(l->transitionOut.duration);
    }
    // Dimmed when the selection cannot take transitions (nothing selected: anything goes); a
    // dot on what the clips have (and on its category).
    const auto selected = ctx_.selectedLayers();
    bool canApply = selected.empty();
    for (Layer* l : selected) canApply = canApply || canTakeTransition(*l);
    for (size_t i = 0; i < tiles_.size(); ++i) {
        bool has = false;
        for (const Layer* l : selected)
            for (const ClipTransition* clip : {&l->transitionIn, &l->transitionOut}) {
                if (!clip->active()) continue;
                const TransitionInfo* info = findTransition(clip->id);
                has = has || clip->id == tileIds_[i] || (category_.empty() && info && tileIds_[i] == info->category);
            }
        tiles_[i]->setDimmed(!canApply);
        tiles_[i]->setMarked(has);
    }
}

} // namespace bemotion
