#include "ui/panels/ColorPanel.h"

#include "app/Platform.h"
#include "render/SceneRenderer.h"
#include "ui/CurvesEditor.h"
#include "ui/EditorContext.h"
#include "ui/Language.h"
#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace bemotion {

namespace {

// The parts, in the order a colorist works (and Lumetri shows them).
constexpr EffectType kParts[] = {EffectType::BrightnessContrast, EffectType::HueSaturation, EffectType::Lut, EffectType::Curves};

QLabel* faint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

int firstOf(const Layer& layer, EffectType type) {
    for (size_t i = 0; i < layer.effects.size(); ++i)
        if (layer.effects[i].type == type) return static_cast<int>(i);
    return -1;
}

bool hasPicture(const Layer& l) {
    return l.type != LayerType::Camera && !(l.type == LayerType::Media && l.media.hasAudio && !l.media.hasVideo);
}

} // namespace

namespace ui {

void fillLutChoices(QComboBox* combo, const std::string& file) {
    combo->clear();
    combo->addItem(tr("None"), QString());
    for (const std::string& name : render::bundledLuts())
        combo->addItem(QString::fromStdString(name.substr(0, name.size() - 5)), QString::fromStdString("bundled:" + name)); // without ".cube"
    if (!file.empty() && combo->findData(QString::fromStdString(file)) < 0)
        combo->addItem(QString::fromStdString(platform::toUtf8(platform::fromUtf8(file).filename())), QString::fromStdString(file));
    combo->insertSeparator(combo->count());
    combo->addItem(tr("Choose File..."), QStringLiteral("?"));
    combo->setCurrentIndex(std::max(0, combo->findData(QString::fromStdString(file))));
    combo->setToolTip(tr("A color look-up table: one of the looks that come with bemotion, or a .cube file"));
}

bool lutOfChoice(QComboBox* combo, int index, std::string& file) {
    file = combo->itemData(index).toString().toStdString();
    if (file != "?") return true;
    const auto chosen = platform::openFileDialog(TR_NOOP("Choose a LUT"), TR_NOOP("Cube LUT"), ".cube");
    if (!chosen) return false;
    file = platform::toUtf8(*chosen);
    return true;
}

} // namespace ui

ColorPanel::ColorPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

std::string ColorPanel::structureKey() {
    std::ostringstream k;
    const Layer* l = ctx_.selected();
    if (!l) return "none";
    k << l->id << ':' << static_cast<int>(l->type) << ':' << hasPicture(*l) << ':' << l->name;
    for (const Effect& e : l->effects) k << '|' << static_cast<int>(e.type) << (e.type == EffectType::Lut ? e.file : "");
    return k.str();
}

void ColorPanel::sync() {
    if (!isVisible()) return;
    // Not mid-drag: the first change of a part adds its effect, and the part's rows come once the
    // mouse is up (the control being dragged stays until then).
    if (structureKey() != key_ && !editing_ && QApplication::mouseButtons() == Qt::NoButton) {
        rebuild();
        return;
    }
    for (PropertyRow* r : rows_) r->refresh();
    for (auto& f : refreshers_) f();
}

bool ColorPanel::eventFilter(QObject* watched, QEvent* e) {
    if (qobject_cast<ui::CurvesEditor*>(watched)) {
        if (e->type() == QEvent::MouseButtonPress) editing_ = true;
        if (e->type() == QEvent::MouseButtonRelease) editing_ = false;
    }
    return QScrollArea::eventFilter(watched, e);
}

Effect* ColorPanel::effectOf(uint64_t layerId, EffectType type, bool create) {
    Layer* l = ctx_.comp().findLayer(layerId);
    if (!l) return nullptr;
    if (const int i = firstOf(*l, type); i >= 0) return &l->effects[i];
    if (!create) return nullptr;
    // In the panel's order among the parts it has (a look after the correction, curves last), so
    // the stack grades the way the panel reads; after the layer's other effects.
    const auto rank = [](EffectType t) { return std::find(std::begin(kParts), std::end(kParts), t) - std::begin(kParts); };
    auto at = std::find_if(l->effects.begin(), l->effects.end(), [&](const Effect& e) {
        return rank(e.type) < static_cast<std::ptrdiff_t>(std::size(kParts)) && rank(e.type) > rank(type);
    });
    at = l->effects.insert(at, makeEffect(type));
    ctx_.clearKeySelection(); // key selections name effects by index
    return &*at;
}

void ColorPanel::rebuild() {
    // The same layer keeps its place in the panel (a part just added, an effect removed).
    const std::string next = structureKey();
    const auto layerOf = [](const std::string& k) { return k.substr(0, k.find(':')); };
    const int scroll = layerOf(key_) == layerOf(next) ? verticalScrollBar()->value() : 0;
    key_ = next;
    editing_ = false;
    rows_.clear();
    refreshers_.clear();
    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(4);
    Layer* layer = ctx_.selected();
    if (!layer || !hasPicture(*layer)) {
        col->addWidget(new QLabel(tr("Color")));
        col->addWidget(faint(!layer ? tr("Select a layer to grade it: brightness, contrast, saturation, a look and curves.")
                                    : tr("This layer has no picture to grade.")));
    } else {
        auto* head = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(ui::Icon::FxColor, theme::kAccent, 20).pixmap(20, 20));
        head->addWidget(icon);
        head->addWidget(new QLabel(tr("Color  ·  %1").arg(QString::fromStdString(layer->name))), 1);
        col->addLayout(head);
        for (EffectType type : kParts) buildPart(col, *layer, type);
        col->addWidget(faint(tr("Each part is an effect of the layer (the Effects tab shows them in order, with the others): "
                                "it is added the first time you change it.")));
    }
    auto* scopes = new QPushButton(ui::makeIcon(ui::Icon::Scopes, theme::kTextDim, 16), tr("Show Scopes"));
    scopes->setToolTip(tr("Waveform, RGB parade, vectorscope and histogram of the frame"));
    connect(scopes, &QPushButton::clicked, this, &ColorPanel::showScopesRequested);
    auto* row = new QHBoxLayout;
    row->addWidget(scopes);
    row->addStretch();
    col->addLayout(row);
    col->addStretch();
    setWidget(content);
    verticalScrollBar()->setValue(scroll);
    // Laid out a moment later: until then the scroll range is too short.
    if (scroll > 0) QTimer::singleShot(0, this, [this, scroll] { verticalScrollBar()->setValue(scroll); });
    for (PropertyRow* r : rows_) r->refresh();
    for (auto& f : refreshers_) f();
}

void ColorPanel::buildPart(QVBoxLayout* col, Layer& layer, EffectType type) {
    const uint64_t id = layer.id;
    const int index = firstOf(layer, type);
    // A part the layer has: on / off and remove, as in the Effects tab.
    QWidget* extra = nullptr;
    if (index >= 0) {
        extra = new QWidget;
        auto* tools = new QHBoxLayout(extra);
        tools->setContentsMargins(0, 0, 0, 0);
        tools->setSpacing(2);
        auto* on = new ui::Switch;
        connect(on, &QAbstractButton::toggled, this, [this, id, type](bool v) {
            if (Effect* e = effectOf(id, type, false); e && e->enabled != v) {
                e->enabled = v;
                ctx_.touch();
            }
        });
        refreshers_.push_back([this, id, type, on] {
            if (const Effect* e = effectOf(id, type, false)) {
                const QSignalBlocker b(on);
                on->setChecked(e->enabled);
                on->setToolTip(e->enabled ? tr("Turn this off") : tr("Turn this on"));
            }
        });
        auto* del = new ui::IconButton(ui::Icon::Reset, tr("Reset (removes the effect)"), 24);
        connect(del, &QAbstractButton::clicked, this, [this, id, index] {
            if (Layer* l = ctx_.comp().findLayer(id); l && index < static_cast<int>(l->effects.size())) {
                l->effects.erase(l->effects.begin() + index);
                ctx_.clearKeySelection();
                ctx_.touch();
            }
        });
        tools->addWidget(on);
        tools->addWidget(del);
    }
    auto* section = new CollapsibleSection(type == EffectType::Lut ? tr("Look") : tr(effectLabel(type)), nullptr, extra);
    if (type == EffectType::Lut) buildLook(section, layer, index);
    if (type == EffectType::Curves) buildCurves(section, layer, index);
    if (index >= 0) {
        for (const PropertyRef& p : layer.properties())
            if (p.effectIndex == index) rows_.push_back(new PropertyRow(ctx_, id, p, section));
    } else if (type != EffectType::Lut && type != EffectType::Curves) {
        // Its defaults, ready to change: the first change adds the effect.
        Effect defaults = makeEffect(type);
        for (const EffectParam& ep : effectParams(type)) {
            auto* value = new ui::DragValue;
            value->setRange(ep.min, ep.max);
            value->setSpeed(ep.speed);
            value->setFormat(ep.format);
            value->setValue(static_cast<AnimFloat&>(defaults.slot(ep.slot)).base);
            const EffectSlot slot = ep.slot;
            connect(value, &ui::DragValue::editStarted, this, [this] { editing_ = true; });
            connect(value, &ui::DragValue::editFinished, this, [this] { editing_ = false; });
            connect(value, &ui::DragValue::valueChanged, this, [this, id, type, slot](float v) {
                if (Effect* e = effectOf(id, type, true)) {
                    static_cast<AnimFloat&>(e->slot(slot)).set(ctx_.time, v);
                    ctx_.touch();
                }
            });
            section->addPlainRow(tr(ep.label), value);
        }
    }
    col->addWidget(section);
}

void ColorPanel::buildLook(CollapsibleSection* section, Layer& layer, int index) {
    const uint64_t id = layer.id;
    auto* lut = new QComboBox;
    ui::fillLutChoices(lut, index >= 0 ? layer.effects[index].file : std::string());
    connect(lut, &QComboBox::activated, this, [this, id, lut](int choice) {
        std::string file;
        if (!ui::lutOfChoice(lut, choice, file)) {
            QTimer::singleShot(0, this, [this] { rebuild(); }); // back to what it was (not from its own handler)
            return;
        }
        Effect* e = effectOf(id, EffectType::Lut, !file.empty());
        if (e && e->file != file) {
            e->file = file;
            e->relative.clear();
            ctx_.touch();
        }
    });
    section->addPlainRow(tr("LUT"), lut);
    std::string title, error;
    if (index >= 0 && !layer.effects[index].file.empty() && !render::lutInfo(layer.effects[index].file, title, error)) {
        auto* problem = new QLabel(QString::fromStdString(error));
        problem->setWordWrap(true);
        ui::setTone(problem, ui::Tone::Danger);
        section->body()->addWidget(problem);
    }
}

void ColorPanel::buildCurves(CollapsibleSection* section, Layer& layer, int index) {
    const uint64_t id = layer.id;
    auto* editor = new ui::CurvesEditor;
    if (index >= 0) editor->setCurves(layer.effects[index].curves);
    connect(editor, &ui::CurvesEditor::changed, this, [this, id, editor] {
        if (Effect* e = effectOf(id, EffectType::Curves, true)) {
            e->curves = editor->curves();
            for (auto& curve : e->curves) cleanCurve(curve);
            ctx_.touch();
        }
    });
    refreshers_.push_back([this, id, editor] {
        if (const Effect* e = effectOf(id, EffectType::Curves, false)) editor->setCurves(e->curves);
    });
    editor->installEventFilter(this); // a point being dragged: the part's rows wait (eventFilter)
    section->body()->addWidget(editor);
}

} // namespace bemotion
