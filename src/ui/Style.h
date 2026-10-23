#pragma once

// The application's widget style: Fusion, with check boxes and radio buttons
// drawn our way (a check mark, not a box filled with the accent color, which
// looks like a color swatch). Colors come from the theme tokens at paint time.

#include <QProxyStyle>

namespace bemotion::ui {

class Style : public QProxyStyle {
public:
    Style();
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
};

} // namespace bemotion::ui
