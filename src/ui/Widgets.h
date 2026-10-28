#pragma once

// The interface's building blocks. The look itself — the icon set and the controls every panel
// shares (IconButton, DragValue, Switch, segmented, FlowLayout, CategoryHeader...) — is the
// bestyle library (libs/bestyle), reusable by other Qt apps; it is brought into ui:: here.
// What is bemotion's own (icons for layers, the audio meter, keyframe and color buttons) is
// declared below.

#include "core/Layer.h"
#include "core/Media.h"
#include "ui/Draw.h"
#include "ui/Language.h"

#include "bestyle/Icons.h"
#include "bestyle/Widgets.h"

#include <QAbstractButton>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QRectF>
#include <QToolButton>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <string>

class QLineEdit;
class QMimeData;

namespace bemotion::ui {

using bestyle::accentButton;
using bestyle::CategoryBody;
using bestyle::CategoryHeader;
using bestyle::ChoiceRow;
using bestyle::DragValue;
using bestyle::drawDiamond;
using bestyle::drawIcon;
using bestyle::ElidedLabel;
using bestyle::FlowLayout;
using bestyle::Icon;
using bestyle::IconButton;
using bestyle::kCategoryGuideX;
using bestyle::LabeledRow;
using bestyle::makeIcon;
using bestyle::menuIcon;
using bestyle::paintCategoryHeader;
using bestyle::ResponsiveGrid;
using bestyle::sectionLabel;
using bestyle::segmented;
using bestyle::setTone;
using bestyle::Switch;
using bestyle::Tile;
using bestyle::Tone;

Icon iconForLayer(const Layer& layer);
Icon iconForShape(ShapeKind kind);
Icon iconForMedia(MediaKind kind);
// Plugin object types name their icon: "bars", "line_chart", "pie", a shape name, ...
Icon iconForPlugin(const std::string& name);

// The bemotion mark (render/LogoMark.h): a disc of `radius` around `center`.
void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color);

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

// The icon of a section of properties by its (English or translated) title: Transform, Fill,
// Stroke, an effect's category... Sliders when none fits.
Icon iconForSection(const QString& title);

} // namespace bemotion::ui
