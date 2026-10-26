#include "ui/ExpressionLibrary.h"

#include "ui/Draw.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

namespace bemotion::ui {

namespace {

constexpr int kWidth = 600, kHeight = 400, kSide = 150;
constexpr int kRow = 46, kHeaderRow = 28, kCategoryRow = 30;

QFont codeFont() {
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(QApplication::font().pointSizeF());
    return mono;
}

// The categories, in order, with how many entries each has.
class Categories : public QWidget {
public:
    std::vector<QString> names;
    std::vector<int> counts;
    int current = 0, hover = -1;
    std::function<void(int)> chosen;

    Categories() {
        setMouseTracking(true);
        setFixedWidth(kSide);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            const float y = 4.0f + i * kCategoryRow;
            const bool on = i == current;
            if (on || i == hover)
                d.rectFilled(Pt(6, y), Pt(width() - 6.0f, y + kCategoryRow - 4), on ? theme::withAlpha(theme::kAccent, 34) : theme::kBgHover,
                             theme::current().rounding);
            if (on) d.rectFilled(Pt(6, y + 6), Pt(8.5f, y + kCategoryRow - 10), theme::kAccent, 1.0f);
            p.setPen(qcolor(on ? theme::kText : theme::kTextDim));
            p.drawText(QRectF(16, y, width() - 50, kCategoryRow - 4), Qt::AlignVCenter, names[i]);
            p.setPen(qcolor(theme::kTextFaint));
            p.drawText(QRectF(width() - 44, y, 32, kCategoryRow - 4), Qt::AlignVCenter | Qt::AlignRight, QString::number(counts[i]));
        }
    }
    int rowAt(float y) const {
        const int i = static_cast<int>((y - 4.0f) / kCategoryRow);
        return y >= 4.0f && i < static_cast<int>(names.size()) ? i : -1;
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (const int i = rowAt(static_cast<float>(e->position().y())); i != hover) hover = i, update();
    }
    void leaveEvent(QEvent*) override { hover = -1, update(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (const int i = rowAt(static_cast<float>(e->position().y())); i >= 0 && chosen) chosen(i);
    }
};

// The entries shown: code over what it does; titles between groups.
class Entries : public QWidget {
public:
    std::vector<const ExpressionEntry*> shown;
    bool showCategory = false; // searching: each entry says where it comes from
    int current = -1, hover = -1;
    std::function<void(const ExpressionEntry&)> picked;

    Entries() { setMouseTracking(true); }

    void setShown(std::vector<const ExpressionEntry*> list, bool categories) {
        shown = std::move(list);
        showCategory = categories;
        hover = -1;
        current = -1;
        for (int i = 0; i < static_cast<int>(shown.size()) && current < 0; ++i)
            if (!shown[i]->header) current = i;
        int h = 6;
        for (const ExpressionEntry* e : shown) h += e->header ? kHeaderRow : kRow;
        setFixedHeight(std::max(h, 40));
        update();
    }
    float top(int i) const {
        float y = 6;
        for (int k = 0; k < i; ++k) y += shown[k]->header ? kHeaderRow : kRow;
        return y;
    }
    int rowAt(float y) const {
        float t = 6;
        for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
            const float h = shown[i]->header ? kHeaderRow : kRow;
            if (y >= t && y < t + h) return shown[i]->header ? -1 : i;
            t += h;
        }
        return -1;
    }
    void step(int by) { // Up / Down, past the titles
        for (int i = current + by; i >= 0 && i < static_cast<int>(shown.size()); i += by)
            if (!shown[i]->header) {
                current = i;
                update();
                return;
            }
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QPainter p(this);
        Draw d(p);
        const QFont base = font(), code = codeFont();
        QFont small = base;
        small.setPointSizeF(base.pointSizeF() * 0.85);
        if (shown.empty()) {
            p.setPen(qcolor(theme::kTextFaint));
            p.drawText(QRectF(12, 12, width() - 24, 30), Qt::AlignLeft, tr("Nothing matches"));
            return;
        }
        float y = 6;
        for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
            const ExpressionEntry& entry = *shown[i];
            const float h = entry.header ? kHeaderRow : kRow;
            if (y + h < e->rect().top() || y > e->rect().bottom()) {
                y += h;
                continue;
            }
            if (entry.header) {
                QFont title = small;
                title.setBold(true);
                title.setCapitalization(QFont::AllUppercase);
                title.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
                p.setFont(title);
                p.setPen(qcolor(theme::kTextFaint));
                p.drawText(QRectF(14, y + 6, width() - 28, h - 6), Qt::AlignLeft | Qt::AlignVCenter, entry.about);
                d.line(Pt(14, y + h - 1), Pt(width() - 14.0f, y + h - 1), theme::kBorder, 1.0f);
                y += h;
                continue;
            }
            const bool on = i == current, hot = i == hover;
            if (on || hot)
                d.rectFilled(Pt(6, y + 1), Pt(width() - 6.0f, y + h - 2), on ? theme::withAlpha(theme::kAccent, 28) : theme::kBgHover,
                             theme::current().rounding);
            QString shownCode = entry.code;
            shownCode.remove('|');
            p.setFont(code);
            p.setPen(qcolor(theme::kAccent));
            p.drawText(QRectF(14, y + 5, width() - 28, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(shownCode, Qt::ElideRight, width() - (showCategory ? 120 : 28)));
            p.setFont(small);
            if (showCategory) {
                p.setPen(qcolor(theme::kTextFaint));
                p.drawText(QRectF(width() - 110, y + 5, 96, 20), Qt::AlignRight | Qt::AlignVCenter, entry.category);
            }
            p.setPen(qcolor(on ? theme::kText : theme::kTextDim));
            p.drawText(QRectF(14, y + 24, width() - 28, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(entry.about, Qt::ElideRight, width() - 28));
            y += h;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (const int i = rowAt(static_cast<float>(e->position().y())); i != hover) hover = i, update();
    }
    void leaveEvent(QEvent*) override { hover = -1, update(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (const int i = rowAt(static_cast<float>(e->position().y())); i >= 0 && picked) picked(*shown[i]);
    }
};

class Library : public QWidget {
public:
    Library(QWidget* parent, std::vector<ExpressionEntry> entries, std::function<void(const QString&)> pick, const QString& start)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), entries_(std::move(entries)), pick_(std::move(pick)) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr("Expressions"));
        setFixedSize(kWidth, kHeight);
        for (const ExpressionEntry& e : entries_)
            if (std::find(categories_.begin(), categories_.end(), e.category) == categories_.end()) categories_.push_back(e.category);

        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(10, 10, 10, 10);
        col->setSpacing(8);
        search_ = new QLineEdit;
        search_->setPlaceholderText(tr("Search functions, names and layers"));
        search_->setClearButtonEnabled(true);
        search_->installEventFilter(this);
        col->addWidget(search_);
        auto* body = new QHBoxLayout;
        body->setSpacing(6);
        side_ = new Categories;
        side_->names = categories_;
        for (const QString& c : categories_)
            side_->counts.push_back(static_cast<int>(
                std::count_if(entries_.begin(), entries_.end(), [&](const ExpressionEntry& e) { return e.category == c && !e.header; })));
        side_->setFixedHeight(8 + kCategoryRow * static_cast<int>(categories_.size()));
        side_->chosen = [this](int i) { showCategory(i); };
        body->addWidget(side_, 0, Qt::AlignTop);
        list_ = new Entries;
        list_->picked = [this](const ExpressionEntry& e) { choose(e); };
        scroll_ = new QScrollArea;
        scroll_->setWidget(list_);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->viewport()->setAutoFillBackground(false);
        list_->setAutoFillBackground(false);
        body->addWidget(scroll_, 1);
        col->addLayout(body, 1);
        connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
        const auto it = std::find(categories_.begin(), categories_.end(), start);
        showCategory(it == categories_.end() ? 0 : static_cast<int>(it - categories_.begin()));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const float r = theme::current().rounding + 2.0f;
        d.rectFilled(Pt(0.5f, 0.5f), Pt(width() - 0.5f, height() - 0.5f), theme::kMenu, r);
        d.rect(Pt(0.5f, 0.5f), Pt(width() - 0.5f, height() - 0.5f), theme::kBorder, r);
        // Between the categories and the entries.
        const float x = 10.0f + kSide + 2.0f;
        d.line(Pt(x, 52), Pt(x, height() - 12.0f), theme::kBorder, 1.0f);
    }
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == search_ && e->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) {
                list_->step(k->key() == Qt::Key_Down ? 1 : -1);
                if (list_->current >= 0)
                    scroll_->ensureVisible(0, static_cast<int>(list_->top(list_->current)) + kRow / 2, 0, kRow);
                return true;
            }
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
                if (list_->current >= 0) choose(*list_->shown[list_->current]);
                return true;
            }
            if ((k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) && search_->text().isEmpty()) { // categories
                const int n = static_cast<int>(categories_.size());
                showCategory((side_->current + (k->key() == Qt::Key_Tab ? 1 : n - 1)) % n);
                return true;
            }
        }
        return QWidget::eventFilter(o, e);
    }
    void showEvent(QShowEvent*) override { search_->setFocus(); }
    bool focusNextPrevChild(bool) override { return false; } // Tab: the next category (eventFilter)
    // Keys that reach the popup itself are typed into the search.
    void keyPressEvent(QKeyEvent* e) override {
        if (forwarding_ || e->key() == Qt::Key_Escape) return QWidget::keyPressEvent(e);
        forwarding_ = true;
        QApplication::sendEvent(search_, e);
        forwarding_ = false;
    }

private:
    void showCategory(int i) {
        side_->current = i;
        side_->update();
        if (!search_->text().isEmpty()) search_->clear(); // refreshes
        else refresh();
    }
    void refresh() {
        const QString q = search_->text().trimmed();
        std::vector<const ExpressionEntry*> list;
        if (q.isEmpty()) {
            for (const ExpressionEntry& e : entries_)
                if (e.category == categories_[side_->current]) list.push_back(&e);
        } else {
            for (const ExpressionEntry& e : entries_)
                if (!e.header && (e.code.contains(q, Qt::CaseInsensitive) || e.about.contains(q, Qt::CaseInsensitive))) list.push_back(&e);
        }
        list_->setShown(std::move(list), !q.isEmpty());
        scroll_->verticalScrollBar()->setValue(0);
    }
    void choose(const ExpressionEntry& e) {
        const QString snippet = e.snippet.isEmpty() ? e.code : e.snippet;
        auto pick = pick_;
        close();
        if (pick) pick(snippet);
    }

    std::vector<ExpressionEntry> entries_;
    std::vector<QString> categories_;
    std::function<void(const QString&)> pick_;
    QLineEdit* search_ = nullptr;
    Categories* side_ = nullptr;
    Entries* list_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    bool forwarding_ = false;
};

} // namespace

void showExpressionLibrary(QWidget* anchor, std::vector<ExpressionEntry> entries, std::function<void(const QString&)> pick,
                           const QString& category) {
    auto* library = new Library(anchor ? anchor->window() : nullptr, std::move(entries), std::move(pick), category);
    // Under the anchor, its right edges lined up; above it when the screen ends; inside the screen.
    QPoint at = anchor ? anchor->mapToGlobal(QPoint(anchor->width() - kWidth, anchor->height() + 4)) : QCursor::pos();
    if (QScreen* screen = anchor ? anchor->screen() : QGuiApplication::screenAt(at)) {
        const QRect area = screen->availableGeometry();
        if (anchor && at.y() + kHeight > area.bottom()) at.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - kHeight - 4);
        at.setX(std::clamp(at.x(), area.left() + 4, area.right() - kWidth - 4));
        at.setY(std::clamp(at.y(), area.top() + 4, area.bottom() - kHeight - 4));
    }
    library->move(at);
    library->show();
}

} // namespace bemotion::ui
