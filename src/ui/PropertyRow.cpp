#include "ui/PropertyRow.h"

#include "app/Settings.h"

#include "core/Expressions.h"
#include "core/Ops.h"
#include "ui/ExpressionEditor.h"
#include "ui/ExpressionLibrary.h"
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
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cfloat>
#include <cmath>
#include <set>
#include <tuple>

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
    header_->setText(trLabel(title).replace("&", "&&")); // an & is not a shortcut here ("Sorting & Highlight")
    header_->setCheckable(true);
    header_->setChecked(!collapsedSections().value(title, false));
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setProperty("sectionHeader", true); // styled by the theme (ui/Theme.cpp), so it follows theme changes
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
    auto* l = new QLabel(trLabel(label));
    ui::setTone(l, ui::Tone::TextDim);
    g->addWidget(l, gridRows_, 1);
    g->addWidget(editor, gridRows_, 2);
    ++gridRows_;
}

// --- PropertyRow -------------------------------------------------------------------------------

namespace {
// The expression editors left open: the panel rebuilds its rows now and then, and they stay so.
std::set<std::tuple<uint64_t, std::string, int>>& openEditors() {
    static std::set<std::tuple<uint64_t, std::string, int>> open;
    return open;
}
} // namespace

PropertyRow::PropertyRow(EditorContext& ctx, uint64_t layerId, const PropertyRef& prop, CollapsibleSection* section)
    : QObject(section), ctx_(ctx), layerId_(layerId), id_(prop.id), effectIndex_(prop.effectIndex), kind_(prop.kind) {
    QGridLayout* g = section->grid();
    const int row = section->gridRows_++;
    QWidget* values = buildEditors(prop);
    auto* keys = new QWidget; // the diamond and fx, side by side
    auto* keyRow = new QHBoxLayout(keys);
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->setSpacing(0);
    keyRow->addWidget(key_);
    keyRow->addWidget(fx_);
    g->addWidget(keys, row, 0);
    g->addWidget(label_, row, 1);
    g->addWidget(values, row, 2);
    g->addWidget(reset_, row, 3);
    // The expression line, under the row: shown while there is one (or it is being written).
    exprBox_ = new QWidget;
    auto* col = new QVBoxLayout(exprBox_);
    col->setContentsMargins(0, 0, 0, 4);
    col->setSpacing(2);
    expr_ = new ui::ExpressionEditor(ctx_, layerId, prop.kind == PropertyKind::Vec2);
    expr_->setProperty(prop.id, prop.kind == PropertyKind::Color);
    connect(expr_, &ui::ExpressionEditor::edited, this, [this](const QString& t) { applyExpression(t); });
    connect(expr_, &ui::ExpressionEditor::closeRequested, this, [this] { closeExpression(); });
    connect(expr_, &ui::ExpressionEditor::removeRequested, this, [this] { removeExpression(); });
    col->addWidget(expr_);
    const int exprRow = section->gridRows_++;
    g->addWidget(exprBox_, exprRow, 1, 1, 3);
    // Closed at first: "= Label" and a lit fx say there is one.
    exprOpen_ = openEditors().count({layerId_, id_, effectIndex_}) > 0;
    exprBox_->setVisible(exprOpen_);
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
    row->addWidget(fx_);
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
                removeExpression();
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

    fx_ = new ui::IconButton(ui::Icon::Expression, tr("Expression: a formula worked out every frame"), 18);
    fx_->setFixedSize(18, 24);
    connect(fx_, &QAbstractButton::clicked, this, [this] {
        auto p = this->prop();
        if (!p) return;
        if (exprOpen_) closeExpression();
        else openExpression(QString::fromStdString(p->anim->expression));
    });

    labelText_ = trLabel(QString::fromUtf8(prop.label));
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

    reset_ = new ui::IconButton(ui::Icon::Reset, tr("Reset to the original value"), 24);
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
    key_->setToolTip(!inClip && keyHere < 0 ? tr("The playhead is outside this clip\nAlt+click: expression")
                                            : tr("Keyframe at the playhead\nAlt+click: expression"));
    // Driven by an expression: "= Label" in the accent, the expression's value shown.
    const bool driven = anim.hasExpression();
    fx_->setActive(driven);
    fx_->setVisible(driven || settings().showExpressionButtons); // off (Friendly, or by choice): formulas stay out of the way
    fx_->setToolTip(driven ? QString(exprOpen_ ? tr("Close the expression") : tr("Show the expression")) + "\n" + QString::fromStdString(anim.expression)
                           : tr("Add an expression: a formula worked out every frame"));
    label_->setText(driven ? "= " + labelText_ : labelText_);
    label_->setToolTip(driven ? tr("Expression: %1").arg(QString::fromStdString(anim.expression)) : QString());
    label_->setStyleSheet(QString("color: %1;").arg(theme::q(driven ? theme::kAccent : anim.isAnimated() ? theme::kText : theme::kTextDim).name()));
    if (exprBox_) {
        exprBox_->setVisible(exprOpen_);
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
    menu.addAction(keyHere >= 0 ? tr("Remove Keyframe") : tr("Add Keyframe"), [&] {
        if (keyHere >= 0) anim.removeKey(keyHere);
        else anim.addKeyAt(t);
        ctx_.touch();
    });
    menu.addAction(tr("Previous Keyframe"), [&] { jumpToKey(ctx_, anim, -1); })->setEnabled(anim.isAnimated());
    menu.addAction(tr("Next Keyframe"), [&] { jumpToKey(ctx_, anim, +1); })->setEnabled(anim.isAnimated());
    menu.addSeparator();
    // Expressions: a formula instead of (or on top of) keyframes.
    if (anim.hasExpression()) {
        menu.addAction(tr("Edit Expression"), [this] { openExpression(QString::fromStdString(prop()->anim->expression)); });
        menu.addAction(tr("Remove Expression"), [this] { removeExpression(); });
        menu.addAction(tr("Convert to Keyframes"), [this] { bakeExpression(); })->setToolTip(tr("A keyframe a frame across the clip, then no expression"));
    } else {
        menu.addAction(tr("Add Expression"), [this] { openExpression({}); });
    }
    // Examples: what each does, its code beside it (the same as the ƒ library's first page).
    QMenu* examples = menu.addMenu(tr("Expression Examples"));
    examples->setToolTipsVisible(true);
    for (const ui::ExpressionEntry& ex : ui::expressionExamples(id_, kind_ == PropertyKind::Vec2, kind_ == PropertyKind::Color)) {
        QString code = ex.snippet.isEmpty() ? ex.code : ex.snippet;
        code.remove('|');
        QAction* a = examples->addAction(ex.about + "\t" + ex.code, [this, code] { openExpression(code); });
        a->setToolTip(code);
    }
    menu.addSeparator();
    menu.addAction(tr("Clear Animation"), [&] {
        anim.clearKeys(t);
        ctx_.touch();
    })->setEnabled(anim.isAnimated());
    menu.addAction(tr("Reset and Clear Animation"), [&] {
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
        dialog.setWindowTitle(labelText_ + tr(" Expression"));
        dialog.setMinimumWidth(560);
        auto* col = new QVBoxLayout(&dialog);
        col->setContentsMargins(20, 18, 20, 16);
        col->setSpacing(8);
        // Which property of which layer, and what a formula does here.
        auto* head = new QHBoxLayout;
        auto* icon = new QLabel;
        icon->setPixmap(ui::makeIcon(ui::Icon::Expression, theme::kAccent, 20).pixmap(20, 20));
        head->addWidget(icon);
        auto* title = new QLabel(labelText_);
        QFont bold = title->font();
        bold.setPointSizeF(bold.pointSizeF() * 1.15);
        bold.setWeight(QFont::DemiBold);
        title->setFont(bold);
        head->addWidget(title);
        if (const Layer* layer = ctx_.comp().findLayer(layerId_)) {
            auto* where = new QLabel(QStringLiteral("·  ") + QString::fromStdString(layer->name));
            ui::setTone(where, ui::Tone::TextDim);
            head->addWidget(where);
        }
        head->addStretch();
        col->addLayout(head);
        auto* about = new QLabel(tr("A formula worked out every frame. value is what the keyframes give; time, the seconds."));
        about->setWordWrap(true);
        ui::setTone(about, ui::Tone::TextFaint);
        col->addWidget(about);
        auto* editor = new ui::ExpressionEditor(ctx_, layerId_, kind_ == PropertyKind::Vec2);
        editor->setProperty(id_, kind_ == PropertyKind::Color);
        editor->setRowButtons(false);
        editor->setText(text);
        auto check = [editor](const QString& t) {
            const std::string e = t.trimmed().toStdString();
            editor->setProblem(e.empty() ? QString() : QString::fromStdString(checkExpression(e)));
        };
        connect(editor, &ui::ExpressionEditor::edited, &dialog, check);
        col->addWidget(editor);
        // A start: the property's first examples, one click each.
        auto* quick = new QHBoxLayout;
        quick->setSpacing(6);
        auto* tryLabel = new QLabel(tr("Try"));
        ui::setTone(tryLabel, ui::Tone::TextFaint);
        quick->addWidget(tryLabel);
        int shown = 0;
        for (const ui::ExpressionEntry& ex : ui::expressionExamples(id_, kind_ == PropertyKind::Vec2, kind_ == PropertyKind::Color)) {
            if (ex.header || shown >= 3) continue;
            QString code = ex.snippet.isEmpty() ? ex.code : ex.snippet;
            code.remove('|');
            auto* chip = new QPushButton(ex.code);
            chip->setProperty("flat", true);
            chip->setToolTip(ex.about);
            chip->setCursor(Qt::PointingHandCursor);
            connect(chip, &QPushButton::clicked, &dialog, [editor, code, check] {
                editor->setText(code);
                check(code);
                editor->focusText();
            });
            quick->addWidget(chip);
            ++shown;
        }
        quick->addStretch();
        col->addLayout(quick);
        auto* line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        ui::setTone(line, ui::Tone::Border);
        col->addWidget(line);
        auto* buttons = new QHBoxLayout;
        const bool had = prop() && prop()->anim->hasExpression();
        if (had) { // off again: the keyframes (or the value) as before
            auto* remove = new QPushButton(tr("Remove"));
            remove->setToolTip(tr("No expression: the keyframes (or the value) again"));
            connect(remove, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(2); });
            buttons->addWidget(remove);
        }
        buttons->addStretch();
        auto* cancel = new QPushButton(tr("Cancel"));
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        auto* apply = new QPushButton(tr("Apply"));
        apply->setProperty("accent", true);
        apply->setDefault(true);
        connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
        buttons->addWidget(cancel);
        buttons->addWidget(apply);
        col->addLayout(buttons);
        editor->focusText();
        const int result = dialog.exec();
        if (result == QDialog::Accepted) applyExpression(editor->text());
        else if (result == 2) removeExpression();
        refresh();
        return;
    }
    exprOpen_ = true;
    openEditors().insert({layerId_, id_, effectIndex_});
    exprBox_->setVisible(true);
    expr_->setText(text);
    expr_->focusText();
    if (!text.isEmpty()) applyExpression(text);
    refresh();
}

void PropertyRow::closeExpression() {
    exprOpen_ = false;
    openEditors().erase({layerId_, id_, effectIndex_});
    if (expr_) {
        // What did not read is dropped: the editor shows the applied expression next time.
        auto p = prop();
        expr_->setText(p ? QString::fromStdString(p->anim->expression) : QString());
        expr_->clearFocus();
    }
    refresh();
}

void PropertyRow::removeExpression() {
    exprOpen_ = false;
    openEditors().erase({layerId_, id_, effectIndex_});
    applyExpression({});
    if (expr_) expr_->setText({});
    refresh();
}

void PropertyRow::bakeExpression() {
    auto p = prop();
    if (!p) return;
    const nlohmann::json op = {{"op", "expression.bake"}, {"layer", layerId_}, {"prop", p->id}};
    if (const OpsResult r = applyOps(ctx_.project, op); !r.ok) {
        QMessageBox::warning(label_->window(), tr("Convert to Keyframes"), QString::fromStdString(r.error.substr(r.error.find(": ") + 2)));
        return;
    }
    exprOpen_ = false;
    openEditors().erase({layerId_, id_, effectIndex_});
    if (expr_) expr_->setText({});
    ctx_.touch(); // one undo step
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
