#pragma once

// Reusable UI building blocks: the vector icon set (drawn with ui::Draw, so
// there is no icon font to ship) and the custom controls the panels share.

#include "core/Layer.h"
#include "core/Media.h"
#include "ui/Draw.h"
#include "ui/Language.h"

#include <QAbstractButton>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QToolButton>
#include <QStringList>
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
    ChevronLeft,
    Minimize, // a panel's minimize button
    Fullscreen,
    Copy,
    Paste,
    Transition,
    Table,
    Cube,       // 3D models
    Background, // the background object
    Coffee,     // support (Buy Me a Coffee)
    Microphone, // recording
    Subtitles,  // the subtitle object
    Map,        // map charts
    Counter,    // the counter object: a number sign
    // Motion (Quick Animations, transitions)
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Burst,     // pop: a dot with rays
    Ripple,    // pulse: rings
    Heartbeat, // a heart monitor line
    Zigzag,    // shake
    Wave,      // float
    Bounce,    // a ball over the floor
    Fade,      // squares fading out
    Expression, // an italic f: a property's expression
    Crop,       // two crossing corners
    Keyboard,   // Settings › Shortcuts
    Edit,       // a pencil: Settings › Editor
    Help,       // a question mark in a circle
    ExitFullscreen, // corners pointing in
    Close,          // a cross
    Maximize,       // a panel's maximize button: a square
    Restore,        // a maximized panel's button: two squares
    Eyedropper,     // picks a color from the screen
    Meter,          // audio meters: two level bars
    Snapshot,       // a photo camera: save the frame as a picture
    Pen,            // the pen tool: a nib
    PathShape,      // a path shape: a curve between two points
    Mask,           // a mask: a dashed outline on a square
    RippleEdit,     // the ripple edit tool: an edge with arrows pushing the next clip
    RollEdit,       // the rolling edit tool: a cut between two clips, arrows both ways
    SlipEdit,       // the slip tool: a clip with arrows inside it
    Marker,         // a marker: a flag on the ruler
    Speaker,        // sound on
    SpeakerMuted,   // sound off
    Solo,           // solo: headphones
    Speed,          // a clip's speed: a gauge
    FxKeying,       // keying effects: a square with a hole
    Scopes,         // the Scopes panel: a waveform trace on a grid
    Proxy,          // proxies: a small film frame
    AlignLeft,      // text alignment: lines of text against the left edge
    AlignCenter,    // ... centered
    AlignRight,     // ... against the right edge
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
QIcon makeIcon(Icon icon, Rgba color, int size = 32); // for Qt buttons
QIcon menuIcon(Icon icon, Rgba color);                 // fills a menu's icon column

// Left and right audio level meters: two upright bars on a dB scale (-60 to 0), green, then
// yellow from -12 dB, red from -3 dB; the loudest of the last moments stays as a line, and a
// red light on top says the mix clipped (a click turns it off).
class AudioMeter : public QWidget {
    Q_OBJECT
public:
    explicit AudioMeter(QWidget* parent = nullptr);
    // Each frame: the loudest samples since the last call (0..1) and the seconds that passed.
    void feed(float left, float right, bool clipped, float seconds);
    QSize sizeHint() const override { return {58, 160}; }
    QSize minimumSizeHint() const override { return {58, 60}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    float level_[2] = {-90, -90}, hold_[2] = {-90, -90}; // dB
    float holdFor_[2] = {0, 0};                         // seconds the hold line stays
    bool clipped_ = false;
};

// The whole window full screen, or back as it was before (maximized or not).
void toggleFullScreen(QWidget* window);

// "Limit length to [N] s": a checkbox and a seconds field; `changed` gets 0 when unchecked.
QWidget* lengthLimitRow(double seconds, std::function<void(double)> changed);

// "HH:MM:SS:FF" timecode.
std::string formatTimecode(double seconds, double fps);

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
class CategoryHeader : public QWidget {
public:
    CategoryHeader(Icon icon, const QString& name, int count, bool closed, QWidget* parent = nullptr);
    std::function<void()> toggled;                        // clicked
    std::function<bool(const QMimeData*)> accepts;        // something dragged over it can go in
    std::function<void(const QMimeData*)> dropped;        // ... and was dropped
    std::function<void(QPoint)> menuRequested;            // right-click

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

} // namespace bemotion::ui
