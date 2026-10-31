#include "ui/panels/PropertiesPanel.h"

// The Properties panel: captions, plugin objects' data and tables, backgrounds, file pickers (the
// rest: PropertiesPanel.cpp, PropertiesSections.cpp).

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Automation.h"
#include "app/PresetLibrary.h"
#include "core/Transitions.h"
#include "app/Settings.h"
#include "core/Data.h"
#include "core/Editing.h"
#include "core/Geometry.h"
#include "core/Ops.h"
#include "core/Presets.h"
#include "media/MediaManager.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/LayerMenus.h"
#include "ui/Modules.h"
#include "ui/PluginInstall.h"
#include "ui/PropertyRow.h"
#include "ui/Transcribe.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include "ui/panels/PropertiesInternal.h"

namespace bemotion {

using namespace properties_internal;

void PropertiesPanel::buildCaptions(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Captions"));
    const uint64_t id = layer.id;
    auto captionsOf = [this, id]() -> std::vector<Caption>* {
        Layer* l = ctx_.comp().findLayer(id);
        return l && l->type == LayerType::Text ? &l->text.captions : nullptr;
    };
    auto stamp = [](double t) {
        const int cs = static_cast<int>(std::llround(std::max(0.0, t) * 100.0));
        return QString("%1:%2.%3").arg(cs / 6000).arg(cs / 100 % 60, 2, 10, QChar('0')).arg(cs % 100, 2, 10, QChar('0'));
    };
    auto* table = new QTableWidget(static_cast<int>(layer.text.captions.size()), 3);
    table->setHorizontalHeaderLabels({tr("Start"), tr("End"), tr("Text")});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(true);
    table->setMinimumHeight(220);
    auto fill = [table, stamp](const std::vector<Caption>& caps) {
        const QSignalBlocker b(table);
        table->setRowCount(static_cast<int>(caps.size()));
        for (int r = 0; r < static_cast<int>(caps.size()); ++r) {
            const QString cells[3] = {stamp(caps[r].start), stamp(caps[r].end), QString::fromStdString(caps[r].text)};
            for (int c = 0; c < 3; ++c) {
                QTableWidgetItem* item = table->item(r, c);
                if (!item) table->setItem(r, c, item = new QTableWidgetItem);
                if (item->text() != cells[c]) item->setText(cells[c]);
            }
        }
    };
    fill(layer.text.captions);
    // Typing in a cell edits the caption (times as 1.5, 0:01.50 or 00:00:01,500).
    connect(table, &QTableWidget::cellChanged, this, [this, table, captionsOf, fill](int r, int c) {
        std::vector<Caption>* caps = captionsOf();
        if (!caps || r < 0 || r >= static_cast<int>(caps->size())) return;
        Caption& cap = (*caps)[r];
        const std::string v = table->item(r, c)->text().toStdString();
        double t = 0;
        if (c == 2) cap.text = v;
        else if (parseCaptionTime(v, t)) (c == 0 ? cap.start : cap.end) = t;
        if (cap.end <= cap.start) cap.end = cap.start + 0.5;
        std::stable_sort(caps->begin(), caps->end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
        ctx_.touch();
        fill(*caps);
    });
    // Picking a caption shows it: the playhead goes to its start.
    connect(table, &QTableWidget::cellClicked, this, [this, captionsOf](int r, int) {
        if (std::vector<Caption>* caps = captionsOf(); caps && r >= 0 && r < static_cast<int>(caps->size())) {
            ctx_.playing = false;
            ctx_.setTime((*caps)[r].start + 1e-3);
        }
    });
    refreshers_.push_back([table, captionsOf, fill] {
        if (std::vector<Caption>* caps = captionsOf(); caps && !table->hasFocus()) fill(*caps);
    });
    s->body()->addWidget(table);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add"));
    add->setToolTip(tr("A caption at the playhead (2 seconds, or until the next one)"));
    connect(add, &QPushButton::clicked, this, [this, captionsOf] {
        std::vector<Caption>* caps = captionsOf();
        if (!caps) return;
        const double t = ctx_.time;
        double end = t + 2.0;
        for (const Caption& c : *caps)
            if (c.start > t) end = std::min(end, c.start);
        for (Caption& c : *caps) // the one under the playhead ends there
            if (c.start < t && c.end > t) c.end = t;
        caps->push_back({t, std::max(end, t + 0.2), tr("New caption").toStdString()});
        std::stable_sort(caps->begin(), caps->end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
        ctx_.touch();
    });
    auto* remove = new QPushButton(tr("Remove"));
    remove->setToolTip(tr("Remove the selected captions"));
    connect(remove, &QPushButton::clicked, this, [this, table, captionsOf] {
        std::vector<Caption>* caps = captionsOf();
        if (!caps) return;
        std::vector<int> rows;
        for (const QModelIndex& i : table->selectionModel()->selectedRows()) rows.push_back(i.row());
        std::sort(rows.rbegin(), rows.rend());
        for (int r : rows)
            if (r < static_cast<int>(caps->size())) caps->erase(caps->begin() + r);
        if (!rows.empty()) ctx_.touch();
    });
    auto* fit = new QPushButton(tr("Auto Fit"));
    fit->setToolTip(tr("Split long captions, give each one time to be read and close tiny gaps"));
    connect(fit, &QPushButton::clicked, this, [this, captionsOf] {
        if (std::vector<Caption>* caps = captionsOf(); caps && !caps->empty()) {
            fitCaptions(*caps);
            ctx_.touch();
        }
    });
    auto* import = new QPushButton(tr("Import .srt..."));
    connect(import, &QPushButton::clicked, this, [this, captionsOf] {
        const auto file = platform::openFileDialog(TR_NOOP("Import Subtitles"), TR_NOOP("Subtitles"), ".srt");
        std::vector<Caption>* caps = captionsOf();
        if (!file || !caps) return;
        std::ifstream in(*file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::vector<Caption> read = parseSrt(ss.str());
        if (read.empty()) return;
        std::stable_sort(read.begin(), read.end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
        *caps = std::move(read);
        ctx_.touch();
    });
    auto* exportSrt = new QPushButton(tr("Export .srt..."));
    connect(exportSrt, &QPushButton::clicked, this, [this, captionsOf] {
        std::vector<Caption>* caps = captionsOf();
        if (!caps) return;
        const auto file = platform::saveFileDialog(TR_NOOP("Export Subtitles"), projectsFolderOrDocuments() / platform::fromUtf8(ctx_.project.name + ".srt"), TR_NOOP("Subtitles"), ".srt");
        if (!file) return;
        std::ofstream out(*file, std::ios::binary);
        out << toSrt(*caps);
    });
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(fit);
    row->addStretch();
    s->body()->addLayout(row);
    auto* row2 = new ui::FlowLayout; // wraps when the panel is narrow
    row2->addWidget(import);
    row2->addWidget(exportSrt);
    s->body()->addLayout(row2);
    if (!ui::moduleEnabled(ui::kSpeechToText)) {
        col->addWidget(s);
        return;
    }
    // Speech to text: the clips with sound, one of which fills this layer.
    std::vector<std::pair<uint64_t, std::string>> sounds;
    for (const Layer& l : ctx_.comp().layers)
        if (ui::canTranscribe(ctx_, l.id)) sounds.emplace_back(l.id, l.name);
    auto* transcribe = ui::accentButton(tr("Transcribe Speech..."));
    transcribe->setToolTip(sounds.empty() ? tr("Add a video or audio clip with speech first")
                                          : tr("The words spoken in a clip become these captions, at their time"));
    transcribe->setEnabled(!sounds.empty());
    connect(transcribe, &QPushButton::clicked, this, [this, transcribe, sounds, id] {
        if (sounds.size() == 1) {
            ui::transcribeClip(ctx_, sounds.front().first, id, this);
            return;
        }
        QMenu menu;
        for (const auto& [clip, name] : sounds)
            menu.addAction(ui::menuIcon(ui::Icon::Audio, theme::kTextDim), QString::fromStdString(name),
                           [this, clip = clip, id] { ui::transcribeClip(ctx_, clip, id, this); });
        menu.exec(transcribe->mapToGlobal(QPoint(0, transcribe->height())));
    });
    s->body()->addWidget(transcribe);
    col->addWidget(s);
}

void PropertiesPanel::buildPluginData(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Data"));
    const uint64_t id = layer.id;
    auto rowsOf = [this, id]() -> std::vector<DataRow>* {
        Layer* l = ctx_.comp().findLayer(id);
        return l ? &l->plugin.rows : nullptr;
    };
    // The names a label can take (a map's regions): typed with completion, picked to add a row.
    std::vector<int> unknown;
    const std::vector<std::string> names = PluginHost::get().rowLabels(layer, &unknown);
    QStringList nameList;
    for (const std::string& n : names) nameList << QString::fromStdString(n);
    auto completerFor = [&](QLineEdit* edit) {
        auto* c = new QCompleter(nameList, edit);
        c->setCaseSensitivity(Qt::CaseInsensitive);
        c->setFilterMode(Qt::MatchContains);
        c->setCompletionMode(QCompleter::PopupCompletion);
        edit->setCompleter(c);
        return c;
    };
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 3);
    grid->setColumnStretch(2, 2);
    for (int i = 0; i < static_cast<int>(layer.plugin.rows.size()); ++i) {
        const DataRow& r = layer.plugin.rows[i];
        auto* color = new ui::ColorButton;
        color->setColor(r.color);
        connect(color, &ui::ColorButton::colorChanged, this, [this, rowsOf, i](Color c) {
            if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                (*rows)[i].color = c;
                ctx_.touch();
            }
        });
        auto* label = new QLineEdit(QString::fromStdString(r.label));
        label->setPlaceholderText(names.empty() ? tr("Label") : tr("Type a name"));
        connect(label, &QLineEdit::textEdited, this, [this, rowsOf, i](const QString& t) {
            if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                (*rows)[i].label = t.toStdString();
                ctx_.touch();
            }
        });
        if (!names.empty()) {
            QCompleter* c = completerFor(label);
            connect(c, qOverload<const QString&>(&QCompleter::activated), this, [this, rowsOf, i](const QString& t) {
                if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                    (*rows)[i].label = t.toStdString();
                    ctx_.touch();
                    key_.clear(); // the list of unknown names changes
                }
            });
        }
        auto* value = new ui::DragValue;
        value->setFormat("%g");
        value->setValue(r.value);
        connect(value, &ui::DragValue::valueChanged, this, [this, rowsOf, i](float v) {
            if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                (*rows)[i].value = v;
                ctx_.touch();
            }
        });
        auto* up = new ui::IconButton(ui::Icon::ChevronUp, tr("Move up"), 24);
        connect(up, &QAbstractButton::clicked, this, [this, rowsOf, i] {
            if (auto* rows = rowsOf(); rows && i > 0 && i < static_cast<int>(rows->size())) {
                std::swap((*rows)[i], (*rows)[i - 1]);
                ctx_.touch();
                key_.clear();
            }
        });
        auto* del = new ui::IconButton(ui::Icon::Trash, tr("Remove row"), 24);
        connect(del, &QAbstractButton::clicked, this, [this, rowsOf, i] {
            if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                rows->erase(rows->begin() + i);
                ctx_.touch();
            }
        });
        grid->addWidget(color, i, 0);
        grid->addWidget(label, i, 1);
        grid->addWidget(value, i, 2);
        grid->addWidget(up, i, 3);
        grid->addWidget(del, i, 4);
    }
    s->body()->addLayout(grid);
    if (!unknown.empty()) {
        QStringList wrong;
        for (int i : unknown)
            if (i >= 0 && i < static_cast<int>(layer.plugin.rows.size())) wrong << QString::fromStdString(layer.plugin.rows[i].label);
        auto* warn = new QLabel(tr("Not found, shown nowhere: %1. Type a few letters and pick a name.").arg(wrong.join(", ")));
        warn->setWordWrap(true);
        ui::setTone(warn, ui::Tone::Danger);
        s->body()->addWidget(warn);
    }
    if (!names.empty()) {
        // Add by name: type a few letters, pick one (Enter takes the first match).
        auto* row = new QHBoxLayout;
        auto* pick = new QLineEdit;
        pick->setPlaceholderText(tr("Add: type a name..."));
        pick->setClearButtonEnabled(true);
        QCompleter* c = completerFor(pick);
        auto addNamed = [this, rowsOf, pick, nameList](QString name) {
            name = name.trimmed();
            if (name.isEmpty()) return;
            if (!nameList.contains(name, Qt::CaseInsensitive)) { // the first name containing it
                for (const QString& n : nameList)
                    if (n.contains(name, Qt::CaseInsensitive)) {
                        name = n;
                        break;
                    }
            }
            if (auto* rows = rowsOf()) {
                DataRow r;
                r.label = name.toStdString();
                r.value = rows->empty() ? 50.0f : rows->back().value;
                r.color = labelColor(static_cast<int>(rows->size()));
                rows->push_back(std::move(r));
                ctx_.touch();
                key_.clear();
            }
            QMetaObject::invokeMethod(pick, [pick] { pick->clear(); }, Qt::QueuedConnection);
        };
        connect(c, qOverload<const QString&>(&QCompleter::activated), this, addNamed);
        connect(pick, &QLineEdit::returnPressed, this, [pick, addNamed] { addNamed(pick->text()); });
        auto* all = new QPushButton(tr("Add All"));
        all->setToolTip(tr("A row for every name not in the data yet (value 0)"));
        connect(all, &QPushButton::clicked, this, [this, rowsOf, nameList] {
            auto* rows = rowsOf();
            if (!rows) return;
            for (const QString& n : nameList) {
                const bool present = std::any_of(rows->begin(), rows->end(), [&](const DataRow& r) {
                    return QString::fromStdString(r.label).compare(n, Qt::CaseInsensitive) == 0;
                });
                if (present) continue;
                DataRow r;
                r.label = n.toStdString();
                r.value = 0.0f;
                r.color = labelColor(static_cast<int>(rows->size()));
                rows->push_back(std::move(r));
            }
            ctx_.touch();
            key_.clear();
        });
        row->addWidget(pick, 1);
        row->addWidget(all);
        s->body()->addLayout(row);
    }
    auto* buttons = new ui::FlowLayout; // wraps when the panel is narrow
    auto* add = new QPushButton(tr("+ Add Row"));
    connect(add, &QPushButton::clicked, this, [this, rowsOf] {
        if (auto* rows = rowsOf()) {
            DataRow r;
            r.label = tr("Item %1").arg(rows->size() + 1).toStdString();
            r.value = rows->empty() ? 50.0f : rows->back().value;
            r.color = labelColor(static_cast<int>(rows->size()));
            rows->push_back(std::move(r));
            ctx_.touch();
        }
    });
    auto* paste = new QPushButton(tr("Paste"));
    paste->setToolTip(tr("Replace the data with rows from the clipboard: \"label, value[, #color]\" per line.\n"
                      "Cells copied from a spreadsheet work too."));
    connect(paste, &QPushButton::clicked, this, [this, rowsOf] {
        auto pasted = parseRows(QApplication::clipboard()->text().toStdString());
        if (auto* rows = rowsOf(); rows && !pasted.empty()) {
            *rows = std::move(pasted);
            ctx_.touch();
            key_.clear();
        }
    });
    auto* copy = new QPushButton(tr("Copy"));
    copy->setToolTip(tr("Copy the data as CSV"));
    connect(copy, &QPushButton::clicked, this, [rowsOf] {
        std::string text;
        if (auto* rows = rowsOf())
            for (const DataRow& r : *rows) {
                char value[32];
                std::snprintf(value, sizeof value, "%g", r.value);
                text += r.label + ", " + value + ", " + colorHex(r.color) + "\n";
            }
        QApplication::clipboard()->setText(QString::fromStdString(text));
    });
    auto* csv = new QPushButton(tr("Import CSV..."));
    csv->setToolTip(tr("Replace the data with a CSV file (comma, semicolon or tab separated)"));
    const uint64_t layerId = layer.id;
    connect(csv, &QPushButton::clicked, this, [this, layerId] { importCsv(layerId); });
    if (names.empty()) buttons->addWidget(add);
    else delete add; // named rows are added by name, above (shown parentless, it would flash up as a window)
    buttons->addWidget(paste);
    buttons->addWidget(copy);
    buttons->addWidget(csv);
    s->body()->addLayout(buttons);
    col->addWidget(s);
}

void PropertiesPanel::buildBackgroundRows(CollapsibleSection* section, Layer& layer) {
    const uint64_t id = layer.id;
    auto bg = [this, id]() -> BackgroundProps* {
        Layer* l = ctx_.comp().findLayer(id);
        return l ? &l->background : nullptr;
    };
    auto* fill = newCombo();
    for (int i = 0; i < static_cast<int>(BackgroundFill::Count); ++i) fill->addItem(backgroundFillLabel(static_cast<BackgroundFill>(i)));
    fill->setCurrentIndex(static_cast<int>(layer.background.fill));
    connect(fill, &QComboBox::currentIndexChanged, this, [this, bg](int i) {
        if (BackgroundProps* b = bg(); b && i >= 0) {
            b->fill = static_cast<BackgroundFill>(i);
            ctx_.touch(); // the panel rebuilds: other colors / settings
        }
    });
    section->addPlainRow(tr("Fill"), fill);
    if (layer.background.fill != BackgroundFill::Image) return;
    // The picture: any image or video of the Media panel, or a new import.
    auto* media = newCombo();
    media->addItem(tr("None"));
    int current = 0;
    std::vector<uint64_t> ids{0};
    for (const MediaItem& m : ctx_.comp().media) {
        if (m.kind == MediaKind::Audio) continue;
        ids.push_back(m.id);
        media->addItem(QString::fromStdString(m.name));
        if (m.id == layer.background.mediaId) current = static_cast<int>(ids.size()) - 1;
    }
    media->addItem(tr("Import..."));
    media->setCurrentIndex(current);
    connect(media, &QComboBox::currentIndexChanged, this, [this, bg, ids, id](int i) {
        BackgroundProps* b = bg();
        if (!b || i < 0) return;
        if (i >= static_cast<int>(ids.size())) { // Import...
            const auto files = platform::openFilesDialog(TR_NOOP("Background Picture or Video"), TR_NOOP("Images and videos"),
                                                         {"png", "jpg", "jpeg", "webp", "bmp", "gif", "mp4", "mov", "webm", "mkv", "avi"});
            if (files.empty()) {
                key_.clear();
                return;
            }
            MediaItem item;
            std::string error;
            if (!automationEnvironment().probeMedia(platform::toUtf8(files.front()), item, error)) {
                presetMessage_ = tr("Could not import: %1").arg(QString::fromStdString(error)).toStdString();
                key_.clear();
                return;
            }
            const uint64_t mediaId = ctx_.comp().addMedia(item);
            if (BackgroundProps* b2 = bg()) b2->mediaId = mediaId;
        } else {
            b->mediaId = ids[static_cast<size_t>(i)];
        }
        ctx_.touch();
        key_.clear();
    });
    section->addPlainRow(tr("Picture"), media);
    auto* fit = newCombo();
    for (int i = 0; i < static_cast<int>(BackgroundFit::Count); ++i) fit->addItem(backgroundFitLabel(static_cast<BackgroundFit>(i)));
    fit->setCurrentIndex(static_cast<int>(layer.background.fit));
    connect(fit, &QComboBox::currentIndexChanged, this, [this, bg](int i) {
        if (BackgroundProps* b = bg(); b && i >= 0) {
            b->fit = static_cast<BackgroundFit>(i);
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Fit"), fit);
}

QWidget* PropertiesPanel::filePicker(uint64_t layerId, const std::string& paramId) {
    auto param = [this, layerId, paramId]() -> PluginParam* {
        Layer* l = ctx_.comp().findLayer(layerId);
        return l ? l->plugin.find(paramId) : nullptr;
    };
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto* choose = new QPushButton;
    choose->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    choose->setStyleSheet("text-align: left;");
    h->addWidget(choose, 1);
    auto* clear = new ui::IconButton(ui::Icon::Trash, tr("No file"), 24);
    h->addWidget(clear);
    connect(choose, &QPushButton::clicked, this, [this, param] {
        PluginParam* pp = param();
        if (!pp) return;
        const std::vector<std::string> accept = pp->accept;
        std::string what = tr("Supported files").toStdString();
        if (!accept.empty()) {
            what.clear();
            for (const auto& a : accept) what += (what.empty() ? "" : ", ") + a;
            for (char& c : what) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (auto file = platform::openFileDialog(tr("Choose %1").arg(QString::fromStdString(pp->label)).toUtf8().constData(), what.c_str(), accept)) {
            if (PluginParam* p2 = param()) {
                p2->text = platform::toUtf8(*file);
                p2->relative.clear();
                ctx_.touch();
            }
        }
    });
    connect(clear, &QAbstractButton::clicked, this, [this, param] {
        if (PluginParam* pp = param(); pp && !pp->text.empty()) {
            pp->text.clear();
            pp->relative.clear();
            ctx_.touch();
        }
    });
    // The plugin must be allowed to read files (plugin.json "permissions": ["files"]).
    std::string pluginId;
    if (const Layer* l = ctx_.comp().findLayer(layerId)) pluginId = l->plugin.objectType.substr(0, l->plugin.objectType.find('.'));
    const Plugin* plugin = PluginHost::get().findPlugin(pluginId);
    const bool allowed = plugin && plugin->may("files");
    refreshers_.push_back([choose, clear, param, allowed] {
        const PluginParam* pp = param();
        if (!pp) return;
        std::error_code ec;
        const auto path = platform::fromUtf8(pp->text);
        const bool missing = !pp->text.empty() && !std::filesystem::exists(path, ec);
        QString text = pp->text.empty() ? tr("Choose...") : QString::fromStdString(platform::toUtf8(path.filename()));
        if (missing) text = tr("Missing: %1").arg(text);
        if (choose->text() != text) choose->setText(text);
        choose->setToolTip(!allowed ? tr("This plugin is not allowed to read files (\"files\" permission)")
                                    : pp->text.empty() ? tr("Choose a file") : QString::fromStdString(pp->text));
        ui::setTone(choose, missing || !allowed ? ui::Tone::Danger : ui::Tone::Text);
        clear->setVisible(!pp->text.empty());
    });
    refreshers_.back()();
    return row;
}

void PropertiesPanel::importCsv(uint64_t layerId) {
    const auto file = platform::openFileDialog(TR_NOOP("Import CSV"), TR_NOOP("CSV or text table"), "csv");
    if (!file) return;
    std::ifstream in(*file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    Layer* l = ctx_.comp().findLayer(layerId);
    if (!l) return;
    std::string error;
    if (!PluginHost::get().importCsv(*l, ss.str(), error)) {
        presetMessage_ = tr("Could not import %1: %2").arg(QString::fromStdString(platform::toUtf8(file->filename())), QString::fromStdString(error)).toStdString();
    } else {
        presetMessage_.clear();
        ctx_.touch();
    }
    key_.clear(); // rebuilt with the new data
}

void PropertiesPanel::buildPluginTable(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Table"));
    const uint64_t id = layer.id;
    auto tableOf = [this, id]() -> DataTable* {
        Layer* l = ctx_.comp().findLayer(id);
        return l ? &l->plugin.table : nullptr;
    };
    const DataTable& t = layer.plugin.table;
    const int cols = static_cast<int>(t.header.size());
    // Row 0 is the header (bold); the cells edit the table directly.
    auto* grid = new QTableWidget(static_cast<int>(t.rows.size()) + 1, cols);
    grid->horizontalHeader()->hide();
    grid->verticalHeader()->setDefaultSectionSize(26);
    grid->verticalHeader()->setMinimumWidth(24);
    grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    grid->setVerticalHeaderLabels([&] {
        QStringList l{"H"};
        for (size_t r = 0; r < t.rows.size(); ++r) l << QString::number(r + 1);
        return l;
    }());
    QFont bold = grid->font();
    bold.setWeight(QFont::DemiBold);
    for (int c = 0; c < cols; ++c) {
        auto* h = new QTableWidgetItem(QString::fromStdString(t.header[c]));
        h->setFont(bold);
        grid->setItem(0, c, h);
        for (int r = 0; r < static_cast<int>(t.rows.size()); ++r)
            grid->setItem(r + 1, c, new QTableWidgetItem(QString::fromStdString(c < static_cast<int>(t.rows[r].size()) ? t.rows[r][c] : "")));
    }
    grid->setFixedHeight(std::min(320, 30 + 26 * grid->rowCount()));
    connect(grid, &QTableWidget::cellChanged, this, [this, tableOf, grid](int row, int column) {
        DataTable* table = tableOf();
        if (!table || column >= static_cast<int>(table->header.size())) return;
        const std::string text = grid->item(row, column) ? grid->item(row, column)->text().toStdString() : "";
        if (row == 0) table->header[column] = text;
        else if (row - 1 < static_cast<int>(table->rows.size())) {
            auto& cells = table->rows[row - 1];
            cells.resize(table->header.size());
            cells[column] = text;
        }
        ctx_.touch();
    });
    s->body()->addWidget(grid);

    auto edit = [this, tableOf](std::function<void(DataTable&)> fn) {
        if (DataTable* table = tableOf()) {
            fn(*table);
            ctx_.touch();
            key_.clear();
        }
    };
    auto* buttons = new ui::FlowLayout; // wraps when the panel is narrow
    auto button = [&](const QString& text, const QString& tip, std::function<void()> click) {
        auto* b = new QPushButton(text);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, std::move(click));
        buttons->addWidget(b);
    };
    button(tr("+ Row"), tr("Add a row at the end"), [edit] {
        edit([](DataTable& t) { t.rows.emplace_back(t.header.size()); });
    });
    button(tr("+ Column"), tr("Add a column on the right"), [edit] {
        edit([](DataTable& t) {
            t.header.push_back(tr("Column %1").arg(t.header.size() + 1).toStdString());
            for (auto& r : t.rows) r.resize(t.header.size());
        });
    });
    button(tr("- Row"), tr("Remove the selected row (or the last one)"), [edit, grid] {
        const int sel = grid->currentRow();
        edit([sel](DataTable& t) {
            if (t.rows.empty()) return;
            const int i = sel >= 1 && sel - 1 < static_cast<int>(t.rows.size()) ? sel - 1 : static_cast<int>(t.rows.size()) - 1;
            t.rows.erase(t.rows.begin() + i);
        });
    });
    button(tr("- Column"), tr("Remove the selected column (or the last one)"), [edit, grid] {
        const int sel = grid->currentColumn();
        edit([sel](DataTable& t) {
            if (t.header.size() <= 1) return;
            const int i = sel >= 0 && sel < static_cast<int>(t.header.size()) ? sel : static_cast<int>(t.header.size()) - 1;
            t.header.erase(t.header.begin() + i);
            for (auto& r : t.rows)
                if (i < static_cast<int>(r.size())) r.erase(r.begin() + i);
        });
    });
    s->body()->addLayout(buttons);
    auto* more = new ui::FlowLayout; // wraps when the panel is narrow
    auto* csv = new QPushButton(tr("Import CSV..."));
    csv->setToolTip(tr("Replace the table with a CSV file (comma, semicolon or tab separated)"));
    connect(csv, &QPushButton::clicked, this, [this, id] { importCsv(id); });
    auto* paste = new QPushButton(tr("Paste"));
    paste->setToolTip(tr("Replace the table with cells from the clipboard (copied from a spreadsheet or CSV text)"));
    connect(paste, &QPushButton::clicked, this, [edit] {
        const Cells cells = parseCsv(QApplication::clipboard()->text().toStdString());
        if (!cells.empty()) edit([&cells](DataTable& t) { t = tableFromCells(cells); });
    });
    auto* copy = new QPushButton(tr("Copy"));
    copy->setToolTip(tr("Copy the table as CSV"));
    connect(copy, &QPushButton::clicked, this, [tableOf] {
        if (DataTable* t = tableOf()) QApplication::clipboard()->setText(QString::fromStdString(toCsv(cellsOf(*t))));
    });
    more->addWidget(csv);
    more->addWidget(paste);
    more->addWidget(copy);
    s->body()->addLayout(more);
    col->addWidget(s);
}
} // namespace bemotion
