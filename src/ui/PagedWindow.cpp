#include "ui/PagedWindow.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
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
    ui::setTone(line, ui::Tone::Border);
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
    // The new page is laid out a moment later: until then the scroll range is too short.
    if (scroll > 0) QTimer::singleShot(0, this, [this, scroll, index] {
            if (current_ == index) area_->verticalScrollBar()->setValue(scroll);
        });
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
    col->setContentsMargins(8, 4, 16, 20);
    col->setSpacing(6);
    col->setAlignment(Qt::AlignTop);
    return w;
}

// --- SettingsCard ----------------------------------------------------------------------------

SettingsCard::SettingsCard() {
    col_ = new QVBoxLayout(this);
    col_->setContentsMargins(0, 0, 0, 0);
    col_->setSpacing(0);
}

QWidget* SettingsCard::addRow(const QString& title, const QString& description, QWidget* control, bool below) {
    auto* row = new QWidget;
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* t = new QLabel(title);
    t->setWordWrap(true);
    text->addWidget(t);
    if (!description.isEmpty()) {
        auto* d = new QLabel(description);
        d->setWordWrap(true);
        QFont f = d->font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        d->setFont(f);
        setTone(d, Tone::TextFaint);
        text->addWidget(d);
    }
    if (below) {
        auto* col = new QVBoxLayout(row);
        col->setContentsMargins(16, 12, 16, 12);
        col->setSpacing(8);
        col->addLayout(text);
        if (control) col->addWidget(control);
    } else {
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(16, 10, 16, 10);
        h->setSpacing(16);
        h->addLayout(text, 1);
        if (control) h->addWidget(control, 0, Qt::AlignVCenter | Qt::AlignRight);
    }
    row->setMinimumHeight(48);
    rows_.push_back(row);
    col_->addWidget(row);
    return row;
}

QWidget* SettingsCard::addContent(QWidget* content) {
    auto* row = new QWidget;
    auto* h = new QVBoxLayout(row);
    h->setContentsMargins(16, 12, 16, 12);
    h->addWidget(content);
    rows_.push_back(row);
    col_->addWidget(row);
    return row;
}

QWidget* SettingsCard::addLayout(QLayout* content) {
    auto* row = new QWidget;
    auto* h = new QVBoxLayout(row);
    h->setContentsMargins(16, 12, 16, 12);
    h->addLayout(content);
    rows_.push_back(row);
    col_->addWidget(row);
    return row;
}

void SettingsCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const float r = theme::current().rounding + 3.0f;
    d.rectFilled(Pt(0, 0), Pt(width(), height()), theme::kBgRaised, r);
    // Thin lines between the visible rows.
    bool first = true;
    for (QWidget* row : rows_) {
        if (!row->isVisible()) continue;
        if (!first) d.line(Pt(16, row->y() + 0.5f), Pt(width() - 16, row->y() + 0.5f), theme::kBorder, 1.0f);
        first = false;
    }
}

SettingsCard* addSection(QVBoxLayout* col, const QString& title, const QString& hint) {
    if (col->count() > 1) col->addSpacing(10);
    if (!title.isEmpty()) col->addWidget(sectionLabel(title));
    if (!hint.isEmpty()) col->addWidget(hintLabel(hint));
    auto* card = new SettingsCard;
    col->addWidget(card);
    return card;
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

QLabel* hintLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTone(l, Tone::TextFaint); // follows the theme (a baked color would not)
    return l;
}

} // namespace bemotion::ui
