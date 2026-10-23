#include "ui/Style.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>

namespace bemotion::ui {

Style::Style() : QProxyStyle(QStyleFactory::create("Fusion")) {}

int Style::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const {
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight || metric == PM_ExclusiveIndicatorWidth ||
        metric == PM_ExclusiveIndicatorHeight)
        return 16;
    return QProxyStyle::pixelMetric(metric, option, widget);
}

void Style::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* p, const QWidget* widget) const {
    const bool check = element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck;
    const bool radio = element == PE_IndicatorRadioButton;
    if (!check && !radio) return QProxyStyle::drawPrimitive(element, option, p, widget);

    const bool on = option->state & (State_On | State_NoChange);
    const bool hover = option->state & State_MouseOver;
    const bool enabled = option->state & State_Enabled;
    const QRectF r = QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    // The box: a raised surface; its outline turns to the accent when checked or hovered.
    const QColor border = theme::q(on || hover ? theme::accent(enabled ? 220 : 90) : theme::kBorder);
    p->setPen(QPen(border, 1.2));
    p->setBrush(theme::q(theme::kBgRaised));
    if (radio) p->drawEllipse(r);
    else p->drawRoundedRect(r, 4, 4);
    if (on) {
        const QColor mark = theme::q(enabled ? theme::kAccent : theme::kTextFaint);
        if (radio) {
            p->setPen(Qt::NoPen);
            p->setBrush(mark);
            const double d = r.width() * 0.25;
            p->drawEllipse(r.adjusted(d, d, -d, -d));
        } else if (option->state & State_NoChange) { // partly checked: a dash
            p->setPen(QPen(mark, 2.0, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(r.left() + r.width() * 0.28, r.center().y()), QPointF(r.right() - r.width() * 0.28, r.center().y()));
        } else { // a check mark
            QPainterPath tick;
            tick.moveTo(r.left() + r.width() * 0.24, r.top() + r.height() * 0.53);
            tick.lineTo(r.left() + r.width() * 0.43, r.top() + r.height() * 0.72);
            tick.lineTo(r.left() + r.width() * 0.78, r.top() + r.height() * 0.30);
            p->setPen(QPen(mark, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(tick);
        }
    }
    p->restore();
}

} // namespace bemotion::ui
