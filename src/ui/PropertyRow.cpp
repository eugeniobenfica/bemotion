#include "ui/PropertyRow.h"

#include "core/Expressions.h"
#include "ui/ExpressionEditor.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <cfloat>
#include <cmath>

namespace bemotion {

namespace {

QMap<QString, bool>& collapsedSections() {
    static QMap<QString, bool> m;
    return m;
}

void jumpToKey(EditorContext& ctx, const AnimatedBase& anim, int direction) {
    double best = direction > 0 ? DBL_MAX : -DBL_MAX;
    for (int i = 0; i < anim.keyCount(); ++i) {
        const double kt = anim.keyTime(i);
        if (direction > 0 && kt > ctx.time + kTimeEpsilon) best = std::min(best, kt);
        if (direction < 0 && kt < ctx.time - kTimeEpsilon) best = std::max(best, kt);
    }
    if (best != DBL_MAX && best != -DBL_MAX) ctx.setTime(best);
}

QString hexOf(Color c) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return QString("#%1%2%3").arg(byte(c.r), 2, 16, QChar('0')).arg(byte(c.g), 2, 16, QChar('0')).arg(byte(c.b), 2, 16, QChar('0')).toUpper();
}

} // namespace

std::optional<PropertyRef> findProperty(EditorContext& ctx, uint64_t layerId, const std::string& id, int effectIndex) {
    Layer* layer = ctx.comp().findLayer(layerId);
    if (!layer) return std::nullopt;
    for (const PropertyRef& p : layer->properties())
        if (p.effectIndex == effectIndex && id == p.id) return p;
    return std::nullopt;
}

// --- CollapsibleSection -------------------------------------------------------------------

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent, QWidget* headerExtra) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 2, 0, 2);
    col->setSpacing(0);
    auto* headRow = new QHBoxLayout;
    headRow->setContentsMargins(0, 0, 0, 0);
    header_ = new QToolButton;
    header_->setText(QString(title).replace("&", "&&")); // an & is not a shortcut here ("Sorting & Highlight")
    header_->setCheckable(true);
    header_->setChecked(!collapsedSections().value(title, false));
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setStyleSheet(QString("QToolButton { text-align: left; font-weight: 600; padding: 5px 6px; background: %1; border-radius: 5px; }"
                                   "QToolButton:hover { background: %2; } QToolButton:checked { background: %1; }")
                               .arg(theme::q(theme::kBgRaised).name(), theme::q(theme::kBgHover).name()));
    auto setArrow = [this] {
        header_->setIcon(ui::makeIcon(header_->isChecked() ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, theme::kTextDim, 14));
    };
    setArrow();
    headRow->addWidget(header_);
    if (headerExtra) headRow->addWidget(headerExtra);
    col->addLayout(headRow);
    content_ = new QWidget;
    body_ = new QVBoxLayout(content_);
    body_->setContentsMargins(4, 6, 2, 8);
    body_->setSpacing(4);
    col->addWidget(content_); // parented first: shown on its own, a widget would flash up as a window
    content_->setVisible(header_->isChecked());
    connect(header_, &QToolButton::toggled, this, [this, title, setArrow](bool open) {
        content_->setVisible(open);
        collapsedSections()[title] = !open;
        setArrow();
    });
}

QGridLayout* CollapsibleSection::grid() {
    if (!grid_) {
        grid_ = new QGridLayout;
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setHorizontalSpacing(6);
        grid_->setVerticalSpacing(4);
        grid_->setColumnMinimumWidth(0, 20);
        grid_->setColumnMinimumWidth(1, 88);
        grid_->setColumnStretch(2, 1);
        grid_->setColumnMinimumWidth(3, 24);
        body_->addLayout(grid_);
    }
    return grid_;
}

void CollapsibleSection::addPlainRow(const QString& label, QWidget* editor) {
    QGridLayout* g = grid();
    auto* l = new QLabel(label);
    ui::setTone(l, ui::Tone::TextDim);
    g->addWidget(l, gridRows_, 1);
    g->addWidget(editor, gridRows_, 2);
    ++gridRows_;
}

// --- PropertyRow -------------------------------------------------------------------------------

PropertyRow::PropertyRow(EditorContext& ctx, uint64_t layerId, const PropertyRef& prop, CollapsibleSection* section)
    : QObject(section), ctx_(ctx), layerId_(layerId), id_(prop.id), effectIndex_(prop.effectIndex), kind_(prop.kind) {
    QGridLayout* g = section->grid();
    const int row = section->gridRows_++;
    QWidget* values = buildEditors(prop);
    g->addWidget(key_, row, 0);
    g->addWidget(label_, row, 1);
    g->addWidget(values, row, 2);
    g->addWidget(reset_, row, 3);
    // The expression line, under the row: shown while there is one (or it is being written).
    exprBox_ = new QWidget;
    auto* col = new QVBoxLayout(exprBox_);
    col->setContentsMargins(0, 0, 0, 4);
    col->setSpacing(2);
    expr_ = new ui::ExpressionEditor(ctx_, layerId, prop.kind == PropertyKind::Vec2);
    connect(expr_, &ui::ExpressionEditor::edited, this, [this](const QString& t) { applyExpression(t); });
    col->addWidget(expr_);
    const int exprRow = section->gridRows_++;
    g->addWidget(exprBox_, exprRow, 1, 1, 3);
    exprBox_->setVisible(prop.anim->hasExpression());
    refresh();
}

PropertyRow::PropertyRow(EditorContext& ctx, uint64_t layerId, const PropertyRef& prop, QWidget* host)
    : QObject(host), ctx_(ctx), layerId_(layerId), id_(prop.id), effectIndex_(prop.effectIndex), kind_(prop.kind) {
    container_ = new QWidget(host);
    auto* row = new QHBoxLayout(container_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    QWidget* values = buildEditors(prop);
    values->setFixedWidth(kind_ == PropertyKind::Vec2 ? 132 : kind_ == PropertyKind::Color ? 112 : 72);
    row->addWidget(key_);
    row->addWidget(label_, 1);
    row->addWidget(values);
    row->addWidget(reset_);
    refresh();
}

PropertyRow::~PropertyRow() { delete container_; }

QWidget* PropertyRow::buildEditors(const PropertyRef& prop) {
    key_ = new ui::KeyframeButton;
    connect(key_, &QAbstractButton::clicked, this, [this] {
        auto p = this->prop();
        Layer* layer = ctx_.comp().findLayer(layerId_);
        if (!p || !layer) return;
        AnimatedBase& anim = *p->anim;
        // Alt+click, as in After Effects: an expression (or none any more).
        if (QApplication::keyboardModifiers() & Qt::AltModifier) {
            if (anim.hasExpression()) {
                exprOpen_ = false;
                applyExpression({});
                if (expr_) expr_->setText({});
            } else {
                openExpression({});
            }
            return;
        }
        const int keyHere = anim.findKey(ctx_.time);
        // Keyframes only make sense where the clip exists.
        const bool inClip = ctx_.time >= layer->inPoint - kTimeEpsilon && ctx_.time <= layer->outPoint + kTimeEpsilon;
        if (keyHere >= 0) anim.removeKey(keyHere);
        else if (inClip) anim.addKeyAt(ctx_.time);
        ctx_.touch();
        refresh();
    });

    labelText_ = QString::fromUtf8(prop.label);
    label_ = new QLabel(labelText_);
    label_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(label_, &QWidget::customContextMenuRequested, this, [this](QPoint p) { showMenu(label_->mapToGlobal(p)); });

    auto* valueBox = new QWidget;
    auto* values = new QHBoxLayout(valueBox);
    values->setContentsMargins(0, 0, 0, 0);
    values->setSpacing(4);
    valueBox->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(valueBox, &QWidget::customContextMenuRequested, this, [this, valueBox](QPoint p) { showMenu(valueBox->mapToGlobal(p)); });
    auto makeDrag = [&](int component) {
        auto* d = new ui::DragValue;
        d->setSpeed(prop.speed);
        d->setRange(prop.min, prop.max);
        d->setFormat(prop.format);
        connect(d, &ui::DragValue::valueChanged, this, [this, component](float v) {
            auto p = this->prop();
            if (!p) return;
            std::vector<float> cur = propertyValue(*p, ctx_.time);
            if (component < static_cast<int>(cur.size())) cur[component] = v;
            setValue(cur);
        });
        values->addWidget(d, 1);
        return d;
    };
    switch (kind_) {
    case PropertyKind::Float: x_ = makeDrag(0); break;
    case PropertyKind::Vec2:
        x_ = makeDrag(0);
        y_ = makeDrag(1);
        break;
    case PropertyKind::Color:
        color_ = new ui::ColorButton;
        connect(color_, &ui::ColorButton::colorChanged, this, [this](Color c) { setValue({c.r, c.g, c.b, c.a}); });
        values->addWidget(color_);
        hex_ = new QLabel;
        ui::setTone(hex_, ui::Tone::TextDim);
        values->addWidget(hex_, 1);
        break;
    }

    reset_ = new ui::IconButton(ui::Icon::Reset, "Reset to the original value", 24);
    connect(reset_, &QAbstractButton::clicked, this, [this] {
        auto p = this->prop();
        Layer* layer = ctx_.comp().findLayer(layerId_);
        if (!p || !layer) return;
        const std::vector<float> def = defaultPropertyValue(*layer, p->id, ctx_.comp().center());
        if (!def.empty()) setValue(def);
    });
    return valueBox;
}

std::optional<PropertyRef> PropertyRow::prop() { return findProperty(ctx_, layerId_, id_, effectIndex_); }

void PropertyRow::setValue(const std::vector<float>& v) {
    auto p = prop();
    if (!p) return;
    setPropertyValue(*p, ctx_.time, v);
    ctx_.touch();
}

void PropertyRow::refresh() {
    auto p = prop();
    Layer* layer = ctx_.comp().findLayer(layerId_);
    if (!p || !layer) return;
    const double t = ctx_.time;
    const AnimatedBase& anim = *p->anim;
    const int keyHere = anim.findKey(t);
    key_->setState(keyHere >= 0, anim.isAnimated());
    const bool inClip = t >= layer->inPoint - kTimeEpsilon && t <= layer->outPoint + kTimeEpsilon;
    key_->setEnabled(inClip || keyHere >= 0);
    key_->setToolTip(!inClip && keyHere < 0 ? "The playhead is outside this clip\nAlt+click: expression"
                                            : "Keyframe at the playhead\nAlt+click: expression");
    // Driven by an expression: "= Label" in the accent, the expression's value shown.
    const bool driven = anim.hasExpression();
    label_->setText(driven ? "= " + labelText_ : labelText_);
    label_->setToolTip(driven ? QString::fromStdString("Expression: " + anim.expression) : QString());
    label_->setStyleSheet(QString("color: %1;").arg(theme::q(driven ? theme::kAccent : anim.isAnimated() ? theme::kText : theme::kTextDim).name()));
    if (exprBox_) {
        exprBox_->setVisible(driven || exprOpen_);
        if (!expr_->editing() && expr_->text().toStdString() != anim.expression && driven)
            expr_->setText(QString::fromStdString(anim.expression));
        // What is being typed first (it is not applied while it does not read), then how the
        // applied one ran.
        const std::string typed = expr_->text().trimmed().toStdString();
        std::string problem = typed.empty() ? std::string() : checkExpression(typed);
        if (problem.empty() && driven) problem = expressionError(anim);
        expr_->setProblem(expr_->text().trimmed().isEmpty() ? QString() : QString::fromStdString(problem));
    }

    const std::vector<float> cur = propertyValue(*p, t);
    if (x_ && !cur.empty()) {
        x_->setValue(cur[0]);
        x_->setHighlighted(anim.isAnimated());
    }
    if (y_ && cur.size() > 1) {
        y_->setValue(cur[1]);
        y_->setHighlighted(anim.isAnimated());
    }
    if (color_ && cur.size() >= 4) {
        const Color c{cur[0], cur[1], cur[2], cur[3]};
        color_->setColor(c);
        hex_->setText(hexOf(c) + (c.a < 1.0f ? QString("  %1%").arg(static_cast<int>(std::lround(c.a * 100))) : QString()));
    }
    const std::vector<float> def = defaultPropertyValue(*layer, p->id, ctx_.comp().center());
    bool differs = false;
    for (size_t i = 0; i < def.size() && i < cur.size(); ++i) differs |= std::fabs(def[i] - cur[i]) > 1e-4f;
    reset_->setEnabled(differs);
}

void PropertyRow::showMenu(QPoint at) {
    auto p = prop();
    Layer* layer = ctx_.comp().findLayer(layerId_);
    if (!p || !layer) return;
    AnimatedBase& anim = *p->anim;
    const double t = ctx_.time;
    const int keyHere = anim.findKey(t);
    QMenu menu;
    menu.addAction(keyHere >= 0 ? "Remove Keyframe" : "Add Keyframe", [&] {
        if (keyHere >= 0) anim.removeKey(keyHere);
        else anim.addKeyAt(t);
        ctx_.touch();
    });
    menu.addAction("Previous Keyframe", [&] { jumpToKey(ctx_, anim, -1); })->setEnabled(anim.isAnimated());
    menu.addAction("Next Keyframe", [&] { jumpToKey(ctx_, anim, +1); })->setEnabled(anim.isAnimated());
    menu.addSeparator();
    // Expressions: a formula instead of (or on top of) keyframes.
    if (anim.hasExpression()) {
        menu.addAction("Edit Expression", [this] { openExpression(QString::fromStdString(prop()->anim->expression)); });
        menu.addAction("Remove Expression", [this] {
            exprOpen_ = false;
            applyExpression({});
            if (expr_) expr_->setText({});
        });
    } else {
        menu.addAction("Add Expression", [this] { openExpression({}); });
    }
    QMenu* examples = menu.addMenu("Expression Examples");
    const bool vec = kind_ == PropertyKind::Vec2, color = kind_ == PropertyKind::Color;
    const std::pair<const char*, QString> samples[] = {
        {"Wiggle (shake gently)", vec ? "wiggle(2, 25)" : color ? "wiggle(1, 0.1)" : "wiggle(2, 10)"},
        {"Keep turning / growing", vec ? "value + [time * 100, 0]" : "value + time * 90"},
        {"Loop the keyframes", "loop()"},
        {"Back and forth", "pingpong()"},
        {"Pulse", vec ? "value * (1 + sin(time * 6) * 0.08)" : "value * (1 + sin(time * 6) * 0.08)"},
        {"Follow another layer", QString("layer(\"Name\").%1").arg(QString::fromStdString(id_))},
        {"Follow it a moment later", QString("layer(\"Name\").%1(time - 0.25)").arg(QString::fromStdString(id_))},
        {"Change over the clip", vec ? "lerp(value, value + [300, 0], smooth(progress))" : "lerp(value, value * 2, smooth(progress))"},
    };
    for (const auto& [name, text] : samples) examples->addAction(name, [this, text = text] { openExpression(text); });
    menu.addSeparator();
    menu.addAction("Clear Animation", [&] {
        anim.clearKeys(t);
        ctx_.touch();
    })->setEnabled(anim.isAnimated());
    menu.addAction("Reset and Clear Animation", [&] {
        const std::vector<float> defaults = defaultPropertyValue(*layer, p->id, ctx_.comp().center());
        if (defaults.empty()) return;
        anim.clearKeys(t);
        setPropertyValue(*p, t, defaults);
        ctx_.touch();
    });
    menu.exec(at);
    refresh();
}

void PropertyRow::openExpression(const QString& text) {
    if (!expr_) { // the timeline's compact rows: the same editor, in a small window
        QDialog dialog(label_->window());
        dialog.setWindowTitle(labelText_ + " Expression");
        dialog.setMinimumWidth(520);
        auto* col = new QVBoxLayout(&dialog);
        col->setContentsMargins(18, 16, 18, 14);
        auto* head = new QLabel(labelText_ + " =");
        ui::setTone(head, ui::Tone::TextDim);
        col->addWidget(head);
        auto* editor = new ui::ExpressionEditor(ctx_, layerId_, kind_ == PropertyKind::Vec2);
        editor->setText(text);
        connect(editor, &ui::ExpressionEditor::edited, &dialog, [editor](const QString& t) {
            const std::string e = t.trimmed().toStdString();
            editor->setProblem(e.empty() ? QString() : QString::fromStdString(checkExpression(e)));
        });
        col->addWidget(editor);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        col->addWidget(buttons);
        editor->focusText();
        if (dialog.exec() == QDialog::Accepted) applyExpression(editor->text());
        return;
    }
    exprOpen_ = true;
    exprBox_->setVisible(true);
    expr_->setText(text);
    expr_->focusText();
    if (!text.isEmpty()) applyExpression(text);
    refresh();
}

void PropertyRow::applyExpression(const QString& text) {
    auto p = prop();
    if (!p) return;
    const std::string e = text.trimmed().toStdString();
    // Only what reads replaces the expression: a half-typed one keeps the last good one.
    if (!e.empty() && !checkExpression(e).empty()) {
        refresh();
        return;
    }
    if (p->anim->expression == e) return;
    p->anim->expression = e;
    ctx_.touch();
    refresh();
}

} // namespace bemotion
