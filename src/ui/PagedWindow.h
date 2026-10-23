#pragma once

#include "ui/Widgets.h"

#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QListWidget;
class QVBoxLayout;
class QScrollArea;

namespace bemotion::ui {

// A window with a page list on the left (icon + label) and the selected page on
// the right, scrolling. Pages are built on demand and rebuilt by rebuild().
// Used by Settings and Help.
class PagedWindow : public QWidget {
    Q_OBJECT
public:
    struct Page {
        QString label;
        Icon icon;
        std::function<QWidget*()> build;
    };
    PagedWindow(const QString& title, std::vector<Page> pages, QWidget* footer = nullptr);
    void showPage(int index);
    int currentPage() const { return current_; }
    void rebuild(); // the current page, keeping its scroll position
    void present(); // shows, raises and focuses the window

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    std::vector<Page> pages_;
    QListWidget* list_ = nullptr;
    QScrollArea* area_ = nullptr;
    int current_ = -1;
};

// Page building helpers.
QWidget* pageWidget(QVBoxLayout*& col);                            // a page with its column layout
QLabel* pageTitle(const QString& text);                             // big bold title
QLabel* paragraph(const QString& text, Rgba color = 0);             // wrapped text (secondary color by default)
QLabel* hintLabel(const QString& text);                             // faint wrapped text

} // namespace bemotion::ui
