#include "ui/panels/PropertiesPanel.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Services.h"
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
      << layer->plugin.objectType << ':' << (PluginHost::get().findType(layer->plugin.objectType) != nullptr) << ':'
      << layer->plugin.params.size() << ':' << layer->plugin.rows.size() << ':' << layer->plugin.table.rows.size() << 'x'
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
    if (layer.type == LayerType::Media && layer.media.hasAudio) buildSoundFixes(col, layer);
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
            // Gradient fills: plugin objects (charts) across each filled part; text (with its own
            // rows, below) across its lines.
            auto addFillRow = [&](CollapsibleSection* section) {
                const bool isText = layer.type == LayerType::Text;
                const ShapeFill now = isText ? layer.text.fill : layer.plugin.fill;
                section->addPlainRow(isText ? tr("Fill") : tr("Gradient"), choiceCombo(kShapeFillLabels, static_cast<int>(now), [this, isText](int i) {
                    for (Layer* l : ctx_.selectedLayers()) {
                        if (isText && l->type == LayerType::Text) l->text.fill = static_cast<ShapeFill>(i);
                        if (!isText && l->type == LayerType::Plugin) l->plugin.fill = static_cast<ShapeFill>(i);
                    }
                    ctx_.touch(); // its color and angle rows come and go (the panel rebuilds)
                }));
            };
            if (group == "Object" && layer.type == LayerType::Plugin) addFillRow(section);
            if (group == "Frame" && layer.type == LayerType::Media) { // mirrored pictures
                for (const bool horizontal : {true, false}) {
                    auto* on = new ui::Switch;
                    connect(on, &QAbstractButton::toggled, this, [this, horizontal](bool v) {
                        for (Layer* l : ctx_.selectedLayers())
                            if (l->type == LayerType::Media && l->media.hasVideo) (horizontal ? l->media.flipH : l->media.flipV) = v;
                        ctx_.touch();
                    });
                    section->addPlainRow(horizontal ? tr("Flip Horizontal") : tr("Flip Vertical"), on);
                    refreshers_.push_back([this, id, on, horizontal] {
                        if (const Layer* l = ctx_.comp().findLayer(id)) {
                            const QSignalBlocker b(on);
                            on->setChecked(horizontal ? l->media.flipH : l->media.flipV);
                        }
                    });
                }
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
                addFillRow(section);
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
            const std::vector<std::string> missing = ui::missingPlugins(ctx_.comp());
            const std::string id = layer.plugin.objectType.substr(0, layer.plugin.objectType.find('.'));
            if (std::find(missing.begin(), missing.end(), id) != missing.end()) {
                auto* install = new QPushButton(tr("Download Plugin"));
                connect(install, &QPushButton::clicked, this, [this, id] { ui::installOfficialPlugins(this, {id}, false); });
                col->addWidget(install, 0, Qt::AlignLeft);
            }
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

} // namespace bemotion
