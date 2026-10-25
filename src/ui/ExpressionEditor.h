#pragma once

// Where expressions are written (Properties, under the property; the timeline, in a small
// window): several lines, colored as they are typed, names completed (functions, the clip's
// times, other layers and their properties), a ƒ menu that inserts them (After Effects'
// expression language menu and pick whip, in one) and the problem, if it does not read.

#include <QPlainTextEdit>
#include <QWidget>

#include <cstdint>

class QCompleter;
class QLabel;
class QStringListModel;

namespace bemotion {

class EditorContext;

namespace ui {

class ExpressionText;

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
    ExpressionText* text_ = nullptr;
    QLabel* problem_ = nullptr;
    QWidget* rowButtons_ = nullptr;
};

} // namespace ui
} // namespace bemotion
