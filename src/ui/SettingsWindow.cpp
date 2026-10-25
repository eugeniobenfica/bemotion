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
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <map>
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
QWidget* sliderControl(float& value, float min, float max, const char* format, float step = 1.0f,
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

QWidget* spinControl(int& value, int min, int max, const QString& suffix) {
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

Switch* switchControl(bool on, std::function<void(bool)> toggled) {
    auto* sw = new Switch;
    sw->setChecked(on);
    QObject::connect(sw, &QAbstractButton::toggled, sw, std::move(toggled));
    return sw;
}

QWidget* buttonRow(std::initializer_list<QWidget*> buttons) {
    auto* box = new QWidget;
    auto* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    for (QWidget* b : buttons) h->addWidget(b);
    return box;
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
    auto* b = new QPushButton(makeIcon(Icon::Reset, theme::kTextDim, 16), "Reset Page");
    b->setToolTip("Restore this page's defaults");
    QObject::connect(b, &QPushButton::clicked, b, [reset] {
        reset();
        changed(true);
    });
    row->addWidget(b);
    col->addLayout(row);
    col->addSpacing(2);
}

void enableAllPlugins() {
    for (const Plugin& p : PluginHost::get().plugins()) PluginHost::get().setEnabled(p.id, true);
    settings().disabledPlugins.clear();
}

// Every color a theme can set, by group. The optional ones show what they are worked out to.
void addColorSections(QVBoxLayout* col, Settings& s) {
    struct Entry {
        const char* label;
        Rgba theme::Palette::*field;
    };
    struct Group {
        const char* title;
        std::vector<Entry> entries;
    };
    static const Group kGroups[] = {
        {"Surfaces",
         {{"Background", &theme::Palette::bgDeep},
          {"Panels", &theme::Palette::bgPanel},
          {"Menu bar and tabs", &theme::Palette::header},
          {"Cards and tiles", &theme::Palette::bgRaised},
          {"Hover", &theme::Palette::bgHover},
          {"Borders", &theme::Palette::border},
          {"Canvas surround", &theme::Palette::canvas},
          {"Menus and tooltips", &theme::Palette::menu}}},
        {"Controls",
         {{"Fields", &theme::Palette::field},
          {"Field outline", &theme::Palette::fieldBorder},
          {"Buttons", &theme::Palette::button},
          {"Button outline", &theme::Palette::buttonBorder},
          {"Focus outline", &theme::Palette::focus},
          {"Selection", &theme::Palette::selection},
          {"Text on accent", &theme::Palette::onAccent}}},
        {"Text",
         {{"Text", &theme::Palette::text}, {"Secondary text", &theme::Palette::textDim}, {"Faint text", &theme::Palette::textFaint}}},
        {"Status",
         {{"Danger", &theme::Palette::danger}, {"Warning", &theme::Palette::warning}, {"Success", &theme::Palette::success}}},
        {"Editing",
         {{"Playhead", &theme::Palette::playhead},
          {"Keyframes", &theme::Palette::keyframe},
          {"Snap lines", &theme::Palette::snap},
          {"Guides", &theme::Palette::guide}}},
    };
    const theme::Palette shown = theme::resolved(s.palette);
    for (const Group& g : kGroups) {
        SettingsCard* card = addSection(col, g.title);
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(10);
        int i = 0;
        for (const Entry& e : g.entries) {
            auto* row = new QHBoxLayout;
            row->setSpacing(10);
            auto* button = new ColorButton;
            const Rgba c = shown.*(e.field);
            button->setColor(Color{red(c) / 255.0f, green(c) / 255.0f, blue(c) / 255.0f, alpha(c) / 255.0f});
            auto field = e.field;
            QObject::connect(button, &ColorButton::colorChanged, button, [field](Color v) {
                Settings& st = settings();
                auto byte = [](float x) { return static_cast<int>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255)); };
                // 0 means "worked out": a color picked fully transparent black stays just visible.
                st.palette.*field = std::max<Rgba>(1, rgba(byte(v.r), byte(v.g), byte(v.b), byte(v.a)));
                st.customPalette = true;
                changed();
            });
            QObject::connect(button, &ColorButton::editFinished, button, [] { changed(true); });
            row->addWidget(button);
            row->addWidget(new QLabel(e.label), 1);
            grid->addLayout(row, i / 2, i % 2);
            ++i;
        }
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        card->addLayout(grid);
    }
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
    SettingsCard* themes = addSection(col, "Theme");
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
    themes->addLayout(cards);
    auto* import = new QPushButton("Import...");
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
    auto* folder = new QPushButton("Open Folder");
    QObject::connect(folder, &QPushButton::clicked, folder, [] { platform::revealInFileManager(ThemeLibrary::userDir()); });
    themes->addRow("More themes", "Import a theme someone shared (a .json file), or open the folder your themes are kept in.",
                   buttonRow({import, folder}));
    if (!g_message.empty()) col->addWidget(hintLabel(QString::fromStdString(g_message)));

    SettingsCard* accentCard = addSection(col, "Accent color");
    const theme::Palette base = [&] {
        Settings copy = s;
        copy.customAccent = false;
        return basePalette(copy);
    }();
    auto* accents = new QHBoxLayout;
    accents->setSpacing(8);
    auto* themeAccent = new Swatch(base.accent, !s.customAccent, "The theme's own");
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
    accentCard->addLayout(accents);

    if (!s.advanced) {
        col->addSpacing(6);
        col->addWidget(hintLabel("Every color, the corners, outlines and control sizes, and saving your own theme are in "
                                 "Advanced Options (bottom left)."));
        return page;
    }

    if (!s.customPalette) s.palette = effectivePalette(); // start from what is on screen
    col->addSpacing(4);
    col->addWidget(hintLabel("Changing anything below turns the current theme into your own palette; Save as Theme keeps it."));
    addColorSections(col, s);

    SettingsCard* shape = addSection(col, "Shape and size");
    shape->addRow("Corner rounding", "Buttons, fields, cards and menus", sliderControl(s.palette.rounding, 0.0f, 12.0f, "%.0f px"));
    shape->addRow("Outline width", "The outline of fields and buttons (0: none)", sliderControl(s.palette.borderWidth, 0.0f, 2.0f, "%.0f px"));
    shape->addRow("Control height", "Buttons, fields and drop-downs in windows like this one",
                  sliderControl(s.palette.controlHeight, 22.0f, 36.0f, "%.0f px"));
    shape->addRow("Light theme", "Overlays on the picture (outlines, handles) are drawn dark instead of light",
                  switchControl(s.palette.light, [](bool on) {
                      settings().palette.light = on;
                      settings().customPalette = true;
                      changed(true);
                  }));

    SettingsCard* share = addSection(col, "Save and share");
    auto* name = new QLineEdit;
    name->setPlaceholderText("Theme name");
    name->setFixedWidth(180);
    auto* save = accentButton("Save as Theme");
    QObject::connect(save, &QPushButton::clicked, save, [name] {
        if (name->text().trimmed().isEmpty()) {
            name->setFocus();
            return;
        }
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
    share->addRow("Save as a theme", "Keeps these colors under a name, next to Dark, Light and Midnight", buttonRow({name, save}));
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
    share->addRow("Export", "A .json file anyone can import", exportButton);
    if (s.customPalette) {
        auto* reset = new QPushButton("Discard Changes");
        QObject::connect(reset, &QPushButton::clicked, reset, [] {
            settings().customPalette = false;
            changed(true);
        });
        share->addRow("Back to the theme", "Drops the colors changed here", reset);
    }
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
    SettingsCard* timeline = addSection(col, "Timeline");
    auto* presets = new QHBoxLayout;
    auto preset = [&](const char* label, float row, float prop, float key) {
        const bool selected = std::fabs(s.timelineLayerRow - row) < 0.1f && std::fabs(s.timelinePropertyRow - prop) < 0.1f;
        auto* b = new TimelineSizeCard(label, row, prop, key, selected);
        QObject::connect(b, &QPushButton::clicked, b, [row, prop, key] {
            Settings& st = settings();
            st.timelineLayerRow = row, st.timelinePropertyRow = prop, st.keyframeSize = key;
            changed(true);
        });
        presets->addWidget(b);
    };
    preset("Compact", 34, 28, 5);
    preset("Comfortable", 46, 38, 6);
    preset("Large", 60, 50, 7.5f);
    presets->addStretch();
    timeline->addLayout(presets);
    if (s.advanced) {
        timeline->addRow("Clip row height", "", sliderControl(s.timelineLayerRow, 22.0f, 80.0f, "%.0f px"));
        timeline->addRow("Keyframe row height", "", sliderControl(s.timelinePropertyRow, 20.0f, 60.0f, "%.0f px"));
        timeline->addRow("Keyframe size", "", sliderControl(s.keyframeSize, 3.0f, 10.0f, "%.1f px", 0.5f));
    }

    SettingsCard* text = addSection(col, "Text");
    static float percent;
    percent = s.textScale * 100.0f;
    text->addRow("Text size", "Every label in the interface",
                 sliderControl(percent, 80.0f, 150.0f, "%.0f%%", 5.0f, [] { settings().textScale = percent / 100.0f; }));
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
    text->addRow("Interface font", "Inter ships with bemotion; the others come from your system", font);

    SettingsCard* startup = addSection(col, "Startup");
    startup->addRow("Intro animation", "The bemotion logo when the app opens (a click or any key skips it)",
                    switchControl(s.showIntro, [](bool on) {
                        settings().showIntro = on;
                        saveSettings();
                    }));

    SettingsCard* reset = addSection(col, "Start over");
    auto* resetInterface = new QPushButton("Reset Interface...");
    QObject::connect(resetInterface, &QPushButton::clicked, resetInterface, [] {
        if (QMessageBox::question(g_window, "Reset Interface", "Restore the entire interface to its defaults?") != QMessageBox::Yes) return;
        Settings& s = settings();
        const Settings d;
        s.advanced = d.advanced;
        s.theme = d.theme;
        s.customAccent = d.customAccent;
        s.accent = d.accent;
        s.customPalette = d.customPalette;
        s.palette = d.palette;
        s.timelineLayerRow = d.timelineLayerRow;
        s.timelinePropertyRow = d.timelinePropertyRow;
        s.keyframeSize = d.keyframeSize;
        s.textScale = d.textScale;
        s.uiFont = d.uiFont;
        s.showIntro = d.showIntro;
        s.showRulers = d.showRulers;
        s.previewQuality = d.previewQuality;
        s.toolbar = d.toolbar;
        s.toolbarInViewport = d.toolbarInViewport;
        s.toolbarInWindow = d.toolbarInWindow;
        s.toolbarX = d.toolbarX;
        s.toolbarY = d.toolbarY;
        loadKeymap("{}");
        std::error_code ec;
        const auto layoutFile = platform::appDataDir() / "layout.ads";
        std::filesystem::remove(layoutFile, ec);
        if (ec) {
            ec.clear(); // OneDrive may hold the file briefly while syncing.
            std::filesystem::remove(layoutFile, ec);
        }
        emit AppSignals::get().shortcutsChanged();
        emit AppSignals::get().toolbarChanged();
        emit AppSignals::get().interfaceResetRequested();
        changed(true);
        if (ec) QMessageBox::warning(g_window, "Reset Interface", "The saved panel layout could not be removed. Close the editor and try again.");
    });
    reset->addRow("Reset the interface", "Theme, toolbar, shortcuts, preview and panel layout go back to their defaults. "
                                         "Projects, media, plugins and recent files stay.",
                  resetInterface);
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
        st.autosaveMinutes = d.autosaveMinutes;
    });
    SettingsCard* saving = addSection(col, "Saving");
    saving->addRow("Save after every change",
                   "A project saves itself a moment after each change, once it has a file (the first Save picks it)",
                   switchControl(s.autosaveMinutes > 0, [](bool on) {
                       settings().autosaveMinutes = on ? 5 : 0; // any value but 0 is on (the setting's older name)
                       saveSettings();
                   }));
    saving->addRow("Crash recovery", "Unsaved changes are always kept aside: if bemotion closes unexpectedly, it offers them "
                                     "back the next time it opens",
                   nullptr);
    if (!s.advanced) {
        col->addSpacing(6);
        col->addWidget(hintLabel("How far the arrow keys step and how close things snap are in Advanced Options (bottom left)."));
        return page;
    }
    SettingsCard* playhead = addSection(col, "Playhead");
    playhead->addRow("Arrow keys step", "Left / Right move the playhead this many frames", spinControl(s.arrowFrames, 1, 60, " frames"));
    playhead->addRow("Big step", "With Shift by default (see Shortcuts)", spinControl(s.bigArrowFrames, 1, 300, " frames"));
    SettingsCard* snapping = addSection(col, "Snapping");
    snapping->addRow("Snap distance",
                     "How close (on screen) an object, clip, keyframe or the playhead gets before it snaps. S or the magnet "
                     "turns snapping on and off; hold Ctrl while dragging to ignore it.",
                     sliderControl(s.snapDistance, 2.0f, 30.0f, "%.0f px"));
    return page;
}

// Click it, then press the new keys (Esc cancels).
class ShortcutButton : public QPushButton {
public:
    explicit ShortcutButton(Action action) : action_(action) {
        setFixedWidth(160);
        setProperty("keycap", true);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Click, then press the new keys (Esc cancels)");
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
        setText(name.empty() ? "None" : QString::fromStdString(name));
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
    // A search over names and keys (kept while the page rebuilds after a change).
    static QString filter;
    auto* search = new QLineEdit(filter);
    search->setPlaceholderText("Search shortcuts (a name or keys, e.g. \"Ctrl+S\")");
    search->setClearButtonEnabled(true);
    col->addWidget(search);
    struct Row {
        QWidget* widget;
        SettingsCard* card;
        QString words;
    };
    auto rows = std::make_shared<std::vector<Row>>();
    std::vector<std::pair<SettingsCard*, QLabel*>> cards; // and the section title above it
    const char* group = nullptr;
    SettingsCard* card = nullptr;
    auto refresh = std::make_shared<std::function<void()>>();
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        const Action a = static_cast<Action>(i);
        const ActionInfo& info = actionInfo(a);
        if (!group || std::strcmp(group, info.group) != 0) {
            group = info.group;
            col->addSpacing(8);
            auto* title = sectionLabel(QString::fromUtf8(group));
            col->addWidget(title);
            card = new SettingsCard;
            col->addWidget(card);
            cards.emplace_back(card, title);
        }
        auto* tools = new QWidget;
        auto* h = new QHBoxLayout(tools);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        const Action other = chordOf(a) ? actionWithChord(chordOf(a), a) : Action::Count;
        if (other != Action::Count) {
            auto* clash = new QLabel(QString("Also %1").arg(actionInfo(other).label));
            setTone(clash, Tone::Danger);
            h->addWidget(clash);
        }
        h->addWidget(new ShortcutButton(a));
        auto* reset = new IconButton(Icon::Reset, "Back to the default", 26);
        reset->setVisible(chordOf(a) != info.defaultChord);
        QObject::connect(reset, &QAbstractButton::clicked, reset, [a] {
            resetChord(a);
            saveSettings();
            emit AppSignals::get().shortcutsChanged();
            if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        auto* clear = new IconButton(Icon::Trash, "Remove this shortcut", 26);
        clear->setEnabled(chordOf(a) != 0);
        QObject::connect(clear, &QAbstractButton::clicked, clear, [a] {
            setChord(a, 0);
            saveSettings();
            emit AppSignals::get().shortcutsChanged();
            if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        // The reset button keeps its place when hidden: the key fields stay in one column.
        auto* resetSlot = new QWidget;
        resetSlot->setFixedSize(26, 26);
        auto* slot = new QHBoxLayout(resetSlot);
        slot->setContentsMargins(0, 0, 0, 0);
        slot->addWidget(reset);
        h->addWidget(resetSlot);
        h->addWidget(clear);
        QWidget* row = card->addRow(QString::fromUtf8(info.label), "", tools);
        row->setMinimumHeight(38);
        row->layout()->setContentsMargins(16, 5, 16, 5); // a long list: compact rows
        rows->push_back({row, card, (QString::fromUtf8(info.label) + " " + QString::fromStdString(chordName(chordOf(a)))).toLower()});
    }
    *refresh = [rows, cards] {
        const QString f = filter.trimmed().toLower();
        std::map<SettingsCard*, int> shown;
        for (const Row& r : *rows) {
            const bool show = f.isEmpty() || r.words.contains(f);
            r.widget->setVisible(show);
            shown[r.card] += show;
        }
        for (const auto& [c, title] : cards) {
            c->setVisible(shown[c] > 0);
            title->setVisible(shown[c] > 0);
            c->update();
        }
    };
    (*refresh)();
    QObject::connect(search, &QLineEdit::textChanged, search, [refresh](const QString& t) {
        filter = t;
        (*refresh)();
    });
    return page;
}

// --- plugins -----------------------------------------------------------------------------

// A plugin's picture: its first object's (or command's) icon on a tile.
class PluginIcon : public QWidget {
public:
    PluginIcon(Icon icon, bool enabled) : icon_(icon), enabled_(enabled) { setFixedSize(40, 40); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        d.rectFilled(Pt(0, 0), Pt(width(), height()), enabled_ ? theme::accent(38) : theme::kField, theme::current().rounding + 3.0f);
        drawIcon(d, icon_, Pt(width() * 0.5f, height() * 0.5f), 22.0f, enabled_ ? theme::kAccent : theme::kTextFaint);
    }

private:
    Icon icon_;
    bool enabled_;
};

// A small rounded tag (permissions, "Built-in").
class Chip : public QWidget {
public:
    Chip(const QString& text, Tone tone = Tone::TextDim) : text_(text), tone_(tone) {
        const int w = fontMetrics().horizontalAdvance(text_) + 16;
        setFixedSize(w, fontMetrics().height() + 6);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const Rgba ink = tone_ == Tone::Accent ? theme::kAccent : tone_ == Tone::Danger ? theme::kDanger : theme::kTextDim;
        d.rectFilled(Pt(0, 0), Pt(width(), height()), tone_ == Tone::Accent ? theme::accent(30) : theme::kField, height() * 0.5f);
        d.text(Pt(8, (height() - d.textHeight()) * 0.5f), ink, text_);
    }

private:
    QString text_;
    Tone tone_;
};

QLabel* pluginText(const QString& text, Tone tone = Tone::TextDim) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTone(label, tone);
    return label;
}

QString permissionLabel(const std::string& permission) {
    if (permission == "edit") return "Edits the project";
    if (permission == "media") return "Imports media";
    if (permission == "files") return "Reads files you choose";
    if (permission == "microphone") return "Uses the microphone";
    return QString::fromStdString(permission);
}

// One plugin, as a row of its card: picture, name and what it does, what it adds and may do,
// and its switch; its folder, removal and log under it.
QWidget* pluginRow(const Plugin& p) {
    PluginHost& host = PluginHost::get();
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 14, 16, 14);
    h->setSpacing(14);
    std::string objects;
    Icon icon = Icon::Plugin;
    bool iconSet = false;
    for (const PluginObjectType* t : host.objectTypes())
        if (t->pluginId == p.id) {
            objects += (objects.empty() ? "" : ", ") + t->name;
            if (!iconSet) icon = iconForPlugin(t->icon), iconSet = true;
        }
    std::string commands;
    for (const PluginCommand* c : host.commands())
        if (c->pluginId == p.id) {
            commands += (commands.empty() ? "" : ", ") + c->name;
            if (!iconSet) icon = iconForPlugin(c->icon), iconSet = true;
        }
    const bool ok = p.error.empty();
    h->addWidget(new PluginIcon(icon, p.enabled && ok), 0, Qt::AlignTop);

    auto* body = new QVBoxLayout;
    body->setSpacing(4);
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* name = new QLabel(QString::fromStdString(p.name));
    QFont f = name->font();
    f.setWeight(QFont::DemiBold);
    name->setFont(f);
    head->addWidget(name);
    auto* version = new QLabel(QString::fromStdString("Version " + p.version + (p.author.empty() ? "" : "  ·  " + p.author)));
    setTone(version, Tone::TextFaint);
    head->addWidget(version);
    head->addStretch();
    body->addLayout(head);
    if (!p.description.empty()) body->addWidget(pluginText(QString::fromStdString(p.description), Tone::TextDim));
    if (!ok) body->addWidget(pluginText(QString::fromStdString("Could not load: " + p.error), Tone::Danger));
    if (!objects.empty()) body->addWidget(pluginText(QString::fromStdString("Adds  " + objects), Tone::Text));
    if (!commands.empty()) body->addWidget(pluginText(QString::fromStdString("Commands  " + commands), Tone::Text));
    if (!p.drawError.empty()) body->addWidget(pluginText(QString::fromStdString("Last error: " + p.drawError), Tone::Danger));
    if (!p.permissions.empty()) {
        auto* chips = new QHBoxLayout;
        chips->setSpacing(6);
        for (const std::string& permission : p.permissions) chips->addWidget(new Chip(permissionLabel(permission)));
        chips->addStretch();
        body->addSpacing(2);
        body->addLayout(chips);
    }
    // Its actions, quiet: they are rarely needed.
    auto* actions = new QHBoxLayout;
    actions->setSpacing(2);
    auto quiet = [](const QString& text) {
        auto* b = new QPushButton(text);
        b->setProperty("flat", true);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* show = quiet("Show Folder");
    const std::filesystem::path dir = p.dir;
    QObject::connect(show, &QPushButton::clicked, show, [dir] { platform::revealInFileManager(dir); });
    actions->addWidget(show);
    const std::string id = p.id;
    if (!p.builtin) {
        auto* remove = quiet("Remove...");
        remove->setToolTip("Move this plugin to the Trash");
        QObject::connect(remove, &QPushButton::clicked, remove, [id, pname = QString::fromStdString(p.name)] {
            const QString question = QString("Move %name% to the Trash?").replace("%name%", pname);
            if (QMessageBox::question(g_window, "Remove Plugin", question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
                QMessageBox::Yes)
                return;
            std::string error;
            g_message = PluginHost::get().uninstall(id, &error) ? "Plugin moved to the Trash." : "Could not remove: " + error;
            emit AppSignals::get().pluginsChanged();
            if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        actions->addWidget(remove);
    }
    QPlainTextEdit* log = nullptr;
    if (!p.log.empty()) {
        auto* toggle = quiet(QString("Log (%1)").arg(p.log.size()));
        toggle->setCheckable(true);
        log = new QPlainTextEdit;
        log->setReadOnly(true);
        log->setMaximumHeight(140);
        QString text;
        for (const auto& l : p.log) text += QString::fromStdString(l) + "\n";
        log->setPlainText(text);
        QObject::connect(toggle, &QPushButton::toggled, log, &QWidget::setVisible);
        actions->addWidget(toggle);
    }
    actions->addStretch();
    body->addSpacing(2);
    body->addLayout(actions);
    if (log) {
        body->addWidget(log);
        log->hide();
    }
    h->addLayout(body, 1);

    auto* enabled = new Switch;
    enabled->setChecked(p.enabled);
    enabled->setEnabled(!(p.may("microphone") && media::Recorder::get().recording()));
    enabled->setAccessibleName(QString::fromStdString("Enable " + p.name));
    enabled->setToolTip(p.enabled ? "On: its objects and commands are in the editor" : "Off: hidden from the editor");
    QObject::connect(enabled, &QAbstractButton::toggled, enabled, [id](bool on) {
        PluginHost::get().setEnabled(id, on);
        emit AppSignals::get().pluginsChanged();
        if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
    });
    h->addWidget(enabled, 0, Qt::AlignTop);
    return row;
}

QWidget* pluginsPage() {
    PluginHost& host = PluginHost::get();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, "Plugins", [] { enableAllPlugins(); });
    col->addWidget(hintLabel("Plugins add objects and commands to the editor. They reach the project, media, files or the "
                             "microphone only with the permissions listed on them."));
    auto* install = accentButton("Install Plugin...");
    install->setToolTip("Choose the plugin.json file in a plugin's folder");
    QObject::connect(install, &QPushButton::clicked, install, [] {
        if (auto manifest = platform::openFileDialog("Install Plugin (choose its plugin.json)", "bemotion plugin", "json")) {
            std::string error;
            g_message = PluginHost::get().install(*manifest, &error) ? "Plugin installed." : "Could not install: " + error;
            emit AppSignals::get().pluginsChanged();
            if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
        }
    });
    auto* folder = new QPushButton("Open Folder");
    folder->setToolTip("Where installed plugins are kept");
    QObject::connect(folder, &QPushButton::clicked, folder, [] { platform::revealInFileManager(PluginHost::userDir()); });
    auto* reload = new QPushButton("Reload");
    reload->setToolTip("Load every plugin again (after editing one)");
    QObject::connect(reload, &QPushButton::clicked, reload, [] {
        PluginHost::get().requestReload();
        emit AppSignals::get().pluginsChanged();
        if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
    });
    auto* docs = new QPushButton("Write a Plugin");
    docs->setProperty("flat", true);
    docs->setToolTip("The plugin guide (opens in the browser)");
    QObject::connect(docs, &QPushButton::clicked, docs, [] { platform::openWithDefaultApp(kPluginDocsUrl); });
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    bar->addWidget(install);
    bar->addWidget(folder);
    bar->addWidget(reload);
    bar->addStretch();
    bar->addWidget(docs);
    col->addSpacing(4);
    col->addLayout(bar);
    if (!g_message.empty()) col->addWidget(pluginText(QString::fromStdString(g_message)));

    // What comes with bemotion first; then what was installed (or how to).
    SettingsCard* builtin = nullptr;
    for (const Plugin& p : host.plugins()) {
        if (!p.builtin) continue;
        if (!builtin) builtin = addSection(col, "Included with bemotion");
        builtin->addContent(pluginRow(p))->layout()->setContentsMargins(0, 0, 0, 0);
    }
    SettingsCard* installed = addSection(col, "Installed");
    bool anyInstalled = false;
    for (const Plugin& p : host.plugins()) {
        if (p.builtin) continue;
        installed->addContent(pluginRow(p))->layout()->setContentsMargins(0, 0, 0, 0);
        anyInstalled = true;
    }
    if (!anyInstalled)
        installed->addContent(pluginText("None yet. Install Plugin adds one from its folder (its plugin.json file).", Tone::TextFaint));
    return page;
}

} // namespace

void openSettings(SettingsTab tab) {
    if (!g_window) {
        auto* footer = new QWidget;
        auto* footerCol = new QVBoxLayout(footer);
        footerCol->setContentsMargins(0, 0, 0, 0);
        footerCol->setSpacing(6);
        auto* advancedRow = new QHBoxLayout;
        advancedRow->setContentsMargins(8, 0, 4, 0);
        auto* advancedLabel = new QLabel("Advanced options");
        advancedLabel->setToolTip("Every page shows all its options (colors, sizes, fine-tuning)");
        advancedRow->addWidget(advancedLabel, 1);
        auto* advanced = new Switch;
        advanced->setToolTip(advancedLabel->toolTip());
        advanced->setChecked(settings().advanced);
        advancedRow->addWidget(advanced);
        QObject::connect(advanced, &QAbstractButton::toggled, advanced, [](bool on) {
            settings().advanced = on;
            saveSettings();
            if (g_window) QTimer::singleShot(0, g_window, [] {
                    if (g_window) g_window->rebuild();
                });
        });
        footerCol->addLayout(advancedRow);
        auto* resetAll = new QPushButton("Reset All");
        resetAll->setToolTip("Restore every setting to its default");
        footerCol->addWidget(resetAll);
        g_window = new PagedWindow("Settings",
                                   {{"Theme", Icon::Palette, themePage},
                                    {"Interface", Icon::Sliders, interfacePage},
                                    {"Editor", Icon::Edit, editorPage},
                                    {"Shortcuts", Icon::Keyboard, shortcutsPage},
                                    {"Plugins", Icon::Plugin, pluginsPage}},
                                   footer);
        g_window->resize(860, 660);
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
