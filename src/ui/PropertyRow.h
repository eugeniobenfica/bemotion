#pragma once

// Editors shared by the Properties and Effects panels: a row per animatable
// property (keyframe toggle, label, value, reset, right-click menu) and
// collapsible sections. Rows find their property again by layer id and
// property id every time, so they never hold pointers into the document.

#include "core/Layer.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QGridLayout;
class QLabel;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

namespace bmstyle {
class IconButton;
class DragValue;
} // namespace bmstyle

namespace bemotion {

class EditorContext;
namespace ui {
class ColorButton;
using bmstyle::DragValue;
using bmstyle::IconButton;
class KeyframeButton;
class ExpressionEditor;
}

// The property `id` of layer `layerId` (effect parameters: `effectIndex` >= 0).
std::optional<PropertyRef> findProperty(EditorContext& ctx, uint64_t layerId, const std::string& id, int effectIndex);

// A titled group whose body folds away (the open / closed state is remembered per title).
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr, QWidget* headerExtra = nullptr);
    QVBoxLayout* body() const { return body_; }
    // A 4-column grid (key, label, value, reset) for property rows, created on first use.
    QGridLayout* grid();
    void addPlainRow(const QString& label, QWidget* editor); // non-animatable setting

private:
    QToolButton* header_;
    QWidget* content_;
    QVBoxLayout* body_;
    QGridLayout* grid_ = nullptr;
    int gridRows_ = 0;
    friend class PropertyRow;
};

class PropertyRow : public QObject {
    Q_OBJECT
public:
    // Adds its widgets as one row of `section`'s grid.
    PropertyRow(EditorContext& ctx, uint64_t layerId, const PropertyRef& prop, CollapsibleSection* section);
    // Compact row (key, label, value, reset) in a widget of its own on `host`, placed by the caller
    // (the timeline's property rows). The widget goes away with the row.
    PropertyRow(EditorContext& ctx, uint64_t layerId, const PropertyRef& prop, QWidget* host);
    ~PropertyRow() override;
    QWidget* widget() const { return container_; } // host mode only
    void refresh(); // value, keyframe state and reset from the document

private:
    // Creates the key toggle, value editors and reset button; returns the box holding the values.
    QWidget* buildEditors(const PropertyRef& prop);
    std::optional<PropertyRef> prop();
    void setValue(const std::vector<float>& v);
    void showMenu(QPoint at);
    void openExpression(const QString& text); // shows the editor with `text` (applied when it reads)
    void applyExpression(const QString& text);
    void closeExpression();  // hides the editor (the expression stays)
    void removeExpression(); // no expression any more
    void bakeExpression();   // the expression becomes keyframes (expression.bake)

    EditorContext& ctx_;
    uint64_t layerId_;
    std::string id_;
    int effectIndex_;
    PropertyKind kind_;
    ui::KeyframeButton* key_ = nullptr;
    // The timeline's rows (host mode): the property's previous / next keyframe on either side of
    // its diamond, as After Effects has them; shown while it is animated.
    ui::IconButton* prevKey_ = nullptr;
    ui::IconButton* nextKey_ = nullptr;
    void stepToKey(int direction);
    ui::IconButton* fx_ = nullptr; // opens / closes the expression
    QLabel* label_ = nullptr;
    ui::DragValue* x_ = nullptr;
    ui::DragValue* y_ = nullptr;
    ui::ColorButton* color_ = nullptr;
    QLabel* hex_ = nullptr;
    ui::IconButton* reset_ = nullptr;
    QWidget* container_ = nullptr;
    QString labelText_;
    // The expression (core/Expressions.h): an editor under the row in Properties, opened by
    // fx_ (or Alt+click on the diamond) and closed by it, its own buttons or Esc.
    QWidget* exprBox_ = nullptr;
    ui::ExpressionEditor* expr_ = nullptr;
    bool exprOpen_ = false;
};

} // namespace bemotion
