#include "ui/panels/ToolSettingsPanel.h"

#include "app/Settings.h"
#include "ui/EditorContext.h"
#include "ui/Language.h"
#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace bemotion {

namespace {

// A value dragged or typed, saved when the edit ends.
ui::DragValue* number(float& value, float lo, float hi, float speed, const char* format) {
    auto* v = new ui::DragValue;
    v->setRange(lo, hi);
    v->setSpeed(speed);
    v->setFormat(format);
    v->setValue(value);
    QObject::connect(v, &ui::DragValue::valueChanged, v, [&value](float x) { value = x; });
    QObject::connect(v, &ui::DragValue::editFinished, v, [] { saveSettings(); });
    return v;
}

ui::Switch* toggle(bool& value) {
    auto* s = new ui::Switch;
    s->setChecked(value);
    QObject::connect(s, &QAbstractButton::toggled, s, [&value](bool on) {
        value = on;
        saveSettings();
    });
    return s;
}

QLabel* hint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextDim);
    return l;
}

// The tool's entry in the toolbar's catalog (its name, icon, description and shortcut).
const ui::ToolInfo* toolInfo(Tool tool) {
    static const char* const ids[] = {"select", "razor", "crop", "pen", "ripple", "roll", "slip", "brush", "eraser", "puppet", "warp", "track"};
    const int i = static_cast<int>(tool);
    if (i < 0 || i >= static_cast<int>(std::size(ids))) return nullptr;
    const int at = ui::toolIndex(ids[i]);
    return at < 0 ? nullptr : &ui::toolCatalog()[static_cast<size_t>(at)];
}

} // namespace

ToolSettingsPanel::ToolSettingsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

std::string ToolSettingsPanel::structureKey() const {
    // What the widgets depend on: the tool, and the layers a track can go onto.
    std::string key = std::to_string(static_cast<int>(ctx_.tool));
    if (ctx_.tool == Tool::Track)
        for (const Layer& l : ctx_.project.comp.layers) key += "/" + std::to_string(l.id) + l.name;
    return key;
}

void ToolSettingsPanel::sync() {
    if (structureKey() != key_) rebuild();
    for (const auto& r : refreshers_) r();
}

void ToolSettingsPanel::rebuild() {
    key_ = structureKey();
    refreshers_.clear();
    ToolSettings& t = settings().tools;
    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(4);

    // The tool in use, always: its icon, name, shortcut and what it does.
    if (const ui::ToolInfo* info = toolInfo(ctx_.tool)) {
        auto* head = new QHBoxLayout;
        head->setSpacing(8);
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(info->icon, theme::kAccent, 22).pixmap(22, 22));
        head->addWidget(icon);
        auto* name = new QLabel(tr(info->label));
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        head->addWidget(name);
        if (info->shortcut != ui::Action::Count && ui::chordOf(info->shortcut)) {
            auto* key = new QLabel(QString::fromStdString(ui::chordName(ui::chordOf(info->shortcut))));
            ui::setTone(key, ui::Tone::TextFaint);
            head->addWidget(key);
        }
        head->addStretch();
        col->addLayout(head);
        col->addWidget(hint(tr(info->description)));
        col->addSpacing(4);
    }

    switch (ctx_.tool) {
    case Tool::Select: {
        auto* s = new CollapsibleSection(tr("Snapping"));
        auto* snap = new ui::Switch;
        snap->setChecked(ctx_.snapping);
        connect(snap, &QAbstractButton::toggled, this, [this](bool on) { ctx_.snapping = on; });
        refreshers_.push_back([this, snap] {
            if (snap->isChecked() != ctx_.snapping) snap->setChecked(ctx_.snapping);
        });
        s->addPlainRow(tr("Snap"), snap);
        s->addPlainRow(tr("Distance"), number(settings().snapDistance, 1, 40, 0.1f, "%.0f px"));
        s->body()->addWidget(hint(tr("Objects, clips and keyframes snap to each other. Hold Ctrl while dragging to skip it.")));
        col->addWidget(s);
        break;
    }
    case Tool::Brush: {
        auto* s = new CollapsibleSection(tr("Brush"));
        auto* color = new ui::ColorButton;
        color->setColor({t.brushColor[0], t.brushColor[1], t.brushColor[2], t.brushColor[3]});
        connect(color, &ui::ColorButton::colorChanged, this, [](Color c) {
            float* to = settings().tools.brushColor;
            to[0] = c.r, to[1] = c.g, to[2] = c.b, to[3] = c.a;
        });
        connect(color, &ui::ColorButton::editFinished, this, [] { saveSettings(); });
        refreshers_.push_back([color] {
            const float* c = settings().tools.brushColor;
            if (!color->isDown() && color->color() != Color{c[0], c[1], c[2], c[3]}) color->setColor({c[0], c[1], c[2], c[3]});
        });
        s->addPlainRow(tr("Color"), color);
        auto* size = number(t.brushSize, 1, 500, 0.5f, "%.0f px");
        refreshers_.push_back([size] {
            if (size->value() != settings().tools.brushSize) size->setValue(settings().tools.brushSize); // Ctrl+wheel in the viewport
        });
        s->addPlainRow(tr("Size"), size);
        s->addPlainRow(tr("Opacity"), number(t.brushOpacity, 1, 100, 0.5f, "%.0f%%"));
        s->addPlainRow(tr("Softness"), number(t.brushSoftness, 0, 200, 0.25f, "%.1f px"));
        s->body()->addWidget(hint(tr("Paints on the selected layer, or on a new one; the layer's box grows with the paint. Write On (in Properties) draws the strokes over time.")));
        col->addWidget(s);
        break;
    }
    case Tool::Eraser: {
        auto* s = new CollapsibleSection(tr("Eraser"));
        auto* size = number(t.eraserSize, 1, 500, 0.5f, "%.0f px");
        refreshers_.push_back([size] {
            if (size->value() != settings().tools.eraserSize) size->setValue(settings().tools.eraserSize);
        });
        s->addPlainRow(tr("Size"), size);
        s->addPlainRow(tr("Softness"), number(t.eraserSoftness, 0, 200, 0.25f, "%.1f px"));
        col->addWidget(s);
        break;
    }
    case Tool::Puppet: {
        auto* s = new CollapsibleSection(tr("Puppet"));
        s->addPlainRow(tr("New pins"), ui::segmented({TR_NOOP("Position"), TR_NOOP("Starch")}, t.pinStarch ? 1 : 0, [](int i) {
                           settings().tools.pinStarch = i == 1;
                           saveSettings();
                       }));
        s->addPlainRow(tr("Keyframe while dragging"), toggle(t.pinAutoKey));
        s->addPlainRow(tr("Show mesh"), toggle(t.showMesh));
        auto* clear = new QPushButton(tr("Remove All Pins"));
        connect(clear, &QPushButton::clicked, this, [this] {
            for (Layer* l : ctx_.selectedLayers()) l->pins.clear();
            ctx_.touch();
        });
        refreshers_.push_back([this, clear] { clear->setEnabled(!ctx_.selection.empty()); });
        s->body()->addWidget(clear);
        s->body()->addWidget(hint(tr("Click the picture to pin it; drag a pin to bend it. The mesh covers only what the picture shows: "
                                     "separate pieces move on their own. Starch pins stiffen the picture around them. Ctrl+drag a pin "
                                     "records its motion as you move it; Alt+click removes a pin.")));
        col->addWidget(s);
        break;
    }
    case Tool::Warp: {
        auto* s = new CollapsibleSection(tr("Warp"));
        s->addPlainRow(tr("Keyframe while dragging"), toggle(t.pinAutoKey));
        auto* clear = new QPushButton(tr("Remove Warp"));
        connect(clear, &QPushButton::clicked, this, [this] {
            for (Layer* l : ctx_.selectedLayers()) {
                l->warp = false;
                l->warpPoints = {};
            }
            ctx_.touch();
        });
        refreshers_.push_back([this, clear] { clear->setEnabled(!ctx_.selection.empty()); });
        s->body()->addWidget(clear);
        col->addWidget(s);
        break;
    }
    case Tool::Track: {
        auto* s = new CollapsibleSection(tr("Track Point"));
        s->addPlainRow(tr("Detail size"), number(t.trackFeature, 8, 120, 0.25f, "%.0f px"));
        s->addPlainRow(tr("Search area"), number(t.trackSearch, 8, 200, 0.25f, "%.0f px"));
        s->addPlainRow(tr("Minimum match"), number(t.trackMatch, 10, 99, 0.25f, "%.0f%%"));
        s->addPlainRow(tr("Direction"), ui::segmented({TR_NOOP("Forward"), TR_NOOP("Backward")}, t.trackBackward ? 1 : 0, [](int i) {
                           settings().tools.trackBackward = i == 1;
                           saveSettings();
                       }));
        auto* target = new QComboBox;
        target->addItem(tr("A new Track Point layer"), QVariant::fromValue<qulonglong>(0));
        for (const Layer& l : ctx_.comp().layers)
            if (l.type != LayerType::Camera && !(l.type == LayerType::Media && !l.media.hasVideo))
                target->addItem(QString::fromStdString(l.name), QVariant::fromValue<qulonglong>(l.id));
        target->setCurrentIndex(std::max(0, target->findData(QVariant::fromValue<qulonglong>(ctx_.trackTarget))));
        connect(target, &QComboBox::activated, this, [this, target](int) { ctx_.trackTarget = target->currentData().toULongLong(); });
        s->addPlainRow(tr("Apply to"), target);
        s->body()->addWidget(hint(tr("Select a video, then drag a box around a detail with contrast (or click it): it is followed frame by frame from the playhead.")));
        col->addWidget(s);
        break;
    }
    default: col->addWidget(hint(tr("This tool has no options."))); break;
    }
    col->addStretch();
    setWidget(content);
}

} // namespace bemotion
