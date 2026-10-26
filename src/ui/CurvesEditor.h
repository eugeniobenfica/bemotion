#pragma once

// The Curves effect's editor: the RGB, red, green and blue curves on one square, a tab per
// curve above it. Click the curve to add a point, drag points, drag one off the square (or
// double-click it) to remove it; right-click resets.

#include "core/Math.h"

#include <QWidget>

#include <array>
#include <vector>

namespace bemotion::ui {

class CurvesEditor : public QWidget {
    Q_OBJECT
public:
    using Curves = std::array<std::vector<Vec2>, 4>;
    explicit CurvesEditor(QWidget* parent = nullptr);
    void setCurves(const Curves& curves); // from the document (ignored mid-drag)
    const Curves& curves() const { return curves_; }
    QSize sizeHint() const override { return {240, 268}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return std::min(w, 320) + kTabs + 8; }

signals:
    void changed(); // curves() changed (repeatedly while dragging)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    static constexpr int kTabs = 24;
    QRectF plot() const;
    QPointF toScreen(Vec2 p) const;
    Vec2 toCurve(QPointF p) const;
    int pointAt(QPointF p) const;
    QRectF tabRect(int i) const;

    Curves curves_;
    int channel_ = 0;
    int drag_ = -1;
    bool dragOff_ = false; // the dragged point is off the square: released there, it goes
};

} // namespace bemotion::ui
