// Help window, in the style of Settings: keyboard shortcuts (drawn as key caps, searchable),
// the guides, about the project, credits and the license.

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
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <set>
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
        {TR_NOOP("Wheel"), TR_NOOP("Zoom the viewport around the cursor")},
        {TR_NOOP("Middle Drag / Space+Drag / Alt+Drag"), TR_NOOP("Pan the viewport")},
        {TR_NOOP("Ctrl+Wheel / Shift+Wheel"), TR_NOOP("Zoom / scroll the timeline")},
        {TR_NOOP("Double Click"), TR_NOOP("Open a compound (its timeline) and pick a layer in it")},
        {TR_NOOP("Shift+Click / Ctrl+Click"), TR_NOOP("Add to / toggle the selection")},
        {TR_NOOP("Alt+Drag"), TR_NOOP("Break a motion path handle into a corner")},
        {TR_NOOP("Shift+Drag"), TR_NOOP("Constrain a move, uniform scale, 15° rotation")},
        {TR_NOOP("Ctrl+Drag"), TR_NOOP("Move without snapping")},
        {TR_NOOP("Pen: Click / Drag"), TR_NOOP("Place a point / a smooth point (the first point again closes the path; Enter: done)")},
        {TR_NOOP("Pen: Click an Outline"), TR_NOOP("Add a point there")},
        {TR_NOOP("Pen: Ctrl+Click / Alt+Click a Point"), TR_NOOP("Remove it / make it a corner or smooth")},
        {TR_NOOP("Alt+Drag a Path Handle"), TR_NOOP("Move it alone (a corner)")},
        {TR_NOOP("Double Click a Marker"), TR_NOOP("Rename it (drag it to move, right-click for its color)")},
        {TR_NOOP("Double Click a tab"), TR_NOOP("Maximize that panel (again to restore)")},
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
    {"whisper.cpp", "Georgi Gerganov and the ggml authors", "MIT", "speech to text (captions)"},
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

// The page's title, with a button on the right (as Settings' Reset Page).
void pageHeader(QVBoxLayout* col, const QString& title, QWidget* right = nullptr) {
    auto* row = new QHBoxLayout;
    row->addWidget(pageTitle(title), 1);
    if (right) row->addWidget(right);
    col->addLayout(row);
    col->addSpacing(2);
}

QPushButton* linkButton(const QString& label, const std::string& url, Icon icon = Icon::ArrowRight) {
    auto* b = new QPushButton(makeIcon(icon, theme::kTextDim, 16), label);
    b->setCursor(Qt::PointingHandCursor);
    QObject::connect(b, &QPushButton::clicked, b, [url] { platform::openWithDefaultApp(url); });
    return b;
}

// Shortcuts, as Settings shows its options: a card per group, the keys on the right. The
// search keeps the rows whose action or keys match.
QWidget* shortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    auto* edit = new QPushButton(makeIcon(Icon::Keyboard, theme::kTextDim, 16), tr("Change Shortcuts..."));
    QObject::connect(edit, &QPushButton::clicked, edit, [] { openSettings(SettingsTab::Shortcuts); });
    pageHeader(col, tr("Keyboard shortcuts"), edit);
    auto* search = new QLineEdit;
    search->setPlaceholderText(tr("Search shortcuts"));
    search->setClearButtonEnabled(true);
    col->addWidget(search);
    col->addSpacing(4);

    struct Row {
        QWidget* row;
        QWidget* section; // its title and card, hidden when nothing in them matches
        QString text;
    };
    auto rows = std::make_shared<std::vector<Row>>();
    std::vector<std::pair<QWidget*, int>> sections; // the section's widgets, how many rows
    auto addGroup = [&](const QString& title, const std::vector<std::pair<std::string, std::string>>& items) {
        auto* holder = new QWidget;
        auto* hcol = new QVBoxLayout(holder);
        hcol->setContentsMargins(0, 0, 0, 0);
        hcol->setSpacing(6);
        SettingsCard* card = addSection(hcol, title);
        for (const auto& [keys, action] : items) {
            QWidget* control = keys.empty() ? static_cast<QWidget*>(hintLabel(tr("None"))) : new KeyCaps(trs(keys).toStdString());
            QWidget* r = card->addRow(trs(action), {}, control);
            rows->push_back({r, holder, trs(action) + " " + trs(keys)});
        }
        col->addWidget(holder);
    };
    std::vector<std::pair<std::string, std::string>> items;
    const char* group = nullptr;
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        const ActionInfo& info = actionInfo(static_cast<Action>(i));
        if (group && std::strcmp(group, info.group) != 0) {
            addGroup(tr(group), items);
            items.clear();
        }
        group = info.group;
        items.emplace_back(chordName(chordOf(static_cast<Action>(i))), info.label);
    }
    if (group) addGroup(tr(group), items);
    items.clear();
    for (const Shortcut& m : mouseGestures()) items.emplace_back(m.keys, m.action);
    addGroup(tr("Mouse"), items);
    QObject::connect(search, &QLineEdit::textChanged, page, [rows](const QString& q) {
        std::set<QWidget*> shown;
        for (const Row& r : *rows) {
            const bool match = q.trimmed().isEmpty() || r.text.contains(q.trimmed(), Qt::CaseInsensitive);
            r.row->setVisible(match);
            if (match) shown.insert(r.section);
        }
        for (const Row& r : *rows) r.section->setVisible(shown.count(r.section) > 0);
    });
    return page;
}

// Where to read more: the guides, one card each.
QWidget* learnPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, tr("Learn"));
    col->addWidget(paragraph(tr("The guides live with bemotion's code on GitHub: what each part does, and how to drive it.")));
    col->addSpacing(4);
    const std::string docs = std::string(kRepoUrl) + "/blob/main/docs/";
    SettingsCard* use = addSection(col, tr("Animating"));
    use->addRow(tr("Expressions"), tr("Formulas on any property: wiggle, loop, follow another layer. The ƒ button opens the library"),
                linkButton(tr("Open"), docs + "EXPRESSIONS.md"));
    use->addRow(tr("Getting around"), tr("Panels, tools, the timeline and the viewport, from the start"),
                linkButton(tr("Open"), std::string(kRepoUrl) + "#readme"));
    SettingsCard* extend = addSection(col, tr("Extending"));
    extend->addRow(tr("Plugins"), tr("New kinds of objects and commands, in Lua"), linkButton(tr("Open"), docs + "PLUGINS.md"));
    extend->addRow(tr("Operations"), tr("Every change a script or an AI can make, with its fields"), linkButton(tr("Open"), docs + "OPERATIONS.md"));
    extend->addRow(tr("Automation and AI"), tr("The command line, and connecting Claude or another assistant (MCP)"),
                   linkButton(tr("Open"), docs + "AUTOMATION.md"));
    SettingsCard* inside = addSection(col, tr("Inside bemotion"));
    inside->addRow(tr("Architecture"), tr("How the code is laid out, for anyone changing it"), linkButton(tr("Open"), docs + "ARCHITECTURE.md"));
    inside->addRow(tr("Report an issue"), tr("Something wrong or missing: tell us on GitHub"),
                   linkButton(tr("Report"), std::string(kRepoUrl) + "/issues", Icon::Help));
    return page;
}

class Mark : public QWidget {
public:
    Mark() { setFixedSize(56, 56); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        drawLogoMark(p, {28, 28}, 27.0f, theme::kAccent);
    }
};

QWidget* aboutPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    // The mark, the name, the version and the slogan, in a card of their own.
    auto* hero = new SettingsCard;
    auto* head = new QWidget;
    auto* hrow = new QHBoxLayout(head);
    hrow->setContentsMargins(4, 6, 4, 6);
    hrow->setSpacing(16);
    hrow->addWidget(new Mark);
    auto* names = new QVBoxLayout;
    names->setSpacing(1);
    auto* title = pageTitle("bemotion");
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.5);
    title->setFont(f);
    names->addWidget(title);
    names->addWidget(paragraph(tr("where motion takes shape")));
    names->addWidget(hintLabel(tr("Version %1  ·  free and open source (MIT)").arg(BEMOTION_VERSION)));
    hrow->addLayout(names, 1);
    hero->addContent(head);
    col->addWidget(hero);
    col->addSpacing(4);
    SettingsCard* idea = addSection(col, tr("The idea"));
    idea->addRow(tr("A video editor first, an animator at heart"), tr("Cut and arrange footage like in Premiere; animate anything like in After Effects"), nullptr);
    idea->addRow(tr("Not an airplane cockpit"), tr("What you need in front of you, the rest out of the way, every panel yours to arrange"), nullptr);
    idea->addRow(tr("Human and robot friendly"), tr("Edited by hand, by scripts and by AI assistants, through the same rules and undo"), nullptr);
    idea->addRow(tr("An engine and a basic app"), tr("A small core you can drive headless, a simple editor on top, the rest as plugins"), nullptr);
    col->addSpacing(4);
    auto* buttons = new QHBoxLayout;
    auto* github = accentButton(tr("View on GitHub"));
    QObject::connect(github, &QPushButton::clicked, [] { platform::openWithDefaultApp(kRepoUrl); });
    buttons->addWidget(github);
    buttons->addWidget(linkButton("Report an Issue", std::string(kRepoUrl) + "/issues", Icon::Help));
    buttons->addWidget(linkButton("Support bemotion", "https://buymeacoffee.com/eugeniobenfica", Icon::Coffee));
    buttons->addStretch();
    col->addLayout(buttons);
    col->addSpacing(6);
    col->addWidget(hintLabel(tr("Written in C++20 with Qt and Skia; video through FFmpeg; plugins are sandboxed Lua scripts.")));
    return page;
}

// The license of a library, as a small pill.
class LicensePill : public QWidget {
public:
    explicit LicensePill(QString text) : text_(std::move(text)) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(fontMetrics().horizontalAdvance(text_) + 20, fontMetrics().height() + 8);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const float h = static_cast<float>(height());
        d.rectFilled({0.5f, 0.5f}, {width() - 0.5f, h - 0.5f}, theme::withAlpha(theme::kAccent, 30), h * 0.5f);
        d.text({10, (h - d.textHeight()) * 0.5f}, theme::kAccent, text_);
    }

private:
    QString text_;
};

QWidget* creditsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, tr("Credits"));
    col->addWidget(paragraph(tr("Created by Eugênio Benfica and the bemotion contributors. bemotion stands on the shoulders of these "
                             "open source projects:")));
    col->addSpacing(4);
    SettingsCard* card = addSection(col, tr("Open source inside bemotion"));
    for (const Credit& c : kCredits) card->addRow(c.name, QString("%1  ·  %2").arg(c.by, tr(c.use)), new LicensePill(c.license));
    col->addSpacing(4);
    col->addWidget(hintLabel(tr("Thank you to everyone who builds, tests, reports and shares.")));
    return page;
}

QWidget* licensePage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, tr("License"));
    SettingsCard* summary = addSection(col, tr("In short"));
    summary->addRow(tr("Free for anything"), tr("Personal and commercial work alike, with no price, account or watermark"), nullptr);
    summary->addRow(tr("Yours to change and share"), tr("As long as the notice below travels with it"), nullptr);
    summary->addRow(tr("Your work is yours"), tr("Projects and exported videos belong entirely to you"), nullptr);
    SettingsCard* full = addSection(col, tr("MIT License"));
    auto* text = new QPlainTextEdit(kLicense);
    text->setReadOnly(true);
    text->setFrameShape(QFrame::NoFrame);
    text->setMinimumHeight(300);
    full->addContent(text);
    return page;
}

// The pages, in their order (HelpTab names them).
int pageOf(HelpTab tab) {
    switch (tab) {
    case HelpTab::Shortcuts: return 0;
    case HelpTab::Learn: return 1;
    case HelpTab::About: return 2;
    case HelpTab::Credits: return 3;
    case HelpTab::License: return 4;
    }
    return 0;
}

} // namespace

void openHelp(HelpTab tab) {
    static QPointer<PagedWindow> window;
    if (!window) {
        window = new PagedWindow(tr("Help"), {{tr("Shortcuts"), Icon::Keyboard, shortcutsPage},
                                          {tr("Learn"), Icon::Help, learnPage},
                                          {tr("About"), Icon::Logo, aboutPage},
                                          {tr("Credits"), Icon::Star, creditsPage},
                                          {tr("License"), Icon::Lock, licensePage}});
        window->resize(860, 660);
        QObject::connect(&AppSignals::get(), &AppSignals::shortcutsChanged, window, [] {
            if (window && window->currentPage() == 0) window->rebuild();
        });
        QObject::connect(&AppSignals::get(), &AppSignals::appearanceChanged, window, [] {
            if (window) window->rebuild();
        });
        // Built again in another language, on the same page and in the same place.
        QObject::connect(&AppSignals::get(), &AppSignals::languageChanged, window, [] {
            PagedWindow* old = window;
            const bool shown = old->isVisible();
            const int page = old->currentPage();
            const QRect geometry = old->geometry();
            window = nullptr;
            old->hide();
            old->deleteLater();
            if (!shown) return;
            const HelpTab tabs[] = {HelpTab::Shortcuts, HelpTab::Learn, HelpTab::About, HelpTab::Credits, HelpTab::License};
            openHelp(tabs[std::clamp(page, 0, 4)]);
            window->setGeometry(geometry);
        });
    }
    window->showPage(pageOf(tab));
    window->present();
}

} // namespace bemotion::ui
