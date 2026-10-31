#pragma once

// What the Settings window's source files share (SettingsWindow.cpp: the window and its pages;
// SettingsPlugins.cpp: the Plugins page). Internal to them.

#include "ui/Dialogs.h"
#include "app/Downloads.h"
#include "app/Platform.h"
#include "app/PluginHost.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "media/Recorder.h"
#include "app/Settings.h"
#include "render/Fonts.h"
#include "ui/AppSignals.h"
#include "ui/Keymap.h"
#include "ui/Language.h"
#include "ui/ColorPicker.h"
#include "ui/GpuContext.h"
#include "ui/Modules.h"
#include "ui/PagedWindow.h"
#include "ui/Updates.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include <QCheckBox>
#include <QDialog>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace bemotion::ui::settings_internal {

inline constexpr const char* kPluginDocsUrl = "https://github.com/eugeniobenfica/bemotion/blob/main/docs/PLUGINS.md";

inline constexpr Rgba kAccentChoices[] = {
    rgba(232, 185, 49),  // gold
    rgba(242, 140, 40),  // orange
    rgba(239, 111, 94),  // coral
    rgba(233, 85, 154),  // pink
    rgba(155, 108, 240), // violet
    rgba(76, 141, 246),  // blue
    rgba(46, 196, 182),  // teal
    rgba(92, 196, 106),  // green
};

inline QPointer<PagedWindow> g_window;
inline std::string g_message; // result of the last import / export

// Applies and saves; `rebuild` redraws the page (its colors come from the theme).
inline void changed(bool rebuild = false) {
    applySettings();
    saveSettings();
    emit AppSignals::get().appearanceChanged();
    if (rebuild && g_window) QTimer::singleShot(0, g_window, [] {
            if (g_window) g_window->rebuild();
        });
}

// Theme card: a tiny preview of panel, raised surface, text and accent.
// The built-in themes' names, shown translated (their files and settings keep the English ones).
[[maybe_unused]] constexpr const char* kThemeNames[] = {TR_NOOP("Dark"), TR_NOOP("Light"), TR_NOOP("Midnight")};

class ThemeCard : public QAbstractButton {
public:
    ThemeCard(theme::Palette p, bool selected) : p_(std::move(p)), selected_(selected) {
        setFixedSize(std::max(120, fontMetrics().horizontalAdvance(trs(p_.name)) + 20), 50 + fontMetrics().height() + 12);
        setCursor(Qt::PointingHandCursor);
        setToolTip(trs(p_.name)); // the built-in themes' names translate
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Draw d(painter);
        const float w = static_cast<float>(width()), h = static_cast<float>(height());
        d.rectFilled({0, 0}, {w, h}, p_.bgDeep, 8.0f);
        d.rectFilled({8, 8}, {52, 46}, p_.bgPanel, 4.0f);
        d.rectFilled({58, 8}, {w - 8, 24}, p_.bgRaised, 4.0f);
        d.rectFilled({58, 30}, {w - 24, 42}, p_.accent, 3.0f);
        d.rectFilled({14, 16}, {44, 20}, p_.textDim, 2.0f);
        d.rectFilled({14, 26}, {36, 30}, p_.textFaint, 2.0f);
        d.text({8, h - d.textHeight() - 6}, p_.text, trs(p_.name));
        if (selected_ || underMouse()) d.rect({1, 1}, {w - 1, h - 1}, selected_ ? theme::kAccent : theme::ink(60), 8.0f, selected_ ? 2.0f : 1.0f);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    theme::Palette p_;
    bool selected_;
};

class TimelineSizeCard : public QAbstractButton {
public:
    TimelineSizeCard(QString label, float clip, float property, float key, bool selected)
        : label_(std::move(label)), clip_(clip), property_(property), key_(key), selected_(selected) {
        setFixedSize(128, 82);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Draw d(painter);
        const float w = width(), h = height();
        d.rectFilled({0, 0}, {w, h}, theme::kBgRaised, 8.0f);
        const float top = 11.0f, row = std::clamp(clip_ * 0.42f, 12.0f, 25.0f);
        d.rectFilled({10, top}, {w - 10, top + row}, theme::accent(70), 4.0f);
        d.rectFilled({10, top + row + 5}, {w - 10, top + row + 5 + std::clamp(property_ * 0.25f, 7.0f, 13.0f)}, theme::shade(120), 3.0f);
        const float y = top + row * 0.5f;
        drawDiamond(d, {w * 0.42f, y}, key_ * 0.6f, theme::kAccent, theme::kBgRaised);
        drawDiamond(d, {w * 0.58f, y}, key_ * 0.6f, theme::kAccent, theme::kBgRaised);
        d.text({10, h - d.textHeight() - 8}, selected_ || underMouse() ? theme::kText : theme::kTextDim, label_);
        if (selected_ || underMouse()) d.rect({1, 1}, {w - 1, h - 1}, selected_ ? theme::kAccent : theme::ink(80), 8.0f, selected_ ? 2.0f : 1.0f);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
private:
    QString label_;
    float clip_, property_, key_;
    bool selected_;
};

// A round color choice.
class Swatch : public QAbstractButton {
public:
    Swatch(Rgba color, bool selected, const QString& tip) : color_(color), selected_(selected) {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tip);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const Pt c(14, 14);
        const float r = 12.0f;
        if (selected_) d.circle(c, r, theme::kText, 2.0f);
        d.circleFilled(c, r - (selected_ ? 3.5f : 1.0f), color_);
        if (underMouse() && !selected_) d.circle(c, r, theme::ink(90));
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    Rgba color_;
    bool selected_;
};

// A slider and its readout: the right side of a settings row. `after` runs before the settings
// are applied (to mirror the value somewhere else).
inline QWidget* sliderControl(float& value, float min, float max, const char* format, float step = 1.0f,
                       std::function<void()> after = {}) {
    auto* box = new QWidget;
    auto* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setFixedWidth(190);
    const int steps = static_cast<int>(std::lround((max - min) / step));
    slider->setRange(0, steps);
    slider->setValue(static_cast<int>(std::lround((value - min) / step)));
    h->addWidget(slider);
    auto* readout = new QLabel;
    readout->setMinimumWidth(48);
    readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setTone(readout, Tone::TextDim);
    auto show = [readout, format](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, format, v);
        readout->setText(buf);
    };
    show(value);
    h->addWidget(readout);
    float* target = &value;
    QObject::connect(slider, &QSlider::valueChanged, box, [=](int i) {
        *target = min + i * step;
        show(*target);
        if (after) after();
        changed();
    });
    return box;
}

inline QWidget* spinControl(int& value, int min, int max, const QString& suffix) {
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(value);
    // "1 frame", "2 frames": the suffix follows the number.
    auto unit = [spin, suffix](int v) { spin->setSuffix(v == 1 && suffix.endsWith('s') ? suffix.chopped(1) : suffix); };
    unit(value);
    spin->setFixedWidth(120);
    int* target = &value;
    QObject::connect(spin, &QSpinBox::valueChanged, spin, [target, unit](int v) {
        *target = v;
        unit(v);
        changed();
    });
    return spin;
}

inline Switch* switchControl(bool on, std::function<void(bool)> toggled) {
    auto* sw = new Switch;
    sw->setChecked(on);
    QObject::connect(sw, &QAbstractButton::toggled, sw, std::move(toggled));
    return sw;
}

inline QWidget* buttonRow(std::initializer_list<QWidget*> buttons) {
    auto* box = new QWidget;
    auto* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    for (QWidget* b : buttons) h->addWidget(b);
    return box;
}

inline theme::Palette basePalette(const Settings& s) {
    if (s.customPalette) return s.palette;
    const LibraryTheme* t = ThemeLibrary::find(s.theme);
    return t ? t->palette : theme::darkPalette();
}

// --- pages ---------------------------------------------------------------------------------

inline void addPageHeader(QVBoxLayout* col, const QString& title, std::function<void()> reset) {
    auto* row = new QHBoxLayout;
    row->addWidget(pageTitle(title), 1);
    auto* b = new QPushButton(makeIcon(Icon::Reset, theme::kTextDim, 16), tr("Reset Page"));
    b->setToolTip(tr("Restore this page's defaults"));
    QObject::connect(b, &QPushButton::clicked, b, [reset] {
        reset();
        changed(true);
    });
    row->addWidget(b);
    col->addLayout(row);
    col->addSpacing(2);
}

inline void enableAllPlugins() {
    for (const Plugin& p : PluginHost::get().plugins()) PluginHost::get().setEnabled(p.id, true);
    settings().disabledPlugins.clear();
    settings().disabledModules.clear(); // the built-in features too
    emit AppSignals::get().modulesChanged();
}

// The Plugins page (SettingsPlugins.cpp).
QWidget* pluginsPage();

} // namespace bemotion::ui::settings_internal
