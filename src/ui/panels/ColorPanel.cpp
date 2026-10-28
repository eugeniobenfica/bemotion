#include "ui/panels/ColorPanel.h"

#include "app/Platform.h"
#include "render/SceneRenderer.h"
#include "ui/CurvesEditor.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/Language.h"
#include "ui/PropertyRow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// A grading slider: its track shows what it does (dark to light, gray to color, the hues), a
// mark where it changes nothing, the value on the right. Click or drag anywhere on it; a
// double-click goes back to that mark. The wheel scrolls the panel.
class ToneSlider : public QWidget {
public:
    ToneSlider(float min, float max, float neutral, std::vector<QColor> stops, const char* format)
        : min_(min), max_(max), neutral_(neutral), stops_(std::move(stops)), format_(format) {
        setMinimumWidth(100);
        setFixedHeight(26);
        setCursor(Qt::PointingHandCursor);
        value_ = neutral;
    }
    std::function<void(float)> changed;
    std::function<void()> started, finished;
    void setValue(float v) {
        if (dragging_) return;
        value_ = std::clamp(v, min_, max_);
        update();
    }

protected:
    QRectF track() const { return QRectF(8, height() * 0.5 - 3, std::max(10, width() - 64), 6); }
    float xOf(float v) const {
        const QRectF t = track();
        return static_cast<float>(t.left() + (v - min_) / (max_ - min_) * t.width());
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF t = track();
        QLinearGradient g(t.topLeft(), t.topRight());
        for (size_t i = 0; i < stops_.size(); ++i) g.setColorAt(stops_.size() > 1 ? static_cast<double>(i) / (stops_.size() - 1) : 0.0, stops_[i]);
        QPainterPath bar;
        bar.addRoundedRect(t, 3, 3);
        p.fillPath(bar, g);
        p.setPen(QPen(theme::q(theme::kFieldBorder), 1));
        p.drawPath(bar);
        // Where it changes nothing.
        const float n = xOf(neutral_);
        p.setPen(QPen(theme::q(theme::kTextDim), 1.5));
        p.drawLine(QPointF(n, t.top() - 4), QPointF(n, t.top() - 1));
        p.drawLine(QPointF(n, t.bottom() + 1), QPointF(n, t.bottom() + 4));
        // The knob.
        const float x = xOf(value_);
        const bool hot = underMouse() || dragging_;
        p.setPen(QPen(theme::q(theme::kBgDeep), 2));
        p.setBrush(theme::q(hot ? theme::kAccent : theme::kText));
        p.drawEllipse(QPointF(x, t.center().y()), hot ? 7.5 : 6.5, hot ? 7.5 : 6.5);
        char buf[32];
        std::snprintf(buf, sizeof buf, format_, value_);
        p.setPen(theme::q(std::fabs(value_ - neutral_) > 1e-3f ? theme::kText : theme::kTextDim));
        p.drawText(QRectF(width() - 52, 0, 48, height()), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8(buf));
    }
    void setFromX(float x) {
        const QRectF t = track();
        float v = min_ + static_cast<float>((x - t.left()) / t.width()) * (max_ - min_);
        v = std::round(std::clamp(v, min_, max_));
        if (std::fabs(v - neutral_) < (max_ - min_) * 0.01f) v = neutral_; // snaps to "nothing" near it
        if (v == value_) return;
        value_ = v;
        update();
        if (changed) changed(v);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        dragging_ = true;
        if (started) started();
        setFromX(static_cast<float>(e->position().x()));
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) setFromX(static_cast<float>(e->position().x()));
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (!dragging_) return;
        dragging_ = false;
        update();
        if (finished) finished();
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        value_ = neutral_;
        update();
        if (changed) changed(neutral_);
        if (finished) finished();
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }

private:
    float min_, max_, neutral_, value_;
    std::vector<QColor> stops_;
    const char* format_;
    bool dragging_ = false;
};

// A look to pick: a swatch of what it does over its name; the chosen one in the accent.
class LookTile : public QAbstractButton {
public:
    LookTile(const QImage& swatch, const QString& name, ui::Icon icon = ui::Icon::FxColor, bool useIcon = false)
        : swatch_(swatch), icon_(icon), useIcon_(useIcon) {
        setText(name);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(name);
        setMinimumSize(76, 70);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override { return {92, 74}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
        QPainterPath card;
        card.addRoundedRect(box, 6, 6);
        p.fillPath(card, theme::q(underMouse() ? theme::kBgHover : theme::kField));
        const QRectF picture(box.left() + 5, box.top() + 5, box.width() - 10, box.height() - 26);
        QPainterPath frame;
        frame.addRoundedRect(picture, 4, 4);
        if (useIcon_) {
            ui::Draw d(p);
            ui::drawIcon(d, icon_, {static_cast<float>(picture.center().x()), static_cast<float>(picture.center().y())}, 20.0f, theme::kTextDim);
        } else if (!swatch_.isNull()) {
            p.save();
            p.setClipPath(frame);
            p.drawImage(picture, swatch_);
            p.restore();
        }
        p.setPen(theme::q(isChecked() ? theme::kText : theme::kTextDim));
        p.drawText(QRectF(box.left() + 4, picture.bottom() + 2, box.width() - 8, 20), Qt::AlignCenter,
                   fontMetrics().elidedText(text(), Qt::ElideRight, static_cast<int>(box.width() - 8)));
        p.setPen(QPen(theme::q(isChecked() ? theme::kAccent : theme::kBorder), isChecked() ? 2.0 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(card);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    QImage swatch_;
    ui::Icon icon_;
    bool useIcon_;
};

std::vector<QColor> gray(float from, float to) { return {QColor::fromRgbF(from, from, from), QColor::fromRgbF(to, to, to)}; }

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
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); // only when narrower than its narrowest (rows wrap and shorten first)
    rebuild();
}

std::string ColorPanel::structureKey() {
    std::ostringstream k;
    const Layer* l = ctx_.selected();
    if (!l) return "none";
    k << l->id << ':' << static_cast<int>(l->type) << ':' << hasPicture(*l) << ':' << l->name;
    for (const Effect& e : l->effects) {
        k << '|' << static_cast<int>(e.type) << (e.type == EffectType::Lut ? e.file : "");
        // Animated (or driven) parameters show as property rows: a key added changes the rows.
        for (const EffectParam& ep : effectParams(e.type)) k << (const_cast<Effect&>(e).slot(ep.slot).isAnimated() || const_cast<Effect&>(e).slot(ep.slot).hasExpression());
    }
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

QImage ColorPanel::swatch(const std::string& lut) {
    if (const auto it = swatches_.find(lut); it != swatches_.end()) return it->second;
    std::vector<uint8_t> rgba;
    QImage image;
    // Made at three times the card's size: drawn smaller it stays sharp on any screen scale
    // (made at the card's size, a 150% screen stretched it blurry).
    constexpr int kW = 288, kH = 132;
    if (render::lutSwatch(lut, kW, kH, rgba)) image = QImage(rgba.data(), kW, kH, QImage::Format_RGBA8888).copy();
    swatches_[lut] = image;
    return image;
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
    col->setSpacing(6);
    Layer* layer = ctx_.selected();
    if (!layer || !hasPicture(*layer)) {
        col->addWidget(new QLabel(tr("Color")));
        col->addWidget(faint(!layer ? tr("Select a layer to grade it: brightness, contrast, saturation, a look and curves.")
                                    : tr("This layer has no picture to grade.")));
    } else {
        const uint64_t id = layer->id;
        auto* head = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(ui::Icon::FxColor, theme::kAccent, 20).pixmap(20, 20));
        head->addWidget(icon);
        head->addWidget(new ui::ElidedLabel(tr("Color  ·  %1").arg(QString::fromStdString(layer->name))), 1); // a long name shortens, never widens the panel
        bool graded = false;
        for (EffectType type : kParts) graded = graded || firstOf(*layer, type) >= 0;
        if (graded) { // all of it back to the picture as it was
            auto* reset = new ui::IconButton(ui::Icon::Reset, tr("Reset all the grading of this layer"), 24);
            connect(reset, &QAbstractButton::clicked, this, [this, id] {
                if (Layer* l = ctx_.comp().findLayer(id)) {
                    std::erase_if(l->effects, [](const Effect& e) { return std::find(std::begin(kParts), std::end(kParts), e.type) != std::end(kParts); });
                    ctx_.clearKeySelection();
                    ctx_.touch();
                }
            });
            head->addWidget(reset);
        }
        col->addLayout(head);
        buildBasic(col, *layer);
        buildLook(col, *layer);
        buildCurves(col, *layer);
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

QWidget* ColorPanel::partTools(uint64_t layerId, const std::vector<EffectType>& types) {
    bool any = false;
    for (EffectType t : types) any = any || effectOf(layerId, t, false);
    if (!any) return nullptr;
    auto* extra = new QWidget;
    auto* tools = new QHBoxLayout(extra);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(2);
    auto* on = new ui::Switch;
    connect(on, &QAbstractButton::toggled, this, [this, layerId, types](bool v) {
        bool changed = false;
        for (EffectType t : types)
            if (Effect* e = effectOf(layerId, t, false); e && e->enabled != v) e->enabled = v, changed = true;
        if (changed) ctx_.touch();
    });
    refreshers_.push_back([this, layerId, types, on] {
        bool enabled = false;
        for (EffectType t : types)
            if (const Effect* e = effectOf(layerId, t, false)) enabled = enabled || e->enabled;
        const QSignalBlocker b(on);
        on->setChecked(enabled);
        on->setToolTip(enabled ? tr("Turn this off") : tr("Turn this on"));
    });
    auto* reset = new ui::IconButton(ui::Icon::Reset, tr("Reset (removes the effect)"), 24);
    connect(reset, &QAbstractButton::clicked, this, [this, layerId, types] {
        if (Layer* l = ctx_.comp().findLayer(layerId)) {
            std::erase_if(l->effects, [&](const Effect& e) { return std::find(types.begin(), types.end(), e.type) != types.end(); });
            ctx_.clearKeySelection();
            ctx_.touch();
        }
    });
    tools->addWidget(on);
    tools->addWidget(reset);
    return extra;
}

void ColorPanel::buildBasic(QVBoxLayout* col, Layer& layer) {
    const uint64_t id = layer.id;
    auto* section = new CollapsibleSection(tr("Basic Correction"), nullptr, partTools(id, {EffectType::BrightnessContrast, EffectType::HueSaturation}));
    struct Control {
        EffectType type;
        EffectSlot slot;
        const char* label;
        float min, max;
        std::vector<QColor> stops;
        const char* format;
    };
    const std::vector<QColor> hues = {QColor(255, 80, 80), QColor(255, 220, 70), QColor(90, 220, 90), QColor(70, 210, 230),
                                      QColor(90, 110, 255), QColor(220, 90, 230), QColor(255, 80, 80)};
    const Control controls[] = {
        {EffectType::BrightnessContrast, EffectSlot::Value1, TR_NOOP("Brightness"), -150, 150, gray(0.05f, 0.95f), "%.0f"},
        {EffectType::BrightnessContrast, EffectSlot::Value2, TR_NOOP("Contrast"), -100, 100, {QColor(110, 110, 110), QColor(140, 140, 140), QColor(20, 20, 20), QColor(240, 240, 240)}, "%.0f"},
        {EffectType::HueSaturation, EffectSlot::Value2, TR_NOOP("Saturation"), -100, 100, {QColor(128, 128, 128), QColor(240, 120, 40)}, "%.0f"},
        {EffectType::HueSaturation, EffectSlot::Value1, TR_NOOP("Hue"), -180, 180, hues, "%.0f°"},
        {EffectType::HueSaturation, EffectSlot::Value3, TR_NOOP("Lightness"), -100, 100, gray(0.0f, 1.0f), "%.0f"},
    };
    const auto props = layer.properties();
    for (const Control& c : controls) {
        // Animated (or driven by an expression): its keyframe row, as in the Effects tab.
        if (Effect* e = effectOf(id, c.type, false); e && (e->slot(c.slot).isAnimated() || e->slot(c.slot).hasExpression())) {
            const int index = firstOf(layer, c.type);
            for (const PropertyRef& p : props)
                if (p.effectIndex == index && p.anim == &e->slot(c.slot)) rows_.push_back(new PropertyRow(ctx_, id, p, section));
            continue;
        }
        auto* slider = new ToneSlider(c.min, c.max, 0.0f, c.stops, c.format);
        const EffectType type = c.type;
        const EffectSlot slot = c.slot;
        slider->started = [this] { editing_ = true; };
        slider->finished = [this] { editing_ = false; };
        slider->changed = [this, id, type, slot](float v) {
            if (Effect* e = effectOf(id, type, true)) {
                static_cast<AnimFloat&>(e->slot(slot)).set(ctx_.time, v);
                ctx_.touch();
            }
        };
        refreshers_.push_back([this, id, type, slot, slider] {
            const Effect* e = effectOf(id, type, false);
            slider->setValue(e ? static_cast<const AnimFloat&>(const_cast<Effect*>(e)->slot(slot)).valueAt(ctx_.time) : 0.0f);
        });
        section->body()->addWidget(new ui::LabeledRow(tr(c.label), slider)); // the label above it when narrow
    }
    col->addWidget(section);
}

void ColorPanel::buildLook(QVBoxLayout* col, Layer& layer) {
    const uint64_t id = layer.id;
    const int index = firstOf(layer, EffectType::Lut);
    const std::string current = index >= 0 ? layer.effects[index].file : std::string();
    auto* section = new CollapsibleSection(tr("Look"), nullptr, partTools(id, {EffectType::Lut}));
    // Cards: none, the looks that come with bemotion, the layer's own file, another file.
    auto* grid = new ui::ResponsiveGrid(84); // as many columns as fit
    auto pick = [this, id](const std::string& file) {
        Effect* e = effectOf(id, EffectType::Lut, !file.empty());
        if (e && e->file != file) {
            e->file = file;
            e->relative.clear();
            ctx_.touch();
        }
    };
    int n = 0;
    auto addTile = [&](LookTile* tile, std::function<void()> clicked, bool chosen) {
        tile->setChecked(chosen);
        connect(tile, &QAbstractButton::clicked, this, [tile, chosen, clicked] {
            tile->setChecked(chosen); // the document decides (redrawn after the change)
            clicked();
        });
        grid->addWidget(tile);
        ++n;
    };
    addTile(new LookTile(swatch(""), tr("None")), [pick] { pick(""); }, current.empty());
    for (const std::string& name : render::bundledLuts()) {
        const std::string file = "bundled:" + name;
        addTile(new LookTile(swatch(file), QString::fromStdString(name.substr(0, name.size() - 5))), [pick, file] { pick(file); }, current == file);
    }
    if (!current.empty() && current.rfind("bundled:", 0) != 0)
        addTile(new LookTile(swatch(current), QString::fromStdString(platform::toUtf8(platform::fromUtf8(current).stem()))), [] {}, true);
    auto* other = new LookTile({}, tr("Choose File..."), ui::Icon::Folder, true);
    other->setToolTip(tr("A .cube LUT of your own (Adobe / Resolve)"));
    addTile(other, [this, pick] {
        const auto chosen = platform::openFileDialog(TR_NOOP("Choose a LUT"), TR_NOOP("Cube LUT"), ".cube");
        if (chosen) pick(platform::toUtf8(*chosen));
    }, false);
    section->body()->addWidget(grid);
    if (index >= 0) {
        std::string title, error;
        if (!current.empty() && !render::lutInfo(current, title, error)) {
            auto* problem = new QLabel(QString::fromStdString(error));
            problem->setWordWrap(true);
            ui::setTone(problem, ui::Tone::Danger);
            section->body()->addWidget(problem);
        }
        for (const PropertyRef& p : layer.properties())
            if (p.effectIndex == index) rows_.push_back(new PropertyRow(ctx_, id, p, section)); // Amount
    }
    col->addWidget(section);
}

void ColorPanel::buildCurves(QVBoxLayout* col, Layer& layer) {
    const uint64_t id = layer.id;
    const int index = firstOf(layer, EffectType::Curves);
    auto* section = new CollapsibleSection(tr("Curves"), nullptr, partTools(id, {EffectType::Curves}));
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
    if (index >= 0)
        for (const PropertyRef& p : layer.properties())
            if (p.effectIndex == index) rows_.push_back(new PropertyRow(ctx_, id, p, section)); // Amount
    col->addWidget(section);
}

} // namespace bemotion
