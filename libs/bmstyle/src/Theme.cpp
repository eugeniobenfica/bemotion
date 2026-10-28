#include "bmstyle/Theme.h"

#include "bmstyle/BmStyle.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QString>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>

namespace bmstyle::theme {

namespace {

using json = nlohmann::json;

Palette g_current;

float luminance(Rgba c) { return (0.2126f * bmstyle::red(c) + 0.7152f * bmstyle::green(c) + 0.0722f * bmstyle::blue(c)) / 255.0f; }

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
    fn("header", p.header);
    fn("field", p.field);
    fn("field_border", p.fieldBorder);
    fn("button", p.button);
    fn("button_border", p.buttonBorder);
    fn("focus", p.focus);
    fn("selection", p.selection);
    fn("menu", p.menu);
    fn("on_accent", p.onAccent);
    fn("success", p.success);
    fn("warning", p.warning);
    fn("playhead", p.playhead);
    fn("keyframe", p.keyframe);
}

QString css(Rgba c) {
    return QString("rgba(%1,%2,%3,%4)").arg(bmstyle::red(c)).arg(bmstyle::green(c)).arg(bmstyle::blue(c)).arg(bmstyle::alpha(c));
}

// A chevron for drop-downs and spin boxes, as a picture style sheets can use (they draw no arrow
// of their own once the control is styled): a small PNG in the temporary folder, per color.
QString chevron(Rgba c, bool up) {
    const QString path = QDir(QDir::tempPath()).filePath(
        QString("bmstyle-chevron-%1-%2.png").arg(up ? "up" : "down").arg(QString::fromStdString(toHex(c)).mid(1)));
    if (!QFile::exists(path)) {
        QImage image(20, 12, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(bmstyle::qcolor(c), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF down[3] = {{3, 3}, {10, 9}, {17, 3}}, upward[3] = {{3, 9}, {10, 3}, {17, 9}};
        p.drawPolyline(up ? upward : down, 3);
        p.end();
        image.save(path, "PNG");
    }
    return QDir::fromNativeSeparators(path);
}

} // namespace

std::string toHex(Rgba c) {
    char buf[16];
    if (bmstyle::alpha(c) == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", bmstyle::red(c), bmstyle::green(c), bmstyle::blue(c));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", bmstyle::red(c), bmstyle::green(c), bmstyle::blue(c), bmstyle::alpha(c));
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
    return rgba(m(bmstyle::red(a), bmstyle::red(b)), m(bmstyle::green(a), bmstyle::green(b)), m(bmstyle::blue(a), bmstyle::blue(b)), m(bmstyle::alpha(a), bmstyle::alpha(b)));
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
        p.borderWidth = std::clamp(j.value("border_width", p.borderWidth), 0.0f, 2.0f);
        p.controlHeight = std::clamp(j.value("control_height", p.controlHeight), 22.0f, 40.0f);
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
    forEachColor(palette, [&](const char* key, const Rgba& c) {
        if (c) colors[key] = toHex(c); // the optional roles left to be worked out stay out
    });
    json j = {{"name", palette.name},           {"light", palette.light},
              {"rounding", palette.rounding},   {"border_width", palette.borderWidth},
              {"control_height", palette.controlHeight}, {"colors", colors}};
    return j.dump(2);
}

Rgba withAlpha(Rgba c, int a) { return (c & 0x00FFFFFFu) | (static_cast<Rgba>(std::clamp(a, 0, 255)) << 24); }
Rgba accent(int a) { return withAlpha(kAccent, a); }
Rgba ink(int a) { return g_current.light ? rgba(0, 0, 0, a) : rgba(255, 255, 255, a); }
Rgba shade(int a) { return rgba(0, 0, 0, g_current.light ? a / 3 : a); }
const Palette& current() { return g_current; }

Palette resolved(const Palette& in) {
    Palette p = in;
    auto pick = [](Rgba& field, Rgba derived) {
        if (!field) field = derived;
    };
    const Rgba white = rgba(255, 255, 255), black = rgba(0, 0, 0);
    pick(p.header, p.bgDeep);
    // Fields are sunk into the panel (darker on dark themes, white on light ones); buttons stand
    // out of it: the two never look alike.
    pick(p.field, p.light ? mix(p.bgRaised, white, 0.6f) : mix(p.bgPanel, p.bgDeep, 0.55f));
    pick(p.fieldBorder, p.light ? p.border : mix(p.border, p.bgRaised, 0.15f));
    pick(p.button, p.light ? mix(p.bgRaised, p.bgPanel, 0.35f) : mix(p.bgRaised, p.bgHover, 0.55f));
    pick(p.buttonBorder, p.light ? mix(p.border, black, 0.06f) : mix(p.border, p.text, 0.08f));
    pick(p.focus, p.accent);
    pick(p.selection, withAlpha(p.accent, 90));
    pick(p.menu, mix(p.bgPanel, p.bgRaised, 0.5f));
    pick(p.onAccent, luminance(p.accent) > 0.55f ? rgba(20, 18, 12) : white);
    pick(p.success, p.light ? rgba(40, 150, 80) : rgba(92, 196, 120));
    pick(p.warning, p.light ? rgba(200, 130, 20) : rgba(238, 172, 64));
    pick(p.playhead, p.accent);
    pick(p.keyframe, p.accent);
    return p;
}

// Docking (Qt Advanced Docking System): added to its own style sheet, which wins over the application's.
QString dockStyleSheet() {
    // Named placeholders (filled below): an unused number would shift every color after it.
    const int r = static_cast<int>(std::clamp(g_current.rounding, 0.0f, 12.0f)) + 2;
    QString s = "ads--CDockContainerWidget, ads--CDockManager { background: DEEP; }\n"
                "ads--CDockAreaWidget { background: PANEL; border: none; }\n"
                "ads--CDockAreaTitleBar { background: HEADER; border: none; padding: 0 2px 0 0; }\n"
                "ads--CDockWidgetTab { background: transparent; border: none; margin: 4px 1px 0 1px; padding: 0 6px;"
                " border-top-left-radius: RADIUSpx; border-top-right-radius: RADIUSpx; }\n"
                "ads--CDockWidgetTab:hover { background: HOVER; }\n"
                "ads--CDockWidgetTab[activeTab=\"true\"] { background: PANEL; }\n"
                "ads--CDockWidgetTab QLabel { color: DIM; }\n"
                "ads--CDockWidgetTab[activeTab=\"true\"] QLabel { color: TEXT; }\n"
                "ads--CDockSplitter::handle { background: DEEP; }\n"
                "ads--CAutoHideTab { background: PANEL; color: DIM; padding: 6px 4px; border: none; }\n"
                "ads--CAutoHideTab:hover { color: TEXT; background: HOVER; }\n"
                "ads--CAutoHideSideBar { background: DEEP; border: none; }\n"
                "ads--CDockWidget { background: PANEL; }\n";
    s.replace("DEEP", css(kBgDeep)).replace("PANEL", css(kBgPanel)).replace("HOVER", css(kBgHover)).replace("HEADER", css(kHeader));
    s.replace("DIM", css(kTextDim)).replace("TEXT", css(kText)).replace("RADIUS", QString::number(r));
    return s;
}

void apply(const Palette& given) {
    const Palette p = resolved(given);
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
    kOnAccent = p.onAccent;
    kDanger = p.danger;
    kSnap = p.snap;
    kGuide = p.guide;
    kCanvas = p.canvas;
    kHeader = p.header;
    kField = p.field;
    kFieldBorder = p.fieldBorder;
    kButton = p.button;
    kButtonBorder = p.buttonBorder;
    kFocus = withAlpha(p.focus, 255);
    kSelection = p.selection;
    kMenu = p.menu;
    kSuccess = p.success;
    kWarning = p.warning;
    kPlayhead = withAlpha(p.playhead, 255);
    kKeyframe = withAlpha(p.keyframe, 255);

    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    if (!app) return;

    QPalette pal;
    pal.setColor(QPalette::Window, q(kBgPanel));
    pal.setColor(QPalette::WindowText, q(kText));
    pal.setColor(QPalette::Base, q(kField));
    pal.setColor(QPalette::AlternateBase, q(kBgHover));
    pal.setColor(QPalette::Text, q(kText));
    pal.setColor(QPalette::PlaceholderText, q(kTextFaint));
    pal.setColor(QPalette::Button, q(kButton));
    pal.setColor(QPalette::ButtonText, q(kText));
    pal.setColor(QPalette::Highlight, q(kSelection));
    pal.setColor(QPalette::HighlightedText, q(kText));
    pal.setColor(QPalette::ToolTipBase, q(kMenu));
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
    const int bw = static_cast<int>(std::lround(std::clamp(p.borderWidth, 0.0f, 2.0f)));
    // Height inside the outline and padding (Qt's min-height is the content box).
    const int inner = std::max(12, static_cast<int>(std::lround(std::clamp(p.controlHeight, 22.0f, 40.0f))) - 2 * bw - 8);
    const Rgba buttonHover = mix(p.button, p.text, 0.07f);
    const Rgba pressed = mix(p.button, p.text, 0.13f);
    const Rgba popup = p.menu;
    QString s;
    s += QString("QWidget { color: %1; }\n").arg(css(kText));
    s += QString("QMainWindow, QDialog { background: %1; }\n").arg(css(kBgPanel));
    s += QString("QToolTip { background: %1; color: %2; border: 1px solid %3; padding: 4px 6px; border-radius: %4px; }\n")
             .arg(css(kMenu), css(kText), css(kBorder))
             .arg(r);
    // Menus.
    s += QString("QMenuBar { background: %1; padding: 2px; }\n"
                 "QMenuBar::item { padding: 6px 10px; border-radius: %3px; background: transparent; }\n"
                 "QMenuBar::item:selected, QMenuBar::item:pressed { background: %2; }\n")
             .arg(css(kHeader), css(kBgHover))
             .arg(r);
    s += QString("QMenu { background: %1; border: 1px solid %2; padding: 4px; border-radius: %5px; }\n"
                 "QMenu::item { padding: 6px 28px 6px 14px; border-radius: %6px; }\n"
                 "QMenu::item:selected { background: %3; }\n"
                 "QMenu::item:disabled { color: %4; }\n"
                 "QMenu::separator { height: 1px; background: %2; margin: 4px 6px; }\n")
             .arg(css(popup), css(kBorder), css(kBgHover), css(kTextFaint))
             .arg(r + 2)
             .arg(r);
    // Buttons: raised, outlined, centered text; the accent one is the main action of a window.
    // (Named placeholders: an unused number would shift every value after it.)
    QString buttons = "QPushButton { background: BUTTON; border: BWpx solid BBORDER; border-radius: Rpx; padding: 4px 14px;"
                      " min-height: INNERpx; }\n"
                      "QPushButton:hover { background: BHOVER; }\n"
                      "QPushButton:pressed { background: BPRESSED; }\n"
                      "QPushButton:focus { border-color: FOCUS; }\n"
                      "QPushButton:disabled { color: FAINT; background: DISABLED; }\n"
                      "QPushButton[accent=\"true\"] { background: ACCENT; color: ONACCENT; font-weight: 600; border-color: ACCENT; }\n"
                      "QPushButton[accent=\"true\"]:hover { background: AHOVER; border-color: AHOVER; }\n"
                      "QPushButton[accent=\"true\"]:pressed { background: AACTIVE; }\n"
                      "QPushButton[accent=\"true\"]:disabled { background: ADISABLED; color: ONACCENT; border-color: transparent; }\n"
                      "QPushButton[danger=\"true\"] { background: DANGER; color: white; border-color: DANGER; }\n"
                      "QPushButton:checked { background: ACHECKED; color: TEXT; border-color: ACHECKEDB; }\n"
                      // A quiet button: text in the accent, no box until hovered (links, minor actions).
                      "QPushButton[flat=\"true\"] { background: transparent; border-color: transparent; color: ACCENT; padding: 4px 8px; }\n"
                      "QPushButton[flat=\"true\"]:hover { background: HOVER; }\n";
    buttons.replace("BUTTON", css(kButton)).replace("BBORDER", css(kButtonBorder)).replace("BHOVER", css(buttonHover));
    buttons.replace("BPRESSED", css(pressed)).replace("FOCUS", css(kFocus)).replace("FAINT", css(kTextFaint));
    buttons.replace("ADISABLED", css(withAlpha(kAccent, 90))).replace("DISABLED", css(mix(p.button, p.bgPanel, 0.5f)));
    buttons.replace("AHOVER", css(kAccentHover)).replace("AACTIVE", css(kAccentActive)).replace("ONACCENT", css(kOnAccent));
    buttons.replace("ACHECKEDB", css(accent(140))).replace("ACHECKED", css(accent(70))).replace("ACCENT", css(kAccent));
    buttons.replace("DANGER", css(kDanger)).replace("TEXT", css(kText)).replace("HOVER", css(kBgHover));
    buttons.replace("INNER", QString::number(inner)).replace("BW", QString::number(bw)).replace("Rpx", QString::number(r) + "px");
    s += buttons;
    // Segmented controls (bmstyle::segmented): one outlined strip; the chosen part in the accent.
    // (Their strip draws the outline, the fill and the chosen part: SegmentStrip in Widgets.cpp.)
    QString seg = "QPushButton[segment] { background: transparent; border: none; border-radius: 0; padding: 5px 12px; color: DIM; }\n"
                  "QPushButton[segment]:hover, QPushButton[segment]:checked { color: TEXT; background: transparent; }\n"
                  "QPushButton[segment]:disabled { color: FAINT; }\n"
                  // A key shortcut shown as a field you click to change (Settings › Shortcuts).
                  "QPushButton[keycap=\"true\"] { background: FIELD; border-color: FBORDER; font-weight: 600; }\n";
    seg.replace("FIELD", css(kField)).replace("FBORDER", css(kFieldBorder)).replace("FHOVER", css(mix(p.field, p.text, 0.06f)));
    seg.replace("DIM", css(kTextDim)).replace("TEXT", css(kText)).replace("SOFT", css(accent(55))).replace("ABORDER", css(accent(170)));
    seg.replace("FAINT", css(kTextFaint)).replace("BW", QString::number(std::max(1, bw))).replace("Rpx", QString::number(r) + "px");
    s += seg;
    // The collapsible sections of Properties and Effects (bmstyle::CollapsibleSection).
    s += QString("QToolButton[sectionHeader=\"true\"] { text-align: left; font-weight: 600; padding: 5px 6px; background: %1;"
                 " border-radius: %3px; }\n"
                 "QToolButton[sectionHeader=\"true\"]:hover { background: %2; }\n"
                 "QToolButton[sectionHeader=\"true\"]:checked { background: %1; }\n"
                 "QToolButton[sectionHeader=\"true\"]:checked:hover { background: %2; }\n")
             .arg(css(kBgRaised), css(kBgHover))
             .arg(r);
    s += QString("QToolButton { background: transparent; border: none; border-radius: %2px; padding: 3px; }\n"
                 "QToolButton:hover { background: %1; }\n"
                 "QToolButton:checked { background: %3; }\n"
                 "QToolButton[popupMode=\"2\"] { padding-right: 14px; }\n"
                 "QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right; right: 4px; }\n")
             .arg(css(kBgHover))
             .arg(r)
             .arg(css(accent(45)));
    // Fields: sunken, outlined, text from the left; the outline lights up on hover and focus. A
    // drop-down is a field with a chevron (bmstyle::Style draws it).
    QString fields = "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QKeySequenceEdit {"
                     " background: FIELD; border: BWpx solid FBORDER; border-radius: Rpx; padding: 3px 8px;"
                     " selection-background-color: SELECTION; }\n"
                     "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { min-height: INNERpx; }\n"
                     "QLineEdit:hover, QPlainTextEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { border-color: FHOVER; }\n"
                     "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,"
                     " QComboBox:on { border-color: FOCUS; }\n"
                     "QLineEdit:disabled, QComboBox:disabled { color: FAINT; }\n"
                     "QLineEdit[readOnly=\"true\"] { color: DIM; }\n"
                     "QComboBox { padding-right: 26px; }\n"
                     "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 24px; }\n"
                     "QComboBox::down-arrow { image: url(CHEVRON); width: 10px; height: 6px; }\n"
                     "QComboBox::down-arrow:disabled { image: url(FCHEVRON); }\n"
                     "QAbstractSpinBox { padding-right: 22px; }\n"
                     "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: border; width: 20px; border: none;"
                     " background: transparent; }\n"
                     "QAbstractSpinBox::up-button { subcontrol-position: top right; }\n"
                     "QAbstractSpinBox::down-button { subcontrol-position: bottom right; }\n"
                     "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: HOVER; }\n"
                     "QAbstractSpinBox::up-arrow { image: url(UCHEVRON); width: 8px; height: 5px; }\n"
                     "QAbstractSpinBox::down-arrow { image: url(CHEVRON); width: 8px; height: 5px; }\n"
                     "QComboBox QAbstractItemView { background: POPUP; border: 1px solid BORDER; selection-background-color: HOVER;"
                     " outline: none; padding: 4px; }\n";
    fields.replace("FCHEVRON", chevron(kTextFaint, false)).replace("UCHEVRON", chevron(kTextDim, true)).replace("CHEVRON", chevron(kTextDim, false));
    fields.replace("FIELD", css(kField)).replace("FBORDER", css(kFieldBorder)).replace("SELECTION", css(kSelection));
    fields.replace("FHOVER", css(mix(p.fieldBorder, p.text, 0.25f))).replace("FOCUS", css(kFocus)).replace("FAINT", css(kTextFaint));
    fields.replace("DIM", css(kTextDim)).replace("POPUP", css(popup)).replace("BORDER", css(kBorder)).replace("HOVER", css(kBgHover));
    fields.replace("INNER", QString::number(inner)).replace("BW", QString::number(bw)).replace("Rpx", QString::number(r) + "px");
    s += fields;
    s += QString("QProgressBar { background: %1; border: none; border-radius: %3px; text-align: center; color: %4; min-height: 8px; }\n"
                 "QProgressBar::chunk { background: %2; border-radius: %3px; }\n")
             .arg(css(kField), css(kAccent))
             .arg(std::max(2, r))
             .arg(css(kText));
    // Check boxes and radio buttons are drawn by bmstyle::Style (a check mark, not an accent-filled box).
    s += QString("QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }\n"
                 "QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }\n"
                 "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: %2; }\n")
             .arg(css(kField), css(kAccent));
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
                 "QListWidget::item:selected:hover, QTreeWidget::item:selected:hover { background: %2; }\n"
                 "QHeaderView::section { background: %5; border: none; padding: 4px 6px; color: %6; }\n")
             .arg(css(kBgHover), css(accent(50)))
             .arg(r)
             .arg(css(kText), css(kBgRaised), css(kTextDim));
    s += QString("QSplitter::handle { background: %1; }\n").arg(css(kBgDeep));
    app->setStyleSheet(s);
    emit Appearance::get().changed();
}

} // namespace bmstyle::theme
