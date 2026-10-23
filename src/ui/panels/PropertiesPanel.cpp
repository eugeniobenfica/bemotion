#include "ui/panels/PropertiesPanel.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/PresetLibrary.h"
#include "core/Data.h"
#include "core/Presets.h"
#include "media/MediaManager.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/PropertyRow.h"
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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

QLabel* faint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

QLabel* dim(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextDim);
    return l;
}

// Combo boxes size to the panel, not to their longest entry (font names can be long).
QComboBox* newCombo() {
    auto* c = new QComboBox;
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(6);
    return c;
}

bool hasAnimatedParams(const Layer& layer, const std::string& group) {
    return std::any_of(layer.plugin.params.begin(), layer.plugin.params.end(), [&](const PluginParam& p) {
        return p.group == group && (p.kind == PluginParamKind::Number || p.kind == PluginParamKind::Color);
    });
}

std::string colorHex(Color c) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

// "label, value[, #RRGGBB]" per line; tabs (spreadsheet paste) and semicolons work too.
std::vector<DataRow> parseRows(const std::string& text) {
    std::vector<DataRow> rows;
    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        for (char& c : line)
            if (c == '\t' || c == ';') c = ',';
        std::vector<std::string> cells;
        std::stringstream ls(line);
        std::string cell;
        while (std::getline(ls, cell, ',')) {
            const size_t a = cell.find_first_not_of(" \r\"");
            const size_t b = cell.find_last_not_of(" \r\"");
            cells.push_back(a == std::string::npos ? "" : cell.substr(a, b - a + 1));
        }
        if (cells.size() < 2) continue;
        char* end = nullptr;
        const float v = std::strtof(cells[1].c_str(), &end);
        if (end == cells[1].c_str()) continue; // header line
        DataRow r;
        r.label = cells[0];
        r.value = v;
        r.color = labelColor(static_cast<int>(rows.size()));
        if (cells.size() > 2 && cells[2].size() == 7 && cells[2][0] == '#')
            r.color = Color::fromHex(static_cast<unsigned>(std::strtoul(cells[2].c_str() + 1, nullptr, 16)));
        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace

PropertiesPanel::PropertiesPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

// What the panel's widgets depend on: when it changes, they are rebuilt.
std::string PropertiesPanel::structureKey() {
    std::ostringstream k;
    Layer* layer = ctx_.selected();
    k << ctx_.selection.size() << ':';
    if (!layer) return k.str() + "comp";
    k << layer->id << ':' << static_cast<int>(layer->type) << ':' << static_cast<int>(layer->shape.kind) << ':' << layer->parent << ':'
      << layer->plugin.objectType << ':' << layer->plugin.rows.size() << ':' << layer->plugin.table.rows.size() << 'x'
      << layer->plugin.table.header.size() << ':' << layer->media.mediaId << ':' << layer->media.hasAudio
      << ':' << PresetLibrary::all().size() << ':' << presetMessage_;
    if (layer->type == LayerType::Compound)
        for (const Layer& c : ctx_.comp().layers)
            if (c.parent == layer->id) k << ',' << c.id << c.name;
    for (const PropertyRef& p : layer->properties())
        if (p.effectIndex < 0) k << p.id << ';';
    return k.str();
}

void PropertiesPanel::sync() {
    if (!isVisible()) return;
    const std::string key = structureKey();
    if (key != key_) {
        key_ = key;
        rebuild();
        return;
    }
    for (PropertyRow* r : rows_) r->refresh();
    for (auto& f : refreshers_) f();
}

void PropertiesPanel::rebuild() {
    const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
    rows_.clear();
    refreshers_.clear();
    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(4);
    if (Layer* layer = ctx_.selected()) buildLayer(col, *layer);
    else buildComposition(col);
    col->addStretch();
    setWidget(content);
    verticalScrollBar()->setValue(scroll);
}

// --- composition ---------------------------------------------------------------------------

void PropertiesPanel::buildComposition(QVBoxLayout* col) {
    auto* title = new QLabel("Composition");
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    f.setWeight(QFont::DemiBold);
    title->setFont(f);
    col->addWidget(title);
    col->addWidget(faint("Select a layer to edit it."));
    col->addSpacing(4);

    auto* section = new CollapsibleSection("Composition Settings");
    auto sizeValue = [this](int Composition::*field) {
        auto* v = new ui::DragValue;
        v->setRange(16.0f, 16384.0f);
        v->setSpeed(1.0f);
        v->setFormat("%.0f px");
        connect(v, &ui::DragValue::valueChanged, this, [this, field](float x) {
            ctx_.comp().*field = static_cast<int>(std::lround(x));
            ctx_.comp().syncCameras();
            ctx_.touch();
        });
        return v;
    };
    auto* w = sizeValue(&Composition::width);
    section->addPlainRow("Width", w);
    auto* h = sizeValue(&Composition::height);
    section->addPlainRow("Height", h);
    auto* fps = new ui::DragValue;
    fps->setRange(1.0f, 240.0f);
    fps->setSpeed(0.1f);
    fps->setFormat("%.2f fps");
    connect(fps, &ui::DragValue::valueChanged, this, [this](float v) {
        ctx_.comp().fps = v;
        ctx_.setTime(ctx_.time);
        ctx_.touch();
    });
    section->addPlainRow("Frame Rate", fps);
    auto* duration = new QLabel;
    duration->setToolTip("The composition ends where its last clip ends: add objects or extend clips to make it longer.");
    section->addPlainRow("Duration", duration);
    auto* bg = new ui::ColorButton;
    bg->setAlphaEnabled(false);
    connect(bg, &ui::ColorButton::colorChanged, this, [this](Color c) {
        ctx_.comp().background = {c.r, c.g, c.b, 1.0f};
        ctx_.touch();
    });
    auto* bgRow = new QWidget;
    auto* bgLayout = new QHBoxLayout(bgRow);
    bgLayout->setContentsMargins(0, 0, 0, 0);
    bgLayout->addWidget(bg);
    auto* bgHex = dim("");
    bgLayout->addWidget(bgHex, 1);
    section->addPlainRow("Background", bgRow);
    col->addWidget(section);

    refreshers_.push_back([=, this] {
        const Composition& c = ctx_.comp();
        w->setValue(static_cast<float>(c.width));
        h->setValue(static_cast<float>(c.height));
        fps->setValue(static_cast<float>(c.fps));
        duration->setText(QString("%1 s").arg(c.duration, 0, 'f', 2));
        bg->setColor(c.background);
        bgHex->setText(QString::fromStdString(colorHex(c.background)));
    });
    refreshers_.back()();
}

// --- layer -----------------------------------------------------------------------------------

void PropertiesPanel::buildLayer(QVBoxLayout* col, Layer& layer) {
    const uint64_t id = layer.id;
    // Header: type icon + editable name + delete.
    {
        auto* row = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(ui::iconForLayer(layer), theme::kAccent, 22).pixmap(22, 22));
        row->addWidget(icon);
        auto* name = new QLineEdit(QString::fromStdString(layer.name));
        connect(name, &QLineEdit::editingFinished, this, [this, id, name] {
            if (Layer* l = ctx_.comp().findLayer(id); l && l->name != name->text().toStdString()) {
                l->name = name->text().toStdString();
                ctx_.touch();
            }
        });
        row->addWidget(name, 1);
        auto* trash = new ui::IconButton(ui::Icon::Trash, "Delete this layer (Delete)", 26);
        connect(trash, &QAbstractButton::clicked, this, [this] { ctx_.deleteSelected(); });
        row->addWidget(trash);
        col->addLayout(row);
        refreshers_.push_back([this, id, name] {
            if (const Layer* l = ctx_.comp().findLayer(id); l && !name->hasFocus()) name->setText(QString::fromStdString(l->name));
        });
    }
    if (ctx_.selection.size() > 1) {
        auto* n = new QLabel(QString("%1 layers selected").arg(ctx_.selection.size()));
        ui::setTone(n, ui::Tone::Accent);
        col->addWidget(n);
        col->addWidget(faint("Properties below belong to this layer. Quick animations apply to all selected layers."));
    }
    // Compounds: where this layer lives, and what a compound contains.
    if (const Layer* parent = layer.parent ? ctx_.comp().findLayer(layer.parent) : nullptr) {
        auto* row = new QHBoxLayout;
        row->addWidget(dim(QString("Inside \"%1\"").arg(QString::fromStdString(parent->name))), 1);
        auto* select = new QPushButton("Select Compound");
        const uint64_t pid = parent->id;
        connect(select, &QPushButton::clicked, this, [this, pid] { ctx_.select(pid); });
        row->addWidget(select);
        col->addLayout(row);
    }
    if (layer.type == LayerType::Compound) {
        auto* section = new CollapsibleSection("Contents");
        for (const Layer& child : ctx_.comp().layers) {
            if (child.parent != layer.id) continue;
            auto* b = new QPushButton(ui::makeIcon(ui::iconForLayer(child), theme::kTextDim, 16), QString::fromStdString(child.name));
            b->setFlat(true);
            b->setStyleSheet("text-align: left; padding: 4px 8px;");
            const uint64_t cid = child.id;
            connect(b, &QPushButton::clicked, this, [this, cid] { ctx_.select(cid); });
            section->body()->addWidget(b);
        }
        section->body()->addWidget(faint("Double-click the compound in the viewport, or a layer here, to edit what is inside."));
        auto* release = new QPushButton("Release Compound");
        release->setToolTip("Put the layers back where the compound is, keeping how they look (Ctrl+Shift+G)");
        connect(release, &QPushButton::clicked, this, [this] { ctx_.releaseSelectedCompounds(); });
        section->body()->addWidget(release);
        col->addWidget(section);
    }
    if (layer.type == LayerType::Media) buildMediaSource(col, layer);

    // Property groups in order, with their non-animatable settings first.
    CollapsibleSection* section = nullptr;
    std::string group;
    for (const PropertyRef& prop : layer.properties()) {
        if (prop.effectIndex >= 0) continue; // shown in the Effects tab
        if (!section || group != prop.group) {
            group = prop.group;
            section = new CollapsibleSection(QString::fromUtf8(prop.group));
            col->addWidget(section);
            if (layer.type == LayerType::Plugin) {
                for (PluginParam& p : layer.plugin.params) {
                    if (p.group != group || p.kind == PluginParamKind::Number || p.kind == PluginParamKind::Color) continue;
                    const std::string pid = p.id;
                    auto param = [this, id, pid]() -> PluginParam* {
                        Layer* l = ctx_.comp().findLayer(id);
                        return l ? l->plugin.find(pid) : nullptr;
                    };
                    switch (p.kind) {
                    case PluginParamKind::Toggle: {
                        auto* cb = new QCheckBox;
                        connect(cb, &QCheckBox::toggled, this, [this, param](bool on) {
                            if (PluginParam* pp = param(); pp && pp->toggle != on) {
                                pp->toggle = on;
                                ctx_.touch();
                            }
                        });
                        section->addPlainRow(QString::fromStdString(p.label), cb);
                        refreshers_.push_back([cb, param] {
                            if (PluginParam* pp = param()) {
                                const QSignalBlocker b(cb);
                                cb->setChecked(pp->toggle);
                            }
                        });
                        break;
                    }
                    case PluginParamKind::Choice: {
                        auto* combo = newCombo();
                        for (const auto& c : p.choices) combo->addItem(QString::fromStdString(c));
                        connect(combo, &QComboBox::currentIndexChanged, this, [this, param](int i) {
                            if (PluginParam* pp = param(); pp && pp->choice != i && i >= 0) {
                                pp->choice = i;
                                ctx_.touch();
                            }
                        });
                        section->addPlainRow(QString::fromStdString(p.label), combo);
                        refreshers_.push_back([combo, param] {
                            if (PluginParam* pp = param(); pp && !combo->view()->isVisible()) {
                                const QSignalBlocker b(combo);
                                combo->setCurrentIndex(pp->choice);
                            }
                        });
                        break;
                    }
                    case PluginParamKind::Text: {
                        auto* edit = new QLineEdit;
                        connect(edit, &QLineEdit::textEdited, this, [this, param](const QString& t) {
                            if (PluginParam* pp = param()) {
                                pp->text = t.toStdString();
                                ctx_.touch();
                            }
                        });
                        section->addPlainRow(QString::fromStdString(p.label), edit);
                        refreshers_.push_back([edit, param] {
                            if (PluginParam* pp = param(); pp && !edit->hasFocus()) edit->setText(QString::fromStdString(pp->text));
                        });
                        break;
                    }
                    default: break;
                    }
                }
            } else if (group == "Shape") {
                auto* kind = newCombo();
                for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) kind->addItem(shapeKindLabel(static_cast<ShapeKind>(i)));
                kind->setCurrentIndex(static_cast<int>(layer.shape.kind));
                connect(kind, &QComboBox::currentIndexChanged, this, [this, id](int i) {
                    if (Layer* l = ctx_.comp().findLayer(id); l && i >= 0) {
                        l->shape.kind = static_cast<ShapeKind>(i);
                        ctx_.touch();
                    }
                });
                section->addPlainRow("Type", kind);
            } else if (group == "Fill" || group == "Stroke") {
                const bool fill = group == "Fill";
                auto* on = new QCheckBox;
                connect(on, &QCheckBox::toggled, this, [this, id, fill](bool v) {
                    if (Layer* l = ctx_.comp().findLayer(id)) {
                        (fill ? l->shape.fillEnabled : l->shape.strokeEnabled) = v;
                        ctx_.touch();
                    }
                });
                section->addPlainRow("Enabled", on);
                refreshers_.push_back([this, id, on, fill] {
                    if (const Layer* l = ctx_.comp().findLayer(id)) {
                        const QSignalBlocker b(on);
                        on->setChecked(fill ? l->shape.fillEnabled : l->shape.strokeEnabled);
                    }
                });
            } else if (group == "Text" && layer.type == LayerType::Text) {
                auto* text = new QPlainTextEdit;
                text->setFixedHeight(text->fontMetrics().height() * 4 + 12);
                text->setPlainText(QString::fromStdString(layer.text.text));
                connect(text, &QPlainTextEdit::textChanged, this, [this, id, text] {
                    if (Layer* l = ctx_.comp().findLayer(id)) {
                        l->text.text = text->toPlainText().toStdString();
                        ctx_.touch();
                    }
                });
                section->addPlainRow("Content", text);
                refreshers_.push_back([this, id, text] {
                    if (const Layer* l = ctx_.comp().findLayer(id); l && !text->hasFocus() && text->toPlainText().toStdString() != l->text.text) {
                        const QSignalBlocker b(text);
                        text->setPlainText(QString::fromStdString(l->text.text));
                    }
                });

                auto* font = newCombo();
                font->setEditable(true);
                font->setInsertPolicy(QComboBox::NoInsert);
                for (const auto& fam : Fonts::families()) font->addItem(QString::fromStdString(fam));
                font->completer()->setFilterMode(Qt::MatchContains);
                font->completer()->setCompletionMode(QCompleter::PopupCompletion);
                font->setCurrentText(QString::fromStdString(layer.text.fontFamily));
                if (!Fonts::hasFamily(layer.text.fontFamily)) font->setToolTip("This font is not installed here: Inter is used instead.");
                section->addPlainRow("Font", font);
                auto* style = newCombo();
                for (const auto& s : Fonts::stylesOf(layer.text.fontFamily)) style->addItem(QString::fromStdString(s));
                style->setCurrentText(QString::fromStdString(layer.text.fontStyle));
                section->addPlainRow("Style", style);
                connect(font, &QComboBox::activated, this, [this, font](int) {
                    const std::string fam = font->currentText().toStdString();
                    if (!Fonts::hasFamily(fam)) return;
                    for (Layer* l : ctx_.selectedLayers()) {
                        if (l->type != LayerType::Text) continue;
                        l->text.fontFamily = fam;
                        const auto styles = Fonts::stylesOf(fam);
                        if (std::find(styles.begin(), styles.end(), l->text.fontStyle) == styles.end() && !styles.empty())
                            l->text.fontStyle = styles.front();
                    }
                    ctx_.touch();
                    key_.clear(); // the style list changes
                });
                connect(style, &QComboBox::activated, this, [this, id, style](int) {
                    const Layer* cur = ctx_.comp().findLayer(id);
                    if (!cur) return;
                    const std::string fam = cur->text.fontFamily, s = style->currentText().toStdString();
                    for (Layer* l : ctx_.selectedLayers())
                        if (l->type == LayerType::Text && l->text.fontFamily == fam) l->text.fontStyle = s;
                    ctx_.touch();
                });

                auto* alignRow = new QWidget;
                auto* align = new QHBoxLayout(alignRow);
                align->setContentsMargins(0, 0, 0, 0);
                align->setSpacing(4);
                std::vector<QPushButton*> buttons;
                for (int i = 0; i < 3; ++i) {
                    auto* b = new QPushButton(i == 0 ? "Left" : i == 1 ? "Center" : "Right");
                    b->setCheckable(true);
                    connect(b, &QPushButton::clicked, this, [this, id, i] {
                        if (Layer* l = ctx_.comp().findLayer(id)) {
                            l->text.align = static_cast<TextAlign>(i);
                            ctx_.touch();
                        }
                    });
                    align->addWidget(b);
                    buttons.push_back(b);
                }
                section->addPlainRow("Align", alignRow);
                refreshers_.push_back([this, id, buttons] {
                    if (const Layer* l = ctx_.comp().findLayer(id))
                        for (int i = 0; i < 3; ++i) buttons[i]->setChecked(static_cast<int>(l->text.align) == i);
                });
            }
        }
        rows_.push_back(new PropertyRow(ctx_, id, prop, section));
    }

    if (layer.type == LayerType::Plugin) {
        // Groups made only of toggles / choices / text never came up above.
        std::vector<std::string> done;
        for (const PluginParam& p : layer.plugin.params) {
            if (hasAnimatedParams(layer, p.group) || std::find(done.begin(), done.end(), p.group) != done.end()) continue;
            done.push_back(p.group);
            auto* s = new CollapsibleSection(QString::fromStdString(p.group));
            for (const PluginParam& q : layer.plugin.params) {
                if (q.group != p.group) continue;
                const std::string pid = q.id;
                auto param = [this, id, pid]() -> PluginParam* {
                    Layer* l = ctx_.comp().findLayer(id);
                    return l ? l->plugin.find(pid) : nullptr;
                };
                if (q.kind == PluginParamKind::Toggle) {
                    auto* cb = new QCheckBox;
                    cb->setChecked(q.toggle);
                    connect(cb, &QCheckBox::toggled, this, [this, param](bool on) {
                        if (PluginParam* pp = param()) {
                            pp->toggle = on;
                            ctx_.touch();
                        }
                    });
                    s->addPlainRow(QString::fromStdString(q.label), cb);
                } else if (q.kind == PluginParamKind::Choice) {
                    auto* combo = newCombo();
                    for (const auto& c : q.choices) combo->addItem(QString::fromStdString(c));
                    combo->setCurrentIndex(q.choice);
                    connect(combo, &QComboBox::currentIndexChanged, this, [this, param](int i) {
                        if (PluginParam* pp = param(); pp && i >= 0) {
                            pp->choice = i;
                            ctx_.touch();
                        }
                    });
                    s->addPlainRow(QString::fromStdString(q.label), combo);
                } else if (q.kind == PluginParamKind::Text) {
                    auto* edit = new QLineEdit(QString::fromStdString(q.text));
                    connect(edit, &QLineEdit::textEdited, this, [this, param](const QString& t) {
                        if (PluginParam* pp = param()) {
                            pp->text = t.toStdString();
                            ctx_.touch();
                        }
                    });
                    s->addPlainRow(QString::fromStdString(q.label), edit);
                }
            }
            col->addWidget(s);
        }
        if (layer.plugin.hasRows) buildPluginData(col, layer);
        if (layer.plugin.hasTable) buildPluginTable(col, layer);
        if (!PluginHost::get().findType(layer.plugin.objectType)) {
            auto* warn = new QLabel(QString("The plugin for this object (%1) is not installed or failed to load.")
                                        .arg(QString::fromStdString(layer.plugin.objectType)));
            warn->setWordWrap(true);
            ui::setTone(warn, ui::Tone::Danger);
            col->addWidget(warn);
        }
    }

    // In / out and the label color.
    {
        auto* s = new CollapsibleSection("Layer");
        auto* in = new ui::DragValue;
        in->setSpeed(0.01f);
        in->setFormat("%.2f s");
        connect(in, &ui::DragValue::valueChanged, this, [this, id](float v) {
            Composition& comp = ctx_.comp();
            comp.setInPoint(id, comp.snapToFrame(v));
            ctx_.touch();
        });
        s->addPlainRow("In", in);
        auto* out = new ui::DragValue;
        out->setSpeed(0.01f);
        out->setFormat("%.2f s");
        connect(out, &ui::DragValue::valueChanged, this, [this, id](float v) {
            ctx_.comp().setOutPoint(id, v);
            ctx_.touch();
        });
        s->addPlainRow("Out", out);
        auto* labels = new QWidget;
        auto* lrow = new QHBoxLayout(labels);
        lrow->setContentsMargins(0, 0, 0, 0);
        lrow->setSpacing(4);
        std::vector<QPushButton*> swatches;
        for (int i = 0; i < kLabelColorCount; ++i) {
            const Color lc = labelColor(i);
            auto* b = new QPushButton;
            b->setFixedSize(18, 18);
            b->setCheckable(true);
            b->setStyleSheet(QString("QPushButton { background: %1; border-radius: 4px; padding: 0; }"
                                     "QPushButton:checked { border: 2px solid %2; }")
                                 .arg(QColor::fromRgbF(lc.r, lc.g, lc.b).name(), theme::q(theme::kText).name()));
            connect(b, &QPushButton::clicked, this, [this, id, i] {
                if (Layer* l = ctx_.comp().findLayer(id)) {
                    l->label = i;
                    ctx_.touch();
                }
            });
            lrow->addWidget(b);
            swatches.push_back(b);
        }
        lrow->addStretch();
        s->addPlainRow("Label", labels);
        col->addWidget(s);
        refreshers_.push_back([this, id, in, out, swatches] {
            Composition& comp = ctx_.comp();
            const Layer* l = comp.findLayer(id);
            if (!l) return;
            // In/out stay inside the composition, between neighboring clips, at least one frame long.
            const double frame = comp.frameDuration();
            double minIn = 0, maxOut = 0;
            comp.clipLimits(id, minIn, maxOut);
            in->setRange(static_cast<float>(minIn), static_cast<float>(std::max(minIn, l->outPoint - frame)));
            in->setValue(static_cast<float>(l->inPoint));
            out->setRange(static_cast<float>(std::min(maxOut, l->inPoint + frame)), static_cast<float>(maxOut));
            out->setValue(static_cast<float>(l->outPoint));
            for (int i = 0; i < static_cast<int>(swatches.size()); ++i) swatches[i]->setChecked(l->label == i);
        });
    }

    if (layer.type != LayerType::Camera) buildPresets(col, layer); // cameras are not drawn: no animation presets
    for (auto& f : refreshers_) f();
}

void PropertiesPanel::buildMediaSource(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection("Source");
    const MediaItem* item = ctx_.comp().findMedia(layer.media.mediaId);
    if (!item) {
        auto* gone = new QLabel("The media of this layer was removed.");
        ui::setTone(gone, ui::Tone::Danger);
        s->body()->addWidget(gone);
        col->addWidget(s);
        return;
    }
    const bool missing = media::MediaManager::get().fileMissing(*item);
    s->body()->addWidget(new QLabel(QString::fromStdString(item->name)));
    char info[128];
    if (item->kind == MediaKind::Image) std::snprintf(info, sizeof info, "Image  ·  %d×%d", item->width, item->height);
    else if (item->kind == MediaKind::Audio) std::snprintf(info, sizeof info, "Audio  ·  %.2f s", item->duration);
    else
        std::snprintf(info, sizeof info, "Video  ·  %d×%d  ·  %.2f s  ·  %.4g fps%s", item->width, item->height, item->duration, item->fps,
                      item->hasAudio ? "  ·  with audio" : "");
    s->body()->addWidget(dim(QString::fromUtf8(info)));
    auto* path = missing ? new QLabel(QString::fromStdString("Missing: " + item->path)) : faint(QString::fromStdString(item->path));
    path->setWordWrap(true);
    if (missing) ui::setTone(path, ui::Tone::Danger);
    s->body()->addWidget(path);
    if (missing) s->body()->addWidget(faint("Relink it from the Media panel (right-click the item)."));
    auto* row = new QHBoxLayout;
    const uint64_t id = layer.id;
    if (layer.media.hasVideo) {
        auto* fit = new QPushButton("Fit to Composition");
        fit->setToolTip("Scale it to fill the composition, keeping its proportions");
        connect(fit, &QPushButton::clicked, this, [this, id] {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l || l->media.size.x <= 0 || l->media.size.y <= 0) return;
            // Like After Effects' Fit to Comp (keeping the aspect ratio): centered, as large as fits.
            const Composition& c = ctx_.comp();
            const float f = std::min(c.width / l->media.size.x, c.height / l->media.size.y) * 100.0f;
            l->transform.scale.base = {f, f};
            l->transform.position.base = c.center();
            if (const Layer* parent = c.findLayer(l->parent))
                l->transform.position.base = c.worldMatrix(*parent, ctx_.time).inverse().apply(c.center());
            ctx_.touch();
        });
        row->addWidget(fit);
    }
    auto* reveal = new QPushButton("Show in Folder");
    reveal->setEnabled(!missing);
    const std::string file = item->path;
    connect(reveal, &QPushButton::clicked, this, [file] { platform::revealInFileManager(platform::fromUtf8(file)); });
    row->addWidget(reveal);
    s->body()->addLayout(row);
    col->addWidget(s);
}

void PropertiesPanel::buildPluginData(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection("Data");
    const uint64_t id = layer.id;
    auto rowsOf = [this, id]() -> std::vector<DataRow>* {
        Layer* l = ctx_.comp().findLayer(id);
        return l ? &l->plugin.rows : nullptr;
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
        label->setPlaceholderText("Label");
        connect(label, &QLineEdit::textEdited, this, [this, rowsOf, i](const QString& t) {
            if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                (*rows)[i].label = t.toStdString();
                ctx_.touch();
            }
        });
        auto* value = new ui::DragValue;
        value->setFormat("%g");
        value->setValue(r.value);
        connect(value, &ui::DragValue::valueChanged, this, [this, rowsOf, i](float v) {
            if (auto* rows = rowsOf(); rows && i < static_cast<int>(rows->size())) {
                (*rows)[i].value = v;
                ctx_.touch();
            }
        });
        auto* up = new ui::IconButton(ui::Icon::ChevronUp, "Move up", 24);
        connect(up, &QAbstractButton::clicked, this, [this, rowsOf, i] {
            if (auto* rows = rowsOf(); rows && i > 0 && i < static_cast<int>(rows->size())) {
                std::swap((*rows)[i], (*rows)[i - 1]);
                ctx_.touch();
                key_.clear();
            }
        });
        auto* del = new ui::IconButton(ui::Icon::Trash, "Remove row", 24);
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
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("+ Add Row");
    connect(add, &QPushButton::clicked, this, [this, rowsOf] {
        if (auto* rows = rowsOf()) {
            DataRow r;
            r.label = "Item " + std::to_string(rows->size() + 1);
            r.value = rows->empty() ? 50.0f : rows->back().value;
            r.color = labelColor(static_cast<int>(rows->size()));
            rows->push_back(std::move(r));
            ctx_.touch();
        }
    });
    auto* paste = new QPushButton("Paste");
    paste->setToolTip("Replace the data with rows from the clipboard: \"label, value[, #color]\" per line.\n"
                      "Cells copied from a spreadsheet work too.");
    connect(paste, &QPushButton::clicked, this, [this, rowsOf] {
        auto pasted = parseRows(QApplication::clipboard()->text().toStdString());
        if (auto* rows = rowsOf(); rows && !pasted.empty()) {
            *rows = std::move(pasted);
            ctx_.touch();
            key_.clear();
        }
    });
    auto* copy = new QPushButton("Copy");
    copy->setToolTip("Copy the data as CSV");
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
    auto* csv = new QPushButton("Import CSV...");
    csv->setToolTip("Replace the data with a CSV file (comma, semicolon or tab separated)");
    const uint64_t layerId = layer.id;
    connect(csv, &QPushButton::clicked, this, [this, layerId] { importCsv(layerId); });
    buttons->addWidget(add);
    buttons->addWidget(paste);
    buttons->addWidget(copy);
    buttons->addWidget(csv);
    buttons->addStretch();
    s->body()->addLayout(buttons);
    col->addWidget(s);
}

void PropertiesPanel::importCsv(uint64_t layerId) {
    const auto file = platform::openFileDialog("Import CSV", "CSV or text table", "csv");
    if (!file) return;
    std::ifstream in(*file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    Layer* l = ctx_.comp().findLayer(layerId);
    if (!l) return;
    std::string error;
    if (!PluginHost::get().importCsv(*l, ss.str(), error)) {
        presetMessage_ = "Could not import " + platform::toUtf8(file->filename()) + ": " + error;
    } else {
        presetMessage_.clear();
        ctx_.touch();
    }
    key_.clear(); // rebuilt with the new data
}

void PropertiesPanel::buildPluginTable(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection("Table");
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
    auto* buttons = new QHBoxLayout;
    auto button = [&](const QString& text, const QString& tip, std::function<void()> click) {
        auto* b = new QPushButton(text);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, std::move(click));
        buttons->addWidget(b);
    };
    button("+ Row", "Add a row at the end", [edit] {
        edit([](DataTable& t) { t.rows.emplace_back(t.header.size()); });
    });
    button("+ Column", "Add a column on the right", [edit] {
        edit([](DataTable& t) {
            t.header.push_back("Column " + std::to_string(t.header.size() + 1));
            for (auto& r : t.rows) r.resize(t.header.size());
        });
    });
    button("- Row", "Remove the selected row (or the last one)", [edit, grid] {
        const int sel = grid->currentRow();
        edit([sel](DataTable& t) {
            if (t.rows.empty()) return;
            const int i = sel >= 1 && sel - 1 < static_cast<int>(t.rows.size()) ? sel - 1 : static_cast<int>(t.rows.size()) - 1;
            t.rows.erase(t.rows.begin() + i);
        });
    });
    button("- Column", "Remove the selected column (or the last one)", [edit, grid] {
        const int sel = grid->currentColumn();
        edit([sel](DataTable& t) {
            if (t.header.size() <= 1) return;
            const int i = sel >= 0 && sel < static_cast<int>(t.header.size()) ? sel : static_cast<int>(t.header.size()) - 1;
            t.header.erase(t.header.begin() + i);
            for (auto& r : t.rows)
                if (i < static_cast<int>(r.size())) r.erase(r.begin() + i);
        });
    });
    buttons->addStretch();
    s->body()->addLayout(buttons);
    auto* more = new QHBoxLayout;
    auto* csv = new QPushButton("Import CSV...");
    csv->setToolTip("Replace the table with a CSV file (comma, semicolon or tab separated)");
    connect(csv, &QPushButton::clicked, this, [this, id] { importCsv(id); });
    auto* paste = new QPushButton("Paste");
    paste->setToolTip("Replace the table with cells from the clipboard (copied from a spreadsheet or CSV text)");
    connect(paste, &QPushButton::clicked, this, [edit] {
        const Cells cells = parseCsv(QApplication::clipboard()->text().toStdString());
        if (!cells.empty()) edit([&cells](DataTable& t) { t = tableFromCells(cells); });
    });
    auto* copy = new QPushButton("Copy");
    copy->setToolTip("Copy the table as CSV");
    connect(copy, &QPushButton::clicked, this, [tableOf] {
        if (DataTable* t = tableOf()) QApplication::clipboard()->setText(QString::fromStdString(toCsv(cellsOf(*t))));
    });
    more->addWidget(csv);
    more->addWidget(paste);
    more->addWidget(copy);
    more->addStretch();
    s->body()->addLayout(more);
    col->addWidget(s);
}

void PropertiesPanel::buildPresets(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection("Quick Animations");
    s->body()->addWidget(faint("Adds keyframes at the playhead (replacing overlapping ones)."));
    // Built-in categories first, then whatever imported presets use.
    std::vector<std::string> categories = {"Entrance", "Emphasis", "Exit"};
    for (const auto& lp : PresetLibrary::all())
        if (std::find(categories.begin(), categories.end(), lp.preset.category) == categories.end()) categories.push_back(lp.preset.category);
    for (const std::string& category : categories) {
        QGridLayout* grid = nullptr;
        int n = 0;
        for (const auto& lp : PresetLibrary::all()) {
            if (lp.preset.category != category || !presetAppliesTo(lp.preset, layer)) continue;
            if (!grid) {
                s->body()->addWidget(ui::sectionLabel(QString::fromStdString(category)));
                grid = new QGridLayout;
                grid->setSpacing(4);
                s->body()->addLayout(grid);
            }
            auto* b = new QPushButton(QString::fromStdString(lp.preset.name));
            const AnimationPreset preset = lp.preset;
            connect(b, &QPushButton::clicked, this, [this, preset] {
                for (Layer* l : ctx_.selectedLayers())
                    if (presetAppliesTo(preset, *l)) applyPreset(preset, *l, ctx_.time);
                ctx_.touch();
            });
            if (!lp.file.empty()) {
                const std::filesystem::path file = lp.file;
                b->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(b, &QWidget::customContextMenuRequested, this, [this, b, file](QPoint p) {
                    QMenu menu;
                    menu.addAction("Show File", [file] { platform::revealInFileManager(file); });
                    menu.addAction("Delete Preset", [this, file] {
                        PresetLibrary::remove(file);
                        key_.clear();
                    });
                    menu.exec(b->mapToGlobal(p));
                });
            }
            grid->addWidget(b, n / 2, n % 2);
            ++n;
        }
    }
    auto* row = new QHBoxLayout;
    auto* import = new QPushButton("Import...");
    import->setToolTip("Add a Quick Animation from a .json preset file");
    connect(import, &QPushButton::clicked, this, [this] {
        if (auto file = platform::openFileDialog("Import Quick Animation", "Quick Animation preset", ".json")) {
            std::string err;
            presetMessage_ = PresetLibrary::importFile(*file, &err) ? "" : "Import failed: " + err;
        }
    });
    auto* save = new QPushButton("Save as Preset...");
    save->setToolTip("Turn this layer's keyframes into a reusable Quick Animation");
    const uint64_t id = layer.id;
    connect(save, &QPushButton::clicked, this, [this, id] {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Save Quick Animation", "Preset name", QLineEdit::Normal,
                                                   QString::fromStdString(l->name + " animation"), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        AnimationPreset preset = presetFromLayer(*l, l->inPoint, l->outPoint, name.toStdString());
        std::string err;
        if (preset.tracks.empty()) presetMessage_ = "This layer has no keyframes to save.";
        else if (PresetLibrary::save(preset, &err)) presetMessage_ = "Saved \"" + name.toStdString() + "\" to Quick Animations (Custom).";
        else presetMessage_ = err;
    });
    row->addWidget(import);
    row->addWidget(save);
    s->body()->addLayout(row);
    if (!presetMessage_.empty()) s->body()->addWidget(dim(QString::fromStdString(presetMessage_)));
    col->addWidget(s);
}

} // namespace bemotion
