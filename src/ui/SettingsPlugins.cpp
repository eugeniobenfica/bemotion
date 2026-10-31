// The Settings window's Plugins page: installed plugins, their settings and permissions, the
// built-in features, and the official plugins (the rest: SettingsWindow.cpp).

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

namespace bemotion::ui::settings_internal {

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
    if (permission == "edit") return tr("Edits the project");
    if (permission == "media") return tr("Imports media");
    if (permission == "files") return tr("Reads files you choose");
    if (permission == "microphone") return tr("Uses the microphone");
    return QString::fromStdString(permission);
}

// One plugin, as a row of its card: picture, name and what it does, what it adds and may do,
// and its switch; its folder, removal and log under it.
// A plugin's own settings (plugin.json "settings"): a window of rows, applied at once.
void showPluginSettings(const std::string& pluginId) {
    const Plugin* plugin = PluginHost::get().findPlugin(pluginId);
    if (!plugin || plugin->settings.empty()) return;
    QDialog dialog(g_window);
    dialog.setWindowTitle(tr("%1 Settings").arg(QString::fromStdString(plugin->name)));
    dialog.setMinimumWidth(460);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(16, 16, 16, 16);
    auto* card = new SettingsCard;
    col->addWidget(card);
    auto store = [pluginId](const std::string& id, const nlohmann::json& value) {
        settings().pluginSettings[pluginId][id] = value.dump();
        saveSettings();
        emit AppSignals::get().pluginsChanged(); // what plugins draw changes: the views draw again
    };
    auto current = [pluginId](const std::string& id) -> nlohmann::json {
        const auto& all = settings().pluginSettings;
        if (const auto p = all.find(pluginId); p != all.end())
            if (const auto v = p->second.find(id); v != p->second.end()) return nlohmann::json::parse(v->second, nullptr, false);
        return nullptr;
    };
    for (const PluginSetting& s : plugin->settings) {
        const nlohmann::json value = current(s.id);
        const std::string id = s.id;
        QWidget* control = nullptr;
        switch (s.kind) {
        case PluginSetting::Kind::Toggle:
            control = switchControl(value.is_boolean() ? value.get<bool>() : s.toggle, [store, id](bool on) { store(id, on); });
            break;
        case PluginSetting::Kind::Number: {
            auto* v = new DragValue;
            if (s.max > s.min) v->setRange(static_cast<float>(s.min), static_cast<float>(s.max));
            v->setSpeed(static_cast<float>(std::max(0.001, s.step)));
            v->setFormat(s.format.c_str());
            v->setValue(static_cast<float>(value.is_number() ? value.get<double>() : s.number));
            v->setFixedWidth(120);
            QObject::connect(v, &DragValue::valueChanged, v, [store, id](float x) { store(id, x); });
            control = v;
            break;
        }
        case PluginSetting::Kind::Choice: {
            auto* combo = new QComboBox;
            for (const std::string& c : s.choices) combo->addItem(trs(c), QString::fromStdString(c));
            combo->setCurrentIndex(std::max(0, combo->findData(QString::fromStdString(value.is_string() ? value.get<std::string>() : s.text))));
            QObject::connect(combo, &QComboBox::activated, combo, [store, id, combo](int) { store(id, combo->currentData().toString().toStdString()); });
            control = combo;
            break;
        }
        case PluginSetting::Kind::Text: {
            auto* edit = new QLineEdit(QString::fromStdString(value.is_string() ? value.get<std::string>() : s.text));
            edit->setMinimumWidth(180);
            QObject::connect(edit, &QLineEdit::editingFinished, edit, [store, id, edit] { store(id, edit->text().toStdString()); });
            control = edit;
            break;
        }
        }
        card->addRow(trs(s.label), trs(s.description), control);
    }
    auto* buttons = new QHBoxLayout;
    auto* reset = new QPushButton(tr("Reset"));
    reset->setProperty("flat", true);
    reset->setToolTip(tr("Back to the plugin's defaults"));
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&dialog, pluginId] {
        settings().pluginSettings.erase(pluginId);
        saveSettings();
        emit AppSignals::get().pluginsChanged();
        dialog.accept();
        QTimer::singleShot(0, [pluginId] { showPluginSettings(pluginId); }); // shown again, at the defaults
    });
    buttons->addWidget(reset);
    buttons->addStretch();
    auto* done = accentButton(tr("Done"));
    QObject::connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(done);
    col->addLayout(buttons);
    dialog.exec();
}

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
    auto* version = new QLabel(tr("Version %1").arg(QString::fromStdString(p.version)) + QString::fromStdString(p.author.empty() ? "" : "  ·  " + p.author));
    setTone(version, Tone::TextFaint);
    head->addWidget(version);
    head->addStretch();
    body->addLayout(head);
    if (!p.description.empty()) body->addWidget(pluginText(QString::fromStdString(p.description), Tone::TextDim));
    if (!ok) body->addWidget(pluginText(tr("Could not load: %1").arg(QString::fromStdString(p.error)), Tone::Danger));
    if (!objects.empty()) body->addWidget(pluginText(tr("Adds  %1").arg(QString::fromStdString(objects)), Tone::Text));
    if (!commands.empty()) body->addWidget(pluginText(tr("Commands  %1").arg(QString::fromStdString(commands)), Tone::Text));
    if (!p.drawError.empty()) body->addWidget(pluginText(tr("Last error: %1").arg(QString::fromStdString(p.drawError)), Tone::Danger));
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
    auto* show = quiet(tr("Show Folder"));
    const std::filesystem::path dir = p.dir;
    QObject::connect(show, &QPushButton::clicked, show, [dir] { platform::revealInFileManager(dir); });
    actions->addWidget(show);
    const std::string id = p.id;
    if (!p.builtin) {
        auto* remove = quiet(tr("Remove..."));
        remove->setToolTip(tr("Move this plugin to the Trash"));
        QObject::connect(remove, &QPushButton::clicked, remove, [id, pname = QString::fromStdString(p.name)] {
            const QString question = tr("Move %name% to the Trash?").replace("%name%", pname);
            if (QMessageBox::question(g_window, tr("Remove Plugin"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
                QMessageBox::Yes)
                return;
            std::string error;
            g_message = PluginHost::get().uninstall(id, &error) ? tr("Plugin moved to the Trash.").toStdString() : tr("Could not remove: %1").arg(QString::fromStdString(error)).toStdString();
            emit AppSignals::get().pluginsChanged();
            if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
        });
        actions->addWidget(remove);
    }
    QPlainTextEdit* log = nullptr;
    if (!p.log.empty()) {
        auto* toggle = quiet(tr("Log (%1)").arg(p.log.size()));
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
    enabled->setAccessibleName(tr("Enable %1").arg(QString::fromStdString(p.name)));
    enabled->setToolTip(p.enabled ? tr("On: its objects and commands are in the editor") : tr("Off: hidden from the editor"));
    QObject::connect(enabled, &QAbstractButton::toggled, enabled, [id](bool on) {
        PluginHost::get().setEnabled(id, on);
        emit AppSignals::get().pluginsChanged();
        if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
    });
    // Its own settings: a gear beside the switch, where the card's controls are.
    if (!p.settings.empty()) {
        const std::string pluginId = p.id;
        auto* gear = new IconButton(Icon::Gear, tr("This plugin's own settings"), 28);
        gear->setEnabled(p.enabled && ok);
        gear->setAccessibleName(tr("%1 Settings").arg(QString::fromStdString(p.name)));
        QObject::connect(gear, &QAbstractButton::clicked, gear, [pluginId] { showPluginSettings(pluginId); });
        h->addWidget(gear, 0, Qt::AlignTop);
    }
    h->addWidget(enabled, 0, Qt::AlignTop);
    return row;
}

// The official plugins from bemotion's repository: listed only after the user asks (it is the
// only time the page goes online), then each one a click away.
struct OfficialPlugins {
    enum State { Idle, Loading, Ready, Failed } state = Idle;
    std::vector<CatalogPlugin> plugins;
    QString error;
    std::string installing; // the id being downloaded
    int percent = 0;
};
OfficialPlugins& official() {
    static OfficialPlugins o;
    return o;
}

void rebuildSoon() {
    if (g_window) QTimer::singleShot(0, g_window, [] { if (g_window) g_window->rebuild(); });
}

QString sizeText(int64_t bytes) {
    return bytes >= 1024 * 1024 ? tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1) : tr("%1 KB").arg(std::max<int64_t>(1, bytes / 1024));
}

void addOfficialPlugins(QVBoxLayout* col) {
    OfficialPlugins& o = official();
    SettingsCard* card = addSection(col, tr("Official plugins"), tr("Made by bemotion, kept in its repository on GitHub. Each file is "
                                                            "checked before the plugin is installed."));
    if (o.state == OfficialPlugins::Idle || o.state == OfficialPlugins::Failed) {
        auto* show = new QPushButton(o.state == OfficialPlugins::Failed ? tr("Try Again") : tr("Show Official Plugins"));
        QObject::connect(show, &QPushButton::clicked, show, [] {
            official().state = OfficialPlugins::Loading;
            rebuildSoon();
            net::fetchPluginCatalog(g_window, [](bool ok, std::vector<CatalogPlugin> plugins, QString error) {
                OfficialPlugins& o = official();
                o.state = ok ? OfficialPlugins::Ready : OfficialPlugins::Failed;
                o.plugins = std::move(plugins);
                o.error = error;
                rebuildSoon();
            });
        });
        card->addRow(tr("Get more plugins"),
                     o.state == OfficialPlugins::Failed ? tr("Could not reach them: ") + o.error
                                                        : tr("bemotion connects to GitHub only when you click, to list them"),
                     show);
        return;
    }
    if (o.state == OfficialPlugins::Loading) {
        card->addRow(tr("Looking for official plugins..."), "", nullptr);
        return;
    }
    if (o.plugins.empty()) card->addRow(tr("No official plugins for this version"), "", nullptr);
    for (const CatalogPlugin& c : o.plugins) {
        const Plugin* have = PluginHost::get().findPlugin(c.id);
        const bool newer = have && compareVersions(c.version, have->version) > 0;
        QString about = QString::fromStdString(c.description);
        about += QString("\n%1  ·  %2").arg(QString::fromStdString(c.version), sizeText(c.size()));
        if (!c.permissions.empty()) {
            QStringList perms;
            for (const std::string& p : c.permissions) perms << QString::fromStdString(p);
            about += tr("  ·  may: %1").arg(perms.join(", "));
        }
        QPushButton* button = nullptr;
        if (o.installing == c.id) {
            button = new QPushButton(tr("Downloading %1%").arg(o.percent));
            button->setEnabled(false);
        } else if (have && !newer) {
            button = new QPushButton(have->builtin ? tr("Included") : tr("Installed"));
            button->setProperty("flat", true);
            button->setEnabled(false);
        } else {
            button = accentButton(newer ? tr("Update") : tr("Get"));
            button->setEnabled(o.installing.empty());
            const CatalogPlugin plugin = c;
            QObject::connect(button, &QPushButton::clicked, button, [plugin] {
                official().installing = plugin.id;
                official().percent = 0;
                rebuildSoon();
                net::installCatalogPlugin(
                    plugin, g_window,
                    [plugin](bool ok, QString error) {
                        official().installing.clear();
                        g_message = (ok ? tr("\"%1\" installed.").arg(QString::fromStdString(plugin.name))
                                        : tr("Could not install \"%1\": %2").arg(QString::fromStdString(plugin.name), error)).toStdString();
                        emit AppSignals::get().pluginsChanged(); // (rebuilds the page after the reload)
                    },
                    [plugin](int64_t received, int64_t total) {
                        const int percent = total > 0 ? static_cast<int>(received * 100 / total) : 0;
                        if (percent / 10 == official().percent / 10) return;
                        official().percent = percent;
                        rebuildSoon();
                    });
            });
        }
        card->addRow(QString::fromStdString(c.name), about, button);
    }
}

QWidget* pluginsPage() {
    PluginHost& host = PluginHost::get();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    addPageHeader(col, tr("Plugins"), [] { enableAllPlugins(); });
    col->addWidget(hintLabel(tr("Plugins add objects and commands to the editor. They reach the project, media, files or the "
                             "microphone only with the permissions listed on them.")));
    auto* install = accentButton(tr("Install Plugin..."));
    install->setToolTip(tr("Choose the plugin.json file in a plugin's folder"));
    QObject::connect(install, &QPushButton::clicked, install, [] {
        if (auto manifest = platform::openFileDialog(TR_NOOP("Install Plugin (choose its plugin.json)"), TR_NOOP("bemotion plugin"), "json")) {
            std::string error;
            g_message = PluginHost::get().install(*manifest, &error) ? tr("Plugin installed.").toStdString() : tr("Could not install: %1").arg(QString::fromStdString(error)).toStdString();
            emit AppSignals::get().pluginsChanged();
            if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
        }
    });
    auto* folder = new QPushButton(tr("Open Folder"));
    folder->setToolTip(tr("Where installed plugins are kept"));
    QObject::connect(folder, &QPushButton::clicked, folder, [] { platform::revealInFileManager(PluginHost::userDir()); });
    auto* reload = new QPushButton(tr("Reload"));
    reload->setToolTip(tr("Load every plugin again (after editing one)"));
    QObject::connect(reload, &QPushButton::clicked, reload, [] {
        PluginHost::get().requestReload();
        emit AppSignals::get().pluginsChanged();
        if (g_window) QTimer::singleShot(200, g_window, [] { if (g_window) g_window->rebuild(); });
    });
    auto* docs = new QPushButton(tr("Write a Plugin"));
    docs->setProperty("flat", true);
    docs->setToolTip(tr("The plugin guide (opens in the browser)"));
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
    if (settings().essentialsOnly) {
        SettingsCard* off = addSection(col, tr("Essentials only"));
        auto* on = accentButton(tr("Turn Plugins On"));
        QObject::connect(on, &QPushButton::clicked, on, [] {
            settings().essentialsOnly = false;
            saveSettings();
            PluginHost::get().requestReload();
            emit AppSignals::get().pluginsChanged();
            emit AppSignals::get().modulesChanged();
        });
        off->addRow(tr("Plugins are off"), tr("Essentials only (Settings › Interface) keeps bemotion to its core: none of these load"), on);
    }

    // Features of the app itself that can be turned off like plugins (ui/Modules.h).
    SettingsCard* features = addSection(col, tr("Built-in features"),
                                        tr("Part of bemotion. Turned off, they leave the menus and tools; scripts and AI tools can still use them"));
    for (const Module& m : modules()) {
        const std::string id = m.id;
        Switch* sw = switchControl(moduleEnabled(id), [id](bool on) { setModuleEnabled(id, on); });
        sw->setEnabled(!settings().essentialsOnly);
        features->addRow(tr(m.name), tr(m.description), sw);
    }

    // What comes with bemotion first; then what was installed (or how to).
    SettingsCard* builtin = nullptr;
    for (const Plugin& p : host.plugins()) {
        if (!p.builtin) continue;
        if (!builtin) builtin = addSection(col, tr("Included with bemotion"));
        builtin->addContent(pluginRow(p))->layout()->setContentsMargins(0, 0, 0, 0);
    }
    SettingsCard* installed = addSection(col, tr("Installed"));
    bool anyInstalled = false;
    for (const Plugin& p : host.plugins()) {
        if (p.builtin) continue;
        installed->addContent(pluginRow(p))->layout()->setContentsMargins(0, 0, 0, 0);
        anyInstalled = true;
    }
    if (!anyInstalled)
        installed->addContent(pluginText(tr("None yet. Install Plugin adds one from its folder (its plugin.json file)."), Tone::TextFaint));
    addOfficialPlugins(col);
    return page;
}

} // namespace bemotion::ui::settings_internal
