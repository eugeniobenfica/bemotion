#pragma once

// Reusable UI building blocks: the vector icon set (drawn with ui::Draw, so
// there is no icon font to ship) and the custom controls the panels share.

#include "core/Layer.h"
#include "core/Media.h"
#include "ui/Draw.h"

#include <QAbstractButton>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <string>

class QLineEdit;
class QMimeData;

namespace bemotion::ui {

enum class Icon {
    Text,
    Rectangle,
    Ellipse,
    Polygon,
    Star,
    Line,
    Eye,
    EyeOff,
    Lock,
    Unlock,
    Play,
    Pause,
    StepBack,
    StepForward,
    ToStart,
    ToEnd,
    Loop,
    Plus,
    Folder,
    ChevronRight,
    ChevronDown,
    ChevronUp,
    Diamond,
    Trash,
    Logo,
    Pointer,       // selection tool
    Razor,         // cut tool
    SplitPlayhead, // split at playhead
    Export,
    ZoomIn,
    ZoomOut,
    Reset,  // circular arrow
    Magnet, // snapping
    Gear,   // settings
    Camera,
    Plugin,
    BarChart,
    LineChart,
    PieChart,
    // Effect categories
    FxBlur,
    FxColor,
    FxChannel,
    FxDistort,
    FxGenerate,
    FxPerspective,
    FxStylize,
    FxTime,
    // Settings pages
    Palette,
    Sliders,
    Graph,    // timeline graph editor
    Compound, // layers grouped together
    Back,     // "go back" arrow
    // Media
    Film,
    Image,
    Audio,
    Import,
    // Tools
    Duplicate,
    Ungroup,
    Ease,
    LayerUp,
    LayerDown,
};

Icon iconForLayer(const Layer& layer);
Icon iconForShape(ShapeKind kind);
Icon iconForMedia(MediaKind kind);
// Plugin object types name their icon: "bars", "line_chart", "pie", a shape name, ...
Icon iconForPlugin(const std::string& name);

void drawIcon(Draw& d, Icon icon, Pt center, float size, Rgba color);
void drawDiamond(Draw& d, Pt center, float radius, Rgba fill, Rgba outline);
// The bemotion mark (render/LogoMark.h): a disc of `radius` around `center`.
void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color);
QIcon makeIcon(Icon icon, Rgba color, int size = 32); // for menus and Qt buttons

// "HH:MM:SS:FF" timecode.
std::string formatTimecode(double seconds, double fps);

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

// The keyframe toggle of a property row: a diamond, filled when a key sits at the playhead.
class KeyframeButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit KeyframeButton(QWidget* parent = nullptr);
    void setState(bool keyHere, bool animated);
    QSize sizeHint() const override { return {20, 24}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    bool keyHere_ = false, animated_ = false;
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

// A color swatch; clicking opens a color picker that edits live.
class ColorButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    void setColor(Color c);
    Color color() const { return color_; }
    void setAlphaEnabled(bool on) { alphaEnabled_ = on; }
    QSize sizeHint() const override { return {28, 24}; }

signals:
    void colorChanged(Color c);
    void editStarted();
    void editFinished();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void pick();
    Color color_;
    bool alphaEnabled_ = true;
};

} // namespace bemotion::ui
