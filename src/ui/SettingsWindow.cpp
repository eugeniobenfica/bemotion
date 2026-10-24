// The Settings window (appearance, interface, editor preferences, shortcuts
// and plugins). One window shared by the home screen and the editor.

#include "ui/Dialogs.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "media/Recorder.h"
#include "app/Settings.h"
#include "render/Fonts.h"
#include "ui/AppSignals.h"
#include "ui/Keymap.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QColorDialog>
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
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace bemotion::ui {

namespace {

constexpr const char* kPluginDocsUrl = "https://github.com/eugeniobenfica/bemotion/blob/main/docs/PLUGINS.md";

constexpr Rgba kAccentChoices[] = {
    rgba(232, 185, 49),  // gold
    rgba(242, 140, 40),  // orange
    rgba(239, 111, 94),  // coral
    rgba(233, 85, 154),  // pink
    rgba(155, 108, 240), // violet
    rgba(76, 141, 246),  // blue
    rgba(46, 196, 182),  // teal
    rgba(92, 196, 106),  // green
};

QPointer<PagedWindow> g_window;
std::string g_message; // result of the last import / export

// Applies and saves; `rebuild` redraws the page (its colors come from the theme).
void changed(bool rebuild = false) {
    applySettings();
    saveSettings();
    emit AppSignals::get().appearanceChanged();
    if (rebuild && g_window) QTimer::singleShot(0, g_window, [] {
            if (g_window) g_window->rebuild();
        });
}

// Theme card: a tiny preview of panel, raised surface, text and accent.
class ThemeCard : public QAbstractButton {
public:
    ThemeCard(theme::Palette p, bool selected) : p_(std::move(p)), selected_(selected) {
        setFixedSize(std::max(120, fontMetrics().horizontalAdvance(QString::fromStdString(p_.name)) + 20), 50 + fontMetrics().height() + 12);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QString::fromStdString(p_.name));
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
        d.text({8, h - d.textHeight() - 6}, p_.text, p_.name);
        if (selected_ || underMouse()) d.rect({1, 1}, {w - 1, h - 1}, selected_ ? theme::kAccent : theme::ink(60), 8.0f, selected_ ? 2.0f : 1.0f);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    theme::Palette p_;
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

// A labelled slider row editing a float setting.
QWidget* sliderRow(const QString& label, float& value, float min, float max, const char* format, float step = 1.0f,
                   const QString& tip = {}) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* l = new QLabel(label);
    l->setMinimumWidth(170);
    h->addWidget(l);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setFixedWidth(220);
    const int steps = static_cast<int>(std::lround((max - min) / step));
    slider->setRange(0, steps);
    slider->setValue(static_cast<int>(std::lround((value - min) / step)));
    if (!tip.isEmpty()) slider->setToolTip(tip);
    h->addWidget(slider);
    auto* readout = new QLabel;
    auto show = [readout, format](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, format, v);
        readout->setText(buf);
    };
    show(value);
    h->addWidget(readout);
    h->addStretch();
    float* target = &value;
    QObject::connect(slider, &QSlider::valueChanged, row, [=](int i) {
        *target = min + i * step;
        show(*target);
        changed();
    });
    return row;
}

QWidget* intSliderRow(const QString& label, int& value, int min, int max) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* l = new QLabel(label);
    l->setMinimumWidth(170);
    h->addWidget(l);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setFixedWidth(220);
    slider->setRange(min, max);
    slider->setValue(value);
    h->addWidget(slider);
    auto* readout = new QLabel(QString::number(value));
    h->addWidget(readout);
    h->addStretch();
    int* target = &value;
    QObject::connect(slider, &QSlider::valueChanged, row, [=](int v) {
        *target = v;
        readout->setText(QString::number(v));
        changed();
    });
    return row;
}

theme::Palette basePalette(const Settings& s) {
    if (s.customPalette) return s.palette;
    const LibraryTheme* t = ThemeLibrary::find(s.theme);
    return t ? t->palette : theme::darkPalette();
}

// --- pages ---------------------------------------------------------------------------------

void addPageHeader(QVBoxLayout* col, const QString& title, std::function<void()> reset) {
    auto* row = new QHBoxLayout;
    row->addWidget(pageTitle(title), 1);
    auto* b = new QPushButton(makeIcon(Icon::Reset, theme::kTextDim, 16), "Reset");
    b->setToolTip("Restore this page's defaults");
    QObject::connect(b, &QPushButton::clicked, b, [reset] {
        reset();
        changed(true);
    });
    row->addWidget(b);
    col->addLayout(row);
    col->addSpacing(4);
}

void enableAllPlugins() {
    for (const Plugin& p : PluginHost::get().plugins()) PluginHost::get().setEnabled(p.id, true);
    settings().disabledPlugins.clear();
}

void addColorGrid(QVBoxLayout* col, Settings& s) {
    struct Entry {
        const char* label;
        Rgba theme::Palette::*field;
    };
    static constexpr Entry kEntries[] = {
        {"Background", &theme::Palette::bgDeep},    {"Panels", &theme::Palette::bgPanel},
        {"Inputs & cards", &theme::Palette::bgRaised}, {"Hover", &theme::Palette::bgHover},
        {"Borders", &theme::Palette::border},       {"Canvas surround", &theme::Palette::canvas},
        {"Text", &theme::Palette::text},            {"Secondary text", &theme::Palette::textDim},
        {"Faint text", &theme::Palette::textFaint}, {"Danger", &theme::Palette::danger},
        {"Snap lines", &theme::Palette::snap},      {"Guides", &theme::Palette::guide},
    };
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    int i = 0;
    for (const Entry& e : kEntries) {
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        auto* button = new ColorButton;
        const Rgba c = s.palette.*(e.field);
        button->setColor(Color{red(c) / 255.0f, green(c) / 255.0f, blue(c) / 255.0f, alpha(c) / 255.0f});
        auto field = e.field;
        QObject::connect(button, &ColorButton::colorChanged, button, [field](Color v) {
            Settings& st = settings();
            auto byte = [](float x) { return static_cast<int>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255)); };
            st.palette.*field = rgba(byte(v.r), byte(v.g), byte(v.b), byte(v.a));
            st.customPalette = true;
            changed();
        });
        QObject::connect(button, &ColorButton::editFinished, button, [] { changed(true); });
        row->addWidget(button);
        row->addWidget(new QLabel(e.label), 1);
        grid->addLayout(row, i / 2, i % 2);
        ++i;
    }
    col->addLayout(grid);
}

QWidget* themePage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, "Theme", [] {
        Settings& st = settings();
        const Settings d;
        st.theme = d.theme, st.customAccent = d.customAccent, st.accent = d.accent;
        st.customPalette = false;
    });
    col->addWidget(sectionLabel("THEME"));
    auto* cards = new QGridLayout;
    cards->setSpacing(10);
    cards->setAlignment(Qt::AlignLeft);
    int n = 0;
    for (const LibraryTheme& t : ThemeLibrary::all()) {
        auto* card = new ThemeCard(t.palette, !s.customPalette && s.theme == t.palette.name);
        const std::string name = t.palette.name;
        QObject::connect(card, &QAbstractButton::clicked, card, [name] {
            settings().theme = name;
            settings().customPalette = false;
            changed(true);
        });
        cards->addWidget(card, n / 4, n % 4);
        ++n;
    }
    col->addLayout(cards);
    auto* row = new QHBoxLayout;
    auto* import = new QPushButton("Import Theme...");
    QObject::connect(import, &QPushButton::clicked, import, [] {
        if (auto file = platform::openFileDialog("Import Theme", "bemotion theme", "json")) {
            std::string name, error;
            if (ThemeLibrary::importFile(*file, &name, &error)) {
                settings().theme = name;
                settings().customPalette = false;
                g_message = "Imported \"" + name + "\".";
            } else {
                g_message = "Could not import: " + error;
            }
            changed(true);
        }
    });
    auto* folder = new QPushButton("Open Themes Folder");
    QObject::connect(folder, &QPushButton::clicked, folder, [] { platform::revealInFileManager(ThemeLibrary::userDir()); });
    row->addWidget(import);
    row->addWidget(folder);
    row->addStretch();
    col->addLayout(row);
    if (!g_message.empty()) col->addWidget(hintLabel(QString::fromStdString(g_message)));

    col->addSpacing(8);
    col->addWidget(sectionLabel("ACCENT COLOR"));
    const theme::Palette base = [&] {
        Settings copy = s;
        copy.customAccent = false;
        return basePalette(copy);
    }();
    auto* accents = new QHBoxLayout;
    accents->setSpacing(8);
    auto* themeAccent = new Swatch(base.accent, !s.customAccent, "Theme default");
    QObject::connect(themeAccent, &QAbstractButton::clicked, themeAccent, [] {
        settings().customAccent = false;
        changed(true);
    });
    accents->addWidget(themeAccent);
    for (Rgba c : kAccentChoices) {
        auto* sw = new Swatch(c, s.customAccent && s.accent == c, "");
        QObject::connect(sw, &QAbstractButton::clicked, sw, [c] {
            settings().customAccent = true;
            settings().accent = c;
            changed(true);
        });
        accents->addWidget(sw);
    }
    accents->addSpacing(8);
    auto* custom = new QPushButton("Custom...");
    custom->setToolTip("Pick any color");
    QObject::connect(custom, &QPushButton::clicked, custom, [custom] {
        const Rgba cur = settings().customAccent ? settings().accent : basePalette(settings()).accent;
        const QColor picked = QColorDialog::getColor(qcolor(cur), custom->window(), "Accent Color");
        if (!picked.isValid()) return;
        settings().customAccent = true;
        settings().accent = rgba(picked.red(), picked.green(), picked.blue());
        changed(true);
    });
    accents->addWidget(custom);
    accents->addStretch();
    col->addLayout(accents);

    if (!s.advanced) {
        col->addSpacing(8);
        col->addWidget(hintLabel("Every color, corner rounding and theme sharing are in Advanced Options (bottom left)."));
        return page;
    }

    col->addSpacing(8);
    col->addWidget(sectionLabel("COLORS"));
    col->addWidget(hintLabel("Editing a color turns the current theme into your own custom palette."));
    if (!s.customPalette) s.palette = effectivePalette(); // start from what is on screen
    addColorGrid(col, s);
    auto* light = new QCheckBox("Light theme (overlay strokes in black)");
    light->setChecked(s.palette.light);
    QObject::connect(light, &QCheckBox::toggled, light, [](bool on) {
        settings().palette.light = on;
        settings().customPalette = true;
        changed(true);
    });
    col->addWidget(light);
    col->addSpacing(8);
    col->addWidget(sectionLabel("SHAPE"));
    col->addWidget(sliderRow("Corner rounding", s.palette.rounding, 0.0f, 12.0f, "%.0f px"));

    col->addSpacing(8);
    col->addWidget(sectionLabel("SHARE"));
    auto* share = new QHBoxLayout;
    auto* name = new QLineEdit;
    name->setPlaceholderText("Theme name");
    name->setFixedWidth(200);
    share->addWidget(name);
    auto* save = new QPushButton("Save as Theme");
    QObject::connect(save, &QPushButton::clicked, save, [name] {
        if (name->text().trimmed().isEmpty()) return;
        theme::Palette out = effectivePalette();
        out.name = name->text().trimmed().toStdString();
        std::string error;
        if (ThemeLibrary::save(out, &error)) {
            settings().theme = out.name;
            settings().customPalette = false;
            settings().customAccent = false;
            g_message = "Saved \"" + out.name + "\".";
        } else {
            g_message = error;
        }
        changed(true);
    });
    share->addWidget(save);
    auto* exportButton = new QPushButton("Export...");
    QObject::connect(exportButton, &QPushButton::clicked, exportButton, [name] {
        theme::Palette out = effectivePalette();
        if (!name->text().trimmed().isEmpty()) out.name = name->text().trimmed().toStdString();
        if (auto file = platform::saveFileDialog("Export Theme", platform::fromUtf8(out.name + ".json"), "bemotion theme", "json")) {
            std::string error;
            g_message = ThemeLibrary::exportTo(out, *file, &error) ? "Exported to " + platform::toUtf8(*file) : error;
            if (g_window) g_window->rebuild();
        }
    });
    share->addWidget(exportButton);
    if (s.customPalette) {
        auto* reset = new QPushButton("Reset to Theme");
        QObject::connect(reset, &QPushButton::clicked, reset, [] {
            settings().customPalette = false;
            changed(true);
        });
        share->addWidget(reset);
    }
    share->addStretch();
    col->addLayout(share);
    return page;
}

QWidget* interfacePage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, "Interface", [] {
        Settings& st = settings();
        const Settings d;
        st.timelineLayerRow = d.timelineLayerRow, st.timelinePropertyRow = d.timelinePropertyRow;
        st.keyframeSize = d.keyframeSize, st.textScale = d.textScale, st.uiFont = d.uiFont;
        st.showIntro = d.showIntro;
    });
    col->addWidget(sectionLabel("TIMELINE"));
    if (s.advanced) {
        col->addWidget(sliderRow("Clip row height", s.timelineLayerRow, 22.0f, 80.0f, "%.0f px"));
        col->addWidget(sliderRow("Keyframe row height", s.timelinePropertyRow, 20.0f, 60.0f, "%.0f px"));
        col->addWidget(sliderRow("Keyframe size", s.keyframeSize, 3.0f, 10.0f, "%.1f px", 0.5f));
    }
    auto* presets = new QHBoxLayout;
    auto preset = [&](const char* label, float row, float prop, float key) {
        auto* b = new QPushButton(label);
        QObject::connect(b, &QPushButton::clicked, b, [row, prop, key] {
            Settings& st = settings();
            st.timelineLayerRow = row, st.timelinePropertyRow = prop, st.keyframeSize = key;
            changed(true);
        });
        presets->addWidget(b);
    };
    preset("Compact", 26, 22, 4);
    preset("Comfortable", 36, 30, 5);
    preset("Large", 48, 38, 6.5f);
    presets->addStretch();
    col->addLayout(presets);

    col->addSpacing(8);
    col->addWidget(sectionLabel("TEXT"));
    static float percent;
    percent = s.textScale * 100.0f;
    QWidget* text = sliderRow("Text size", percent, 80.0f, 150.0f, "%.0f%%", 5.0f, "Scales every label in the interface");
    // The slider edits `percent`; mirror it into the setting.
    for (QSlider* sl : text->findChildren<QSlider*>())
        QObject::connect(sl, &QSlider::valueChanged, sl, [](int) {
            settings().textScale = percent / 100.0f;
            changed();
        });
    col->addWidget(text);
    col->addWidget(hintLabel("Colors, corner rounding and themes are in the Theme page."));

    col->addSpacing(8);
    col->addWidget(sectionLabel("FONT"));
    auto* fontRow = new QHBoxLayout;
    auto* fl = new QLabel("Interface font");
    fl->setMinimumWidth(170);
    fontRow->addWidget(fl);
    auto* font = new QComboBox;
    font->setEditable(true);
    font->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& f : Fonts::families()) font->addItem(QString::fromStdString(f));
    font->setCurrentText(QString::fromStdString(s.uiFont));
    font->completer()->setFilterMode(Qt::MatchContains);
    font->completer()->setCompletionMode(QCompleter::PopupCompletion);
    font->setFixedWidth(240);
    QObject::connect(font, &QComboBox::activated, font, [font](int) {
        const std::string f = font->currentText().toStdString();
        if (!Fonts::hasFamily(f)) return;
        settings().uiFont = f;
        changed();
    });
    fontRow->addWidget(font);
    fontRow->addStretch();
    col->addLayout(fontRow);
    col->addWidget(hintLabel("Inter ships with bemotion. Other fonts come from your system."));

    col->addSpacing(8);
    col->addWidget(sectionLabel("STARTUP"));
    auto* intro = new QCheckBox("Play the intro animation");
    intro->setChecked(s.showIntro);
    QObject::connect(intro, &QCheckBox::toggled, intro, [](bool on) {
        settings().showIntro = on;
        saveSettings();
    });
    col->addWidget(intro);
    col->addWidget(hintLabel("The bemotion logo animation shown when the app opens (a click or any key skips it)."));
    return page;
}

QWidget* editorPage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, "Editor", [] {
        Settings& st = settings();
        const Settings d;
        st.snapDistance = d.snapDistance, st.arrowFrames = d.arrowFrames, st.bigArrowFrames = d.bigArrowFrames;
        st.maxDuration = d.maxDuration, st.autosaveMinutes = d.autosaveMinutes;
    });
    col->addWidget(sectionLabel("NEW PROJECTS"));
    col->addWidget(lengthLimitRow(s.maxDuration, [](double v) {
        settings().maxDuration = v;
        saveSettings();
    }));
    col->addWidget(hintLabel("A project grows with its content. With a limit it stops there: handy for 15 s ads, "
                             "60 s shorts and the like. Each new project can change it."));
    col->addSpacing(8);
    col->addWidget(sectionLabel("SAVING"));
    auto* autosave = new QCheckBox("Save open projects automatically");
    autosave->setChecked(s.autosaveMinutes > 0);
    col->addWidget(autosave);
    static int minutes;
    minutes = s.autosaveMinutes > 0 ? s.autosaveMinutes : 5;
    QWidget* every = intSliderRow("Every (minutes)", minutes, 1, 30);
    every->setEnabled(s.autosaveMinutes > 0);
    for (QSlider* sl : every->findChildren<QSlider*>())
        QObject::connect(sl, &QSlider::valueChanged, sl, [](int) {
            settings().autosaveMinutes = minutes;
            saveSettings();
        });
    QObject::connect(autosave, &QCheckBox::toggled, every, [every](bool on) {
        settings().autosaveMinutes = on ? minutes : 0;
        every->setEnabled(on);
        saveSettings();
    });
    col->addWidget(every);
    col->addWidget(hintLabel("Unsaved changes are also kept aside every few seconds: if bemotion closes unexpectedly, "
                             "it offers them back the next time it opens."));
    if (!s.advanced) return page;
    col->addSpacing(8);
    col->addWidget(sectionLabel("PLAYHEAD"));
    col->addWidget(intSliderRow("Frames per arrow press", s.arrowFrames, 1, 60));
    col->addWidget(intSliderRow("Frames per big step", s.bigArrowFrames, 1, 300));
    col->addWidget(hintLabel("Arrow keys step the playhead; the big step uses Shift + arrows by default (see Shortcuts)."));
    col->addSpacing(8);
    col->addWidget(sectionLabel("SNAPPING"));
    col->addWidget(sliderRow("Snap distance", s.snapDistance, 2.0f, 30.0f, "%.0f px"));
    col->addWidget(hintLabel("How close (on screen) an object, clip, keyframe or the playhead must get before it snaps. "
                             "Toggle snapping with S or the magnet button; hold Ctrl while dragging to ignore it."));
    return page;
}

// Click it, then press the new keys (Esc cancels).
class ShortcutButton : public QPushButton {
public:
    explicit ShortcutButton(Action action) : action_(action) {
        setMinimumWidth(170);
        refreshText();
        connect(this, &QPushButton::clicked, this, [this] {
            capturing_ = true;
            setText("Press keys...");
            setProperty("accent", true);
            style()->unpolish(this);
            style()->polish(this);
            grabKeyboard();
        });
    }

protected:
    bool event(QEvent* e) override {
        // While capturing, every key (shortcuts included) comes here.
        if (capturing_ && e->type() == QEvent::ShortcutOverride) {
            e->accept();
            return true;
        }
        return QPushButton::event(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (!capturing_) {
            QPushButton::keyPressEvent(e);
            return;
        }
        const int key = e->key();
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_unknown) return;
        if (key != Qt::Key_Escape || e->modifiers() != Qt::NoModifier) {
            setChord(action_, e->keyCombination().toCombined());
            saveSettings();
            emit AppSignals::get().shortcutsChanged();
        }
        stop();
        if (g_window) QTimer::singleShot(0, g_window, [] {
                if (g_window) g_window->rebuild(); // conflicts may have changed
            });
    }
    void focusOutEvent(QFocusEvent* e) override {
        if (capturing_) stop();
        QPushButton::focusOutEvent(e);
    }

private:
    void stop() {
        capturing_ = false;
        releaseKeyboard();
        setProperty("accent", false);
        style()->unpolish(this);
        style()->polish(this);
        refreshText();
    }
    void refreshText() {
        const std::string name = chordName(chordOf(action_));
        setText(name.empty() ? "(none)" : QString::fromStdString(name));
    }
    Action action_;
    bool capturing_ = false;
};

QWidget* shortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, "Shortcuts", [] {
        loadKeymap("{}");
        emit AppSignals::get().shortcutsChanged();
    });
    col->addWidget(hintLabel("Click a shortcut and press the new keys (Esc cancels)."));
    const char* group = nullptr;
    QGridLayout* grid = nullptr;
    int row = 0;
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        const Action a = static_cast<Action>(i);
        const ActionInfo& info = actionInfo(a);
        if (!group || std::strcmp(group, info.group) != 0) {
            group = info.group;
            std::string upper = group;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            col->addSpacing(6);
            col->addWidget(sectionLabel(QString::fromStdString(upper)));
            grid = new QGridLayout;
            grid->setColumnMinimumWidth(0, 200);
            grid->setColumnStretch(2, 1);
            grid->setVerticalSpacing(4);
            col->addLayout(grid);
            row = 0;
        }
        grid->addWidget(new QLabel(info.label), row, 0);
        grid->addWidget(new ShortcutButton(a), row, 1);
        auto* tools = new QHBoxLayout;
        tools->setSpacing(4);
        if (chordOf(a) != info.defaultChord) {
            auto* reset = new IconButton(Icon::Reset, "Back to the default", 24);
            QObject::connect(reset, &QAbstractButton::clicked, reset, [a] {
                resetChord(a);
                saveSettings();
                emit AppSignals::get().shortcutsChanged();
                if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
            });
            tools->addWidget(reset);
        }
        if (chordOf(a)) {
            auto* clear = new IconButton(Icon::Trash, "Remove this shortcut", 24);
            QObject::connect(clear, &QAbstractButton::clicked, clear, [a] {
                setChord(a, 0);
                saveSettings();
                emit AppSignals::get().shortcutsChanged();
                if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
            });
            tools->addWidget(clear);
            const Action other = actionWithChord(chordOf(a), a);
            if (other != Action::Count) tools->addWidget(paragraph(QString("also %1").arg(actionInfo(other).label), theme::kDanger));
        }
        tools->addStretch();
        grid->addLayout(tools, row, 2);
        ++row;
    }
    return page;
}

// Cards read theme colors while painting; controls keep their normal focus and keyboard behavior.
class PluginCard : public QWidget {
public:
    PluginCard() {
        setMinimumHeight(220);
        connect(&AppSignals::get(), &AppSignals::appearanceChanged, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        d.rectFilled({0.5f, 0.5f}, {width() - 0.5f, height() - 0.5f}, theme::kBgRaised, 8.0f);
        d.rect({0.5f, 0.5f}, {width() - 0.5f, height() - 0.5f}, theme::kBorder, 8.0f);
    }
};

class PluginGrid : public QWidget {
public:
    PluginGrid() {
        grid_ = new QGridLayout(this);
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setSpacing(12);
    }
    void addCard(QWidget* card) {
        cards_.push_back(card);
        grid_->addWidget(card, static_cast<int>(cards_.size()) - 1, 0);
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        const int columns = std::clamp((width() + 12) / 252, 1, 3);
        if (columns == columns_) return;
        for (int c = 0; c < columns_; ++c) grid_->setColumnStretch(c, 0);
        columns_ = columns;
        for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
            grid_->removeWidget(cards_[i]);
            grid_->addWidget(cards_[i], i / columns, i % columns);
        }
        for (int c = 0; c < columns; ++c) grid_->setColumnStretch(c, 1);
    }

private:
    QGridLayout* grid_;
    std::vector<QWidget*> cards_;
    int columns_ = 1;
};

QLabel* pluginText(const QString& text, Tone tone = Tone::TextDim) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTone(label, tone);
    return label;
}

QWidget* pluginsPage() {
    PluginHost& host = PluginHost::get();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, "Plugins", [] { enableAllPlugins(); });
    auto* buttons = new QHBoxLayout;
    auto* folder = new QPushButton("Open plugins folder");
    QObject::connect(folder, &QPushButton::clicked, folder, [] { platform::revealInFileManager(PluginHost::userDir()); });
    auto* reload = new QPushButton("Reload");
    QObject::connect(reload, &QPushButton::clicked, reload, [] {
        PluginHost::get().requestReload();
        emit AppSignals::get().pluginsChanged();
        if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
    });
    auto* docs = new QPushButton("Write a plugin");
    QObject::connect(docs, &QPushButton::clicked, docs, [] { platform::openWithDefaultApp(kPluginDocsUrl); });
    for (QPushButton* b : {folder, reload, docs}) buttons->addWidget(b);
    buttons->addStretch();
    col->addLayout(buttons);
    if (!g_message.empty()) col->addWidget(pluginText(QString::fromStdString(g_message)));
    col->addSpacing(6);

    auto* cards = new PluginGrid;
    auto* add = new PluginCard;
    auto* addCol = new QVBoxLayout(add);
    addCol->setContentsMargins(16, 16, 16, 16);
    addCol->setSpacing(10);
    auto* addTitle = pageTitle("Add a plugin");
    setTone(addTitle, Tone::Text);
    addCol->addWidget(addTitle);
    addCol->addWidget(pluginText("Add objects and workflows to your editor. Choose the plugin.json file from a plugin folder."));
    addCol->addStretch();
    auto* install = accentButton("Install plugin...");
    QObject::connect(install, &QPushButton::clicked, install, [] {
        if (auto manifest = platform::openFileDialog("Install Plugin (choose its plugin.json)", "bemotion plugin", "json")) {
            std::string error;
            g_message = PluginHost::get().install(*manifest, &error) ? "Plugin installed." : "Could not install: " + error;
            emit AppSignals::get().pluginsChanged();
            if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
        }
    });
    addCol->addWidget(install, 0, Qt::AlignLeft);
    cards->addCard(add);

    for (const Plugin& p : host.plugins()) {
        auto* card = new PluginCard;
        auto* body = new QVBoxLayout(card);
        body->setContentsMargins(16, 16, 16, 16);
        body->setSpacing(8);
        auto* head = new QHBoxLayout;
        auto* name = pageTitle(QString::fromStdString(p.name));
        name->setWordWrap(true);
        setTone(name, Tone::Text);
        head->addWidget(name, 1);
        auto* enabled = new QCheckBox;
        enabled->setChecked(p.enabled);
        enabled->setEnabled(!(p.may("microphone") && media::Recorder::get().recording()));
        enabled->setAccessibleName(QString::fromStdString("Enable " + p.name));
        enabled->setToolTip("Enable or disable this plugin");
        head->addWidget(enabled, 0, Qt::AlignTop);
        body->addLayout(head);
        body->addWidget(pluginText(QString::fromStdString("v" + p.version + (p.author.empty() ? "" : " by " + p.author)), Tone::TextFaint));
        if (!p.description.empty()) body->addWidget(pluginText(QString::fromStdString(p.description), Tone::Text));
        if (!p.permissions.empty()) {
            QStringList permissions;
            for (const std::string& permission : p.permissions) {
                if (permission == "edit") permissions << "Edit project";
                else if (permission == "media") permissions << "Import media";
                else if (permission == "files") permissions << "Read chosen files";
                else if (permission == "microphone") permissions << "Microphone";
                else permissions << QString::fromStdString(permission);
            }
            body->addWidget(pluginText("Permissions: " + permissions.join(", "), Tone::TextFaint));
        }
        if (!p.error.empty()) {
            body->addWidget(pluginText(QString::fromStdString("Failed to load: " + p.error), Tone::Danger));
        } else {
            std::string objects;
            for (const PluginObjectType* t : host.objectTypes())
                if (t->pluginId == p.id) objects += (objects.empty() ? "" : ", ") + t->name;
            if (!objects.empty()) body->addWidget(pluginText(QString::fromStdString("Objects: " + objects)));
        }
        if (!p.drawError.empty()) body->addWidget(pluginText(QString::fromStdString("Last error: " + p.drawError), Tone::Danger));
        body->addStretch();
        auto* foot = new QHBoxLayout;
        auto* status = pluginText(p.enabled ? (p.error.empty() ? "Enabled" : "Failed to load") : "Disabled",
                                  p.enabled ? (p.error.empty() ? Tone::Accent : Tone::Danger) : Tone::TextFaint);
        foot->addWidget(status);
        if (p.builtin) foot->addWidget(pluginText("Built-in", Tone::TextFaint));
        foot->addStretch();
        body->addLayout(foot);
        auto* actions = new QHBoxLayout;
        auto* show = new QPushButton("Show folder");
        show->setToolTip(p.builtin ? "Open this bundled plugin's folder" : "Open the installed plugin's folder to manage its files");
        const std::filesystem::path dir = p.dir;
        QObject::connect(show, &QPushButton::clicked, show, [dir] { platform::revealInFileManager(dir); });
        actions->addWidget(show);
        const std::string id = p.id;
        if (!p.builtin) {
            auto* remove = new QPushButton("Remove");
            setTone(remove, Tone::Danger);
            remove->setToolTip("Move this installed plugin to Trash");
            QObject::connect(remove, &QPushButton::clicked, remove, [id, name = QString::fromStdString(p.name)] {
                const QString question = QString("Move %name% to Trash?").replace("%name%", name);
                if (QMessageBox::question(g_window, "Remove plugin", question, QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No) != QMessageBox::Yes) return;
                std::string error;
                g_message = PluginHost::get().uninstall(id, &error) ? "Plugin moved to Trash." : "Could not remove: " + error;
                emit AppSignals::get().pluginsChanged();
                if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
            });
            actions->addWidget(remove);
        }
        actions->addStretch();
        body->addLayout(actions);
        QObject::connect(enabled, &QCheckBox::toggled, enabled, [id, status](bool on) {
            PluginHost::get().setEnabled(id, on);
            status->setText(on ? "Enabled" : "Disabled");
            setTone(status, on ? Tone::Accent : Tone::TextFaint);
            emit AppSignals::get().pluginsChanged();
            if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        if (!p.log.empty()) {
            auto* toggle = new QPushButton(QString("Log (%1)").arg(p.log.size()));
            toggle->setCheckable(true);
            auto* log = new QPlainTextEdit;
            log->setReadOnly(true);
            log->setMaximumHeight(140);
            QString text;
            for (const auto& l : p.log) text += QString::fromStdString(l) + "\n";
            log->setPlainText(text);
            auto* tr = new QHBoxLayout;
            tr->addWidget(toggle);
            tr->addStretch();
            body->addLayout(tr);
            body->addWidget(log);
            log->hide();
            QObject::connect(toggle, &QPushButton::toggled, log, &QWidget::setVisible);
        }
        cards->addCard(card);
    }
    col->addWidget(cards);
    return page;
}

} // namespace

void openSettings(SettingsTab tab) {
    if (!g_window) {
        auto* footer = new QWidget;
        auto* footerCol = new QVBoxLayout(footer);
        footerCol->setContentsMargins(0, 0, 0, 0);
        footerCol->setSpacing(6);
        auto* advanced = new QCheckBox("Advanced Options");
        advanced->setToolTip("Every page shows all its options (colors, sizes, fine-tuning)");
        advanced->setChecked(settings().advanced);
        QObject::connect(advanced, &QCheckBox::toggled, advanced, [](bool on) {
            settings().advanced = on;
            saveSettings();
            if (g_window) QTimer::singleShot(0, g_window, [] {
                    if (g_window) g_window->rebuild();
                });
        });
        footerCol->addWidget(advanced);
        auto* resetAll = new QPushButton("Reset All");
        resetAll->setToolTip("Restore every setting to its default");
        footerCol->addWidget(resetAll);
        g_window = new PagedWindow("Settings",
                                   {{"Theme", Icon::Palette, themePage},
                                    {"Interface", Icon::Sliders, interfacePage},
                                    {"Editor", Icon::Magnet, editorPage},
                                    {"Shortcuts", Icon::Text, shortcutsPage},
                                    {"Plugins", Icon::Plugin, pluginsPage}},
                                   footer);
        g_window->resize(780, 600);
        QObject::connect(&AppSignals::get(), &AppSignals::pluginsChanged, g_window, [] {
            if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        QObject::connect(resetAll, &QPushButton::clicked, g_window, [] {
            if (QMessageBox::question(g_window, "Reset All", "Reset every setting to its default?") != QMessageBox::Yes) return;
            const bool keepAdvanced = settings().advanced; // a view choice, not a preference
            settings() = Settings{};
            settings().advanced = keepAdvanced;
            loadKeymap("{}");
            enableAllPlugins();
            emit AppSignals::get().shortcutsChanged();
            emit AppSignals::get().toolbarChanged();
            changed(true);
        });
    }
    g_window->showPage(static_cast<int>(tab));
    g_window->present();
}

} // namespace bemotion::ui
