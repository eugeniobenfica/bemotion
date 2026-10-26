#include "ui/panels/EffectsPanel.h"

#include "app/EffectLibrary.h"
#include "app/Platform.h"
#include "render/SceneRenderer.h"
#include "ui/CurvesEditor.h"
#include "ui/EditorContext.h"
#include "ui/PropertyRow.h"
#include "ui/panels/ColorPanel.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

namespace bemotion {

namespace {

bool matches(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(text).find(lower(filter)) != std::string::npos;
}

QLabel* faint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

void applyToSelection(EditorContext& ctx, const std::vector<Effect>& effects) {
    for (Layer* l : ctx.selectedLayers()) {
        if (l->type == LayerType::Camera) continue; // cameras are not drawn
        l->effects.insert(l->effects.end(), effects.begin(), effects.end());
    }
    ctx.touch();
}

struct Category {
    const char* name;  // After Effects' category name
    const char* label; // short tile label
    ui::Icon icon;
};
// After Effects' categories (those with effects here), then the presets.
constexpr Category kCategories[] = {
    {TR_NOOP("Blur & Sharpen"), TR_NOOP("Blur"), ui::Icon::FxBlur},       {TR_NOOP("Color Correction"), TR_NOOP("Color"), ui::Icon::FxColor},
    {TR_NOOP("Channel"), TR_NOOP("Channel"), ui::Icon::FxChannel},        {TR_NOOP("Distort"), TR_NOOP("Distort"), ui::Icon::FxDistort},
    {TR_NOOP("Generate"), TR_NOOP("Generate"), ui::Icon::FxGenerate},     {TR_NOOP("Perspective"), TR_NOOP("Perspective"), ui::Icon::FxPerspective},
    {TR_NOOP("Stylize"), TR_NOOP("Stylize"), ui::Icon::FxStylize},        {TR_NOOP("Time"), TR_NOOP("Time"), ui::Icon::FxTime},
    {TR_NOOP("Keying"), TR_NOOP("Keying"), ui::Icon::FxKeying},
};
constexpr const char* kPresets = TR_NOOP("Presets");

ui::Icon categoryIcon(const char* category) {
    for (const Category& c : kCategories)
        if (std::strcmp(c.name, category) == 0) return c.icon;
    return ui::Icon::Star;
}

// Equally sized tiles in as many columns as fit `width`.
struct TileGrid {
    QGridLayout* grid;
    int columns;
    QSize size;
    int count = 0;
    TileGrid(QVBoxLayout* into, int width) {
        grid = new QGridLayout;
        grid->setSpacing(8);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        into->addLayout(grid);
        columns = std::max(2, (width + 8) / (80 + 8));
        const int w = std::max(56, (width - 8 * (columns - 1)) / columns);
        size = QSize(w, std::clamp(static_cast<int>(w * 0.78f), 50, 66));
    }
    ui::Tile* add(ui::Icon icon, const QString& label) {
        auto* t = new ui::Tile(icon, label);
        t->setTileSize(size);
        grid->addWidget(t, count / columns, count % columns);
        ++count;
        return t;
    }
};

} // namespace

void fillEffectMenu(QMenu* menu, EditorContext& ctx) {
    for (const Category& c : kCategories) {
        QMenu* sub = menu->addMenu(ui::makeIcon(c.icon, theme::kTextDim, 16), tr(c.name));
        sub->setToolTipsVisible(true);
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t) {
            const auto type = static_cast<EffectType>(t);
            if (std::strcmp(effectCategory(type), c.name) != 0) continue;
            QAction* a = sub->addAction(tr(effectLabel(type)), [&ctx, type] { applyToSelection(ctx, {makeEffect(type)}); });
            a->setToolTip(tr(effectDescription(type)));
        }
    }
    menu->addSeparator();
    QMenu* presets = menu->addMenu(tr("Effect Presets"));
    for (const auto& lp : EffectLibrary::all()) {
        const std::vector<Effect> effects = lp.preset.effects;
        presets->addAction(trs(lp.preset.name), [&ctx, effects] { applyToSelection(ctx, effects); });
    }
    presets->setEnabled(!presets->isEmpty());
    menu->addAction(tr("Remove All Effects"), [&ctx] {
        for (Layer* l : ctx.selectedLayers()) l->effects.clear();
        ctx.clearKeySelection();
        ctx.touch();
    });
}

EffectsPanel::EffectsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    stackHost_ = new QVBoxLayout;
    stackHost_->setContentsMargins(0, 0, 0, 0);
    col->addLayout(stackHost_);
    col->addSpacing(6);
    col->addWidget(ui::sectionLabel(tr("Effects & Presets")));
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search effects"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this] { sync(); });
    col->addWidget(search_);
    browserHost_ = new QVBoxLayout;
    browserHost_->setContentsMargins(0, 0, 0, 0);
    col->addLayout(browserHost_);
    col->addStretch();
    setWidget(content);
}

void EffectsPanel::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    browserKey_.clear(); // tile columns depend on the width
    sync();
}

std::string EffectsPanel::stackKey() {
    std::ostringstream k;
    const Layer* layer = ctx_.selected();
    if (!layer) return "none";
    k << layer->id << ':' << static_cast<int>(layer->type) << ':' << layer->name << ':';
    for (const Effect& e : layer->effects) k << static_cast<int>(e.type) << ':' << e.file << ',';
    k << '|' << layer->media.hasAudio << ':';
    for (const AudioEffect& e : layer->audioEffects) k << static_cast<int>(e.type) << ',';
    return k.str();
}

std::string EffectsPanel::browserKey() {
    std::ostringstream k;
    k << search_->text().toStdString() << '|' << category_ << '|' << ctx_.selection.empty() << '|' << message_ << '|';
    for (const Layer* layer : ctx_.selectedLayers()) k << layer->id << ':' << static_cast<int>(layer->type) << ':' << layer->media.hasAudio << ':' << layer->media.hasVideo << ',';
    for (const auto& lp : EffectLibrary::all()) k << lp.preset.name << ',';
    return k.str();
}

void EffectsPanel::sync() {
    if (!isVisible()) return;
    if (const std::string k = stackKey(); k != stackKey_) {
        stackKey_ = k;
        rebuildStack();
    } else {
        for (PropertyRow* r : rows_) r->refresh();
        for (auto& f : refreshers_) f();
    }
    if (const std::string k = browserKey(); k != browserKey_) {
        browserKey_ = k;
        rebuildBrowser();
    }
}

void EffectsPanel::rebuildStack() {
    rows_.clear();
    refreshers_.clear();
    if (stack_) stack_->deleteLater();
    stack_ = new QWidget;
    auto* col = new QVBoxLayout(stack_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    stackHost_->addWidget(stack_);

    Layer* layer = ctx_.selected();
    if (!layer || layer->type == LayerType::Camera) {
        col->addWidget(new QLabel(tr("Effect Controls")));
        col->addWidget(faint(layer ? tr("Cameras have no effects.") : tr("Select a layer to see and edit its effects.")));
        return;
    }
    const bool audioOnly = layer->type == LayerType::Media && layer->media.hasAudio && !layer->media.hasVideo;
    if (audioOnly) {
        col->addWidget(new QLabel(tr("Audio Effects  ·  %1").arg(QString::fromStdString(layer->name))));
        buildAudioEffects(col, *layer);
        for (auto& f : refreshers_) f();
        return;
    }
    const uint64_t id = layer->id;
    {
        auto* row = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(ui::iconForLayer(*layer), theme::kAccent, 20).pixmap(20, 20));
        row->addWidget(icon);
        row->addWidget(new QLabel(tr("Effect Controls  ·  %1").arg(QString::fromStdString(layer->name))), 1);
        col->addLayout(row);
    }
    const auto props = layer->properties();
    const int count = static_cast<int>(layer->effects.size());
    for (int i = 0; i < count; ++i) {
        const Effect& e = layer->effects[i];
        auto* extra = new QWidget;
        auto* tools = new QHBoxLayout(extra);
        tools->setContentsMargins(0, 0, 0, 0);
        tools->setSpacing(2);
        auto* on = new ui::Switch;
        connect(on, &QAbstractButton::toggled, this, [this, id, i](bool v) {
            if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size()) && l->effects[i].enabled != v) {
                l->effects[i].enabled = v;
                ctx_.touch();
            }
        });
        refreshers_.push_back([this, id, i, on] {
            if (const Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size())) {
                const QSignalBlocker b(on);
                on->setChecked(l->effects[i].enabled);
                on->setToolTip(l->effects[i].enabled ? tr("Turn this effect off") : tr("Turn this effect on"));
            }
        });
        tools->addWidget(on);
        auto move = [this, id](int from, int to) {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l || from < 0 || to < 0 || from >= static_cast<int>(l->effects.size()) || to >= static_cast<int>(l->effects.size())) return;
            std::swap(l->effects[from], l->effects[to]);
            ctx_.clearKeySelection();
            ctx_.touch();
        };
        auto* up = new ui::IconButton(ui::Icon::ChevronUp, tr("Move up (applied earlier)"), 24);
        up->setEnabled(i > 0);
        connect(up, &QAbstractButton::clicked, this, [move, i] { move(i, i - 1); });
        auto* down = new ui::IconButton(ui::Icon::ChevronDown, tr("Move down (applied later)"), 24);
        down->setEnabled(i + 1 < count);
        connect(down, &QAbstractButton::clicked, this, [move, i] { move(i, i + 1); });
        auto* del = new ui::IconButton(ui::Icon::Trash, tr("Remove effect"), 24);
        connect(del, &QAbstractButton::clicked, this, [this, id, i] {
            if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size())) {
                l->effects.erase(l->effects.begin() + i);
                ctx_.clearKeySelection();
                ctx_.touch();
            }
        });
        tools->addWidget(up);
        tools->addWidget(down);
        tools->addWidget(del);

        auto* section = new CollapsibleSection(QString::fromStdString(e.group), nullptr, extra);
        for (const PropertyRef& p : props)
            if (p.effectIndex == i) rows_.push_back(new PropertyRow(ctx_, id, p, section));
        if (e.type == EffectType::MotionBlur) {
            auto* samples = new ui::DragValue;
            samples->setRange(2.0f, 64.0f);
            samples->setSpeed(0.2f);
            samples->setFormat("%.0f");
            samples->setToolTip(tr("More samples: smoother blur, slower preview"));
            connect(samples, &ui::DragValue::valueChanged, this, [this, id, i](float x) {
                const int v = static_cast<int>(std::lround(x));
                if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size()) && l->effects[i].samples != v) {
                    l->effects[i].samples = v;
                    ctx_.touch();
                }
            });
            refreshers_.push_back([this, id, i, samples] {
                if (const Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size()))
                    samples->setValue(static_cast<float>(l->effects[i].samples));
            });
            section->addPlainRow(tr("Samples"), samples);
        }
        if (e.type == EffectType::Lut) {
            auto* lut = new QComboBox;
            ui::fillLutChoices(lut, e.file);
            connect(lut, &QComboBox::activated, this, [this, id, i, lut](int index) {
                std::string file;
                if (!ui::lutOfChoice(lut, index, file)) {
                    stackKey_.clear(); // back to what it was
                    sync();
                    return;
                }
                if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size()) && l->effects[i].file != file) {
                    l->effects[i].file = file;
                    l->effects[i].relative.clear();
                    ctx_.touch();
                }
            });
            section->addPlainRow(tr("LUT"), lut);
            std::string title, error;
            if (!e.file.empty() && !render::lutInfo(e.file, title, error)) {
                auto* problem = new QLabel(QString::fromStdString(error));
                problem->setWordWrap(true);
                ui::setTone(problem, ui::Tone::Danger);
                section->body()->addWidget(problem);
            }
        }
        if (e.type == EffectType::Curves) {
            auto* editor = new ui::CurvesEditor;
            connect(editor, &ui::CurvesEditor::changed, this, [this, id, i, editor] {
                if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size())) {
                    l->effects[i].curves = editor->curves();
                    for (auto& curve : l->effects[i].curves) cleanCurve(curve);
                    ctx_.touch();
                }
            });
            refreshers_.push_back([this, id, i, editor] {
                if (const Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size())) editor->setCurves(l->effects[i].curves);
            });
            section->body()->addWidget(editor);
        }
        col->addWidget(section);
    }
    col->addWidget(faint(layer->effects.empty() ? tr("No effects yet. Pick one below: it is added to every selected layer.")
                                                : tr("Effects run top to bottom. Every parameter can be keyframed here or in the timeline.")));
    if (!layer->effects.empty()) {
        auto* save = new QPushButton(tr("Save Stack as Preset..."));
        connect(save, &QPushButton::clicked, this, [this, id] {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l) return;
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Save Effect Preset"), tr("Preset name"), QLineEdit::Normal,
                                                       QString::fromStdString(l->name + " effects"), &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            EffectPreset preset{name.toStdString(), l->effects};
            if (EffectLibrary::save(preset, &message_)) message_ = "Saved \"" + preset.name + "\" to Effect Presets.";
            sync();
        });
        auto* row = new QHBoxLayout;
        row->addWidget(save);
        row->addStretch();
        col->addLayout(row);
    }
    if (layer->type == LayerType::Media && layer->media.hasAudio) { // a video with sound: its audio effects too
        col->addSpacing(6);
        col->addWidget(ui::sectionLabel(tr("Audio Effects")));
        buildAudioEffects(col, *layer);
    }
    for (auto& f : refreshers_) f();
}

// The layer's audio effects: each a section with its switch, values and a remove button.
void EffectsPanel::buildAudioEffects(QVBoxLayout* col, const Layer& layer) {
    const uint64_t id = layer.id;
    for (int i = 0; i < static_cast<int>(layer.audioEffects.size()); ++i) {
        const AudioEffect& effect = layer.audioEffects[i];
        auto* extra = new QWidget;
        auto* tools = new QHBoxLayout(extra);
        tools->setContentsMargins(0, 0, 0, 0);
        tools->setSpacing(2);
        auto* enabled = new ui::Switch;
        tools->addWidget(enabled);
        auto* remove = new ui::IconButton(ui::Icon::Trash, tr("Remove audio effect"), 24);
        tools->addWidget(remove);
        connect(enabled, &QAbstractButton::toggled, this, [this, id, i](bool on) {
            if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->audioEffects.size()) && l->audioEffects[i].enabled != on)
                l->audioEffects[i].enabled = on, ctx_.touch();
        });
        connect(remove, &QAbstractButton::clicked, this, [this, id, i] {
            if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->audioEffects.size()))
                l->audioEffects.erase(l->audioEffects.begin() + i), ctx_.touch();
        });
        refreshers_.push_back([this, id, i, enabled] {
            if (const Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->audioEffects.size())) {
                const QSignalBlocker b(enabled);
                enabled->setChecked(l->audioEffects[i].enabled);
            }
        });
        auto* section = new CollapsibleSection(tr(audioEffectLabel(effect.type)), nullptr, extra);
        section->setToolTip(tr(audioEffectDescription(effect.type)));
        for (const AudioEffectParam& p : audioEffectParams(effect.type)) {
            auto* value = new ui::DragValue;
            value->setRange(p.min, p.max);
            value->setSpeed(p.speed);
            value->setFormat(p.format);
            const int index = p.index;
            connect(value, &ui::DragValue::valueChanged, this, [this, id, i, index](float v) {
                if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->audioEffects.size()) && audioEffectValue(l->audioEffects[i], index) != v)
                    audioEffectValue(l->audioEffects[i], index) = v, ctx_.touch();
            });
            refreshers_.push_back([this, id, i, index, value] {
                if (const Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->audioEffects.size()))
                    value->setValue(audioEffectValue(l->audioEffects[i], index));
            });
            section->addPlainRow(tr(p.label), value);
        }
        if (effect.type == AudioEffectType::Ducking)
            section->body()->addWidget(faint(tr("Quieter while any other clip with sound plays: put it on the music.")));
        col->addWidget(section);
    }
    col->addWidget(faint(layer.audioEffects.empty() ? tr("No audio effects yet. Pick one below.") : tr("Audio effects are applied during playback and export.")));
}

void EffectsPanel::rebuildBrowser() {
    if (browser_) browser_->deleteLater();
    browser_ = new QWidget;
    auto* col = new QVBoxLayout(browser_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);
    browserHost_->addWidget(browser_);
    const int width = std::max(120, viewport()->width() - 16);
    const bool canApply = !ctx_.selection.empty();
    const auto selectedLayers = ctx_.selectedLayers();
    const bool audioOnly = canApply && std::all_of(selectedLayers.begin(), selectedLayers.end(), [](const Layer* l) {
        return l->type == LayerType::Media && l->media.hasAudio && !l->media.hasVideo;
    });
    const bool withSound = canApply && std::all_of(selectedLayers.begin(), selectedLayers.end(), [](const Layer* l) {
        return l->type == LayerType::Media && l->media.hasAudio;
    });
    auto audioTiles = [&] {
        col->addWidget(ui::sectionLabel(tr("Audio Effects")));
        TileGrid grid(col, width);
        for (int i = 0; i < static_cast<int>(AudioEffectType::Count); ++i) {
            const auto type = static_cast<AudioEffectType>(i);
            ui::Tile* tile = grid.add(ui::Icon::Audio, tr(audioEffectLabel(type)));
            tile->setToolTip(tr(audioEffectLabel(type)) + "\n" + tr(audioEffectDescription(type)));
            connect(tile, &QAbstractButton::clicked, this, [this, type] {
                for (Layer* l : ctx_.selectedLayers())
                    if (l->type == LayerType::Media && l->media.hasAudio) l->audioEffects.push_back(makeAudioEffect(type));
                ctx_.touch();
            });
        }
    };
    if (audioOnly) {
        audioTiles();
        return;
    }
    if (!canApply) {
        col->addWidget(faint(tr("Select a layer to browse its effects.")));
        return;
    }

    auto effectTile = [&](EffectType type, TileGrid& grid) {
        ui::Tile* t = grid.add(categoryIcon(effectCategory(type)), tr(effectLabel(type)));
        t->setDimmed(!canApply);
        t->setToolTip(QString("%1\n%2").arg(tr(effectLabel(type)), tr(effectDescription(type))));
        connect(t, &QAbstractButton::clicked, this, [this, type] {
            if (!ctx_.selection.empty()) applyToSelection(ctx_, {makeEffect(type)});
        });
    };
    auto presetTile = [&](const LibraryEffectPreset& lp, TileGrid& grid) {
        ui::Tile* t = grid.add(ui::Icon::Star, trs(lp.preset.name));
        t->setDimmed(!canApply);
        QString list;
        for (const Effect& e : lp.preset.effects) list += (list.isEmpty() ? "" : " + ") + tr(effectLabel(e.type));
        t->setToolTip(trs(lp.preset.name) + "\n" + list);
        const std::vector<Effect> effects = lp.preset.effects;
        connect(t, &QAbstractButton::clicked, this, [this, effects] {
            if (!ctx_.selection.empty()) applyToSelection(ctx_, effects);
        });
        if (!lp.file.empty()) {
            const std::filesystem::path file = lp.file;
            t->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(t, &QWidget::customContextMenuRequested, this, [this, t, file](QPoint p) {
                QMenu menu;
                menu.addAction(tr("Delete Preset"), [this, file] {
                    EffectLibrary::remove(file);
                    browserKey_.clear();
                });
                menu.exec(t->mapToGlobal(p));
                sync();
            });
        }
    };

    const std::string search = search_->text().toStdString();
    if (!search.empty()) { // flat results
        TileGrid grid(col, width);
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t) {
            const auto type = static_cast<EffectType>(t);
            if (matches(effectLabel(type), search) || matches(effectCategory(type), search) || matches(tr(effectLabel(type)).toStdString(), search))
                effectTile(type, grid);
        }
        for (const auto& lp : EffectLibrary::all())
            if (matches(lp.preset.name, search)) presetTile(lp, grid);
        if (!grid.count) col->addWidget(faint(tr("No effect matches your search.")));
    } else if (category_.empty()) { // the categories
        TileGrid grid(col, width);
        for (const Category& c : kCategories) {
            ui::Tile* t = grid.add(c.icon, c.label);
            t->setToolTip(tr(c.name));
            const std::string name = c.name;
            connect(t, &QAbstractButton::clicked, this, [this, name] {
                category_ = name;
                sync();
            });
        }
        ui::Tile* t = grid.add(ui::Icon::Star, kPresets);
        connect(t, &QAbstractButton::clicked, this, [this] {
            category_ = kPresets;
            sync();
        });
    } else { // one category
        auto* head = new QHBoxLayout;
        auto* back = new ui::IconButton(ui::Icon::Back, tr("All categories"), 24);
        connect(back, &QAbstractButton::clicked, this, [this] {
            category_.clear();
            sync();
        });
        head->addWidget(back);
        head->addWidget(new QLabel(trs(category_)), 1);
        col->addLayout(head);
        TileGrid grid(col, width);
        if (category_ == kPresets) {
            for (const auto& lp : EffectLibrary::all()) presetTile(lp, grid);
            auto* import = new QPushButton(tr("Import Preset..."));
            connect(import, &QPushButton::clicked, this, [this] {
                if (auto file = platform::openFileDialog(TR_NOOP("Import Effect Preset"), TR_NOOP("Effect preset"), ".json")) {
                    std::string err;
                    message_ = EffectLibrary::importFile(*file, &err) ? "Imported." : "Import failed: " + err;
                    sync();
                }
            });
            auto* row = new QHBoxLayout;
            row->addWidget(import);
            row->addStretch();
            col->addLayout(row);
        } else {
            for (int t = 0; t < static_cast<int>(EffectType::Count); ++t)
                if (category_ == effectCategory(static_cast<EffectType>(t))) effectTile(static_cast<EffectType>(t), grid);
        }
    }
    if (withSound && search.empty() && category_.empty()) {
        col->addSpacing(4);
        audioTiles();
    }
    if (!message_.empty()) col->addWidget(faint(QString::fromStdString(message_)));
}

} // namespace bemotion
