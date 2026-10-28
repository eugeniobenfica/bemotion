#pragma once

// The controls bmstyle apps share, drawn with the theme's tokens at paint time (so a theme
// change shows at once): toned labels, icon buttons and tiles, the After Effects-like number
// field, switches, segmented choices, layouts that reflow in narrow panels, and the category
// headings of libraries. Their text goes through bmstyle::translate.

#include "bmstyle/Draw.h"
#include "bmstyle/Icons.h"

#include <QAbstractButton>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class QLineEdit;
class QMimeData;

namespace bmstyle {

// Text colors that follow the theme: setTone colors a label (or any widget's text) with one of
// the theme's roles, and recolors it whenever the appearance changes.
enum class Tone { Text, TextDim, TextFaint, Accent, Danger, Border };
void setTone(QWidget* widget, Tone tone);

// A small label in capitals, faint (section headers in panels).
QLabel* sectionLabel(const QString& text, QWidget* parent = nullptr);
// The prominent (accent colored) push button.
QPushButton* accentButton(const QString& text, QWidget* parent = nullptr);

// A square button showing one of the icons; `active` (checkable) draws it in the accent color.
class IconButton : public QAbstractButton {
    Q_OBJECT
public:
    IconButton(Icon icon, const QString& tip, int size = 26, QWidget* parent = nullptr);
    void setIcon(Icon icon) {
        icon_ = icon;
        update();
    }
    void setActive(bool active) {
        if (active_ == active) return;
        active_ = active;
        update();
    }
    void setActiveColor(Rgba color) { activeColor_ = color; } // 0: the accent (a recording button is red)
    bool active() const { return active_; }
    QSize sizeHint() const override { return {size_, size_}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    Icon icon_;
    int size_;
    bool active_ = false;
    Rgba activeColor_ = 0;
};

// An object / tool / category tile: big icon and a label underneath. Dragging it
// starts a drag with the mime data `dragData` returns (when set).
class Tile : public QAbstractButton {
    Q_OBJECT
public:
    Tile(Icon icon, const QString& label, QWidget* parent = nullptr);
    void setDragData(std::function<QMimeData*()> make) { dragData_ = std::move(make); }
    void setMarked(bool on) { set(marked_, on); }      // a dot in the corner
    void setHighlighted(bool on) { set(highlighted_, on); } // accent outline
    void setDimmed(bool on) { set(dimmed_, on); }
    void setTileSize(QSize size) { size_ = size; updateGeometry(); }
    QSize sizeHint() const override { return size_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    void set(bool& flag, bool on) {
        if (flag == on) return;
        flag = on;
        update();
    }
    Icon icon_;
    QString label_;
    QSize size_{84, 68};
    QPoint pressPos_;
    std::function<QMimeData*()> dragData_;
    bool marked_ = false, highlighted_ = false, dimmed_ = false;
};

// A number edited like After Effects' values: drag horizontally to scrub
// (Shift = x10, Alt = /10), click to type. editStarted / editFinished bracket
// one undoable change.
class DragValue : public QWidget {
    Q_OBJECT
public:
    explicit DragValue(QWidget* parent = nullptr);
    void setRange(float min, float max) { min_ = min, max_ = max; } // min == max: unbounded
    void setSpeed(float speed) { speed_ = speed; }
    void setFormat(const std::string& printfFormat) { format_ = printfFormat; update(); }
    void setValue(float v);    // from the model: no signal
    float value() const { return value_; }
    void setHighlighted(bool on) { highlighted_ = on; update(); } // animated value
    bool editing() const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {40, 24}; }

signals:
    void valueChanged(float value);
    void editStarted();
    void editFinished();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    void beginTyping();
    void commit(float v);
    QString text() const;

    float value_ = 0.0f, min_ = 0.0f, max_ = 0.0f, speed_ = 0.5f;
    std::string format_ = "%.1f";
    bool highlighted_ = false;
    bool dragging_ = false, moved_ = false;
    QPointF pressPos_;
    float pressValue_ = 0.0f;
    QLineEdit* edit_ = nullptr;
};

// An on / off switch, for settings that apply at once (a check box is for choices confirmed
// later, in a form). Checkable: `toggled` says when it flips.
class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {38, 22}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
};

// A row of buttons of which one is chosen (Size: Full / Half / Quarter...). `changed` gets the
// index. Disabled choices (`enabled` false) stay visible.
QWidget* segmented(const QStringList& choices, int current, std::function<void(int)> changed,
                   const std::vector<bool>& enabled = {});

// A row of buttons (or chips) that wraps onto the next line when the panel is narrow, instead
// of making the panel wider than it is.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); }
    ~FlowLayout() override;
    void addItem(QLayoutItem* item) override { items_.push_back(item); }
    int count() const override { return static_cast<int>(items_.size()); }
    QLayoutItem* itemAt(int i) const override { return i >= 0 && i < count() ? items_[static_cast<size_t>(i)] : nullptr; }
    QLayoutItem* takeAt(int i) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& r) override {
        QLayout::setGeometry(r);
        arrange(r, true);
    }

private:
    int arrange(const QRect& r, bool place) const;
    std::vector<QLayoutItem*> items_;
};

// A label that shortens its text with "…" when there is no room (the whole text in its
// tooltip), so a long name or path never widens its panel.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString& text = {}, Qt::TextElideMode mode = Qt::ElideRight, QWidget* parent = nullptr)
        : QLabel(text, parent), mode_(mode) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred); // its whole text when there is room
    }
    QSize minimumSizeHint() const override { return {std::min(QLabel::minimumSizeHint().width(), 36), QLabel::minimumSizeHint().height()}; }
    QSize sizeHint() const override { return QLabel::sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Qt::TextElideMode mode_;
};

// Cards in as many columns as fit (each at least `minCell` wide, sharing the width), as many
// rows as they need: a panel of tiles that reflows when it is made narrower or wider.
class ResponsiveGrid : public QWidget {
public:
    explicit ResponsiveGrid(int minCell, int gap = 6, QWidget* parent = nullptr);
    void addWidget(QWidget* w);
    int columns(int width) const { return std::max(1, (width + gap_) / (minCell_ + gap_)); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent*) override { place(); }

private:
    void place();
    int minCell_, gap_;
    std::vector<QWidget*> cells_;
};

// A library sorted in categories (Effects, Animate) reads the same everywhere: each category is a
// heading row (open or closed, its icon, its name, how many items, a rule to the edge), never a
// tile; its items are tiles in a CategoryBody under it, set in from a guide line.
// Draws a category's heading row in `r` (CategoryHeader; painted lists like the Media panel's
// folders draw theirs with it, to look the same). The guide line under it is at kCategoryGuideX.
// `count` below 0: no count (a section of properties).
void paintCategoryHeader(QPainter& p, const QRectF& r, Icon icon, const QString& name, int count, bool closed, bool hovered, bool target);
constexpr float kCategoryGuideX = 9.0f;

class CategoryHeader : public QWidget {
public:
    CategoryHeader(Icon icon, const QString& name, int count, bool closed, QWidget* parent = nullptr);
    std::function<void()> toggled;                        // clicked
    std::function<bool(const QMimeData*)> accepts;        // something dragged over it can go in
    std::function<void(const QMimeData*)> dropped;        // ... and was dropped
    std::function<void(QPoint)> menuRequested;            // right-click
    void setCount(int count) {
        count_ = count;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent* e) override;

private:
    Icon icon_;
    QString name_;
    int count_;
    bool closed_, target_ = false;
};

// A choice in a list (the Effects browser): its icon, its name, a "+" when the mouse is on it.
// Clicked, it is applied; it is not dragged (that is what a Tile is for), so it does not look
// like one.
class ChoiceRow : public QAbstractButton {
    Q_OBJECT
public:
    ChoiceRow(Icon icon, const QString& label, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {160, 28}; }
    QSize minimumSizeHint() const override { return {60, 28}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    Icon icon_;
    QString label_;
};

// The items of an open category: `content` (a ResponsiveGrid of tiles, a note...), set in under
// the guide line that ties them to their CategoryHeader.
class CategoryBody : public QWidget {
public:
    explicit CategoryBody(QWidget* content, QWidget* parent = nullptr);
    void add(QWidget* more); // under the content (an Import button, a note)

protected:
    void paintEvent(QPaintEvent*) override;
};

// A label and its control side by side, or the label above when there is not room for both
// (a narrow panel keeps its sliders long).
class LabeledRow : public QWidget {
public:
    LabeledRow(const QString& label, QWidget* control, int controlMin = 150, QWidget* parent = nullptr);
    QLabel* label() const { return label_; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent*) override { place(); }

private:
    bool stacked(int width) const { return width < labelWidth() + 10 + controlMin_; }
    int labelWidth() const { return label_->sizeHint().width(); }
    void place();
    QLabel* label_;
    QWidget* control_;
    int controlMin_;
};

} // namespace bmstyle
