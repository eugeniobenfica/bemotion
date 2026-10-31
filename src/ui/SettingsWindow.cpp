// The Settings window (appearance, interface, editor preferences, shortcuts
// and plugins). One window shared by the home screen and the editor.

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
#include "ui/SettingsInternal.h"

namespace bemotion::ui {

using namespace settings_internal;

namespace {

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
        {TR_NOOP("Surfaces"),
         {{TR_NOOP("Background"), &theme::Palette::bgDeep},
          {TR_NOOP("Panels"), &theme::Palette::bgPanel},
          {TR_NOOP("Menu bar and tabs"), &theme::Palette::header},
          {TR_NOOP("Cards and tiles"), &theme::Palette::bgRaised},
          {TR_NOOP("Hover"), &theme::Palette::bgHover},
          {TR_NOOP("Borders"), &theme::Palette::border},
          {TR_NOOP("Canvas surround"), &theme::Palette::canvas},
          {TR_NOOP("Menus and tooltips"), &theme::Palette::menu}}},
        {TR_NOOP("Controls"),
         {{TR_NOOP("Fields"), &theme::Palette::field},
          {TR_NOOP("Field outline"), &theme::Palette::fieldBorder},
          {TR_NOOP("Buttons"), &theme::Palette::button},
          {TR_NOOP("Button outline"), &theme::Palette::buttonBorder},
          {TR_NOOP("Focus outline"), &theme::Palette::focus},
          {TR_NOOP("Selection"), &theme::Palette::selection},
          {TR_NOOP("Text on accent"), &theme::Palette::onAccent}}},
        {TR_NOOP("Text"),
         {{TR_NOOP("Text"), &theme::Palette::text}, {TR_NOOP("Secondary text"), &theme::Palette::textDim}, {TR_NOOP("Faint text"), &theme::Palette::textFaint}}},
        {TR_NOOP("Status"),
         {{TR_NOOP("Danger"), &theme::Palette::danger}, {TR_NOOP("Warning"), &theme::Palette::warning}, {TR_NOOP("Success"), &theme::Palette::success}}},
        {TR_NOOP("Editing"),
         {{TR_NOOP("Playhead"), &theme::Palette::playhead},
          {TR_NOOP("Keyframes"), &theme::Palette::keyframe},
          {TR_NOOP("Snap lines"), &theme::Palette::snap},
          {TR_NOOP("Guides"), &theme::Palette::guide}}},
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
            row->addWidget(new QLabel(tr(e.label)), 1);
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
    addPageHeader(col, tr("Theme"), [] {
        Settings& st = settings();
        const Settings d;
        st.theme = d.theme, st.customAccent = d.customAccent, st.accent = d.accent;
        st.customPalette = false;
    });
    SettingsCard* themes = addSection(col, tr("Theme"));
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
    auto* import = new QPushButton(tr("Import..."));
    QObject::connect(import, &QPushButton::clicked, import, [] {
        if (auto file = platform::openFileDialog(TR_NOOP("Import Theme"), TR_NOOP("bemotion theme"), "json")) {
            std::string name, error;
            if (ThemeLibrary::importFile(*file, &name, &error)) {
                settings().theme = name;
                settings().customPalette = false;
                g_message = tr("Imported \"%1\".").arg(QString::fromStdString(name)).toStdString();
            } else {
                g_message = tr("Could not import: %1").arg(QString::fromStdString(error)).toStdString();
            }
            changed(true);
        }
    });
    auto* folder = new QPushButton(tr("Open Folder"));
    QObject::connect(folder, &QPushButton::clicked, folder, [] { platform::revealInFileManager(ThemeLibrary::userDir()); });
    themes->addRow(tr("More themes"), tr("Import a theme someone shared (a .json file), or open the folder your themes are kept in."),
                   buttonRow({import, folder}));
    if (!g_message.empty()) col->addWidget(hintLabel(QString::fromStdString(g_message)));

    SettingsCard* accentCard = addSection(col, tr("Accent color"));
    const theme::Palette base = [&] {
        Settings copy = s;
        copy.customAccent = false;
        return basePalette(copy);
    }();
    auto* accents = new QHBoxLayout;
    accents->setSpacing(8);
    auto* themeAccent = new Swatch(base.accent, !s.customAccent, tr("The theme's own"));
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
    auto* custom = new QPushButton(tr("Custom..."));
    custom->setToolTip(tr("Pick any color"));
    QObject::connect(custom, &QPushButton::clicked, custom, [custom] {
        const Rgba cur = settings().customAccent ? settings().accent : basePalette(settings()).accent;
        Color c{red(cur) / 255.0f, green(cur) / 255.0f, blue(cur) / 255.0f, 1.0f};
        if (!pickColor(custom, c, false)) return;
        settings().customAccent = true;
        settings().accent = rgba(static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)),
                                 static_cast<int>(std::lround(c.b * 255)));
        changed(true);
    });
    accents->addWidget(custom);
    accents->addStretch();
    accentCard->addLayout(accents);

    if (!s.advanced) {
        col->addSpacing(6);
        col->addWidget(hintLabel(tr("Every color, the corners, outlines and control sizes, and saving your own theme are in "
                                 "Advanced Options (bottom left).")));
        return page;
    }

    if (!s.customPalette) s.palette = effectivePalette(); // start from what is on screen
    col->addSpacing(4);
    col->addWidget(hintLabel(tr("Changing anything below turns the current theme into your own palette; Save as Theme keeps it.")));
    addColorSections(col, s);

    SettingsCard* shape = addSection(col, tr("Shape and size"));
    shape->addRow(tr("Corner rounding"), tr("Buttons, fields, cards and menus"), sliderControl(s.palette.rounding, 0.0f, 12.0f, "%.0f px"));
    shape->addRow(tr("Outline width"), tr("The outline of fields and buttons (0: none)"), sliderControl(s.palette.borderWidth, 0.0f, 2.0f, "%.0f px"));
    shape->addRow(tr("Control height"), tr("Buttons, fields and drop-downs in windows like this one"),
                  sliderControl(s.palette.controlHeight, 22.0f, 36.0f, "%.0f px"));
    shape->addRow(tr("Light theme"), tr("Overlays on the picture (outlines, handles) are drawn dark instead of light"),
                  switchControl(s.palette.light, [](bool on) {
                      settings().palette.light = on;
                      settings().customPalette = true;
                      changed(true);
                  }));

    SettingsCard* share = addSection(col, tr("Save and share"));
    auto* name = new QLineEdit;
    name->setPlaceholderText(tr("Theme name"));
    name->setFixedWidth(180);
    auto* save = accentButton(tr("Save as Theme"));
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
            g_message = tr("Saved \"%1\".").arg(QString::fromStdString(out.name)).toStdString();
        } else {
            g_message = error;
        }
        changed(true);
    });
    share->addRow(tr("Save as a theme"), tr("Keeps these colors under a name, next to Dark, Light and Midnight"), buttonRow({name, save}));
    auto* exportButton = new QPushButton(tr("Export..."));
    QObject::connect(exportButton, &QPushButton::clicked, exportButton, [name] {
        theme::Palette out = effectivePalette();
        if (!name->text().trimmed().isEmpty()) out.name = name->text().trimmed().toStdString();
        if (auto file = platform::saveFileDialog(TR_NOOP("Export Theme"), platform::fromUtf8(out.name + ".json"), TR_NOOP("bemotion theme"), "json")) {
            std::string error;
            g_message = ThemeLibrary::exportTo(out, *file, &error) ? tr("Exported to %1").arg(QString::fromStdString(platform::toUtf8(*file))).toStdString() : error;
            if (g_window) g_window->rebuild();
        }
    });
    share->addRow(tr("Export"), tr("A .json file anyone can import"), exportButton);
    if (s.customPalette) {
        auto* reset = new QPushButton(tr("Discard Changes"));
        QObject::connect(reset, &QPushButton::clicked, reset, [] {
            settings().customPalette = false;
            changed(true);
        });
        share->addRow(tr("Back to the theme"), tr("Drops the colors changed here"), reset);
    }
    return page;
}

QWidget* interfacePage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, tr("Interface"), [] {
        Settings& st = settings();
        const Settings d;
        st.timelineLayerRow = d.timelineLayerRow, st.timelinePropertyRow = d.timelinePropertyRow;
        st.keyframeSize = d.keyframeSize, st.textScale = d.textScale, st.uiFont = d.uiFont;
        st.showIntro = d.showIntro;
    });
    SettingsCard* language = addSection(col, tr("Language"));
    {
        auto* combo = new QComboBox;
        combo->addItem(tr("Automatic (the system's)"), QStringLiteral("auto"));
        combo->addItem(QStringLiteral("English"), QStringLiteral("en"));
        for (const LanguageInfo& l : availableLanguages())
            combo->addItem(QString::fromStdString(l.name), QString::fromStdString(l.code));
        combo->setCurrentIndex(std::max(0, combo->findData(QString::fromStdString(s.language))));
        QObject::connect(combo, &QComboBox::activated, combo, [combo](int i) {
            const std::string code = combo->itemData(i).toString().toStdString();
            if (code == settings().language) return;
            settings().language = code;
            saveSettings();
            // At once: every window builds itself again in the new language (this one too, so
            // after this handler).
            QTimer::singleShot(0, [code] {
                installLanguage(code);
                emit AppSignals::get().languageChanged();
            });
        });
        language->addRow(tr("Interface language"),
                         tr("Languages are JSON files: add your own to the languages folder in bemotion's data"), combo);
    }
    SettingsCard* experience = addSection(col, tr("Experience"));
    const int which = s.experience == "friendly" ? 0 : s.experience == "expert" ? 2 : 1;
    // One description for the three: the row keeps its height while choosing (it used to grow
    // and shrink under the mouse).
    experience->addRow(tr("How much the interface shows"),
                       tr("Friendly: the essentials. Balanced: the usual tools. Expert: every option, rulers, a fuller toolbar"),
                       segmented({tr("Friendly"), tr("Balanced"), tr("Expert")}, which, [](int i) {
                           applyExperience(i == 0 ? "friendly" : i == 2 ? "expert" : "balanced");
                           emit AppSignals::get().toolbarChanged();
                           changed(true);
                       }));
    experience->addRow(tr("Essentials only"),
                       tr("Plugins and built-in extras off: no charts, maps, 3D models, voice recording, speech to text or tracking. The simplest bemotion"),
                       switchControl(s.essentialsOnly, [](bool on) {
                           settings().essentialsOnly = on;
                           saveSettings();
                           PluginHost::get().requestReload();
                           emit AppSignals::get().pluginsChanged();
                           emit AppSignals::get().modulesChanged();
                       }));

    SettingsCard* timeline = addSection(col, tr("Timeline"));
    auto* presets = new QHBoxLayout;
    auto preset = [&](const QString& label, float row, float prop, float key) {
        const bool selected = std::fabs(s.timelineLayerRow - row) < 0.1f && std::fabs(s.timelinePropertyRow - prop) < 0.1f;
        auto* b = new TimelineSizeCard(label, row, prop, key, selected);
        QObject::connect(b, &QPushButton::clicked, b, [row, prop, key] {
            Settings& st = settings();
            st.timelineLayerRow = row, st.timelinePropertyRow = prop, st.keyframeSize = key;
            changed(true);
        });
        presets->addWidget(b);
    };
    preset(tr("Compact"), 34, 28, 5);
    preset(tr("Comfortable"), 46, 38, 6);
    preset(tr("Large"), 60, 50, 7.5f);
    presets->addStretch();
    timeline->addLayout(presets);
    if (s.advanced) {
        timeline->addRow(tr("Clip row height"), "", sliderControl(s.timelineLayerRow, 22.0f, 80.0f, "%.0f px"));
        timeline->addRow(tr("Keyframe row height"), "", sliderControl(s.timelinePropertyRow, 20.0f, 60.0f, "%.0f px"));
        timeline->addRow(tr("Keyframe size"), "", sliderControl(s.keyframeSize, 3.0f, 10.0f, "%.1f px", 0.5f));
    }

    SettingsCard* text = addSection(col, tr("Text"));
    static float percent;
    percent = s.textScale * 100.0f;
    text->addRow(tr("Text size"), tr("Every label in the interface"),
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
    text->addRow(tr("Interface font"), tr("Inter ships with bemotion; the others come from your system"), font);

    // Each extra on its own, whatever the experience.
    SettingsCard* show = addSection(col, tr("Show"));
    auto showRow = [&](const QString& name, const QString& about, bool Settings::*field) {
        show->addRow(name, about, switchControl(s.*field, [field](bool on) {
                         settings().*field = on;
                         saveSettings();
                         emit AppSignals::get().appearanceChanged();
                     }));
    };
    showRow(tr("Expression buttons"), tr("The ƒ beside each property (one with an expression keeps it)"), &Settings::showExpressionButtons);
    showRow(tr("Graph editor button"), tr("The curves of the keyframes, in the timeline"), &Settings::showGraphButton);
    showRow(tr("Export Frame button"), tr("The camera in the viewport: this frame as a picture (also Shift+E)"), &Settings::showSnapshotButton);
    showRow(tr("Timeline scrollbar"), tr("Under the timeline, to move along long projects"), &Settings::showTimelineScrollbar);
    showRow(tr("Audio meters"), tr("Left and right sound levels beside the timeline (also View › Audio Meters)"), &Settings::showMeters);

    SettingsCard* startup = addSection(col, tr("Startup"));
    startup->addRow(tr("Intro animation"), tr("The bemotion logo when the app opens (a click or any key skips it)"),
                    switchControl(s.showIntro, [](bool on) {
                        settings().showIntro = on;
                        saveSettings();
                    }));
    startup->addRow(tr("Welcome project"), tr("The example project on the home screen (deleting it there turns this off)"),
                    switchControl(!s.welcomeRemoved, [](bool on) {
                        settings().welcomeRemoved = !on; // on: a fresh copy comes back if it was deleted
                        saveSettings();
                        emit AppSignals::get().projectsChanged();
                    }));

    // New versions from GitHub: off until turned on here (ui/Updates.h).
    SettingsCard* updates = addSection(col, tr("Updates"));
    updates->addRow(tr("Check for updates"),
                    tr("Once a day bemotion looks for a new version on GitHub and offers it. Nothing is installed without a click"),
                    switchControl(s.checkUpdates, [](bool on) {
                        settings().checkUpdates = on;
                        saveSettings();
                        if (on) Updates::get().checkIfDue();
                    }));
    updates->addRow(tr("Versions"), tr("Betas come sooner and change more"),
                    segmented({tr("Stable"), tr("Beta")}, Updates::get().betas() ? 1 : 0, [](int i) {
                        settings().updateChannel = i == 1 ? "beta" : "stable";
                        saveSettings();
                    }));
    auto* updateStatus = new QLabel(Updates::get().status());
    updateStatus->setWordWrap(true);
    setTone(updateStatus, Tone::TextDim);
    auto* checkNow = new QPushButton(tr("Check Now"));
    auto* updateNow = accentButton(tr("Update"));
    updateNow->setVisible(Updates::get().available().has_value());
    QObject::connect(checkNow, &QPushButton::clicked, checkNow, [] { Updates::get().checkNow(); });
    QObject::connect(updateNow, &QPushButton::clicked, updateNow, [] { Updates::get().install(); });
    QObject::connect(&Updates::get(), &Updates::changed, updateStatus, [updateStatus, checkNow, updateNow] {
        updateStatus->setText(Updates::get().status());
        checkNow->setEnabled(!Updates::get().checking());
        updateNow->setVisible(Updates::get().available().has_value());
    });
    updates->addRow(tr("This version"), tr("bemotion %1").arg(BEMOTION_VERSION), buttonRow({checkNow, updateNow}));
    updates->addContent(updateStatus);

    SettingsCard* reset = addSection(col, tr("Start over"));
    auto* resetInterface = new QPushButton(tr("Reset Interface..."));
    QObject::connect(resetInterface, &QPushButton::clicked, resetInterface, [] {
        if (QMessageBox::question(g_window, tr("Reset Interface"), tr("Restore the entire interface to its defaults?")) != QMessageBox::Yes) return;
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
        if (ec) QMessageBox::warning(g_window, tr("Reset Interface"), tr("The saved panel layout could not be removed. Close the editor and try again."));
    });
    reset->addRow(tr("Reset the interface"), tr("Theme, toolbar, shortcuts, preview and panel layout go back to their defaults. "
                                         "Projects, media, plugins and recent files stay."),
                  resetInterface);
    return page;
}

QWidget* editorPage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, tr("Editor"), [] {
        Settings& st = settings();
        const Settings d;
        st.snapDistance = d.snapDistance, st.arrowFrames = d.arrowFrames, st.bigArrowFrames = d.bigArrowFrames;
        st.autosaveMinutes = d.autosaveMinutes;
    });
    SettingsCard* saving = addSection(col, tr("Saving"));
    saving->addRow(tr("Save after every change"),
                   tr("A project saves itself a moment after each change, once it has a file (the first Save picks it)"),
                   switchControl(s.autosaveMinutes > 0, [](bool on) {
                       settings().autosaveMinutes = on ? 5 : 0; // any value but 0 is on (the setting's older name)
                       saveSettings();
                   }));
    saving->addRow(tr("Crash recovery"), tr("Unsaved changes are always kept aside: if bemotion closes unexpectedly, it offers them "
                                     "back the next time it opens"),
                   nullptr);
    if (!s.advanced) {
        col->addSpacing(6);
        col->addWidget(hintLabel(tr("How far the arrow keys step and how close things snap are in Advanced Options (bottom left).")));
        return page;
    }
    SettingsCard* playhead = addSection(col, tr("Playhead"));
    playhead->addRow(tr("Arrow keys step"), tr("Left / Right move the playhead this many frames"), spinControl(s.arrowFrames, 1, 60, tr(" frames")));
    playhead->addRow(tr("Big step"), tr("With Shift by default (see Shortcuts)"), spinControl(s.bigArrowFrames, 1, 300, tr(" frames")));
    SettingsCard* snapping = addSection(col, tr("Snapping"));
    snapping->addRow(tr("Snap distance"),
                     tr("How close (on screen) an object, clip, keyframe or the playhead gets before it snaps. S or the magnet "
                     "turns snapping on and off; hold Ctrl while dragging to ignore it."),
                     sliderControl(s.snapDistance, 2.0f, 30.0f, "%.0f px"));
    return page;
}

QWidget* performancePage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, tr("Performance"), [] {
        Settings& st = settings();
        const Settings d;
        st.hardwareDecoding = d.hardwareDecoding, st.useProxies = d.useProxies, st.cacheMegabytes = d.cacheMegabytes;
        st.scaledVideoPreview = d.scaledVideoPreview, st.autoProxies = d.autoProxies, st.showFilmstrips = d.showFilmstrips;
        st.adaptivePlayback = d.adaptivePlayback, st.gpuRendering = d.gpuRendering;
        st.processorThreads = d.processorThreads, st.gpuCacheMegabytes = d.gpuCacheMegabytes;
        st.renderEngine = d.renderEngine;
        useEngine(Engine::OpenGL);
        setGpuCacheMegabytes(st.gpuCacheMegabytes);
        media::setThreadCount(st.processorThreads);
        st.previewQuality = d.previewQuality;
        media::setHardwareDecoding(st.hardwareDecoding);
        media::MediaManager::get().reopenVideos();
        media::MediaManager::get().setUseProxies(st.useProxies);
        media::MediaManager::get().setFrameCacheMegabytes(st.cacheMegabytes / 2);
        media::MediaManager::get().setScaledPreview(st.scaledVideoPreview);
    });
    SettingsCard* preview = addSection(col, tr("Preview"));
    preview->addRow(tr("Preview quality"),
                    tr("The viewport draws at full, half or a quarter of its size, then enlarges it: lighter while editing "
                       "heavy scenes. Exports are always at full quality (also in the viewport's corner)"),
                    segmented({TR_NOOP("Full"), TR_NOOP("Half"), TR_NOOP("Quarter")}, s.previewQuality >= 4 ? 2 : s.previewQuality >= 2 ? 1 : 0,
                              [](int i) {
                                  settings().previewQuality = i == 2 ? 4 : i == 1 ? 2 : 1;
                                  saveSettings();
                                  emit AppSignals::get().appearanceChanged();
                              }));
    // What draws the pictures: the graphics card through OpenGL (Skia's Ganesh), or the processor.
    preview->addRow(tr("Drawing engine"),
                    tr("Vulkan and OpenGL draw on the graphics card: effects, blurs and big videos many times faster (Vulkan "
                       "is often quicker on newer cards and drivers). The processor works everywhere, slower. Exports from "
                       "the editor use the same engine") +
                        (*gpuName() ? tr(" (in use: %1)").arg(QString::fromUtf8(gpuName())) : QString()),
                    segmented({TR_NOOP("Vulkan"), TR_NOOP("OpenGL"), TR_NOOP("Processor")},
                              !s.gpuRendering ? 2 : s.renderEngine == "vulkan" ? 0 : 1,
                              [](int i) {
                                  settings().gpuRendering = i != 2;
                                  if (i != 2) settings().renderEngine = i == 0 ? "vulkan" : "opengl";
                                  const Engine got = useEngine(i == 0 ? Engine::Vulkan : i == 1 ? Engine::OpenGL : Engine::Processor);
                                  if (i == 0 && got != Engine::Vulkan) {
                                      settings().renderEngine = "opengl";
                                      QMessageBox::information(g_window, tr("Drawing engine"), tr("Vulkan does not work on this computer: drawing with OpenGL."));
                                  }
                                  saveSettings();
                                  emit AppSignals::get().appearanceChanged(); // the viewport draws again
                                  if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); }); // the engine in use
                              },
                              {vulkanAvailable(), true, true}));
    preview->addRow(tr("Lower the resolution while playing"),
                    tr("When a frame takes longer to draw than it lasts (heavy effects, big videos), playback draws at half or a "
                       "quarter of the size to stay smooth. Paused, the picture is back at the preview quality"),
                    switchControl(s.adaptivePlayback, [](bool on) {
                        settings().adaptivePlayback = on;
                        saveSettings();
                    }));
    SettingsCard* playback = addSection(col, tr("Video"));
    playback->addRow(tr("Use proxies"),
                     tr("Videos with a proxy (Media panel › right-click › Create Proxy) play from it in the editor: smooth even "
                     "for 4K. Exports always use the originals"),
                     switchControl(s.useProxies, [](bool on) {
                         settings().useProxies = on;
                         media::MediaManager::get().setUseProxies(on);
                         saveSettings();
                     }));
    SettingsCard* timeline = addSection(col, tr("Timeline"));
    timeline->addRow(tr("Video frames on clips"),
                     tr("Video clips show pictures of their frames (made in the background). Off: plain clips, lighter on a slow computer"),
                     switchControl(s.showFilmstrips, [](bool on) {
                         settings().showFilmstrips = on;
                         saveSettings();
                         emit AppSignals::get().appearanceChanged();
                     }));
    // The rest is fine-tuning: Advanced Options.
    if (!s.advanced) {
        col->addSpacing(6);
        col->addWidget(hintLabel(tr("Processor threads, graphics memory, hardware decoding, proxy files and memory for cached frames are in Advanced Options (bottom left).")));
        return page;
    }
    {
        // Threads for decoding, encoding and speech: fewer leaves the computer free for other work.
        auto* threads = new QComboBox;
        threads->addItem(tr("Automatic"), 0);
        const int cores = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
        for (int n : {1, 2, 4, 6, 8, 12, 16, 24, 32})
            if (n <= cores) threads->addItem(QString::number(n), n);
        threads->setCurrentIndex(std::max(0, threads->findData(s.processorThreads)));
        QObject::connect(threads, &QComboBox::activated, threads, [threads](int) {
            settings().processorThreads = threads->currentData().toInt();
            media::setThreadCount(settings().processorThreads);
            media::MediaManager::get().reopenVideos();
            saveSettings();
        });
        SettingsCard* processing = addSection(col, tr("Processing"));
        processing->addRow(tr("Processor threads"),
                           tr("How many threads decoding, exporting and transcribing may each use (this computer has %1). "
                              "Fewer leaves it free for other work").arg(cores),
                           threads);
        auto* memory = new QComboBox;
        for (int mb : {128, 256, 512, 1024, 2048, 4096}) memory->addItem(mb < 1024 ? QString("%1 MB").arg(mb) : QString("%1 GB").arg(mb / 1024), mb);
        memory->setCurrentIndex(std::max(0, memory->findData(s.gpuCacheMegabytes)));
        QObject::connect(memory, &QComboBox::activated, memory, [memory](int) {
            settings().gpuCacheMegabytes = memory->currentData().toInt();
            setGpuCacheMegabytes(settings().gpuCacheMegabytes);
            saveSettings();
        });
        processing->addRow(tr("Graphics memory for the preview"),
                           tr("How much of the graphics card's memory keeps pictures, letters and effects between frames: "
                              "more makes scrubbing smoother on cards with memory to spare"),
                           memory);
    }
    preview->addRow(tr("Decode video at the size shown"),
                    tr("A video shown small in the viewport is read small: much less work for 4K and HD footage. Exports "
                       "read it at the size it comes out, never smaller"),
                    switchControl(s.scaledVideoPreview, [](bool on) {
                        settings().scaledVideoPreview = on;
                        media::MediaManager::get().setScaledPreview(on);
                        saveSettings();
                    }));
    const std::string decoder = media::hardwareDecoderName();
    playback->addRow(tr("Hardware video decoding"),
                     tr("The graphics card reads HD and 4K videos (H.264, HEVC, VP9, AV1): lighter on a laptop's processor, though a fast processor is often quicker on its own") +
                         (decoder.empty() ? QString() : tr(" (in use: %1)").arg(QString::fromStdString(decoder))),
                     switchControl(s.hardwareDecoding, [](bool on) {
                         settings().hardwareDecoding = on;
                         media::setHardwareDecoding(on);
                         media::MediaManager::get().reopenVideos();
                         saveSettings();
                     }));
    playback->addRow(tr("Make proxies when importing"),
                     tr("Videos bigger than 1280 × 720 get a proxy as soon as they are imported (in the background)"),
                     switchControl(s.autoProxies, [](bool on) {
                         settings().autoProxies = on;
                         saveSettings();
                     }));
    {
        auto* size = new QLabel;
        setTone(size, Tone::TextDim);
        auto showSize = [size] {
            const double mb = static_cast<double>(media::MediaManager::get().proxyBytes()) / (1024.0 * 1024.0);
            size->setText(mb < 0.05 ? tr("None") : tr("%1 MB").arg(mb, 0, 'f', mb < 10 ? 1 : 0));
        };
        showSize();
        auto* clear = new QPushButton(tr("Delete All"));
        QObject::connect(clear, &QPushButton::clicked, size, [showSize] {
            media::MediaManager::get().deleteAllProxies();
            showSize();
        });
        playback->addRow(tr("Proxy files"), tr("Kept with bemotion's own data; made again when needed"), buttonRow({static_cast<QWidget*>(size), clear}));
    }
    auto* memory = new QSpinBox;
    memory->setRange(256, 32768);
    memory->setSingleStep(256);
    memory->setSuffix(tr(" MB"));
    memory->setValue(s.cacheMegabytes);
    memory->setFixedWidth(120);
    QObject::connect(memory, &QSpinBox::valueChanged, memory, [](int v) {
        settings().cacheMegabytes = v;
        media::MediaManager::get().setFrameCacheMegabytes(v / 2);
        saveSettings();
    });
    SettingsCard* memoryCard = addSection(col, tr("Memory"));
    memoryCard->addRow(tr("Memory for cached frames"),
                     tr("Decoded video frames and rendered previews already shown are kept, so they play again at once. "
                     "More memory: longer stretches"),
                     memory);
    return page;
}

// Click it, then press the new keys (Esc cancels).
class ShortcutButton : public QPushButton {
public:
    explicit ShortcutButton(Action action) : action_(action) {
        setFixedWidth(160);
        setProperty("keycap", true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Click, then press the new keys (Esc cancels)"));
        refreshText();
        connect(this, &QPushButton::clicked, this, [this] {
            capturing_ = true;
            setText(tr("Press keys..."));
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
        setText(name.empty() ? tr("None") : QString::fromStdString(name));
    }
    Action action_;
    bool capturing_ = false;
};

QWidget* shortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, tr("Shortcuts"), [] {
        loadKeymap("{}");
        emit AppSignals::get().shortcutsChanged();
    });
    // A search over names and keys (kept while the page rebuilds after a change).
    static QString filter;
    auto* search = new QLineEdit(filter);
    search->setPlaceholderText(tr("Search shortcuts (a name or keys, e.g. \"Ctrl+S\")"));
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
            auto* title = sectionLabel(tr(group));
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
            auto* clash = new QLabel(tr("Also %1").arg(tr(actionInfo(other).label)));
            setTone(clash, Tone::Danger);
            h->addWidget(clash);
        }
        h->addWidget(new ShortcutButton(a));
        auto* reset = new IconButton(Icon::Reset, tr("Back to the default"), 26);
        reset->setVisible(chordOf(a) != info.defaultChord);
        QObject::connect(reset, &QAbstractButton::clicked, reset, [a] {
            resetChord(a);
            saveSettings();
            emit AppSignals::get().shortcutsChanged();
            if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        auto* clear = new IconButton(Icon::Trash, tr("Remove this shortcut"), 26);
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
        QWidget* row = card->addRow(tr(info.label), "", tools);
        row->setMinimumHeight(38);
        row->layout()->setContentsMargins(16, 5, 16, 5); // a long list: compact rows
        rows->push_back({row, card, (tr(info.label) + " " + QString::fromStdString(chordName(chordOf(a)))).toLower()});
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

} // namespace

void openSettings(SettingsTab tab) {
    if (!g_window) {
        auto* footer = new QWidget;
        auto* footerCol = new QVBoxLayout(footer);
        footerCol->setContentsMargins(0, 0, 0, 0);
        footerCol->setSpacing(6);
        auto* advancedRow = new QHBoxLayout;
        advancedRow->setContentsMargins(8, 0, 4, 0);
        auto* advancedLabel = new QLabel(tr("Advanced options"));
        advancedLabel->setToolTip(tr("Every page shows all its options (colors, sizes, fine-tuning)"));
        advancedRow->addWidget(advancedLabel, 1);
        auto* advanced = new Switch;
        advanced->setToolTip(advancedLabel->toolTip());
        advanced->setChecked(settings().advanced);
        advancedRow->addWidget(advanced);
        // An experience can switch it too (Expert: on): follow it.
        QObject::connect(&AppSignals::get(), &AppSignals::appearanceChanged, advanced, [advanced] {
            const QSignalBlocker block(advanced);
            advanced->setChecked(settings().advanced);
        });
        QObject::connect(advanced, &QAbstractButton::toggled, advanced, [](bool on) {
            settings().advanced = on;
            saveSettings();
            if (g_window) QTimer::singleShot(0, g_window, [] {
                    if (g_window) g_window->rebuild();
                });
        });
        footerCol->addLayout(advancedRow);
        auto* resetAll = new QPushButton(tr("Reset All"));
        resetAll->setToolTip(tr("Restore every setting to its default"));
        footerCol->addWidget(resetAll);
        g_window = new PagedWindow(tr("Settings"),
                                   {{tr("Theme"), Icon::Palette, themePage},
                                    {tr("Interface"), Icon::Sliders, interfacePage},
                                    {tr("Editor"), Icon::Edit, editorPage},
                                    {tr("Performance"), Icon::Speed, performancePage},
                                    {tr("Shortcuts"), Icon::Keyboard, shortcutsPage},
                                    {tr("Plugins"), Icon::Plugin, pluginsPage}},
                                   footer);
        g_window->resize(860, 660);
        QObject::connect(&AppSignals::get(), &AppSignals::pluginsChanged, g_window, [] {
            if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        // Its page names and footer are read as it is built: another language builds it again,
        // on the same page and in the same place.
        QObject::connect(&AppSignals::get(), &AppSignals::languageChanged, g_window, [] {
            PagedWindow* old = g_window;
            const bool shown = old->isVisible();
            const int page = old->currentPage();
            const QRect geometry = old->geometry();
            g_window = nullptr;
            old->hide();
            old->deleteLater();
            if (!shown) return;
            openSettings(static_cast<SettingsTab>(page));
            g_window->setGeometry(geometry);
        });
        QObject::connect(resetAll, &QPushButton::clicked, g_window, [] {
            if (QMessageBox::question(g_window, tr("Reset All"), tr("Reset every setting to its default?")) != QMessageBox::Yes) return;
            const bool keepAdvanced = settings().advanced; // a view choice, not a preference
            const std::string language = settings().language;
            settings() = Settings{};
            settings().advanced = keepAdvanced;
            loadKeymap("{}");
            enableAllPlugins();
            emit AppSignals::get().shortcutsChanged();
            emit AppSignals::get().toolbarChanged();
            changed(true);
            if (settings().language != language) QTimer::singleShot(0, [] { // as the Language row does
                    installLanguage(settings().language);
                    emit AppSignals::get().languageChanged();
                });
        });
    }
    g_window->showPage(static_cast<int>(tab));
    g_window->present();
}

} // namespace bemotion::ui
