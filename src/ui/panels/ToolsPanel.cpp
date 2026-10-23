#include "ui/panels/ToolsPanel.h"

#include "app/Settings.h"
#include "ui/AppSignals.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace bemotion {

ToolsPanel::ToolsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

void ToolsPanel::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    if (std::max(2, (viewport()->width() - 16) / 92) != columns_) rebuild();
}

void ToolsPanel::rebuild() {
    columns_ = std::max(2, (viewport()->width() - 16) / 92);
    const int tileW = std::max(56, (viewport()->width() - 16 - 8 * (columns_ - 1)) / columns_);
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.8f), 56, 84));
    const auto& catalog = ui::toolCatalog();
    tiles_.assign(catalog.size(), nullptr);

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    for (bool modal : {true, false}) {
        col->addWidget(ui::sectionLabel(modal ? "Tools" : "Actions"));
        auto* grid = new QGridLayout;
        grid->setSpacing(8);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        int n = 0;
        for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
            const ui::ToolInfo& tool = catalog[i];
            if (tool.modal != modal) continue;
            auto* t = new ui::Tile(tool.icon, tool.label);
            t->setTileSize(tile);
            t->setToolTip(QString::fromStdString(ui::toolTooltip(tool)) + "\n\nDrag it into the timeline's toolbar to keep it at hand.");
            t->setDragData([i] { return ui::makeMime(ui::kToolMime, i); });
            connect(t, &QAbstractButton::clicked, this, [this, i] {
                const ui::ToolInfo& tool = ui::toolCatalog()[i];
                if (ui::toolEnabled(ctx_, tool)) ui::runTool(ctx_, tool);
            });
            grid->addWidget(t, n / columns_, n % columns_);
            tiles_[i] = t;
            ++n;
        }
        col->addLayout(grid);
        col->addSpacing(6);
    }
    auto* hint = new QLabel("Drag tools into the timeline's toolbar. Drag them along it to reorder, out of it (or right-click) "
                            "to remove. A dot marks the ones already there.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kTextFaint).name()));
    col->addWidget(hint);
    auto* reset = new QPushButton("Reset Toolbar");
    connect(reset, &QPushButton::clicked, this, [] {
        settings().toolbar = ui::defaultToolbar();
        saveSettings();
        emit ui::AppSignals::get().toolbarChanged();
    });
    col->addWidget(reset, 0, Qt::AlignLeft);
    col->addStretch();
    setWidget(content);
}

void ToolsPanel::sync() {
    if (!isVisible()) return;
    const auto& catalog = ui::toolCatalog();
    const auto& bar = settings().toolbar;
    for (size_t i = 0; i < catalog.size(); ++i) {
        ui::Tile* t = tiles_[i];
        if (!t) continue;
        t->setDimmed(!ui::toolEnabled(ctx_, catalog[i]));
        t->setHighlighted(ui::toolActive(ctx_, catalog[i]));
        t->setMarked(std::find(bar.begin(), bar.end(), catalog[i].id) != bar.end());
    }
}

} // namespace bemotion
