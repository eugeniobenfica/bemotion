#pragma once

#include "core/Layer.h"

#include <QScrollArea>

#include <functional>
#include <string>
#include <vector>

class QLineEdit;
class QMenu;
class QVBoxLayout;

namespace bemotion {

class EditorContext;
class PropertyRow;

// The Effects tab (next to Properties), like After Effects' Effect Controls
// plus Effects & Presets: the selected layer's effect stack with animatable
// parameters, and a searchable browser of effects by category and of effect
// presets (import / save).
class EffectsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit EffectsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync();

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    std::string stackKey();
    std::string browserKey();
    void rebuildStack();
    void rebuildBrowser();

    EditorContext& ctx_;
    QVBoxLayout* stackHost_ = nullptr;
    QVBoxLayout* browserHost_ = nullptr;
    QWidget* stack_ = nullptr;
    QWidget* browser_ = nullptr;
    QLineEdit* search_ = nullptr;
    std::string stackKey_, browserKey_;
    std::vector<PropertyRow*> rows_;
    std::vector<std::function<void()>> refreshers_;
    std::string category_; // browser page; empty: the category grid
    std::string message_;
};

// "Effect" menu contents (categories as submenus, then presets): applies the
// choice to the selected layers. Used by the timeline's layer menu and Layer > Effect.
void fillEffectMenu(QMenu* menu, EditorContext& ctx);

} // namespace bemotion
