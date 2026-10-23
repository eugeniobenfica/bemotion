#include "ui/panels/TransitionsPanel.h"

#include "core/Transitions.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace bemotion {

namespace {

ui::Icon iconOf(const std::string& id) {
    if (id == "fade") return ui::Icon::Transition;
    if (id == "slide_left") return ui::Icon::ChevronLeft;
    if (id == "slide_right") return ui::Icon::ChevronRight;
    if (id == "slide_up") return ui::Icon::ChevronUp;
    if (id == "slide_down") return ui::Icon::ChevronDown;
    if (id == "zoom") return ui::Icon::ZoomIn;
    if (id == "pop") return ui::Icon::Plus;
    if (id == "spin") return ui::Icon::Reset;
    return ui::Icon::Transition;
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
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.8f), 56, 84));

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);

    // Where and how long.
    col->addWidget(ui::sectionLabel("Apply To"));
    auto* edges = new QHBoxLayout;
    edges->setSpacing(4);
    edge_ = new QButtonGroup(content);
    const char* labels[] = {"Start", "End", "Both"};
    const char* tips[] = {"The clip comes in with it", "The clip goes away with it", "In at the start and out at the end"};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(labels[i]);
        b->setCheckable(true);
        b->setChecked(i == edgeChoice_);
        b->setToolTip(tips[i]);
        edge_->addButton(b, i);
        edges->addWidget(b);
    }
    connect(edge_, &QButtonGroup::idClicked, this, [this](int id) { edgeChoice_ = id; });
    col->addLayout(edges);
    auto* length = new QHBoxLayout;
    length->addWidget(new QLabel("Duration"));
    duration_ = new ui::DragValue;
    duration_->setRange(0.05f, 10.0f);
    duration_->setSpeed(0.01f);
    duration_->setFormat("%.2f s");
    duration_->setValue(seconds_);
    connect(duration_, &ui::DragValue::valueChanged, this, [this](float v) { seconds_ = v; });
    length->addWidget(duration_, 1);
    col->addLayout(length);
    col->addSpacing(4);

    col->addWidget(ui::sectionLabel("Transitions"));
    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    tiles_.clear();
    const auto& list = transitions();
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        auto* t = new ui::Tile(iconOf(list[i].id), list[i].name);
        t->setTileSize(tile);
        t->setToolTip(QString("%1\n\nClick to put it on the selected clips.").arg(list[i].about));
        connect(t, &QAbstractButton::clicked, this, [this, i] { apply(i); });
        grid->addWidget(t, i / columns_, i % columns_);
        tiles_.push_back(t);
    }
    col->addLayout(grid);
    auto* hint = new QLabel("Transitions are keyframes at the clip's start or end: move them in the timeline, "
                            "or ease them like any animation.");
    hint->setWordWrap(true);
    ui::setTone(hint, ui::Tone::TextFaint);
    col->addWidget(hint);
    col->addStretch();
    setWidget(content);
}

void TransitionsPanel::apply(int index) {
    const auto& list = transitions();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const TransitionEdge edge = edgeChoice_ == 1 ? TransitionEdge::Out : edgeChoice_ == 2 ? TransitionEdge::Both : TransitionEdge::In;
    bool any = false;
    for (Layer* l : ctx_.selectedLayers()) any = applyTransition(*l, list[index].id, edge, seconds_) || any;
    if (any) ctx_.touch();
}

void TransitionsPanel::sync() {
    if (!isVisible()) return;
    const bool any = !ctx_.selection.empty();
    for (ui::Tile* t : tiles_) t->setDimmed(!any);
}

} // namespace bemotion
