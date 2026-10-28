#include "bestyle/BeStyle.h"

#include <QApplication>

namespace bestyle {

namespace {
std::function<QString(const QString&)>& translator() {
    static std::function<QString(const QString&)> f;
    return f;
}
} // namespace

void install(const theme::Palette& palette) {
    QApplication::setStyle(new Style);
    theme::apply(palette);
}

void setTranslator(std::function<QString(const QString&)> f) { translator() = std::move(f); }

QString translate(const QString& text) { return translator() ? translator()(text) : text; }

Appearance& Appearance::get() {
    static Appearance a;
    return a;
}

} // namespace bestyle
