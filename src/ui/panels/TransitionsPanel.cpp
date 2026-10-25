#include "ui/panels/TransitionsPanel.h"

#include "core/Transitions.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
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

    // The categories, or the transitions of the open one.
    tiles_.clear();
    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    int n = 0;
    auto* about = new QLabel("Click one for the selected clips (in and out), or drag it onto the start or end of a clip, "
                             "or onto the cut between two clips.");
    about->setWordWrap(true);
    ui::setTone(about, ui::Tone::TextFaint);
    if (category_.empty()) {
        col->addWidget(ui::sectionLabel("Transitions"));
        col->addWidget(about);
        for (const char* c : transitionCategories()) {
            auto* t = new ui::Tile(categoryIcon(c), c);
            t->setTileSize(tile);
            tiles_.push_back(t);
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
        auto* back = new ui::IconButton(ui::Icon::Back, "All transitions", 24);
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
            t->setToolTip(QString("%1\n\nClick: the selected clips, in and out. Or drag it onto the start or end of a clip, "
                                  "or onto the cut between two clips. Drag its edge on the timeline to set its duration.")
                              .arg(list[i].about));
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
            ++n;
        }
    }
    col->addLayout(grid);
    col->addStretch();
    setWidget(content);
}

void TransitionsPanel::sync() {
    if (!isVisible()) return;
    // Dimmed when the selection cannot take transitions (nothing selected: anything goes).
    const auto selected = ctx_.selectedLayers();
    bool canApply = selected.empty();
    for (Layer* l : selected) canApply = canApply || canTakeTransition(*l);
    for (ui::Tile* t : tiles_) t->setDimmed(!canApply);
}

} // namespace bemotion
