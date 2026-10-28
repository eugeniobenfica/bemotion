#pragma once

// The application's widget style: Fusion, with check boxes and radio buttons
// drawn our way (a check mark, not a box filled with the accent color, which
// looks like a color swatch), and lists the wheel does not change. Colors come from the theme tokens at paint time.

#include <QProxyStyle>

namespace bmstyle {

class Style : public QProxyStyle {
public:
    Style();
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
    using QProxyStyle::polish;
    void polish(QWidget* widget) override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
                  QStyleHintReturn* data = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
};

} // namespace bmstyle
