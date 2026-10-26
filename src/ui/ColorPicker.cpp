#include "ui/ColorPicker.h"

#include "app/Settings.h"
#include "ui/Draw.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bemotion::ui {

namespace {

constexpr int kPad = 12;
constexpr int kWidth = 256;
constexpr int kSquare = 156; // the saturation / brightness square's height
constexpr int kBar = 12;     // hue and opacity bars
constexpr int kMaxRecent = 16;

Rgba packed(Color c, float alpha = -1.0f) {
    auto ch = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return rgba(ch(c.r), ch(c.g), ch(c.b), ch(alpha < 0.0f ? c.a : alpha));
}

QString hexOf(Color c, bool withAlpha) {
    const Rgba p = packed(c);
    QString s = QString("#%1%2%3").arg(red(p), 2, 16, QChar('0')).arg(green(p), 2, 16, QChar('0')).arg(blue(p), 2, 16, QChar('0'));
    if (withAlpha && alpha(p) < 255) s += QString("%1").arg(alpha(p), 2, 16, QChar('0'));
    return s.toUpper();
}

// "#RGB", "#RRGGBB" or "#RRGGBBAA", with or without the "#".
bool parseHex(QString text, Color& out) {
    text = text.trimmed();
    if (text.startsWith('#')) text.remove(0, 1);
    if (text.size() == 3) text = QString("%1%1%2%2%3%3").arg(text[0]).arg(text[1]).arg(text[2]);
    if (text.size() != 6 && text.size() != 8) return false;
    bool ok = false;
    const uint v = text.toUInt(&ok, 16);
    if (!ok) return false;
    if (text.size() == 6) out = {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, out.a};
    else out = {((v >> 24) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f};
    return true;
}

std::vector<Color> recentColors() {
    std::vector<Color> out;
    for (const std::string& s : settings().recentColors) {
        Color c;
        if (parseHex(QString::fromStdString(s), c)) out.push_back(c);
    }
    return out;
}

void remember(Color c) {
    auto& list = settings().recentColors;
    const std::string hex = QString("%1%2").arg(hexOf(c, false)).arg(alpha(packed(c)), 2, 16, QChar('0')).toUpper().toStdString();
    list.erase(std::remove(list.begin(), list.end(), hex), list.end());
    list.insert(list.begin(), hex);
    if (list.size() > kMaxRecent) list.resize(kMaxRecent);
    saveSettings();
}

// A checkerboard under translucent colors.
void checker(Draw& d, QRectF r, float rounding) {
    d.rectFilled(Pt(r.left(), r.top()), Pt(r.right(), r.bottom()), rgba(204, 204, 204), rounding);
    d.pushClip(Pt(r.left(), r.top()), Pt(r.right(), r.bottom()));
    const int cell = 5;
    for (int y = 0; y * cell < r.height(); ++y)
        for (int x = y % 2; x * cell < r.width(); x += 2)
            d.rectFilled(Pt(r.left() + x * cell, r.top() + y * cell), Pt(r.left() + (x + 1) * cell, r.top() + (y + 1) * cell), rgba(153, 153, 153));
    d.popClip();
}

// The last colors used: a row of squares, a click takes one.
class Recents : public QWidget {
public:
    Recents(std::vector<Color> colors, std::function<void(Color)> picked) : colors_(std::move(colors)), picked_(std::move(picked)) {
        setMouseTracking(true);
        const int rows = std::max(1, static_cast<int>((colors_.size() + kPerRow - 1) / kPerRow));
        setFixedHeight(colors_.empty() ? 18 : rows * (kCell + 4));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        if (colors_.empty()) {
            d.text(Pt(0, 2), theme::kTextFaint, tr("Colors you pick show here"));
            return;
        }
        for (size_t i = 0; i < colors_.size(); ++i) {
            const QRectF r = cell(static_cast<int>(i));
            if (colors_[i].a < 1.0f) checker(d, r, 3.0f);
            d.rectFilled(Pt(r.left(), r.top()), Pt(r.right(), r.bottom()), packed(colors_[i]), 3.0f);
            d.rect(Pt(r.left(), r.top()), Pt(r.right(), r.bottom()), static_cast<int>(i) == hover_ ? theme::kText : theme::kBorder, 3.0f);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const int at = indexAt(e->position());
        if (at != hover_) {
            hover_ = at;
            setCursor(at >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent*) override {
        hover_ = -1;
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (const int at = indexAt(e->position()); at >= 0) picked_(colors_[at]);
    }

private:
    static constexpr int kPerRow = 8, kCell = 22;
    QRectF cell(int i) const {
        const float step = (width() - kCell) / static_cast<float>(kPerRow - 1);
        return QRectF((i % kPerRow) * step, (i / kPerRow) * (kCell + 4), kCell, kCell);
    }
    int indexAt(QPointF pos) const {
        for (size_t i = 0; i < colors_.size(); ++i)
            if (cell(static_cast<int>(i)).contains(pos)) return static_cast<int>(i);
        return -1;
    }
    std::vector<Color> colors_;
    std::function<void(Color)> picked_;
    int hover_ = -1;
};

class Picker : public QWidget {
public:
    Picker(QWidget* parent, Color start, bool alpha, std::function<void(Color)> changed)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), start_(start), alpha_(alpha), changed_(std::move(changed)) {
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle(tr("Color"));
        setMouseTracking(true);
        setFixedWidth(kWidth);
        if (!alpha_) start_.a = 1.0f;
        fromColor(start_);

        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(kPad, paintedHeight(), kPad, kPad);
        col->setSpacing(8);

        // Before / after, the eyedropper, hex and opacity.
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addSpacing(kSwatchWidth + 4); // the swatch is painted
        dropper_ = new IconButton(Icon::Eyedropper, tr("Pick a color from the screen (Esc cancels)"), 26);
        QObject::connect(dropper_, &QAbstractButton::clicked, this, [this] { startPicking(); });
        row->addWidget(dropper_);
        hex_ = new QLineEdit;
        hex_->setToolTip(tr("Hex color: #RRGGBB") + QString(alpha_ ? tr(" or #RRGGBBAA") : ""));
        hex_->setMaxLength(9);
        QObject::connect(hex_, &QLineEdit::textEdited, this, [this](const QString& text) {
            Color c = color();
            if (!parseHex(text, c)) return;
            if (!alpha_) c.a = 1.0f;
            fromColor(c);
            changedHere(false);
        });
        QObject::connect(hex_, &QLineEdit::editingFinished, this, [this] { syncFields(); });
        row->addWidget(hex_, 1);
        if (alpha_) {
            alphaField_ = new DragValue;
            alphaField_->setRange(0.0f, 100.0f);
            alphaField_->setSpeed(0.5f);
            alphaField_->setFormat("%.0f%%");
            alphaField_->setFixedWidth(58);
            alphaField_->setToolTip(tr("Opacity"));
            QObject::connect(alphaField_, &DragValue::valueChanged, this, [this](float v) {
                a_ = v / 100.0f;
                changedHere();
            });
            row->addWidget(alphaField_);
        }
        col->addLayout(row);

        // Red, green, blue.
        auto* rgb = new QHBoxLayout;
        rgb->setSpacing(6);
        const char* names[] = {"R", "G", "B"};
        for (int i = 0; i < 3; ++i) {
            auto* v = new DragValue;
            v->setRange(0.0f, 255.0f);
            v->setSpeed(0.5f);
            v->setFormat(std::string(names[i]) + "  %.0f");
            v->setToolTip(tr("%1 (0 to 255)").arg(i == 0 ? tr("Red") : i == 1 ? tr("Green") : tr("Blue")));
            QObject::connect(v, &DragValue::valueChanged, this, [this, i](float value) {
                Color c = color();
                (i == 0 ? c.r : i == 1 ? c.g : c.b) = value / 255.0f;
                fromColor(c);
                changedHere();
            });
            channels_[i] = v;
            rgb->addWidget(v);
        }
        col->addLayout(rgb);

        auto* recentTitle = new QLabel(tr("Recent"));
        setTone(recentTitle, Tone::TextFaint);
        col->addWidget(recentTitle);
        col->addWidget(new Recents(recentColors(), [this](Color c) {
            if (!alpha_) c.a = 1.0f;
            fromColor(c);
            changedHere();
        }));
        syncFields();
        adjustSize();
    }

    Color color() const {
        const QColor q = QColor::fromHsvF(h_, s_, v_, alpha_ ? a_ : 1.0f);
        return {static_cast<float>(q.redF()), static_cast<float>(q.greenF()), static_cast<float>(q.blueF()), static_cast<float>(q.alphaF())};
    }
    bool kept = false;
    std::function<void()> onClosed;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const float r = theme::current().rounding + 2.0f;
        d.rectFilled(Pt(0.5f, 0.5f), Pt(width() - 0.5f, height() - 0.5f), theme::kMenu, r);
        d.rect(Pt(0.5f, 0.5f), Pt(width() - 0.5f, height() - 0.5f), theme::kBorder, r);

        // Saturation (left to right) and brightness (top to bottom) for the hue.
        const QRectF sq = squareRect();
        QLinearGradient across(sq.topLeft(), sq.topRight());
        across.setColorAt(0, Qt::white);
        across.setColorAt(1, QColor::fromHsvF(h_, 1.0f, 1.0f));
        QLinearGradient down(sq.topLeft(), sq.bottomLeft());
        down.setColorAt(0, QColor(0, 0, 0, 0));
        down.setColorAt(1, Qt::black);
        QPainterPath clip;
        clip.addRoundedRect(sq, 4, 4);
        p.save();
        p.setClipPath(clip);
        p.fillRect(sq, across);
        p.fillRect(sq, down);
        p.restore();
        const Pt knob(static_cast<float>(sq.left() + s_ * sq.width()), static_cast<float>(sq.top() + (1.0f - v_) * sq.height()));
        d.circle(knob, 7.0f, rgba(0, 0, 0, 120), 3.0f);
        d.circle(knob, 7.0f, rgba(255, 255, 255), 2.0f);

        // Hue.
        const QRectF hue = hueRect();
        QLinearGradient hues(hue.topLeft(), hue.topRight());
        for (int i = 0; i <= 6; ++i) hues.setColorAt(i / 6.0, QColor::fromHsvF(std::min(0.9999f, i / 6.0f), 1.0f, 1.0f));
        p.setPen(Qt::NoPen);
        p.setBrush(hues);
        p.drawRoundedRect(hue, kBar / 2.0, kBar / 2.0);
        barKnob(d, hue, h_, packed(Color{static_cast<float>(QColor::fromHsvF(h_, 1, 1).redF()), static_cast<float>(QColor::fromHsvF(h_, 1, 1).greenF()),
                                         static_cast<float>(QColor::fromHsvF(h_, 1, 1).blueF()), 1.0f}));

        // Opacity, over a checkerboard.
        if (alpha_) {
            const QRectF ar = alphaRect();
            checker(d, ar, kBar / 2.0f);
            Color opaque = color();
            opaque.a = 1.0f;
            QLinearGradient fade(ar.topLeft(), ar.topRight());
            fade.setColorAt(0, qcolor(packed(opaque, 0.0f)));
            fade.setColorAt(1, qcolor(packed(opaque, 1.0f)));
            p.setBrush(fade);
            p.drawRoundedRect(ar, kBar / 2.0, kBar / 2.0);
            barKnob(d, ar, a_, packed(color()));
        }

        // Before (left: click to go back) and after.
        const QRectF sw = swatchRect();
        const QRectF before(sw.left(), sw.top(), sw.width() / 2, sw.height()), after(sw.center().x(), sw.top(), sw.width() / 2, sw.height());
        checker(d, sw, 4.0f);
        p.save();
        QPainterPath round;
        round.addRoundedRect(sw, 4, 4);
        p.setClipPath(round);
        p.fillRect(before, qcolor(packed(start_)));
        p.fillRect(after, qcolor(packed(color())));
        p.restore();
        d.rect(Pt(sw.left(), sw.top()), Pt(sw.right(), sw.bottom()), hoverBefore_ ? theme::kText : theme::kFieldBorder, 4.0f);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (picking_) {
            sample(e->globalPosition().toPoint());
            stopPicking(true);
            return;
        }
        const QPointF pos = e->position();
        if (!rect().contains(pos.toPoint())) { // outside: done, keeping the color
            kept = true;
            close();
            return;
        }
        const QRectF sw = swatchRect();
        if (QRectF(sw.left(), sw.top(), sw.width() / 2, sw.height()).contains(pos)) { // the color before
            fromColor(start_);
            changedHere();
            return;
        }
        if (squareRect().adjusted(-6, -6, 6, 6).contains(pos)) drag_ = Drag::Square;
        else if (hueRect().adjusted(-4, -6, 4, 6).contains(pos)) drag_ = Drag::Hue;
        else if (alpha_ && alphaRect().adjusted(-4, -6, 4, 6).contains(pos)) drag_ = Drag::Alpha;
        dragTo(pos);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (picking_) {
            sample(e->globalPosition().toPoint());
            return;
        }
        if (drag_ != Drag::None) dragTo(e->position());
        const QRectF sw = swatchRect();
        const bool over = QRectF(sw.left(), sw.top(), sw.width() / 2, sw.height()).contains(e->position());
        if (over != hoverBefore_) {
            hoverBefore_ = over;
            setToolTip(over ? tr("The color before: click to go back to it") : QString());
            update();
        }
        const bool handy = over || squareRect().contains(e->position()) || hueRect().adjusted(0, -4, 0, 4).contains(e->position()) ||
                           (alpha_ && alphaRect().adjusted(0, -4, 0, 4).contains(e->position()));
        setCursor(handy ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void mouseReleaseEvent(QMouseEvent*) override { drag_ = Drag::None; }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            if (picking_) {
                stopPicking(false);
                return;
            }
            fromColor(start_); // undo it all
            changedHere();
            close();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            kept = true;
            close();
            return;
        }
        QWidget::keyPressEvent(e);
    }
    void hideEvent(QHideEvent*) override {
        if (picking_) stopPicking(true);
        if (onClosed) onClosed();
    }

private:
    enum class Drag { None, Square, Hue, Alpha };
    static constexpr int kSwatchWidth = 52;

    int paintedHeight() const { return kPad + kSquare + 12 + kBar + (alpha_ ? 10 + kBar : 0) + 14; }
    QRectF squareRect() const { return QRectF(kPad, kPad, kWidth - 2 * kPad, kSquare); }
    QRectF hueRect() const { return QRectF(kPad + 2, kPad + kSquare + 12, kWidth - 2 * kPad - 4, kBar); }
    QRectF alphaRect() const { return QRectF(kPad + 2, hueRect().bottom() + 10, kWidth - 2 * kPad - 4, kBar); }
    QRectF swatchRect() const {
        const int y = paintedHeight();
        return QRectF(kPad, y, kSwatchWidth, hex_ ? hex_->height() : 26);
    }

    void barKnob(Draw& d, QRectF bar, float u, Rgba fill) {
        const Pt c(static_cast<float>(bar.left() + u * bar.width()), static_cast<float>(bar.center().y()));
        d.circleFilled(c, kBar / 2.0f + 3.0f, rgba(255, 255, 255));
        d.circleFilled(c, kBar / 2.0f, fill | 0xFF000000u);
        d.circle(c, kBar / 2.0f + 3.0f, rgba(0, 0, 0, 90), 1.0f);
    }

    void dragTo(QPointF pos) {
        auto unit = [](double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); };
        switch (drag_) {
        case Drag::Square: {
            const QRectF sq = squareRect();
            s_ = unit((pos.x() - sq.left()) / sq.width());
            v_ = 1.0f - unit((pos.y() - sq.top()) / sq.height());
            break;
        }
        case Drag::Hue: h_ = std::min(0.9999f, unit((pos.x() - hueRect().left()) / hueRect().width())); break;
        case Drag::Alpha: a_ = unit((pos.x() - alphaRect().left()) / alphaRect().width()); break;
        case Drag::None: return;
        }
        changedHere();
    }

    // The hue stays as it was where a color has none (grays): dragging down to black and back
    // up does not lose it.
    void fromColor(Color c) {
        float h = 0, s = 0, v = 0, a = 0;
        QColor::fromRgbF(c.r, c.g, c.b, c.a).getHsvF(&h, &s, &v, &a);
        if (h >= 0.0f && s > 0.0f && v > 0.0f) h_ = h;
        if (v > 0.0f) s_ = s;
        v_ = v;
        a_ = a;
        update();
    }

    void changedHere(bool fields = true) {
        if (fields) syncFields();
        else syncFields(false);
        update();
        if (changed_) changed_(color());
    }

    void syncFields(bool hex = true) {
        const Color c = color();
        if (hex && hex_) hex_->setText(hexOf(c, alpha_));
        for (int i = 0; i < 3; ++i)
            if (channels_[i]) channels_[i]->setValue(std::round((i == 0 ? c.r : i == 1 ? c.g : c.b) * 255.0f));
        if (alphaField_) alphaField_->setValue(std::round(a_ * 100.0f));
    }

    void startPicking() {
        picking_ = true;
        beforePick_ = color();
        dropper_->setActive(true);
        grabMouse(Qt::CrossCursor);
        grabKeyboard();
    }
    void stopPicking(bool keep) {
        picking_ = false;
        releaseMouse();
        releaseKeyboard();
        dropper_->setActive(false);
        if (!keep) {
            fromColor(beforePick_);
            changedHere();
        }
    }
    // The color on the screen under `global`, as the screen shows it.
    void sample(QPoint global) {
        QScreen* screen = QGuiApplication::screenAt(global);
        if (!screen) return;
        const QPoint local = global - screen->geometry().topLeft();
        const QImage px = screen->grabWindow(0, local.x(), local.y(), 1, 1).toImage();
        if (px.isNull()) return;
        const QColor q = px.pixelColor(0, 0);
        Color c{static_cast<float>(q.redF()), static_cast<float>(q.greenF()), static_cast<float>(q.blueF()), alpha_ ? a_ : 1.0f};
        fromColor(c);
        changedHere();
    }

    float h_ = 0, s_ = 0, v_ = 0, a_ = 1;
    Color start_, beforePick_;
    bool alpha_;
    std::function<void(Color)> changed_;
    Drag drag_ = Drag::None;
    bool picking_ = false, hoverBefore_ = false;
    QLineEdit* hex_ = nullptr;
    DragValue* channels_[3] = {};
    DragValue* alphaField_ = nullptr;
    IconButton* dropper_ = nullptr;
};

} // namespace

bool pickColor(QWidget* anchor, Color& color, bool alpha, const std::function<void(Color)>& changed) {
    QPointer<Picker> picker = new Picker(anchor ? anchor->window() : nullptr, color, alpha, changed);
    // Under the control (above it when the screen ends), inside the screen.
    const QSize size = picker->sizeHint().expandedTo(picker->size());
    QPoint at = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height() + 4)) : QCursor::pos();
    if (QScreen* screen = anchor ? anchor->screen() : QGuiApplication::screenAt(at)) {
        const QRect area = screen->availableGeometry();
        if (at.y() + size.height() > area.bottom() && anchor) at.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - size.height() - 4);
        at.setX(std::clamp(at.x(), area.left() + 4, std::max(area.left() + 4, area.right() - size.width() - 4)));
        at.setY(std::clamp(at.y(), area.top() + 4, std::max(area.top() + 4, area.bottom() - size.height() - 4)));
    }
    picker->move(at);
    QEventLoop loop;
    picker->onClosed = [&loop] { loop.quit(); };
    picker->show();
    picker->activateWindow();
    loop.exec();
    if (!picker) return false;
    const bool kept = picker->kept;
    const Color picked = picker->color();
    picker->onClosed = nullptr;
    picker->deleteLater();
    if (!kept) return false;
    color = picked;
    remember(picked);
    return true;
}

} // namespace bemotion::ui
