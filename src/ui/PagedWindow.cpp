#include "ui/PagedWindow.h"

#include "ui/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace bemotion::ui {

PagedWindow::PagedWindow(const QString& title, std::vector<Page> pages, QWidget* footer)
    : QWidget(nullptr, Qt::Window), pages_(std::move(pages)) {
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(800, 600);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(12, 12, 12, 12);
    row->setSpacing(12);
    auto* side = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setFixedWidth(170);
    list_->setIconSize(QSize(16, 16));
    list_->setFrameShape(QFrame::NoFrame);
    for (const Page& p : pages_) {
        auto* item = new QListWidgetItem(makeIcon(p.icon, theme::kTextDim, 32), p.label);
        item->setSizeHint(QSize(160, 36));
        list_->addItem(item);
    }
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row != current_) showPage(row);
    });
    side->addWidget(list_, 1);
    if (footer) side->addWidget(footer);
    row->addLayout(side);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kBorder).name()));
    row->addWidget(line);
    area_ = new QScrollArea;
    area_->setWidgetResizable(true);
    area_->setFrameShape(QFrame::NoFrame);
    area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    row->addWidget(area_, 1);
}

void PagedWindow::showPage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) return;
    const bool same = index == current_;
    const int scroll = same ? area_->verticalScrollBar()->value() : 0;
    current_ = index;
    {
        const QSignalBlocker b(list_);
        list_->setCurrentRow(index);
    }
    area_->setWidget(pages_[index].build());
    area_->verticalScrollBar()->setValue(scroll);
}

void PagedWindow::rebuild() { showPage(current_); }

void PagedWindow::present() {
    show();
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void PagedWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) close();
    else QWidget::keyPressEvent(e);
}

QWidget* pageWidget(QVBoxLayout*& col) {
    auto* w = new QWidget;
    col = new QVBoxLayout(w);
    col->setContentsMargins(4, 4, 12, 16);
    col->setSpacing(6);
    col->setAlignment(Qt::AlignTop);
    return w;
}

QLabel* pageTitle(const QString& text) {
    auto* l = new QLabel(text);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    return l;
}

QLabel* paragraph(const QString& text, Rgba color) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QString("color: %1;").arg(theme::q(color ? color : theme::kTextDim).name()));
    return l;
}

QLabel* hintLabel(const QString& text) { return paragraph(text, theme::kTextFaint); }

} // namespace bemotion::ui
