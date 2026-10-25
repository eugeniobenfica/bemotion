#pragma once

// The ƒ library: everything an expression can say, in one popup. Categories on the left
// (examples for this property, this clip, animation, math, values, other layers), each entry
// with its code and what it does; a search over all of them. A click (or Enter) inserts it.

#include <QString>

#include <functional>
#include <vector>

class QWidget;

namespace bemotion::ui {

struct ExpressionEntry {
    QString category;
    QString code;    // shown, in the code font
    QString snippet; // inserted; "|" is where the cursor goes (empty: the code)
    QString about;   // a line under it
    bool header = false; // a title inside the category (a layer's name), not an entry
};

// Opens the library under `anchor` (on `category` when given); `pick` gets the snippet.
void showExpressionLibrary(QWidget* anchor, std::vector<ExpressionEntry> entries, std::function<void(const QString&)> pick,
                           const QString& category = {});

} // namespace bemotion::ui
