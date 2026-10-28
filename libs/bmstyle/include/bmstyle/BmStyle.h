#pragma once

// bmstyle: bemotion's look as a library for any Qt Widgets app in C++ — the theme (palettes and
// color tokens), the widget style, the vector icons and the shared controls. One call sets an
// app up:
//
//     QApplication app(argc, argv);
//     bmstyle::install(bmstyle::theme::darkPalette());
//
// then build with bmstyle's controls (bmstyle/Widgets.h) and draw custom parts with
// bmstyle::Draw and the theme's tokens (theme::kText, kAccent...), read at paint time.
// Metrics.h holds the spacing and sizes the controls are laid out with.

#include "bmstyle/Draw.h"
#include "bmstyle/Icons.h"
#include "bmstyle/Metrics.h"
#include "bmstyle/Style.h"
#include "bmstyle/Theme.h"
#include "bmstyle/Widgets.h"

#include <QObject>
#include <QString>

#include <functional>

namespace bmstyle {

// The style (Fusion with bmstyle's check boxes, radio buttons and wheel rules) and the palette,
// for the whole application. Call once the QApplication exists; theme::apply changes the
// palette later.
void install(const theme::Palette& palette);

// Text the controls show (labels, tooltips, segmented choices) goes through translate(): an app
// with translations sets its own function (it gets the English text); unset, text is shown as
// given.
void setTranslator(std::function<QString(const QString&)> translator);
QString translate(const QString& text);

// Says when the look changed (theme::apply calls it; an app also emits it after changing fonts
// or sizes): toned labels recolor themselves, and custom-drawn widgets can listen to repaint.
class Appearance : public QObject {
    Q_OBJECT
public:
    static Appearance& get();

signals:
    void changed();

private:
    Appearance() = default;
};

} // namespace bmstyle
