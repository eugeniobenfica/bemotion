#include "ui/panels/PropertiesPanel.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Automation.h"
#include "app/PresetLibrary.h"
#include "core/Transitions.h"
#include "app/Settings.h"
#include "core/Data.h"
#include "core/Editing.h"
#include "core/Geometry.h"
#include "core/Presets.h"
#include "media/MediaManager.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/LayerMenus.h"
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
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); // only when narrower than its narrowest (rows wrap and shorten first)
}

// What the panel's widgets depend on: when it changes, they are rebuilt.
std::string PropertiesPanel::structureKey() {
    std::ostringstream k;
    Layer* layer = ctx_.selected();
    k << ctx_.selection.size() << ':' << ctx_.maskFocus << ':';
    if (!layer) return k.str() + "comp";
    k << layer->id << ':' << static_cast<int>(layer->type) << ':' << static_cast<int>(layer->shape.kind) << ':'
      << static_cast<int>(layer->background.fill) << ':' << layer->background.mediaId << ':' << ctx_.comp().media.size() << ':'
      << layer->parent << ':'
      << layer->plugin.objectType << ':' << layer->plugin.rows.size() << ':' << layer->plugin.table.rows.size() << 'x'
      << layer->plugin.table.header.size() << ':' << layer->media.mediaId << ':' << layer->media.hasAudio
      << ':' << PresetLibrary::all().size() << ':' << presetMessage_ << ':' << layer->text.captions.size();
    if (layer->type == LayerType::Compound)
        for (const Layer& c : ctx_.comp().layers)
            if (c.parent == layer->id) k << ',' << c.id << c.name;
    for (const Mask& m : layer->masks) k << '|' << m.name; // each mask's header
    k << ':' << hasTimeline(ctx_.comp(), *layer);
    for (const Layer& other : ctx_.comp().layers) // the track matte's choices: the layers beside it
        if (other.parent == layer->parent) k << '/' << other.id << other.name;
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
    if (Layer* layer = ctx_.selected(); layer && ctx_.maskFocus >= 0 && ctx_.maskFocus < static_cast<int>(layer->masks.size()))
        buildMask(col, *layer, ctx_.maskFocus);
    else if (layer)
        buildLayer(col, *layer);
    else
        buildComposition(col);
    col->addStretch();
    setWidget(content);
    verticalScrollBar()->setValue(scroll);
}

// --- a mask, as an object of its own ----------------------------------------------------------

void PropertiesPanel::buildMask(QVBoxLayout* col, Layer& layer, int index) {
    const uint64_t id = layer.id;
    const Mask& mask = layer.masks[static_cast<size_t>(index)];
    auto* head = new QHBoxLayout;
    auto* icon = new QLabel;
    icon->setPixmap(ui::makeIcon(ui::Icon::Mask, theme::kAccent, 20).pixmap(20, 20));
    head->addWidget(icon);
    head->addWidget(new QLabel(trLabel(QString::fromStdString(mask.name)) + QStringLiteral("  ·  ") + QString::fromStdString(layer.name)), 1);
    auto* remove = new ui::IconButton(ui::Icon::Trash, tr("Delete this mask"), 24);
    connect(remove, &QAbstractButton::clicked, this, [this] { ctx_.deleteFocusedMask(); });
    head->addWidget(remove);
    col->addLayout(head);
    // Back to everything the layer has.
    auto* back = new QPushButton(ui::makeIcon(ui::Icon::Back, theme::kAccent, 16), tr("All of %1").arg(QString::fromStdString(layer.name)));
    back->setProperty("flat", true);
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this, id] { ctx_.select(id); });
    auto* row = new QHBoxLayout;
    row->addWidget(back);
    row->addStretch();
    col->addLayout(row);
    // Its settings, then its animatable rows (feather, opacity, expansion, the path).
    auto* section = new CollapsibleSection(trLabel(QString::fromStdString(mask.name)));
    buildMaskRows(section, layer, index);
    for (const PropertyRef& prop : layer.properties())
        if (prop.maskIndex == index && (!prop.whileAnimated || prop.anim->isAnimated())) rows_.push_back(new PropertyRow(ctx_, id, prop, section));
    col->addWidget(section);
    auto* hint = new QLabel(tr("Its points show in the viewport: drag them, or draw another mask with the Pen (G)."));
    hint->setWordWrap(true);
    ui::setTone(hint, ui::Tone::TextFaint);
    col->addWidget(hint);
}

// --- composition ---------------------------------------------------------------------------

void PropertiesPanel::buildComposition(QVBoxLayout* col) {
    auto* title = new QLabel(tr("Composition"));
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    f.setWeight(QFont::DemiBold);
    title->setFont(f);
    col->addWidget(title);
    col->addWidget(faint(tr("Select a layer to edit it.")));
    col->addSpacing(4);

    auto* section = new CollapsibleSection(tr("Composition Settings"));
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
    section->addPlainRow(tr("Width"), w);
    auto* h = sizeValue(&Composition::height);
    section->addPlainRow(tr("Height"), h);
    auto* fps = new ui::DragValue;
    fps->setRange(1.0f, 240.0f);
    fps->setSpeed(0.1f);
    fps->setFormat("%.2f fps");
    connect(fps, &ui::DragValue::valueChanged, this, [this](float v) {
        ctx_.comp().fps = v;
        ctx_.setTime(ctx_.time);
        ctx_.touch();
    });
    section->addPlainRow(tr("Frame Rate"), fps);
    auto* duration = new QLabel;
    duration->setToolTip(tr("The composition ends where its last clip ends: add objects or extend clips to make it longer."));
    section->addPlainRow(tr("Duration"), duration);
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
    section->addPlainRow(tr("Background"), bgRow);
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

// A combo box of `labels`, at `current`; picking one runs `pick` with its index.
template <size_t N>
QComboBox* choiceCombo(const char* const (&labels)[N], int current, std::function<void(int)> pick) {
    auto* combo = newCombo();
    for (const char* label : labels) combo->addItem(tr(label));
    combo->setCurrentIndex(current);
    QObject::connect(combo, &QComboBox::activated, combo, [pick = std::move(pick)](int i) {
        if (i >= 0) pick(i);
    });
    return combo;
}

// How a counter counts: for how long from its clip's start, and how it eases (Start / End follow).
void PropertiesPanel::buildCountRows(CollapsibleSection* section, const Layer& layer) {
    auto apply = [this](auto&& change) {
        for (Layer* l : ctx_.selectedLayers())
            if (l->type == LayerType::Text && l->text.counter) change(l->text);
        ctx_.touch();
    };
    const uint64_t id = layer.id;
    auto* length = new ui::DragValue;
    length->setRange(0.0f, 3600.0f);
    length->setSpeed(0.02f);
    length->setFormat("%.1f s");
    length->setToolTip(tr("How long it counts, from the start of its clip (0: the whole clip)"));
    connect(length, &ui::DragValue::valueChanged, this, [apply](float v) { apply([&](TextProps& p) { p.counterDuration = v; }); });
    section->addPlainRow(tr("Counts For"), length);
    auto* easing = choiceCombo(kCountEasingLabels, static_cast<int>(layer.text.counterEasing),
                               [apply](int i) { apply([&](TextProps& p) { p.counterEasing = static_cast<CountEasing>(i); }); });
    section->addPlainRow(tr("Easing"), easing);
    refreshers_.push_back([this, id, length, easing] {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            length->setValue(l->text.counterDuration);
            const QSignalBlocker b(easing);
            easing->setCurrentIndex(static_cast<int>(l->text.counterEasing));
        }
    });
}

// Letter Animation: letters (words, lines) coming in one after another, leaving, moving all along.
void PropertiesPanel::buildLetterRows(QVBoxLayout* col, const Layer& layer) {
    auto* s = new CollapsibleSection(tr("Letter Animation"));
    const TextProps& t = layer.text;
    auto apply = [this](auto&& change, bool rebuild) {
        for (Layer* l : ctx_.selectedLayers())
            if (l->type == LayerType::Text) change(l->text);
        if (rebuild) key_.clear(); // the timing rows come and go
        ctx_.touch();
    };
    s->addPlainRow(tr("In"), choiceCombo(kLetterAnimLabels, static_cast<int>(t.letterIn), [apply](int i) {
        apply([&](TextProps& p) { p.letterIn = static_cast<LetterAnim>(i); }, true);
    }));
    s->addPlainRow(tr("Out"), choiceCombo(kLetterAnimLabels, static_cast<int>(t.letterOut), [apply](int i) {
        apply([&](TextProps& p) { p.letterOut = static_cast<LetterAnim>(i); }, true);
    }));
    s->addPlainRow(tr("Loop"), choiceCombo(kLetterLoopLabels, static_cast<int>(t.letterLoop), [apply](int i) {
        apply([&](TextProps& p) { p.letterLoop = static_cast<LetterLoop>(i); }, true);
    }));
    if (t.lettersAnimated()) {
        s->addPlainRow(tr("By"), choiceCombo(kLetterUnitLabels, static_cast<int>(t.letterUnit), [apply](int i) {
            apply([&](TextProps& p) { p.letterUnit = static_cast<LetterUnit>(i); }, false);
        }));
        s->addPlainRow(tr("Order"), choiceCombo(kLetterOrderLabels, static_cast<int>(t.letterOrder), [apply](int i) {
            apply([&](TextProps& p) { p.letterOrder = static_cast<LetterOrder>(i); }, false);
        }));
        auto seconds = [&](float TextProps::*field, float lo, const QString& label, const QString& tip) {
            auto* v = new ui::DragValue;
            v->setRange(lo, 10.0f);
            v->setSpeed(0.005f);
            v->setFormat("%.2f s");
            v->setValue(t.*field);
            v->setToolTip(tip);
            connect(v, &ui::DragValue::valueChanged, this, [apply, field](float x) { apply([&](TextProps& p) { p.*field = x; }, false); });
            s->addPlainRow(label, v);
        };
        seconds(&TextProps::letterDelay, 0.0f, tr("Delay"), tr("Seconds between one and the next"));
        seconds(&TextProps::letterDuration, 0.01f, tr("Duration"), tr("Seconds each one takes to come in or leave"));
    }
    auto* about = new QLabel(tr("In starts with the clip, Out ends with it; Loop moves them all along."));
    about->setWordWrap(true);
    ui::setTone(about, ui::Tone::TextFaint);
    s->body()->addWidget(about);
    col->addWidget(s);
}

// A counter's look, in place of a text's content: prefix, suffix, decimals, thousands.
void PropertiesPanel::buildCounterRows(CollapsibleSection* section, const Layer& layer) {
    const uint64_t id = layer.id;
    auto apply = [this](auto&& change) {
        for (Layer* l : ctx_.selectedLayers())
            if (l->type == LayerType::Text && l->text.counter) change(l->text);
        ctx_.touch();
    };
    auto affix = [&](std::string TextProps::*field, const QString& label, const QString& hint) {
        auto* edit = new QLineEdit(QString::fromStdString(layer.text.*field));
        edit->setPlaceholderText(hint);
        connect(edit, &QLineEdit::textEdited, this, [apply, field](const QString& t) {
            apply([&](TextProps& p) { p.*field = t.toStdString(); });
        });
        section->addPlainRow(label, edit);
        refreshers_.push_back([this, id, edit, field] {
            if (const Layer* l = ctx_.comp().findLayer(id); l && !edit->hasFocus()) edit->setText(QString::fromStdString(l->text.*field));
        });
    };
    affix(&TextProps::counterPrefix, tr("Prefix"), tr("Before the number: R$, +..."));
    affix(&TextProps::counterSuffix, tr("Suffix"), tr("After the number: %, k, pts..."));

    auto* decimals = new ui::DragValue;
    decimals->setRange(0.0f, 6.0f);
    decimals->setSpeed(0.05f);
    decimals->setFormat("%.0f");
    connect(decimals, &ui::DragValue::valueChanged, this, [this, apply](float v) {
        apply([&](TextProps& p) { p.counterDecimals = static_cast<int>(std::lround(v)); });
        key_.clear(); // the Value row shows as many decimals
    });
    section->addPlainRow(tr("Decimals"), decimals);

    auto* separator = newCombo();
    const char* const separators[4][2] = {{"", "1234.5"}, {",", "1,234.5"}, {".", "1.234,5"}, {" ", "1 234.5"}};
    for (const auto& sep : separators) separator->addItem(sep[1], QString(sep[0]));
    connect(separator, &QComboBox::activated, this, [apply, separator](int) {
        const std::string sep = separator->currentData().toString().toStdString();
        apply([&](TextProps& p) { p.counterSeparator = sep; });
    });
    section->addPlainRow(tr("Thousands"), separator);
    refreshers_.push_back([this, id, decimals, separator] {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            decimals->setValue(static_cast<float>(l->text.counterDecimals));
            const QSignalBlocker b(separator);
            separator->setCurrentIndex(std::max(0, separator->findData(QString::fromStdString(l->text.counterSeparator))));
        }
    });
}

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
        auto* trash = new ui::IconButton(ui::Icon::Trash, tr("Delete this layer (Delete)"), 26);
        connect(trash, &QAbstractButton::clicked, this, [this] { ctx_.deleteSelected(); });
        row->addWidget(trash);
        col->addLayout(row);
        refreshers_.push_back([this, id, name] {
            if (const Layer* l = ctx_.comp().findLayer(id); l && !name->hasFocus()) name->setText(QString::fromStdString(l->name));
        });
    }
    // What it has besides its properties: its effects and transitions, each a click away.
    if (layer.type != LayerType::Camera) {
        const bool audioOnly = layer.type == LayerType::Media && layer.media.hasAudio && !layer.media.hasVideo;
        auto* row = new ui::FlowLayout; // wraps when the panel is narrow
        row->setSpacing(6);
        auto chip = [&](const QString& panel, const QString& tip) { // `panel`: its English id, never translated
            auto* b = new QPushButton;
            b->setProperty("flat", true);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(tip);
            connect(b, &QPushButton::clicked, this, [this, panel] { emit panelRequested(panel); });
            row->addWidget(b);
            return b;
        };
        auto* fx = chip(QStringLiteral("Effects"), audioOnly ? tr("Its audio effects (Effects panel)") : tr("Its effects (Effects panel)"));
        fx->setIcon(ui::makeIcon(ui::Icon::FxStylize, theme::kTextDim, 16));
        QPushButton* trChip = audioOnly ? nullptr : chip(QStringLiteral("Transitions"), tr("How it moves: its transitions and animations (Animate panel)"));
        if (trChip) trChip->setIcon(ui::makeIcon(ui::Icon::Transition, theme::kTextDim, 16));
        col->addLayout(row);
        auto fill = [fx, trChip, audioOnly](const Layer& l) {
            const size_t n = audioOnly ? l.audioEffects.size() : l.effects.size();
            fx->setText(n == 0 ? tr("No effects") : n == 1 ? tr("1 effect") : tr("%1 effects").arg(n));
            if (!trChip) return;
            // Transitions and Quick Animations alike: how many it has.
            const size_t m = (l.transitionIn.active() ? 1 : 0) + (l.transitionOut.active() ? 1 : 0) + l.animations.size();
            trChip->setText(m == 0 ? tr("No animations") : m == 1 ? tr("1 animation") : tr("%1 animations").arg(m));
        };
        fill(layer);
        refreshers_.push_back([this, id, fill] {
            if (const Layer* l = ctx_.comp().findLayer(id)) fill(*l);
        });
    }
    if (ctx_.selection.size() > 1) {
        auto* n = new QLabel(tr("%1 layers selected").arg(ctx_.selection.size()));
        ui::setTone(n, ui::Tone::Accent);
        col->addWidget(n);
        col->addWidget(faint(tr("Properties below belong to this layer.")));
    }
    // Compounds: where this layer lives, and what a compound contains.
    if (const Layer* parent = layer.parent ? ctx_.comp().findLayer(layer.parent) : nullptr) {
        auto* row = new QHBoxLayout;
        row->addWidget(dim(tr("Inside \"%1\"").arg(QString::fromStdString(parent->name))), 1);
        auto* select = new QPushButton(tr("Select Compound"));
        const uint64_t pid = parent->id;
        connect(select, &QPushButton::clicked, this, [this, pid] { ctx_.select(pid); });
        row->addWidget(select);
        col->addLayout(row);
    }
    if (layer.type == LayerType::Compound) {
        auto* section = new CollapsibleSection(tr("Contents"));
        for (const Layer& child : ctx_.comp().layers) {
            if (child.parent != layer.id) continue;
            auto* b = new QPushButton(ui::makeIcon(ui::iconForLayer(child), theme::kTextDim, 16), QString::fromStdString(child.name));
            b->setFlat(true);
            b->setStyleSheet("text-align: left; padding: 4px 8px;");
            const uint64_t cid = child.id;
            connect(b, &QPushButton::clicked, this, [this, cid] { ctx_.select(cid); });
            section->body()->addWidget(b);
        }
        section->body()->addWidget(faint(tr("Double-click the compound in the viewport, or a layer here, to edit what is inside.")));
        auto* release = new QPushButton(tr("Release Compound"));
        release->setToolTip(tr("Put the layers back where the compound is, keeping how they look (Ctrl+Shift+G)"));
        connect(release, &QPushButton::clicked, this, [this] { ctx_.releaseSelectedCompounds(); });
        section->body()->addWidget(release);
        col->addWidget(section);
    }
    if (layer.type == LayerType::Media) buildMediaSource(col, layer);
    if (layer.type == LayerType::Text && layer.text.subtitle) buildCaptions(col, layer);

    // Plugin groups made only of toggles / choices / text / files (no property rows): shown where the
    // plugin declares them, before the next group with properties.
    std::vector<std::string> loneDone;
    auto addLoneGroup = [&](const std::string& group) {
        if (layer.type != LayerType::Plugin || hasAnimatedParams(layer, group) ||
            std::find(loneDone.begin(), loneDone.end(), group) != loneDone.end())
            return;
        loneDone.push_back(group);
        auto* s = new CollapsibleSection(QString::fromStdString(group));
        for (const PluginParam& q : layer.plugin.params) {
            if (q.group != group) continue;
            const std::string pid = q.id;
            auto param = [this, id, pid]() -> PluginParam* {
                Layer* l = ctx_.comp().findLayer(id);
                return l ? l->plugin.find(pid) : nullptr;
            };
            if (q.kind == PluginParamKind::Toggle) {
                auto* cb = new ui::Switch;
                cb->setChecked(q.toggle);
                connect(cb, &QAbstractButton::toggled, this, [this, param](bool on) {
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
            } else if (q.kind == PluginParamKind::File) {
                s->addPlainRow(QString::fromStdString(q.label), filePicker(id, pid));
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
    };
    auto addLoneGroupsBefore = [&](const std::string& group) {
        const auto& params = layer.plugin.params;
        if (std::none_of(params.begin(), params.end(), [&](const PluginParam& p) { return p.group == group; })) return; // Transform...
        for (const PluginParam& p : params) {
            if (p.group == group) return;
            addLoneGroup(p.group);
        }
    };

    // Property groups in order, with their non-animatable settings first.
    CollapsibleSection* section = nullptr;
    std::string group;
    for (const PropertyRef& prop : layer.properties()) {
        if (prop.effectIndex >= 0) continue; // shown in the Effects tab
        if (prop.whileAnimated && !prop.anim->isAnimated()) continue;
        if (!section || group != prop.group) {
            if (layer.type == LayerType::Plugin) addLoneGroupsBefore(prop.group);
            group = prop.group;
            section = new CollapsibleSection(QString::fromUtf8(prop.group));
            col->addWidget(section);
            if (prop.maskIndex >= 0) buildMaskRows(section, layer, prop.maskIndex);
            // A path shape has no "Shape" property of its own: its section comes before its fill.
            if (group == "Fill" && layer.type == LayerType::Shape && layer.shape.kind == ShapeKind::Path) {
                auto* shape = new CollapsibleSection(tr("Shape"));
                col->insertWidget(col->indexOf(section), shape);
                buildShapeRows(shape, layer);
            }
            if (group == "Transform" && layer.type != LayerType::Camera && layer.type != LayerType::Background && !layer.parent) {
                // A 3D layer gets a Z position: the camera sees it in perspective (parallax).
                auto* on = new ui::Switch;
                on->setToolTip(tr("Seen in perspective by the camera at its Z position: nearer layers look bigger and move faster"));
                connect(on, &QAbstractButton::toggled, this, [this](bool v) {
                    for (Layer* l : ctx_.selectedLayers())
                        if (l->type != LayerType::Camera && l->type != LayerType::Background && !l->parent) l->threeD = v;
                    ctx_.touch(); // the Z Position row comes and goes (the panel rebuilds)
                });
                section->addPlainRow(tr("3D Layer"), on);
                refreshers_.push_back([this, id, on] {
                    if (const Layer* l = ctx_.comp().findLayer(id)) {
                        const QSignalBlocker b(on);
                        on->setChecked(l->threeD);
                    }
                });
            }
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
                        auto* cb = new ui::Switch;
                        connect(cb, &QAbstractButton::toggled, this, [this, param](bool on) {
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
                    case PluginParamKind::File:
                        section->addPlainRow(QString::fromStdString(p.label), filePicker(id, pid));
                        break;
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
            } else if (group == "Crop" && layer.type == LayerType::Media) {
                // The four sides below; the tool drags them in the viewport.
                auto* tool = new QPushButton(tr("Crop in Viewport"));
                tool->setCheckable(true);
                tool->setToolTip(tr("The Crop tool: drag the picture's edges, corners or inside (Shift+C; Esc: done)"));
                connect(tool, &QPushButton::clicked, this, [this](bool on) {
                    ctx_.tool = on ? Tool::Crop : Tool::Select;
                });
                section->addPlainRow(tr("Tool"), tool);
                refreshers_.push_back([this, tool] {
                    const QSignalBlocker b(tool);
                    tool->setChecked(ctx_.tool == Tool::Crop);
                });
            } else if (group == "Counter" && layer.type == LayerType::Text && layer.text.counter) {
                buildCountRows(section, layer);
            } else if (group == "Shape") {
                buildShapeRows(section, layer);
            } else if ((group == "Outline" || group == "Box") && layer.type == LayerType::Text) {
                const bool outline = group == "Outline";
                auto* on = new ui::Switch;
                connect(on, &QAbstractButton::toggled, this, [this, outline](bool v) {
                    for (Layer* l : ctx_.selectedLayers())
                        if (l->type == LayerType::Text) (outline ? l->text.outlineEnabled : l->text.boxEnabled) = v;
                    ctx_.touch();
                });
                section->addPlainRow(tr("Enabled"), on);
                refreshers_.push_back([this, id, on, outline] {
                    if (const Layer* l = ctx_.comp().findLayer(id)) {
                        const QSignalBlocker b(on);
                        on->setChecked(outline ? l->text.outlineEnabled : l->text.boxEnabled);
                    }
                });
            } else if (group == "Fill" || group == "Stroke") {
                const bool fill = group == "Fill";
                auto* on = new ui::Switch;
                connect(on, &QAbstractButton::toggled, this, [this, id, fill](bool v) {
                    if (Layer* l = ctx_.comp().findLayer(id)) {
                        (fill ? l->shape.fillEnabled : l->shape.strokeEnabled) = v;
                        ctx_.touch();
                    }
                });
                section->addPlainRow(tr("Enabled"), on);
                refreshers_.push_back([this, id, on, fill] {
                    if (const Layer* l = ctx_.comp().findLayer(id)) {
                        const QSignalBlocker b(on);
                        on->setChecked(fill ? l->shape.fillEnabled : l->shape.strokeEnabled);
                    }
                });
                if (fill) { // a color, or a gradient to a second one
                    section->addPlainRow(tr("Type"), choiceCombo(kShapeFillLabels, static_cast<int>(layer.shape.fill), [this](int i) {
                        for (Layer* l : ctx_.selectedLayers())
                            if (l->type == LayerType::Shape) l->shape.fill = static_cast<ShapeFill>(i);
                        ctx_.touch();
                    }));
                }
            } else if (group == "Background" && layer.type == LayerType::Background) {
                buildBackgroundRows(section, layer);
            } else if (group == "Text" && layer.type == LayerType::Text) {
                if (layer.text.counter) buildCounterRows(section, layer);
                else {
                    auto* text = new QPlainTextEdit;
                    if (layer.text.subtitle) text->setPlaceholderText(tr("Shown while there are no captions"));
                    text->setFixedHeight(text->fontMetrics().height() * 4 + 12);
                    text->setPlainText(QString::fromStdString(layer.text.text));
                    connect(text, &QPlainTextEdit::textChanged, this, [this, id, text] {
                        if (Layer* l = ctx_.comp().findLayer(id)) {
                            l->text.text = text->toPlainText().toStdString();
                            ctx_.touch();
                        }
                    });
                    section->addPlainRow(layer.text.subtitle ? tr("Placeholder") : tr("Content"), text);
                    refreshers_.push_back([this, id, text] {
                        if (const Layer* l = ctx_.comp().findLayer(id); l && !text->hasFocus() && text->toPlainText().toStdString() != l->text.text) {
                            const QSignalBlocker b(text);
                            text->setPlainText(QString::fromStdString(l->text.text));
                        }
                    });
                }

                auto* font = newCombo();
                font->setEditable(true);
                font->setInsertPolicy(QComboBox::NoInsert);
                for (const auto& fam : Fonts::families()) font->addItem(QString::fromStdString(fam));
                font->completer()->setFilterMode(Qt::MatchContains);
                font->completer()->setCompletionMode(QCompleter::PopupCompletion);
                font->setCurrentText(QString::fromStdString(layer.text.fontFamily));
                if (!Fonts::hasFamily(layer.text.fontFamily)) font->setToolTip(tr("This font is not installed here: Inter is used instead."));
                section->addPlainRow(tr("Font"), font);
                auto* style = newCombo();
                for (const auto& s : Fonts::stylesOf(layer.text.fontFamily)) style->addItem(QString::fromStdString(s));
                style->setCurrentText(QString::fromStdString(layer.text.fontStyle));
                section->addPlainRow(tr("Style"), style);
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
                // Icons, as in After Effects: three words did not fit a narrow panel (or a longer language).
                for (int i = 0; i < 3; ++i) {
                    const ui::Icon icon = i == 0 ? ui::Icon::AlignLeft : i == 1 ? ui::Icon::AlignCenter : ui::Icon::AlignRight;
                    auto* b = new QPushButton(ui::makeIcon(icon, theme::kText, 32), {});
                    b->setToolTip(i == 0 ? tr("Left") : i == 1 ? tr("Center") : tr("Right"));
                    b->setAccessibleName(b->toolTip());
                    b->setIconSize(QSize(18, 18));
                    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
                section->addPlainRow(tr("Align"), alignRow);
                auto* fitLines = newCombo();
                for (const char* label : {TR_NOOP("Off"), TR_NOOP("1 line"), TR_NOOP("2 lines"), TR_NOOP("3 lines"), TR_NOOP("4 lines")}) fitLines->addItem(tr(label));
                fitLines->setToolTip(tr("With a Wrap Width: the text gets smaller so it fits in that many lines"));
                connect(fitLines, &QComboBox::activated, this, [this](int i) {
                    for (Layer* l : ctx_.selectedLayers())
                        if (l->type == LayerType::Text) l->text.fitLines = std::max(0, i);
                    ctx_.touch();
                });
                section->addPlainRow(tr("Auto Fit"), fitLines);
                refreshers_.push_back([this, id, fitLines] {
                    if (const Layer* l = ctx_.comp().findLayer(id)) {
                        const QSignalBlocker b(fitLines);
                        fitLines->setCurrentIndex(std::clamp(l->text.fitLines, 0, 4));
                    }
                });
                refreshers_.push_back([this, id, buttons] {
                    if (const Layer* l = ctx_.comp().findLayer(id))
                        for (int i = 0; i < 3; ++i) buttons[i]->setChecked(static_cast<int>(l->text.align) == i);
                });
            }
        }
        rows_.push_back(new PropertyRow(ctx_, id, prop, section));
    }
    if (layer.type == LayerType::Text) buildLetterRows(col, layer);

    if (layer.type == LayerType::Plugin) {
        for (const PluginParam& p : layer.plugin.params) addLoneGroup(p.group); // the ones after the last property group
        if (layer.plugin.hasRows) buildPluginData(col, layer);
        if (layer.plugin.hasTable) buildPluginTable(col, layer);
        if (!PluginHost::get().findType(layer.plugin.objectType)) {
            auto* warn = new QLabel(tr("The plugin for this object (%1) is not installed or failed to load.")
                                        .arg(QString::fromStdString(layer.plugin.objectType)));
            warn->setWordWrap(true);
            ui::setTone(warn, ui::Tone::Danger);
            col->addWidget(warn);
        }
    }

    if (layer.type != LayerType::Camera && !(layer.type == LayerType::Media && !layer.media.hasVideo)) buildBlending(col, layer);

    // In / out and the label color.
    {
        auto* s = new CollapsibleSection(tr("Layer"));
        auto* in = new ui::DragValue;
        in->setSpeed(0.01f);
        in->setFormat("%.2f s");
        connect(in, &ui::DragValue::valueChanged, this, [this, id](float v) {
            Composition& comp = ctx_.comp();
            comp.setInPoint(id, comp.snapToFrame(v));
            ctx_.touch();
        });
        s->addPlainRow(tr("In"), in);
        auto* out = new ui::DragValue;
        out->setSpeed(0.01f);
        out->setFormat("%.2f s");
        connect(out, &ui::DragValue::valueChanged, this, [this, id](float v) {
            ctx_.comp().setOutPoint(id, v);
            ctx_.touch();
        });
        s->addPlainRow(tr("Out"), out);
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
        s->addPlainRow(tr("Label"), labels);
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

    for (auto& f : refreshers_) f();
}

void PropertiesPanel::buildMediaSource(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Source"));
    const MediaItem* item = ctx_.comp().findMedia(layer.media.mediaId);
    if (!item) {
        auto* gone = new QLabel(tr("The media of this layer was removed."));
        ui::setTone(gone, ui::Tone::Danger);
        s->body()->addWidget(gone);
        col->addWidget(s);
        return;
    }
    const bool missing = media::MediaManager::get().fileMissing(*item);
    s->body()->addWidget(new ui::ElidedLabel(QString::fromStdString(item->name)));
    char info[128];
    if (item->kind == MediaKind::Image) std::snprintf(info, sizeof info, "%s  ·  %d×%d", tr("Image").toUtf8().constData(), item->width, item->height);
    else if (item->kind == MediaKind::Audio) std::snprintf(info, sizeof info, "%s  ·  %.2f s", tr("Audio").toUtf8().constData(), item->duration);
    else
        std::snprintf(info, sizeof info, "%s  ·  %d×%d  ·  %.2f s  ·  %.4g fps%s", tr("Video").toUtf8().constData(), item->width, item->height,
                      item->duration, item->fps, item->hasAudio ? tr("  ·  with audio").toUtf8().constData() : "");
    auto* about = dim(QString::fromUtf8(info));
    about->setWordWrap(true);
    s->body()->addWidget(about);
    // The path, shortened in its middle (the folder and the file name stay readable).
    auto* path = new ui::ElidedLabel(missing ? tr("Missing: %1").arg(QString::fromStdString(item->path)) : QString::fromStdString(item->path), Qt::ElideMiddle);
    path->setToolTip(QString::fromStdString(item->path));
    ui::setTone(path, missing ? ui::Tone::Danger : ui::Tone::TextFaint);
    s->body()->addWidget(path);
    if (missing) s->body()->addWidget(faint(tr("Relink it from the Media panel (right-click the item).")));
    auto* row = new ui::FlowLayout; // wraps when the panel is narrow
    const uint64_t id = layer.id;
    if (layer.media.hasVideo) {
        auto* fit = new QPushButton(tr("Fit to Composition"));
        fit->setToolTip(tr("Scale it to fill the composition, keeping its proportions"));
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
    auto* reveal = new QPushButton(tr("Show in Folder"));
    reveal->setEnabled(!missing);
    const std::string file = item->path;
    connect(reveal, &QPushButton::clicked, this, [file] { platform::revealInFileManager(platform::fromUtf8(file)); });
    row->addWidget(reveal);
    s->body()->addLayout(row);
    col->addWidget(s);
    if (hasTimeline(ctx_.comp(), layer)) buildMediaTime(col, layer);
}

// A shape's kind (a path keeps the outline it had), its path's points, the repeater.
void PropertiesPanel::buildShapeRows(CollapsibleSection* section, Layer& layer) {
    const uint64_t id = layer.id;
    auto* kind = newCombo();
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) kind->addItem(tr(shapeKindLabel(static_cast<ShapeKind>(i))));
    kind->setCurrentIndex(static_cast<int>(layer.shape.kind));
    connect(kind, &QComboBox::currentIndexChanged, this, [this, id](int i) {
        if (Layer* l = ctx_.comp().findLayer(id); l && i >= 0) {
            // A shape becoming a path keeps its outline, as points to edit.
            if (static_cast<ShapeKind>(i) == ShapeKind::Path && l->shape.kind != ShapeKind::Path) {
                l->shape.path = shapeToBezier(l->shape, ctx_.time);
                if (l->shape.kind == ShapeKind::Line) l->shape.path.closed = false;
            }
            l->shape.kind = static_cast<ShapeKind>(i);
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Type"), kind);
    if (layer.shape.kind == ShapeKind::Path) {
        auto* edit = new QPushButton(ui::makeIcon(ui::Icon::Pen, theme::kTextDim, 16), tr("Edit Points"));
        edit->setCheckable(true);
        edit->setToolTip(tr("The Pen (G): drag points and handles; click the outline to add a point, "
                         "Ctrl+click a point to remove it, Alt+click to make it a corner or smooth"));
        connect(edit, &QPushButton::clicked, this, [this](bool on) { ctx_.tool = on ? Tool::Pen : Tool::Select; });
        section->addPlainRow(tr("Path"), edit);
        refreshers_.push_back([this, edit] {
            const QSignalBlocker b(edit);
            edit->setChecked(ctx_.tool == Tool::Pen);
        });
        auto* closed = new ui::Switch;
        closed->setToolTip(tr("The last point joins the first"));
        connect(closed, &QAbstractButton::toggled, this, [this, id](bool v) {
            if (Layer* l = ctx_.comp().findLayer(id)) {
                l->shape.path.closed = v;
                ctx_.touch();
            }
        });
        section->addPlainRow(tr("Closed"), closed);
        refreshers_.push_back([this, id, closed] {
            if (const Layer* l = ctx_.comp().findLayer(id)) {
                const QSignalBlocker b(closed);
                closed->setChecked(l->shape.path.closed);
            }
        });
    }
    auto* repeater = new ui::Switch;
    repeater->setToolTip(tr("Copies of the shape, each moved, turned and scaled from the last (After Effects' Repeater)"));
    connect(repeater, &QAbstractButton::toggled, this, [this](bool v) {
        for (Layer* l : ctx_.selectedLayers())
            if (l->type == LayerType::Shape) l->shape.repeater = v;
        ctx_.touch(); // its rows come and go (the panel rebuilds)
    });
    section->addPlainRow(tr("Repeater"), repeater);
    refreshers_.push_back([this, id, repeater] {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            const QSignalBlocker b(repeater);
            repeater->setChecked(l->shape.repeater);
        }
    });
}

// A clip's time: its speed, direction, time remap, a frozen frame.
void PropertiesPanel::buildMediaTime(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Speed"));
    const uint64_t id = layer.id;
    auto* speed = new ui::DragValue;
    speed->setRange(1.0f, 10000.0f);
    speed->setSpeed(1.0f);
    speed->setFormat("%.0f%%");
    speed->setToolTip(tr("100%: as recorded; 200%: twice as fast (half as long). The first frame stays where it is"));
    connect(speed, &ui::DragValue::valueChanged, this, [this, id](float v) {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            ctx_.comp().duration += kMaxOverhang; // room to grow (fitted again when the edit is done)
            setClipSpeed(ctx_.comp(), id, v * 0.01, l->media.reversed);
            ctx_.comp().duration -= kMaxOverhang;
            ctx_.touch();
        }
    });
    s->addPlainRow(tr("Speed"), speed);
    auto* reverse = new ui::Switch;
    reverse->setToolTip(tr("Plays the same footage backwards"));
    connect(reverse, &QAbstractButton::toggled, this, [this, id](bool v) {
        if (const Layer* l = ctx_.comp().findLayer(id)) {
            setClipSpeed(ctx_.comp(), id, l->media.speed, v);
            ctx_.touch();
        }
    });
    s->addPlainRow(tr("Reverse"), reverse);
    auto* remap = new ui::Switch;
    remap->setToolTip(tr("Keyframe which moment of the footage shows at each moment (Time Remap): slow motion, ramps, holds"));
    connect(remap, &QAbstractButton::toggled, this, [this, id](bool v) {
        if (setTimeRemap(ctx_.comp(), id, v)) ctx_.touch(); // its row comes and goes (the panel rebuilds)
    });
    s->addPlainRow(tr("Time Remap"), remap);
    auto* buttons = new ui::FlowLayout; // wraps when the panel is narrow
    auto* freeze = new QPushButton(tr("Freeze Frame"));
    freeze->setToolTip(tr("The whole clip holds the frame under the playhead"));
    connect(freeze, &QPushButton::clicked, this, [this, id] {
        if (freezeFrame(ctx_.comp(), id, ctx_.time)) ctx_.touch();
    });
    buttons->addWidget(freeze);
    auto* more = new QPushButton(tr("Speed / Duration..."));
    connect(more, &QPushButton::clicked, this, [this, id] {
        ctx_.select(id);
        ui::showSpeedDialog(ctx_, this);
    });
    buttons->addWidget(more);
    s->body()->addLayout(buttons);
    col->addWidget(s);
    refreshers_.push_back([this, id, speed, reverse, remap] {
        const Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        speed->setValue(static_cast<float>(l->media.speed * 100.0));
        speed->setEnabled(!l->media.timeRemap);
        const QSignalBlocker a(reverse), b(remap);
        reverse->setChecked(l->media.reversed);
        reverse->setEnabled(!l->media.timeRemap);
        remap->setChecked(l->media.timeRemap);
    });
}

// How the layer mixes with what is under it, and its track matte.
void PropertiesPanel::buildBlending(QVBoxLayout* col, Layer& layer) {
    auto* s = new CollapsibleSection(tr("Blending"));
    const uint64_t id = layer.id;
    auto* mode = ui::blendModeCombo(ctx_);
    ui::showBlendMode(mode, layer.blend);
    mode->setToolTip(tr("How its colors mix with the layers under it (the one under the mouse shows on the picture)"));
    s->addPlainRow(tr("Mode"), mode);
    // The track matte: a mode, and the layer beside it whose picture decides.
    std::vector<uint64_t> candidates;
    for (int i = 0; i < static_cast<int>(ctx_.comp().layers.size()); ++i) {
        const Layer& other = ctx_.comp().layers[i];
        if (other.id != id && other.parent == layer.parent && other.type != LayerType::Camera) candidates.push_back(other.id);
    }
    auto* matte = choiceCombo(kMatteModeLabels, static_cast<int>(layer.matte), [this, id, candidates](int i) {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        if (i == 0 || candidates.empty()) {
            l->matte = MatteMode::None;
            l->matteLayer = 0;
        } else {
            if (!l->matteLayer) { // the layer right above it, as After Effects does
                const int at = ctx_.comp().indexOf(id);
                uint64_t above = candidates.front();
                for (uint64_t c : candidates)
                    if (ctx_.comp().indexOf(c) < at) above = c;
                l->matteLayer = above;
            }
            l->matte = static_cast<MatteMode>(i);
        }
        ctx_.touch();
    });
    matte->setToolTip(tr("Shows this layer only where another one is opaque (Alpha) or bright (Luma); that one is then not drawn"));
    matte->setEnabled(!candidates.empty());
    s->addPlainRow(tr("Track Matte"), matte);
    auto* source = newCombo();
    for (uint64_t c : candidates)
        if (const Layer* other = ctx_.comp().findLayer(c))
            source->addItem(ui::makeIcon(ui::iconForLayer(*other), theme::kTextDim, 16), QString::fromStdString(other->name), QVariant::fromValue<qulonglong>(c));
    connect(source, &QComboBox::activated, this, [this, id, source](int i) {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l || i < 0) return;
        l->matteLayer = source->itemData(i).toULongLong();
        if (l->matte == MatteMode::None) l->matte = MatteMode::Alpha;
        ctx_.touch();
    });
    s->addPlainRow(tr("Matte Layer"), source);
    // Masks: added here (fitted to it, or drawn with the Pen); each gets a section above.
    auto* masks = new QPushButton(ui::makeIcon(ui::Icon::Mask, theme::kTextDim, 16), tr("Add Mask"));
    masks->setToolTip(tr("Cut the layer to a shape: a rectangle or an ellipse over it, or one drawn with the Pen (G)"));
    connect(masks, &QPushButton::clicked, this, [this, masks] {
        QMenu menu;
        ui::fillMaskMenu(&menu, ctx_);
        menu.exec(masks->mapToGlobal(QPoint(0, masks->height())));
    });
    s->addPlainRow(tr("Masks"), masks);
    col->addWidget(s);
    refreshers_.push_back([this, id, mode, matte, source] {
        const Layer* l = ctx_.comp().findLayer(id);
        if (!l) return;
        const QSignalBlocker a(mode), b(matte), c(source);
        if (!mode->view()->isVisible()) ui::showBlendMode(mode, l->blend);
        if (!matte->view()->isVisible()) matte->setCurrentIndex(static_cast<int>(l->matte));
        source->setEnabled(l->matte != MatteMode::None);
        if (!source->view()->isVisible()) source->setCurrentIndex(l->matte == MatteMode::None ? -1 : source->findData(QVariant::fromValue<qulonglong>(l->matteLayer)));
    });
}

// A mask's settings above its feather, opacity and expansion: how it combines, inverted, on.
void PropertiesPanel::buildMaskRows(CollapsibleSection* section, Layer& layer, int index) {
    const uint64_t id = layer.id;
    auto mask = [this, id, index]() -> Mask* {
        Layer* l = ctx_.comp().findLayer(id);
        return l && index < static_cast<int>(l->masks.size()) ? &l->masks[index] : nullptr;
    };
    auto* mode = choiceCombo(kMaskModeLabels, static_cast<int>(layer.masks[index].mode), [this, mask](int i) {
        if (Mask* m = mask()) {
            m->mode = static_cast<MaskMode>(i);
            ctx_.touch();
        }
    });
    mode->setToolTip(tr("Add: shows its inside; Subtract: takes it away; Intersect: only where it overlaps the masks above; Difference: where only one is"));
    section->addPlainRow(tr("Mode"), mode);
    auto* inverted = new ui::Switch;
    connect(inverted, &QAbstractButton::toggled, this, [this, mask](bool v) {
        if (Mask* m = mask()) {
            m->inverted = v;
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Inverted"), inverted);
    auto* enabled = new ui::Switch;
    enabled->setToolTip(tr("Turn the mask off without losing it"));
    connect(enabled, &QAbstractButton::toggled, this, [this, mask](bool v) {
        if (Mask* m = mask()) {
            m->enabled = v;
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("Enabled"), enabled);
    // Its points (the Pen), its name, removing it: small buttons on the right.
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* edit = new ui::IconButton(ui::Icon::Pen, tr("Edit its points with the Pen (G): drag points and handles in the viewport"), 26);
    connect(edit, &QAbstractButton::clicked, this, [this] { ctx_.tool = Tool::Pen; });
    row->addWidget(edit);
    auto* rename = new ui::IconButton(ui::Icon::Edit, tr("Rename this mask"), 26);
    connect(rename, &QAbstractButton::clicked, this, [this, mask] {
        Mask* m = mask();
        if (!m) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Mask"), tr("Name:"), QLineEdit::Normal, QString::fromStdString(m->name), &ok);
        if (ok && !name.trimmed().isEmpty() && (m = mask())) {
            m->name = name.trimmed().toStdString();
            ctx_.touch();
        }
    });
    row->addWidget(rename);
    auto* remove = new ui::IconButton(ui::Icon::Trash, tr("Delete this mask"), 26);
    connect(remove, &QAbstractButton::clicked, this, [this, id, index] {
        Layer* l = ctx_.comp().findLayer(id);
        if (!l || index >= static_cast<int>(l->masks.size())) return;
        l->masks.erase(l->masks.begin() + index);
        ctx_.touch();
    });
    row->addWidget(remove);
    section->body()->addLayout(row);
    refreshers_.push_back([mask, mode, inverted, enabled] {
        const Mask* m = mask();
        if (!m) return;
        const QSignalBlocker a(mode), b(inverted), c(enabled);
        if (!mode->view()->isVisible()) mode->setCurrentIndex(static_cast<int>(m->mode));
        inverted->setChecked(m->inverted);
        enabled->setChecked(m->enabled);
    });
}

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
    add->setVisible(names.empty()); // named rows are added by name, above
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
