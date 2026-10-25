#pragma once

#include "ui/Widgets.h"

#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QLayout;
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

// A group of settings: a rounded card of rows, each with a title (and a short description
// under it) on the left and its control on the right, as in the system settings of Windows and
// macOS. Every Settings page is made of them, so every option looks and sits the same way.
class SettingsCard : public QWidget {
public:
    SettingsCard();
    // `control` on the right (or, `below`, under the text: wide controls). Returns the row.
    QWidget* addRow(const QString& title, const QString& description, QWidget* control, bool below = false);
    QWidget* addContent(QWidget* content); // a row of its own, padded (grids, previews, lists)
    QWidget* addLayout(QLayout* content);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVBoxLayout* col_;
    std::vector<QWidget*> rows_;
};

// A section: its small capitals title, then its card (added to `col`).
SettingsCard* addSection(QVBoxLayout* col, const QString& title, const QString& hint = {});

// Page building helpers.
QWidget* pageWidget(QVBoxLayout*& col);                            // a page with its column layout
QLabel* pageTitle(const QString& text);                             // big bold title
QLabel* paragraph(const QString& text, Rgba color = 0);             // wrapped text (secondary color by default)
QLabel* hintLabel(const QString& text);                             // faint wrapped text

} // namespace bemotion::ui
