// Help window: keyboard shortcuts (drawn as key caps), about the project,
// credits and the license.

#include "ui/Dialogs.h"

#include "app/Platform.h"
#include "ui/AppSignals.h"
#include "ui/Keymap.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace bemotion::ui {

namespace {

constexpr const char* kRepoUrl = "https://github.com/eugeniobenfica/bemotion";

struct Shortcut {
    const char* keys; // "+" joins keys, " / " separates alternatives
    const char* action;
};

// Mouse gestures (not rebindable); keyboard shortcuts come from the keymap.
const std::vector<Shortcut>& mouseGestures() {
    static const std::vector<Shortcut> items = {
        {"Wheel", "Zoom the viewport around the cursor"},
        {"Middle Drag / Space+Drag / Alt+Drag", "Pan the viewport"},
        {"Ctrl+Wheel / Shift+Wheel", "Zoom / scroll the timeline"},
        {"Double Click", "Open a compound (its timeline) and pick a layer in it"},
        {"Shift+Click / Ctrl+Click", "Add to / toggle the selection"},
        {"Alt+Drag", "Break a motion path handle into a corner"},
        {"Shift+Drag", "Constrain a move, uniform scale, 15° rotation"},
        {"Ctrl+Drag", "Move without snapping"},
        {"Double Click a tab", "Maximize that panel (again to restore)"},
    };
    return items;
}

struct Credit {
    const char* name;
    const char* by;
    const char* license;
    const char* use;
};
constexpr Credit kCredits[] = {
    {"Qt 6", "The Qt Company and contributors", "LGPL 3", "windows, widgets, input and the whole user interface"},
    {"Qt Advanced Docking System", "Uwe Kindler and contributors", "LGPL 2.1", "dockable, tabbed and floating panels"},
    {"Skia", "Google and the Skia authors", "BSD 3-Clause", "drawing shapes, text, images and effects"},
    {"HarfBuzz", "The HarfBuzz developers", "MIT", "text shaping (ligatures, complex scripts)"},
    {"ICU", "Unicode, Inc.", "Unicode License", "bidirectional text and line breaking"},
    {"FFmpeg", "The FFmpeg developers", "LGPL 2.1+ / GPL", "video and audio import, MP4 / ProRes / WebM / GIF export"},
    {"nlohmann/json", "Niels Lohmann", "MIT", "project, settings, preset and theme files"},
    {"stb", "Sean Barrett", "MIT / Public Domain", "image import and PNG export"},
    {"miniaudio", "David Reid", "MIT-0 / Public Domain", "audio playback"},
    {"Lua 5.4", "PUC-Rio (Lua.org)", "MIT", "the plugin scripting language"},
    {"Inter", "Rasmus Andersson", "SIL Open Font License 1.1", "the interface typeface"},
};

constexpr const char* kLicense =
    "MIT License\n\n"
    "Copyright (c) 2026 The bemotion contributors\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated "
    "documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation "
    "the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to "
    "permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be included in all copies or substantial portions of "
    "the Software.\n\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO "
    "THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
    "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    "SOFTWARE.";

// "Ctrl+Shift+Z / Ctrl+Y" drawn as key caps.
class KeyCaps : public QWidget {
public:
    explicit KeyCaps(std::string keys) : keys_(std::move(keys)) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setMinimumHeight(fontMetrics().height() + 12);
        setMinimumWidth(static_cast<int>(layoutWidth()));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        float x = 0;
        const float h = static_cast<float>(fontMetrics().height() + 8);
        for (const Part& part : parts()) {
            if (part.separator) {
                const float w = d.textWidth(part.text);
                d.text({x + 3, (h - d.textHeight()) * 0.5f}, theme::kTextFaint, part.text);
                x += w + 6;
                continue;
            }
            const float w = std::max(h, d.textWidth(part.text) + 14.0f);
            const float r = std::min(6.0f, h * 0.25f);
            d.rectFilled({x, 2}, {x + w, h + 2}, theme::kBorder, r); // the key's side
            d.rectFilled({x, 0}, {x + w, h}, theme::kBgRaised, r);
            d.rect({x, 0}, {x + w, h}, theme::kBorder, r);
            d.text({x + (w - d.textWidth(part.text)) * 0.5f, (h - d.textHeight()) * 0.5f}, theme::kText, part.text);
            x += w + 3;
        }
    }

private:
    struct Part {
        QString text;
        bool separator;
    };
    std::vector<Part> parts() const {
        std::vector<Part> out;
        const std::string& text = keys_;
        size_t start = 0;
        bool firstCombo = true;
        while (start <= text.size()) {
            size_t end = text.find(" / ", start);
            if (end == std::string::npos) end = text.size();
            const std::string combo = text.substr(start, end - start);
            if (!firstCombo) out.push_back({" / ", true});
            firstCombo = false;
            // "+" joins keys, except a key that is itself "+".
            size_t k = 0;
            bool firstKey = true;
            while (k <= combo.size()) {
                size_t plus = combo.find('+', k + 1);
                if (plus == std::string::npos) plus = combo.size();
                if (!firstKey) out.push_back({"+", true});
                firstKey = false;
                out.push_back({QString::fromStdString(combo.substr(k, plus - k)), false});
                k = plus + 1;
                if (plus >= combo.size()) break;
            }
            start = end + 3;
            if (end >= text.size()) break;
        }
        return out;
    }
    float layoutWidth() const {
        const QFontMetricsF fm(font());
        const float h = static_cast<float>(fm.height() + 8);
        float x = 0;
        for (const Part& part : parts())
            x += part.separator ? static_cast<float>(fm.horizontalAdvance(part.text)) + 6 : std::max(h, static_cast<float>(fm.horizontalAdvance(part.text)) + 14.0f) + 3;
        return x + 4;
    }
    std::string keys_;
};

void shortcutTable(QVBoxLayout* col, const std::vector<std::pair<std::string, std::string>>& rows) {
    auto* grid = new QGridLayout;
    grid->setColumnMinimumWidth(0, 270);
    grid->setColumnStretch(1, 1);
    grid->setVerticalSpacing(4);
    int r = 0;
    for (const auto& [keys, action] : rows) {
        if (keys.empty()) grid->addWidget(hintLabel("(none)"), r, 0);
        else grid->addWidget(new KeyCaps(keys), r, 0);
        grid->addWidget(new QLabel(QString::fromStdString(action)), r, 1);
        ++r;
    }
    col->addLayout(grid);
}

QWidget* shortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    col->addWidget(pageTitle("Keyboard shortcuts"));
    col->addWidget(paragraph("Change any of them in Settings > Shortcuts."));
    std::vector<std::pair<std::string, std::string>> rows;
    const char* group = nullptr;
    auto flush = [&] {
        if (!group) return;
        std::string upper = group;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        col->addSpacing(6);
        col->addWidget(sectionLabel(QString::fromStdString(upper)));
        shortcutTable(col, rows);
        rows.clear();
    };
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        const ActionInfo& info = actionInfo(static_cast<Action>(i));
        if (group && std::strcmp(group, info.group) != 0) flush();
        group = info.group;
        rows.emplace_back(chordName(chordOf(static_cast<Action>(i))), info.label);
    }
    flush();
    col->addSpacing(6);
    col->addWidget(sectionLabel("MOUSE"));
    for (const Shortcut& m : mouseGestures()) rows.emplace_back(m.keys, m.action);
    shortcutTable(col, rows);
    return page;
}

class Mark : public QWidget {
public:
    Mark() { setFixedSize(48, 48); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        drawLogoMark(p, {24, 24}, 23.0f, theme::kAccent);
    }
};

QWidget* aboutPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    head->addWidget(new Mark);
    auto* names = new QVBoxLayout;
    names->setSpacing(0);
    auto* title = pageTitle("bemotion");
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    title->setFont(f);
    names->addWidget(title);
    names->addWidget(hintLabel(QString("Version %1  ·  free and open source").arg(BEMOTION_VERSION)));
    head->addLayout(names, 1);
    col->addLayout(head);
    col->addSpacing(6);
    col->addWidget(paragraph("bemotion is a minimal 2D motion graphics animator. Drop text, shapes and charts on the canvas, set "
                             "keyframes, shape the motion with paths and curves, add effects and export a video.",
                             theme::kText));
    col->addWidget(paragraph("The goal is simplicity without giving up control: the common moves are one click away (quick "
                             "animations, effect presets, snapping), while every property stays animatable and the interface "
                             "follows the industry standards motion designers already know. Themes and plugins let anyone shape "
                             "it to their taste and extend it."));
    col->addWidget(paragraph("Written in C++20 with Qt and Skia; video through FFmpeg. Plugins are sandboxed Lua scripts."));
    col->addSpacing(10);
    auto* buttons = new QHBoxLayout;
    auto* github = accentButton("View on GitHub");
    QObject::connect(github, &QPushButton::clicked, [] { platform::openWithDefaultApp(kRepoUrl); });
    auto* issue = new QPushButton("Report an Issue");
    QObject::connect(issue, &QPushButton::clicked, [] { platform::openWithDefaultApp(std::string(kRepoUrl) + "/issues"); });
    auto* plugin = new QPushButton("Write a Plugin");
    QObject::connect(plugin, &QPushButton::clicked, [] { platform::openWithDefaultApp(std::string(kRepoUrl) + "/blob/main/docs/PLUGINS.md"); });
    buttons->addWidget(github);
    buttons->addWidget(issue);
    buttons->addWidget(plugin);
    buttons->addStretch();
    col->addLayout(buttons);
    return page;
}

QWidget* creditsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    col->addWidget(pageTitle("Credits"));
    col->addWidget(paragraph("Created by Eugênio Benfica and the bemotion contributors. bemotion stands on the shoulders of these "
                             "open source projects:"));
    col->addSpacing(6);
    for (const Credit& c : kCredits) {
        auto* row = new QHBoxLayout;
        auto* name = new QLabel(c.name);
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        row->addWidget(name);
        row->addWidget(paragraph(c.license, theme::kAccent));
        row->addStretch();
        col->addLayout(row);
        col->addWidget(paragraph(QString("%1  ·  %2").arg(c.by, c.use)));
        col->addSpacing(4);
    }
    col->addWidget(hintLabel("Thank you to everyone who builds, tests, reports and shares."));
    return page;
}

QWidget* licensePage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    col->addWidget(pageTitle("License"));
    col->addWidget(paragraph("bemotion is free software under the MIT license: use it for anything, including commercial work, "
                             "and change and share it, as long as the notice below travels with it. Your projects and exported "
                             "videos are entirely yours.",
                             theme::kText));
    auto* text = new QPlainTextEdit(kLicense);
    text->setReadOnly(true);
    text->setMinimumHeight(320);
    col->addWidget(text, 1);
    return page;
}

} // namespace

void openHelp(HelpTab tab) {
    static QPointer<PagedWindow> window;
    if (!window) {
        window = new PagedWindow("Help", {{"Shortcuts", Icon::Text, shortcutsPage},
                                          {"About", Icon::Logo, aboutPage},
                                          {"Credits", Icon::Star, creditsPage},
                                          {"License", Icon::Lock, licensePage}});
        window->resize(820, 620);
        QObject::connect(&AppSignals::get(), &AppSignals::shortcutsChanged, window, [] {
            if (window && window->currentPage() == 0) window->rebuild();
        });
        QObject::connect(&AppSignals::get(), &AppSignals::appearanceChanged, window, [] {
            if (window) window->rebuild();
        });
    }
    window->showPage(static_cast<int>(tab));
    window->present();
}

} // namespace bemotion::ui
