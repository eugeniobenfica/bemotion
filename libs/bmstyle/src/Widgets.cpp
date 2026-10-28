#include "bmstyle/Widgets.h"

#include "bmstyle/BmStyle.h"
#include "bmstyle/Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bmstyle {

namespace {

Rgba toneColor(Tone t) {
    switch (t) {
    case Tone::Text: return theme::kText;
    case Tone::TextDim: return theme::kTextDim;
    case Tone::TextFaint: return theme::kTextFaint;
    case Tone::Accent: return theme::kAccent;
    case Tone::Danger: return theme::kDanger;
    case Tone::Border: return theme::kBorder;
    }
    return theme::kText;
}

void applyTone(QWidget* w) {
    const QVariant v = w->property("bmstyleTone");
    if (v.isValid()) w->setStyleSheet(QString("color: %1;").arg(theme::q(toneColor(static_cast<Tone>(v.toInt()))).name()));
}

} // namespace

void setTone(QWidget* widget, Tone tone) {
    static bool hooked = false;
    if (!hooked) { // one listener recolors every toned widget
        hooked = true;
        QObject::connect(&Appearance::get(), &Appearance::changed, [] {
            for (QWidget* w : QApplication::allWidgets()) applyTone(w);
        });
    }
    widget->setProperty("bmstyleTone", static_cast<int>(tone));
    applyTone(widget);
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(translate(text).toUpper(), parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.8);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    l->setFont(f);
    setTone(l, Tone::TextFaint);
    return l;
}

QPushButton* accentButton(const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setProperty("accent", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// --- IconButton -----------------------------------------------------------------------

IconButton::IconButton(Icon icon, const QString& tip, int size, QWidget* parent) : QAbstractButton(parent), icon_(icon), size_(size) {
    setToolTip(translate(tip));
    setFixedSize(size, size);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void IconButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hovered = underMouse() && isEnabled();
    const bool danger = icon_ == Icon::Trash; // deleting stands out
    if (isDown()) d.rectFilled(Pt(0, 0), Pt(width(), height()), danger ? theme::withAlpha(theme::kDanger, 90) : theme::kBorder, 5.0f);
    else if (hovered) d.rectFilled(Pt(0, 0), Pt(width(), height()), danger ? theme::withAlpha(theme::kDanger, 45) : theme::kBgHover, 5.0f);
    if (active_ && activeColor_) d.rectFilled(Pt(0, 0), Pt(width(), height()), theme::withAlpha(activeColor_, 50), 5.0f);
    Rgba col = active_ ? (activeColor_ ? activeColor_ : theme::kAccent) : (hovered ? (danger ? theme::kDanger : theme::kText) : theme::kTextDim);
    if (!isEnabled()) col = theme::withAlpha(theme::kTextFaint, 140);
    drawIcon(d, icon_, Pt(width() * 0.5f, height() * 0.5f), std::min(width(), height()) * 0.58f, col);
}

// --- Tile ------------------------------------------------------------------------------

Tile::Tile(Icon icon, const QString& label, QWidget* parent) : QAbstractButton(parent), icon_(icon), label_(translate(label)) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void Tile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hovered = underMouse();
    const Pt a(0.5f, 0.5f), b(width() - 0.5f, height() - 0.5f);
    d.rectFilled(a, b, hovered || isDown() ? theme::kBgHover : theme::kBgRaised, 8.0f);
    if (hovered || highlighted_) d.rect(a, b, theme::kAccent, 8.0f, highlighted_ ? 1.5f : 1.0f);
    const float iconSize = std::min(width(), height()) * 0.34f;
    drawIcon(d, icon_, Pt(width() * 0.5f, height() * 0.42f), iconSize, hovered ? theme::kAccent : theme::kText);
    const QString text = p.fontMetrics().elidedText(label_, Qt::ElideRight, width() - 8);
    const float tw = d.textWidth(text);
    d.text(Pt((width() - tw) * 0.5f, height() - d.textHeight() - height() * 0.1f), hovered ? theme::kText : theme::kTextDim, text);
    if (marked_) d.circleFilled(Pt(width() - 9.0f, 9.0f), 3.0f, theme::kAccent);
    if (dimmed_) d.rectFilled(a, b, theme::withAlpha(theme::kBgPanel, 140), 8.0f);
}

void Tile::mousePressEvent(QMouseEvent* e) {
    pressPos_ = e->position().toPoint();
    QAbstractButton::mousePressEvent(e);
}

void Tile::mouseMoveEvent(QMouseEvent* e) {
    if (dragData_ && (e->buttons() & Qt::LeftButton) &&
        (e->position().toPoint() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
        setDown(false);
        auto* drag = new QDrag(this);
        drag->setMimeData(dragData_());
        QPixmap pm(56, 56);
        pm.setDevicePixelRatio(1.0);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            Draw d(p);
            d.rectFilled(Pt(2, 2), Pt(54, 54), theme::withAlpha(theme::kBgRaised, 230), 10.0f);
            drawIcon(d, icon_, Pt(28, 28), 26.0f, theme::kAccent);
        }
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(28, 28));
        drag->exec(Qt::CopyAction);
        update();
        return;
    }
    QAbstractButton::mouseMoveEvent(e);
}

// --- Switch ----------------------------------------------------------------------------------

Switch::Switch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(38, 22);
    setFocusPolicy(Qt::TabFocus);
}

void Switch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool on = isChecked(), enabled = isEnabled(), hot = underMouse() && enabled;
    const float w = static_cast<float>(width()), h = static_cast<float>(height()), r = h * 0.5f;
    Rgba track = on ? theme::kAccent : theme::kField;
    if (on && hot) track = theme::kAccentHover;
    if (!enabled) track = theme::withAlpha(track, 90);
    d.rectFilled(Pt(0.5f, 0.5f), Pt(w - 0.5f, h - 0.5f), track, r);
    if (!on) d.rect(Pt(0.5f, 0.5f), Pt(w - 0.5f, h - 0.5f), hot ? theme::mix(theme::kFieldBorder, theme::kText, 0.3f) : theme::kFieldBorder, r, 1.0f);
    if (hasFocus()) d.rect(Pt(0.5f, 0.5f), Pt(w - 0.5f, h - 0.5f), theme::kFocus, r, 1.5f);
    const float knob = r - 4.0f;
    const Pt c(on ? w - r : r, r);
    d.circleFilled(c, knob, on ? rgba(255, 255, 255) : (enabled ? theme::kTextDim : theme::kTextFaint));
}

namespace {

// A segmented control's strip, drawn whole: a rounded track, and the chosen part a pill inset
// in it, the same shape wherever it sits (first, middle or last). (A style sheet rounding only
// some corners of each button drew edges thin, broken, or different per position.)
class SegmentStrip : public QWidget {
public:
    bool eventFilter(QObject* watched, QEvent* e) override {
        if (e->type() == QEvent::Enter || e->type() == QEvent::Leave || e->type() == QEvent::EnabledChange) update();
        return QWidget::eventFilter(watched, e);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const theme::Palette& pal = theme::current();
        const float r = std::clamp(pal.rounding, 0.0f, 12.0f);
        const float bw = std::max(1.0f, std::round(std::clamp(pal.borderWidth, 0.0f, 2.0f)));
        const QRectF outer = QRectF(rect()).adjusted(bw * 0.5, bw * 0.5, -bw * 0.5, -bw * 0.5);
        QPainterPath shape;
        shape.addRoundedRect(outer, r, r);
        p.fillPath(shape, theme::q(theme::kField));
        p.setPen(QPen(theme::q(theme::kFieldBorder), bw));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
        // Each part a pill inset in the track: the chosen one in the accent, a hovered one lighter.
        const float inset = 2.0f + bw * 0.5f, pr = std::max(2.0f, r - inset * 0.5f);
        for (QPushButton* b : findChildren<QPushButton*>(Qt::FindDirectChildrenOnly)) {
            const bool hot = b->underMouse() && b->isEnabled();
            if (!b->isChecked() && !hot) continue;
            const QRectF pill = QRectF(b->geometry()).intersected(outer).adjusted(inset, inset, -inset, -inset);
            QPainterPath path;
            path.addRoundedRect(pill, pr, pr);
            if (b->isChecked()) {
                p.fillPath(path, theme::q(theme::accent(55)));
                p.setPen(QPen(theme::q(theme::accent(170)), bw));
                p.drawPath(path);
            } else {
                p.fillPath(path, theme::q(theme::mix(pal.field, pal.text, 0.07f)));
            }
        }
    }
};

} // namespace

QWidget* segmented(const QStringList& choices, int current, std::function<void(int)> changed, const std::vector<bool>& enabled) {
    auto* box = new SegmentStrip;
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* group = new QButtonGroup(box);
    group->setExclusive(true);
    for (int i = 0; i < choices.size(); ++i) {
        auto* b = new QPushButton(translate(choices[i]));
        b->setCheckable(true);
        b->setChecked(i == current);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("segment", "part"); // text only: the strip draws the rest
        if (i < static_cast<int>(enabled.size()) && !enabled[i]) b->setEnabled(false);
        b->installEventFilter(box); // hover: the strip draws it
        group->addButton(b, i);
        row->addWidget(b);
    }
    QObject::connect(group, &QButtonGroup::idToggled, box, [box](int, bool) { box->update(); });
    QObject::connect(group, &QButtonGroup::idClicked, box, [changed](int i) {
        if (changed) changed(i);
    });
    return box;
}

// --- DragValue -------------------------------------------------------------------------------

DragValue::DragValue(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize DragValue::sizeHint() const { return {80, 24}; }

bool DragValue::editing() const { return dragging_ || (edit_ && edit_->isVisible()); }

void DragValue::setValue(float v) {
    if (editing() || v == value_) return;
    value_ = v;
    update();
}

QString DragValue::text() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, format_.c_str(), value_);
    return QString::fromUtf8(buf);
}

void DragValue::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hot = underMouse() || dragging_;
    // A field, like every other one (Theme: field colors), that also scrubs when dragged.
    const float r = theme::current().rounding;
    d.rectFilled(Pt(0, 0), Pt(width(), height()), hot ? theme::mix(theme::kField, theme::kText, 0.06f) : theme::kField, r);
    if (theme::current().borderWidth > 0.0f)
        d.rect(Pt(0.5f, 0.5f), Pt(width() - 0.5f, height() - 0.5f), dragging_ ? theme::kFocus : hot ? theme::mix(theme::kFieldBorder, theme::kText, 0.25f) : theme::kFieldBorder, r, 1.0f);
    if (edit_ && edit_->isVisible()) return;
    const QString t = text();
    const float tw = d.textWidth(t);
    d.text(Pt((width() - tw) * 0.5f, (height() - d.textHeight()) * 0.5f), highlighted_ ? theme::kAccent : theme::kText, t);
}

void DragValue::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    dragging_ = true;
    moved_ = false;
    pressPos_ = e->globalPosition();
    pressValue_ = value_;
}

void DragValue::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    const float dx = static_cast<float>(e->globalPosition().x() - pressPos_.x());
    if (!moved_ && std::fabs(dx) < 3.0f) return;
    if (!moved_) {
        moved_ = true;
        emit editStarted();
    }
    float step = speed_;
    if (e->modifiers() & Qt::ShiftModifier) step *= 10.0f;
    if (e->modifiers() & Qt::AltModifier) step *= 0.1f;
    commit(pressValue_ + dx * step);
}

void DragValue::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !dragging_) return;
    dragging_ = false;
    if (moved_) emit editFinished();
    else beginTyping(); // a click: type the value
    update();
}

void DragValue::commit(float v) {
    if (min_ != max_) v = std::clamp(v, min_, max_);
    if (v == value_) return;
    value_ = v;
    update();
    emit valueChanged(v);
}

void DragValue::beginTyping() {
    if (!edit_) {
        edit_ = new QLineEdit(this);
        edit_->setAlignment(Qt::AlignCenter);
        edit_->setFrame(false);
        // Exactly the value's box: the fields' own minimum height and padding (Theme) would make it
        // taller than a short DragValue and cut its outline off at the bottom.
        edit_->setStyleSheet("QLineEdit { min-height: 0px; padding: 0px 4px; }");
        connect(edit_, &QLineEdit::editingFinished, this, [this] {
            if (!edit_->isVisible()) return;
            bool ok = false;
            // Accept "12", "12.5", "12 px", "45°", "50%" ...
            QString s = edit_->text().trimmed();
            while (!s.isEmpty() && !(s.back().isDigit() || s.back() == '.')) s.chop(1);
            const float v = s.toFloat(&ok);
            edit_->hide();
            if (ok) {
                emit editStarted();
                commit(v);
                emit editFinished();
            }
            update();
        });
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", value_);
    edit_->setText(QString::fromUtf8(buf));
    edit_->setGeometry(rect());
    edit_->show();
    edit_->setFocus();
    edit_->selectAll();
}

// --- FlowLayout, ElidedLabel ----------------------------------------------------------------

FlowLayout::~FlowLayout() {
    for (QLayoutItem* item : items_) delete item;
}

QLayoutItem* FlowLayout::takeAt(int i) {
    if (i < 0 || i >= count()) return nullptr;
    QLayoutItem* item = items_[static_cast<size_t>(i)];
    items_.erase(items_.begin() + i);
    return item;
}

QSize FlowLayout::sizeHint() const {
    QSize size;
    const int gap = std::max(0, spacing());
    for (const QLayoutItem* item : items_) {
        if (item->isEmpty()) continue;
        const QSize hint = item->sizeHint();
        size.setWidth(size.width() + (size.width() ? gap : 0) + hint.width());
        size.setHeight(std::max(size.height(), hint.height()));
    }
    return size;
}

QSize FlowLayout::minimumSize() const {
    QSize size; // the widest item: anything narrower wraps
    for (const QLayoutItem* item : items_)
        if (!item->isEmpty()) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::arrange(const QRect& r, bool place) const {
    const int gap = std::max(0, spacing());
    const QMargins m = contentsMargins();
    const QRect area = r.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x(), y = area.y(), lineHeight = 0;
    for (QLayoutItem* item : items_) {
        if (item->isEmpty()) continue;
        QSize hint = item->sizeHint();
        hint.setWidth(std::min(hint.width(), std::max(item->minimumSize().width(), area.width())));
        if (x > area.x() && x + hint.width() > area.right() + 1) { // no room left on this line
            x = area.x();
            y += lineHeight + gap;
            lineHeight = 0;
        }
        if (place) item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + gap;
        lineHeight = std::max(lineHeight, hint.height());
    }
    return y + lineHeight - r.y() + m.bottom();
}

void ElidedLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect area = contentsRect();
    const QString shown = fontMetrics().elidedText(text(), mode_, area.width());
    p.setPen(palette().color(foregroundRole()));
    p.setFont(font());
    p.drawText(area, static_cast<int>(alignment() | Qt::TextSingleLine), shown);
}

// --- ResponsiveGrid, LabeledRow -----------------------------------------------------------

ResponsiveGrid::ResponsiveGrid(int minCell, int gap, QWidget* parent) : QWidget(parent), minCell_(minCell), gap_(gap) {
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void ResponsiveGrid::addWidget(QWidget* w) {
    w->setParent(this);
    cells_.push_back(w);
    w->show();
    updateGeometry();
}

int ResponsiveGrid::heightForWidth(int width) const {
    const int cols = columns(width);
    int h = 0, row = 0;
    for (size_t i = 0; i < cells_.size(); ++i) {
        row = std::max(row, cells_[i]->sizeHint().height());
        if (static_cast<int>(i % cols) == cols - 1 || i + 1 == cells_.size()) {
            h += row + (h ? gap_ : 0);
            row = 0;
        }
    }
    return h;
}

QSize ResponsiveGrid::sizeHint() const {
    const int w = minCell_ * 3 + gap_ * 2;
    return {w, heightForWidth(w)};
}

QSize ResponsiveGrid::minimumSizeHint() const { return {minCell_, heightForWidth(minCell_)}; }

void ResponsiveGrid::place() {
    const int cols = columns(width());
    const int cell = std::max(1, (width() - gap_ * (cols - 1)) / cols);
    int y = 0, row = 0;
    for (size_t i = 0; i < cells_.size(); ++i) {
        const int c = static_cast<int>(i % cols);
        int rowHeight = 0;
        for (size_t k = i - c; k < std::min(cells_.size(), i - c + cols); ++k) rowHeight = std::max(rowHeight, cells_[k]->sizeHint().height());
        cells_[i]->setGeometry(c * (cell + gap_), y, cell, rowHeight);
        row = rowHeight;
        if (c == cols - 1) y += row + gap_;
    }
}

// --- CategoryHeader / CategoryBody -----------------------------------------------------------

namespace {
constexpr float kGuideX = kCategoryGuideX; // the chevron's middle, and the guide line under it
}

void paintCategoryHeader(QPainter& p, const QRectF& r, Icon icon, const QString& name, int count, bool closed, bool hovered, bool target) {
    Draw d(p);
    const float x = static_cast<float>(r.x()), y = static_cast<float>(r.y()), w = static_cast<float>(r.width());
    const float cy = y + static_cast<float>(r.height()) * 0.5f;
    if (target || hovered) d.rectFilled({x, y + 1}, {x + w, y + static_cast<float>(r.height()) - 1}, theme::kBgHover, 6.0f);
    if (target) d.rect({x + 0.5f, y + 1.5f}, {x + w - 0.5f, y + static_cast<float>(r.height()) - 1.5f}, theme::kTextDim, 6.0f); // something dropped goes in here
    drawIcon(d, closed ? Icon::ChevronRight : Icon::ChevronDown, {x + kGuideX, cy}, 12.0f, theme::kTextDim);
    drawIcon(d, icon, {x + 29, cy}, 15.0f, theme::kText);
    const QFont normal = p.font();
    QFont bold = normal;
    bold.setWeight(QFont::DemiBold);
    p.setFont(bold);
    const QString n = count >= 0 ? QString::number(count) : QString();
    const float countW = count >= 0 ? d.textWidth(n) + 12.0f : 0.0f;
    const QString shown = p.fontMetrics().elidedText(name, Qt::ElideRight, static_cast<int>(w - 44 - countW - 16));
    d.text({x + 44, cy - d.textHeight() * 0.5f}, theme::kText, shown);
    // A rule from the name to the count: the heading of what follows, not one more item.
    const float ruleFrom = x + 44 + d.textWidth(shown) + 10, ruleTo = x + w - countW - 10;
    if (ruleTo > ruleFrom) d.line({ruleFrom, cy}, {ruleTo, cy}, theme::kBorder, 1.0f);
    p.setFont(normal);
    if (count < 0) return;
    d.rectFilled({x + w - countW - 2, cy - 9}, {x + w - 2, cy + 9}, theme::kBgRaised, 9.0f);
    d.text({x + w - countW - 2 + (countW - d.textWidth(n)) * 0.5f, cy - d.textHeight() * 0.5f}, theme::kTextDim, n);
}

CategoryHeader::CategoryHeader(Icon icon, const QString& name, int count, bool closed, QWidget* parent)
    : QWidget(parent), icon_(icon), name_(name), count_(count), closed_(closed) {
    setFixedHeight(30);
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        if (menuRequested) menuRequested(at);
    });
    setAcceptDrops(true);
}

void CategoryHeader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintCategoryHeader(p, rect(), icon_, name_, count_, closed_, underMouse(), target_);
}

void CategoryHeader::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && toggled) toggled();
}

void CategoryHeader::dragEnterEvent(QDragEnterEvent* e) {
    if (!accepts || !accepts(e->mimeData())) return e->ignore();
    target_ = true;
    update();
    e->acceptProposedAction();
}

void CategoryHeader::dragLeaveEvent(QDragLeaveEvent*) {
    target_ = false;
    update();
}

void CategoryHeader::dropEvent(QDropEvent* e) {
    target_ = false;
    update();
    if (!accepts || !accepts(e->mimeData())) return;
    e->acceptProposedAction();
    if (dropped) dropped(e->mimeData());
}

ChoiceRow::ChoiceRow(Icon icon, const QString& label, QWidget* parent) : QAbstractButton(parent), icon_(icon), label_(label) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(28);
}

void ChoiceRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hovered = underMouse();
    const float w = static_cast<float>(width()), cy = height() * 0.5f;
    if (hovered || isDown()) d.rectFilled({0, 0}, {w, static_cast<float>(height())}, isDown() ? theme::kBgRaised : theme::kBgHover, 6.0f);
    drawIcon(d, icon_, {14, cy}, 14.0f, hovered ? theme::kText : theme::kTextDim);
    const QString text = p.fontMetrics().elidedText(label_, Qt::ElideRight, static_cast<int>(w - 30 - (hovered ? 26 : 6)));
    d.text({30, cy - d.textHeight() * 0.5f}, hovered ? theme::kText : theme::kTextDim, text);
    if (hovered) drawIcon(d, Icon::Plus, {w - 14, cy}, 12.0f, theme::kText);
}

CategoryBody::CategoryBody(QWidget* content, QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(static_cast<int>(kGuideX) + 9, 2, 0, 10);
    col->setSpacing(6);
    col->addWidget(content);
}

void CategoryBody::add(QWidget* more) { static_cast<QVBoxLayout*>(layout())->addWidget(more); }

void CategoryBody::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    d.line({kGuideX, 0}, {kGuideX, height() - 12.0f}, theme::kBorder, 1.0f);
}

LabeledRow::LabeledRow(const QString& label, QWidget* control, int controlMin, QWidget* parent)
    : QWidget(parent), label_(new QLabel(label, this)), control_(control), controlMin_(controlMin) {
    control_->setParent(this);
    setTone(label_, Tone::TextDim);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

// A control's height: what it asks for, or what it is held to.
static int heightOf(const QWidget* w) { return std::max({w->sizeHint().height(), w->minimumHeight(), w->minimumSizeHint().height()}); }

int LabeledRow::heightForWidth(int width) const {
    const int ch = heightOf(control_), lh = label_->sizeHint().height();
    return stacked(width) ? lh + 2 + ch : std::max(ch, lh);
}

QSize LabeledRow::sizeHint() const {
    const int w = labelWidth() + 10 + std::max(controlMin_, control_->sizeHint().width());
    return {w, heightForWidth(w)};
}

QSize LabeledRow::minimumSizeHint() const {
    const int w = std::max(control_->minimumSizeHint().width(), std::min(labelWidth(), 60));
    return {w, heightForWidth(w)};
}

void LabeledRow::place() {
    const int ch = heightOf(control_), lh = label_->sizeHint().height();
    if (stacked(width())) {
        label_->setGeometry(0, 0, width(), lh);
        control_->setGeometry(0, lh + 2, width(), ch);
    } else {
        const int h = std::max(ch, lh), lw = std::max(labelWidth(), 88);
        label_->setGeometry(0, (h - lh) / 2, lw, lh);
        control_->setGeometry(lw + 10, (h - ch) / 2, width() - lw - 10, ch);
    }
}

} // namespace bmstyle
