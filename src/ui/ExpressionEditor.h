#pragma once

// Where expressions are written (Properties, under the property; the timeline, in a small
// window): several lines, colored as they are typed, names completed (functions, the clip's
// times, other layers and their properties), a ƒ menu that inserts them (After Effects'
// expression language menu and pick whip, in one) and the problem, if it does not read.

#include <QPlainTextEdit>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

class QCompleter;
class QLabel;
class QStringListModel;

namespace bemotion {

class EditorContext;

namespace ui {

class ExpressionText;
struct ExpressionEntry;

// Examples for a property (`propId`: "position"...; `vector`: [x, y]; `color`: rgba), the
// first page of the ƒ library and of the property's menu.
std::vector<ExpressionEntry> expressionExamples(const std::string& propId, bool vector, bool color);

class ExpressionEditor : public QWidget {
    Q_OBJECT
public:
    // `layerId`: the layer it is on (its own properties are offered first); `vector`: a
    // position-like property (the examples use [x, y]).
    ExpressionEditor(EditorContext& ctx, uint64_t layerId, bool vector, QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& text); // without emitting edited
    bool editing() const;              // the text has the focus
    void focusText();
    void setProblem(const QString& problem); // shown under it (empty: none)
    void setRowButtons(bool shown);          // close and remove (not in a dialog, which has its own)
    void setProperty(const std::string& propId, bool color); // what the examples are written for

signals:
    void edited(const QString& text); // as typed
    void closeRequested();            // its close button, or Esc
    void removeRequested();           // its remove button

private:
    void insert(const QString& snippet); // at the cursor; "|" marks where the cursor goes
    void showMenu();

    EditorContext& ctx_;
    uint64_t layerId_;
    bool vector_;
    std::string propId_;
    bool color_ = false;
    QWidget* fx_ = nullptr; // the ƒ button: the library opens under it
    ExpressionText* text_ = nullptr;
    QLabel* problem_ = nullptr;
    QWidget* rowButtons_ = nullptr;
};

} // namespace ui
} // namespace bemotion
