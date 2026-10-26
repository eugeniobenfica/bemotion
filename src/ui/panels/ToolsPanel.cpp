#include "ui/panels/ToolsPanel.h"

#include "app/Settings.h"
#include "ui/AppSignals.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
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
    // Replacing the scroll widget during its show/resize can leave the new tiles unpainted.
    if (std::max(2, (viewport()->width() - 16) / 80) != columns_)
        QTimer::singleShot(0, this, [this] {
            if (std::max(2, (viewport()->width() - 16) / 80) != columns_) rebuild();
        });
}

void ToolsPanel::rebuild() {
    columns_ = std::max(2, (viewport()->width() - 16) / 80);
    const int tileW = std::max(56, (viewport()->width() - 16 - 8 * (columns_ - 1)) / columns_);
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.78f), 50, 66));
    const auto& catalog = ui::toolCatalog();
    tiles_.assign(catalog.size(), nullptr);

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    for (bool modal : {true, false}) {
        col->addWidget(ui::sectionLabel(modal ? tr("Tools") : tr("Actions")));
        auto* grid = new QGridLayout;
        grid->setSpacing(8);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        int n = 0;
        for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
            const ui::ToolInfo& tool = catalog[i];
            if (tool.modal != modal) continue;
            auto* t = new ui::Tile(tool.icon, tr(tool.label));
            t->setTileSize(tile);
            t->setToolTip(QString::fromStdString(ui::toolTooltip(tool)) + tr("\n\nDrag it into the toolbar to keep it at hand."));
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
    auto* hint = new QLabel(tr("Drag tools into the toolbar; drag them out of it to remove them."));
    hint->setWordWrap(true);
    ui::setTone(hint, ui::Tone::TextFaint);
    col->addWidget(hint);
    auto* reset = new QPushButton(tr("Reset Toolbar"));
    connect(reset, &QPushButton::clicked, this, [] {
        settings().toolbar = ui::defaultToolbar();
        saveSettings();
        emit ui::AppSignals::get().toolbarChanged();
    });
    col->addWidget(reset, 0, Qt::AlignLeft);
    col->addStretch();
    setWidget(content);
}

ToolsPopover::ToolsPopover(ToolsPanel* panel, QWidget* host) : QFrame(host) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(6, 10, 6, 6);
    col->setSpacing(4);
    auto* head = new QHBoxLayout;
    head->setContentsMargins(8, 0, 0, 0);
    auto* title = new QLabel(tr("Tools"));
    QFont f = title->font();
    f.setWeight(QFont::DemiBold);
    title->setFont(f);
    head->addWidget(title, 1);
    auto* close = new ui::IconButton(ui::Icon::Minimize, tr("Close (Esc)"), 24);
    connect(close, &QAbstractButton::clicked, this, &QWidget::hide);
    head->addWidget(close);
    col->addLayout(head);
    col->addWidget(panel, 1);
    resize(420, 400);
    hide();
}

void ToolsPopover::popup(QPoint anchor) {
    QWidget* host = parentWidget();
    const int h = std::min(height(), host->height() - 16);
    resize(width(), h);
    QPoint at(anchor.x(), anchor.y() - h - 6);
    if (at.y() < 8) at.setY(anchor.y() + 30); // no room above: below the anchor
    at.setX(std::clamp(at.x(), 8, std::max(8, host->width() - width() - 8)));
    at.setY(std::clamp(at.y(), 8, std::max(8, host->height() - h - 8)));
    move(at);
    show();
    raise();
    qApp->installEventFilter(this);
}

void ToolsPopover::hideEvent(QHideEvent* e) {
    qApp->removeEventFilter(this);
    QFrame::hideEvent(e);
}

bool ToolsPopover::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        hide();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* w = qobject_cast<QWidget*>(watched);
        const QPoint global = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        if (w && w->window() == window() && !rect().contains(mapFromGlobal(global))) hide();
    }
    return QFrame::eventFilter(watched, event);
}

void ToolsPopover::paintEvent(QPaintEvent*) {
    QPainter p(this);
    ui::Draw d(p);
    const ui::Pt a(0.5f, 0.5f), b(width() - 0.5f, height() - 0.5f);
    d.rectFilled(a, b, theme::kBgPanel, 10.0f);
    d.rect(a, b, theme::kBorder, 10.0f);
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
