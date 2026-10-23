#include "ui/panels/EffectsPanel.h"

#include "app/EffectLibrary.h"
#include "app/Platform.h"
#include "ui/EditorContext.h"
#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
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
    l->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kTextFaint).name()));
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
    {"Blur & Sharpen", "Blur", ui::Icon::FxBlur},       {"Color Correction", "Color", ui::Icon::FxColor},
    {"Channel", "Channel", ui::Icon::FxChannel},        {"Distort", "Distort", ui::Icon::FxDistort},
    {"Generate", "Generate", ui::Icon::FxGenerate},     {"Perspective", "Perspective", ui::Icon::FxPerspective},
    {"Stylize", "Stylize", ui::Icon::FxStylize},        {"Time", "Time", ui::Icon::FxTime},
};
constexpr const char* kPresets = "Presets";

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
        grid->setSpacing(6);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        into->addLayout(grid);
        columns = std::max(1, (width + 6) / (86 + 6));
        const int w = std::max(56, (width - 6 * (columns - 1)) / columns);
        size = QSize(w, std::clamp(static_cast<int>(w * 0.8f), 64, 88));
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
        QMenu* sub = menu->addMenu(ui::makeIcon(c.icon, theme::kTextDim, 16), c.name);
        sub->setToolTipsVisible(true);
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t) {
            const auto type = static_cast<EffectType>(t);
            if (std::strcmp(effectCategory(type), c.name) != 0) continue;
            QAction* a = sub->addAction(effectLabel(type), [&ctx, type] { applyToSelection(ctx, {makeEffect(type)}); });
            a->setToolTip(effectDescription(type));
        }
    }
    menu->addSeparator();
    QMenu* presets = menu->addMenu("Effect Presets");
    for (const auto& lp : EffectLibrary::all()) {
        const std::vector<Effect> effects = lp.preset.effects;
        presets->addAction(QString::fromStdString(lp.preset.name), [&ctx, effects] { applyToSelection(ctx, effects); });
    }
    presets->setEnabled(!presets->isEmpty());
    menu->addAction("Remove All Effects", [&ctx] {
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
    col->addWidget(ui::sectionLabel("Effects & Presets"));
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search effects");
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
    for (const Effect& e : layer->effects) k << static_cast<int>(e.type) << ',';
    return k.str();
}

std::string EffectsPanel::browserKey() {
    std::ostringstream k;
    k << search_->text().toStdString() << '|' << category_ << '|' << ctx_.selection.empty() << '|' << message_ << '|';
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
        col->addWidget(new QLabel("Effect Controls"));
        col->addWidget(faint(layer ? "Cameras have no effects." : "Select a layer to see and edit its effects."));
        return;
    }
    const uint64_t id = layer->id;
    {
        auto* row = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(ui::iconForLayer(*layer), theme::kAccent, 20).pixmap(20, 20));
        row->addWidget(icon);
        row->addWidget(new QLabel(QString("Effect Controls  ·  %1").arg(QString::fromStdString(layer->name))), 1);
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
        auto* on = new QCheckBox;
        connect(on, &QCheckBox::toggled, this, [this, id, i](bool v) {
            if (Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size()) && l->effects[i].enabled != v) {
                l->effects[i].enabled = v;
                ctx_.touch();
            }
        });
        refreshers_.push_back([this, id, i, on] {
            if (const Layer* l = ctx_.comp().findLayer(id); l && i < static_cast<int>(l->effects.size())) {
                const QSignalBlocker b(on);
                on->setChecked(l->effects[i].enabled);
                on->setToolTip(l->effects[i].enabled ? "Turn this effect off" : "Turn this effect on");
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
        auto* up = new ui::IconButton(ui::Icon::ChevronUp, "Move up (applied earlier)", 24);
        up->setEnabled(i > 0);
        connect(up, &QAbstractButton::clicked, this, [move, i] { move(i, i - 1); });
        auto* down = new ui::IconButton(ui::Icon::ChevronDown, "Move down (applied later)", 24);
        down->setEnabled(i + 1 < count);
        connect(down, &QAbstractButton::clicked, this, [move, i] { move(i, i + 1); });
        auto* del = new ui::IconButton(ui::Icon::Trash, "Remove effect", 24);
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
            samples->setToolTip("More samples: smoother blur, slower preview");
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
            section->addPlainRow("Samples", samples);
        }
        col->addWidget(section);
    }
    col->addWidget(faint(layer->effects.empty() ? "No effects yet. Pick one below: it is added to every selected layer."
                                                : "Effects run top to bottom. Every parameter can be keyframed here or in the timeline."));
    if (!layer->effects.empty()) {
        auto* save = new QPushButton("Save Stack as Preset...");
        connect(save, &QPushButton::clicked, this, [this, id] {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l) return;
            bool ok = false;
            const QString name = QInputDialog::getText(this, "Save Effect Preset", "Preset name", QLineEdit::Normal,
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
    for (auto& f : refreshers_) f();
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
    if (!canApply) col->addWidget(faint("Select layers, then click an effect to apply it."));

    auto effectTile = [&](EffectType type, TileGrid& grid) {
        ui::Tile* t = grid.add(categoryIcon(effectCategory(type)), effectLabel(type));
        t->setDimmed(!canApply);
        t->setToolTip(QString("%1\n%2").arg(effectLabel(type), effectDescription(type)));
        connect(t, &QAbstractButton::clicked, this, [this, type] {
            if (!ctx_.selection.empty()) applyToSelection(ctx_, {makeEffect(type)});
        });
    };
    auto presetTile = [&](const LibraryEffectPreset& lp, TileGrid& grid) {
        ui::Tile* t = grid.add(ui::Icon::Star, QString::fromStdString(lp.preset.name));
        t->setDimmed(!canApply);
        std::string list;
        for (const Effect& e : lp.preset.effects) list += (list.empty() ? "" : " + ") + std::string(effectLabel(e.type));
        t->setToolTip(QString::fromStdString(lp.preset.name + "\n" + list));
        const std::vector<Effect> effects = lp.preset.effects;
        connect(t, &QAbstractButton::clicked, this, [this, effects] {
            if (!ctx_.selection.empty()) applyToSelection(ctx_, effects);
        });
        if (!lp.file.empty()) {
            const std::filesystem::path file = lp.file;
            t->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(t, &QWidget::customContextMenuRequested, this, [this, t, file](QPoint p) {
                QMenu menu;
                menu.addAction("Delete Preset", [this, file] {
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
            if (matches(effectLabel(type), search) || matches(effectCategory(type), search)) effectTile(type, grid);
        }
        for (const auto& lp : EffectLibrary::all())
            if (matches(lp.preset.name, search)) presetTile(lp, grid);
        if (!grid.count) col->addWidget(faint("No effect matches your search."));
    } else if (category_.empty()) { // the categories
        TileGrid grid(col, width);
        for (const Category& c : kCategories) {
            ui::Tile* t = grid.add(c.icon, c.label);
            t->setToolTip(c.name);
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
        auto* back = new ui::IconButton(ui::Icon::Back, "All categories", 24);
        connect(back, &QAbstractButton::clicked, this, [this] {
            category_.clear();
            sync();
        });
        head->addWidget(back);
        head->addWidget(new QLabel(QString::fromStdString(category_)), 1);
        col->addLayout(head);
        TileGrid grid(col, width);
        if (category_ == kPresets) {
            for (const auto& lp : EffectLibrary::all()) presetTile(lp, grid);
            auto* import = new QPushButton("Import Preset...");
            connect(import, &QPushButton::clicked, this, [this] {
                if (auto file = platform::openFileDialog("Import Effect Preset", "Effect preset", ".json")) {
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
    if (!message_.empty()) col->addWidget(faint(QString::fromStdString(message_)));
}

} // namespace bemotion
