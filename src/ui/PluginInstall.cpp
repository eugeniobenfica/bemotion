#include "ui/PluginInstall.h"

#include "app/Downloads.h"
#include "app/PluginHost.h"
#include "app/Platform.h"
#include "core/Project.h"
#include "ui/Language.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace bemotion::ui {

std::vector<std::string> missingPlugins(const Composition& comp) {
    std::vector<std::string> ids;
    for (const Layer& l : comp.layers) {
        if (l.type != LayerType::Plugin) continue;
        const std::string& type = l.plugin.objectType; // "<plugin id>.<object id>"
        const std::string id = type.substr(0, type.find('.'));
        if (id.empty() || PluginHost::get().findPlugin(id)) continue;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    return ids;
}

namespace {

QString joined(const std::vector<std::string>& ids) {
    QStringList names;
    for (const std::string& id : ids) names << QString::fromStdString(id);
    return names.join(", ");
}

// One plugin after another: `left` shrinks as they arrive; `failed` keeps what did not.
struct Queue {
    std::vector<CatalogPlugin> left;
    QStringList failed;
    QStringList installed;
};

void next(std::shared_ptr<Queue> q, QPointer<QDialog> dialog, QLabel* stage, QProgressBar* bar, QWidget* parent) {
    if (!dialog) return;
    if (q->left.empty()) {
        dialog->close();
        if (!q->failed.isEmpty())
            QMessageBox::warning(parent, tr("Plugins"), tr("Could not install: %list").replace("%list", q->failed.join("\n")));
        return;
    }
    const CatalogPlugin plugin = q->left.front();
    q->left.erase(q->left.begin());
    stage->setText(tr("Installing \"%name\"...").replace("%name", QString::fromStdString(plugin.name)));
    bar->setValue(0);
    net::installCatalogPlugin(
        plugin, dialog,
        [q, dialog, stage, bar, parent, plugin](bool ok, QString error) {
            if (ok) q->installed << QString::fromStdString(plugin.name);
            else q->failed << QString::fromStdString(plugin.name) + ": " + error;
            platform::log(std::string(ok ? "installed plugin " : "could not install plugin ") + plugin.id);
            next(q, dialog, stage, bar, parent);
        },
        [bar](int64_t received, int64_t total) { bar->setValue(total > 0 ? static_cast<int>(received * 1000 / total) : 0); });
}

} // namespace

void installOfficialPlugins(QWidget* parent, std::vector<std::string> ids, bool ask) {
    if (ids.empty()) return;
    if (ask) {
        QMessageBox box(parent);
        box.setWindowTitle(tr("Plugins"));
        box.setText(tr("This project uses plugins that are not installed: %list").replace("%list", joined(ids)));
        box.setInformativeText(tr("Download them from bemotion's repository on GitHub? Their objects show as placeholders until then."));
        QPushButton* download = box.addButton(tr("Download"), QMessageBox::AcceptRole);
        download->setProperty("accent", true);
        box.addButton(tr("Not Now"), QMessageBox::RejectRole);
        box.setDefaultButton(download);
        box.exec();
        if (box.clickedButton() != download) return;
    }

    auto* dialog = new QDialog(parent);
    dialog->setWindowTitle(tr("Downloading Plugins"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMinimumWidth(420);
    auto* col = new QVBoxLayout(dialog);
    col->setContentsMargins(24, 20, 24, 18);
    col->setSpacing(8);
    auto* stage = new QLabel(tr("Looking for official plugins..."));
    col->addWidget(stage);
    auto* bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    col->addWidget(bar);
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    row->addWidget(cancel);
    col->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::close); // closing aborts the downloads
    dialog->show();

    QPointer<QDialog> guard(dialog);
    net::fetchPluginCatalog(dialog, [guard, stage, bar, parent, ids](bool ok, std::vector<CatalogPlugin> catalog, QString error) {
        if (!guard) return;
        if (!ok) {
            guard->close();
            QMessageBox::warning(parent, tr("Plugins"), tr("Could not reach them: ") + error);
            return;
        }
        auto q = std::make_shared<Queue>();
        for (const std::string& id : ids) {
            const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const CatalogPlugin& c) { return c.id == id; });
            if (it != catalog.end()) q->left.push_back(*it);
            else q->failed << QString::fromStdString(id) + ": " + tr("not an official plugin for this version");
        }
        next(q, guard, stage, bar, parent);
    });
}

} // namespace bemotion::ui
