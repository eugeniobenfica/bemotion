#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QString>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>

namespace bemotion::theme {

namespace {

using json = nlohmann::json;

Palette g_current;

float luminance(Rgba c) { return (0.2126f * ui::red(c) + 0.7152f * ui::green(c) + 0.0722f * ui::blue(c)) / 255.0f; }

// Every color field, for (de)serialization.
template <typename P, typename Fn>
void forEachColor(P& p, Fn&& fn) {
    fn("background", p.bgDeep);
    fn("panel", p.bgPanel);
    fn("raised", p.bgRaised);
    fn("hover", p.bgHover);
    fn("border", p.border);
    fn("text", p.text);
    fn("text_dim", p.textDim);
    fn("text_faint", p.textFaint);
    fn("accent", p.accent);
    fn("danger", p.danger);
    fn("snap", p.snap);
    fn("guide", p.guide);
    fn("canvas", p.canvas);
}

QString css(Rgba c) {
    return QString("rgba(%1,%2,%3,%4)").arg(ui::red(c)).arg(ui::green(c)).arg(ui::blue(c)).arg(ui::alpha(c));
}

} // namespace

std::string toHex(Rgba c) {
    char buf[16];
    if (ui::alpha(c) == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", ui::red(c), ui::green(c), ui::blue(c));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", ui::red(c), ui::green(c), ui::blue(c), ui::alpha(c));
    return buf;
}

bool fromHex(const std::string& s, Rgba& out) {
    if (s.empty() || s[0] != '#' || (s.size() != 7 && s.size() != 9)) return false;
    unsigned v = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + static_cast<unsigned>(d);
    }
    if (s.size() == 7) v = (v << 8) | 0xFF;
    out = rgba((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

Rgba mix(Rgba a, Rgba b, float t) {
    auto m = [&](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5f); };
    return rgba(m(ui::red(a), ui::red(b)), m(ui::green(a), ui::green(b)), m(ui::blue(a), ui::blue(b)), m(ui::alpha(a), ui::alpha(b)));
}

Palette darkPalette() { return {}; }

Palette lightPalette() {
    Palette p;
    p.name = "Light";
    p.light = true;
    p.bgDeep = rgba(222, 222, 227);
    p.bgPanel = rgba(240, 240, 243);
    p.bgRaised = rgba(252, 252, 253);
    p.bgHover = rgba(228, 228, 234);
    p.border = rgba(206, 206, 213);
    p.text = rgba(28, 28, 32);
    p.textDim = rgba(88, 88, 98);
    p.textFaint = rgba(140, 140, 150);
    p.accent = rgba(214, 158, 14);
    p.danger = rgba(208, 68, 52);
    p.snap = rgba(226, 38, 136);
    p.guide = rgba(0, 146, 214, 220);
    p.canvas = rgba(204, 204, 210);
    return p;
}

Palette midnightPalette() {
    Palette p;
    p.name = "Midnight";
    p.bgDeep = rgba(10, 13, 22);
    p.bgPanel = rgba(16, 20, 32);
    p.bgRaised = rgba(24, 29, 44);
    p.bgHover = rgba(33, 40, 58);
    p.border = rgba(38, 45, 64);
    p.text = rgba(226, 231, 245);
    p.textDim = rgba(140, 150, 175);
    p.textFaint = rgba(88, 98, 125);
    p.accent = rgba(110, 168, 255);
    p.canvas = rgba(22, 27, 40);
    return p;
}

bool paletteFromJson(const std::string& text, Palette& out, std::string* error) {
    try {
        const json j = json::parse(text);
        if (!j.is_object()) throw std::runtime_error("expected a JSON object");
        Palette p = j.value("light", false) ? lightPalette() : darkPalette();
        p.name = j.value("name", std::string("Imported"));
        p.light = j.value("light", false);
        p.rounding = std::clamp(j.value("rounding", p.rounding), 0.0f, 12.0f);
        const json colors = j.value("colors", json::object());
        forEachColor(p, [&](const char* key, Rgba& c) {
            if (colors.contains(key) && colors[key].is_string() && !fromHex(colors[key].get<std::string>(), c))
                throw std::runtime_error(std::string("bad color for \"") + key + "\"");
        });
        out = p;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::string paletteToJson(const Palette& palette) {
    json colors = json::object();
    forEachColor(palette, [&](const char* key, const Rgba& c) { colors[key] = toHex(c); });
    json j = {{"name", palette.name}, {"light", palette.light}, {"rounding", palette.rounding}, {"colors", colors}};
    return j.dump(2);
}

Rgba withAlpha(Rgba c, int a) { return (c & 0x00FFFFFFu) | (static_cast<Rgba>(std::clamp(a, 0, 255)) << 24); }
Rgba accent(int a) { return withAlpha(kAccent, a); }
Rgba ink(int a) { return g_current.light ? rgba(0, 0, 0, a) : rgba(255, 255, 255, a); }
Rgba shade(int a) { return rgba(0, 0, 0, g_current.light ? a / 3 : a); }
const Palette& current() { return g_current; }

// Docking (Qt Advanced Docking System): added to its own style sheet, which wins over the application's.
QString dockStyleSheet() {
    QString s;
    s += QString("ads--CDockContainerWidget, ads--CDockManager { background: %1; }\n"
                 "ads--CDockAreaWidget { background: %2; border: none; }\n"
                 "ads--CDockAreaTitleBar { background: %1; border: none; }\n"
                 "ads--CDockWidgetTab { background: %1; border: none; padding: 0 6px; }\n"
                 "ads--CDockWidgetTab[activeTab=\"true\"] { background: %2; border-top: 2px solid %3; }\n"
                 "ads--CDockWidgetTab QLabel { color: %4; }\n"
                 "ads--CDockWidgetTab[activeTab=\"true\"] QLabel { color: %5; }\n"
                 "ads--CDockSplitter::handle { background: %1; }\n"
                 "ads--CAutoHideTab { background: %2; color: %4; padding: 6px 4px; border: none; }\n"
                 "ads--CAutoHideTab:hover { color: %5; background: %6; }\n"
                 "ads--CAutoHideSideBar { background: %1; border: none; }\n"
                 "ads--CDockWidget { background: %2; }\n")
             .arg(css(kBgDeep), css(kBgPanel), css(kAccent), css(kTextDim), css(kText), css(kBgHover));
    return s;
}

void apply(const Palette& p) {
    g_current = p;
    kBgDeep = p.bgDeep;
    kBgPanel = p.bgPanel;
    kBgRaised = p.bgRaised;
    kBgHover = p.bgHover;
    kBorder = p.border;
    kText = p.text;
    kTextDim = p.textDim;
    kTextFaint = p.textFaint;
    kAccent = withAlpha(p.accent, 255);
    kAccentHover = mix(kAccent, rgba(255, 255, 255), 0.18f);
    kAccentActive = mix(kAccent, rgba(0, 0, 0), 0.12f);
    kAccentSoft = withAlpha(kAccent, 40);
    kOnAccent = luminance(kAccent) > 0.55f ? rgba(20, 18, 12) : rgba(255, 255, 255);
    kDanger = p.danger;
    kSnap = p.snap;
    kGuide = p.guide;
    kCanvas = p.canvas;

    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    if (!app) return;

    QPalette pal;
    pal.setColor(QPalette::Window, q(kBgPanel));
    pal.setColor(QPalette::WindowText, q(kText));
    pal.setColor(QPalette::Base, q(kBgRaised));
    pal.setColor(QPalette::AlternateBase, q(kBgHover));
    pal.setColor(QPalette::Text, q(kText));
    pal.setColor(QPalette::PlaceholderText, q(kTextFaint));
    pal.setColor(QPalette::Button, q(kBgRaised));
    pal.setColor(QPalette::ButtonText, q(kText));
    pal.setColor(QPalette::Highlight, q(accent(150)));
    pal.setColor(QPalette::HighlightedText, q(kText));
    pal.setColor(QPalette::ToolTipBase, q(kBgRaised));
    pal.setColor(QPalette::ToolTipText, q(kText));
    pal.setColor(QPalette::Link, q(kAccent));
    pal.setColor(QPalette::Mid, q(kBorder));
    pal.setColor(QPalette::Dark, q(kBgDeep));
    pal.setColor(QPalette::Light, q(kBgDeep));
    pal.setColor(QPalette::Midlight, q(kBorder));
    pal.setColor(QPalette::Shadow, q(kBgDeep));
    pal.setColor(QPalette::Disabled, QPalette::Text, q(kTextFaint));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, q(kTextFaint));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, q(kTextFaint));
    app->setPalette(pal);

    const int r = static_cast<int>(std::clamp(p.rounding, 0.0f, 12.0f));
    const Rgba pressed = mix(p.bgHover, p.text, 0.08f);
    const Rgba popup = mix(p.bgPanel, p.bgRaised, 0.5f);
    QString s;
    s += QString("QWidget { color: %1; }\n").arg(css(kText));
    s += QString("QMainWindow, QDialog { background: %1; }\n").arg(css(kBgPanel));
    s += QString("QToolTip { background: %1; color: %2; border: 1px solid %3; padding: 4px 6px; border-radius: %4px; }\n")
             .arg(css(kBgRaised), css(kText), css(kBorder))
             .arg(r);
    // Menus.
    s += QString("QMenuBar { background: %1; padding: 2px; }\n"
                 "QMenuBar::item { padding: 6px 10px; border-radius: %3px; background: transparent; }\n"
                 "QMenuBar::item:selected, QMenuBar::item:pressed { background: %2; }\n")
             .arg(css(kBgDeep), css(kBgHover))
             .arg(r);
    s += QString("QMenu { background: %1; border: 1px solid %2; padding: 4px; border-radius: %5px; }\n"
                 "QMenu::item { padding: 6px 28px 6px 14px; border-radius: %6px; }\n"
                 "QMenu::item:selected { background: %3; }\n"
                 "QMenu::item:disabled { color: %4; }\n"
                 "QMenu::separator { height: 1px; background: %2; margin: 4px 6px; }\n")
             .arg(css(popup), css(kBorder), css(kBgHover), css(kTextFaint))
             .arg(r + 2)
             .arg(r);
    // Buttons.
    s += QString("QPushButton { background: %1; border: none; border-radius: %4px; padding: 6px 14px; }\n"
                 "QPushButton:hover { background: %2; }\n"
                 "QPushButton:pressed { background: %3; }\n"
                 "QPushButton:disabled { color: %5; }\n"
                 "QPushButton[accent=\"true\"] { background: %6; color: %7; font-weight: 600; }\n"
                 "QPushButton[accent=\"true\"]:hover { background: %8; }\n"
                 "QPushButton[accent=\"true\"]:pressed { background: %9; }\n")
             .arg(css(kBgRaised), css(kBgHover), css(pressed))
             .arg(r)
             .arg(css(kTextFaint), css(kAccent), css(kOnAccent), css(kAccentHover), css(kAccentActive));
    s += QString("QPushButton[danger=\"true\"] { background: %1; color: white; }\n").arg(css(kDanger));
    s += QString("QToolButton { background: transparent; border: none; border-radius: %2px; padding: 3px; }\n"
                 "QToolButton:hover { background: %1; }\n"
                 "QToolButton:checked { background: %3; }\n")
             .arg(css(kBgHover))
             .arg(r)
             .arg(css(accent(45)));
    // Inputs.
    s += QString("QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QKeySequenceEdit {"
                 " background: %1; border: 1px solid transparent; border-radius: %4px; padding: 4px 6px;"
                 " selection-background-color: %3; }\n"
                 "QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %2; }\n"
                 "QComboBox::drop-down { border: none; width: 18px; }\n"
                 "QComboBox QAbstractItemView { background: %5; border: 1px solid %6; selection-background-color: %7; }\n")
             .arg(css(kBgRaised), css(accent(160)), css(accent(90)))
             .arg(r)
             .arg(css(popup), css(kBorder), css(kBgHover));
    s += QString("QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; border-radius: 4px;"
                 " background: %1; border: 1px solid %2; }\n"
                 "QRadioButton::indicator { border-radius: 8px; }\n"
                 "QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %3; border-color: %3; }\n")
             .arg(css(kBgRaised), css(kBorder), css(kAccent));
    s += QString("QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }\n"
                 "QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }\n"
                 "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: %2; }\n")
             .arg(css(kBgRaised), css(kAccent));
    // Scroll bars: thin, no arrows.
    s += QString("QScrollBar:vertical { width: 10px; background: transparent; margin: 0; }\n"
                 "QScrollBar:horizontal { height: 10px; background: transparent; margin: 0; }\n"
                 "QScrollBar::handle { background: %1; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 2px; }\n"
                 "QScrollBar::handle:hover { background: %2; }\n"
                 "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }\n"
                 "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }\n")
             .arg(css(mix(p.border, p.text, 0.12f)), css(mix(p.border, p.text, 0.28f)));
    s += QString("QScrollArea { background: transparent; border: none; }\n"
                 "QScrollArea > QWidget > QWidget { background: transparent; }\n");
    // Tabs and lists.
    s += QString("QTabWidget::pane { border: none; }\n"
                 "QTabBar::tab { background: transparent; padding: 7px 14px; border-radius: %3px; color: %1; }\n"
                 "QTabBar::tab:selected { color: %2; background: %4; }\n")
             .arg(css(kTextDim), css(kText))
             .arg(r)
             .arg(css(kBgRaised));
    s += QString("QListWidget, QTreeWidget, QTableWidget { background: transparent; border: none; outline: none; }\n"
                 "QListWidget::item, QTreeWidget::item { padding: 6px; border-radius: %3px; }\n"
                 "QListWidget::item:hover, QTreeWidget::item:hover { background: %1; }\n"
                 "QListWidget::item:selected, QTreeWidget::item:selected { background: %2; color: %4; }\n"
                 "QHeaderView::section { background: %5; border: none; padding: 4px 6px; color: %6; }\n")
             .arg(css(kBgHover), css(accent(50)))
             .arg(r)
             .arg(css(kText), css(kBgRaised), css(kTextDim));
    s += QString("QSplitter::handle { background: %1; }\n").arg(css(kBgDeep));
    app->setStyleSheet(s);
}

} // namespace bemotion::theme
