#pragma once

#include <QScrollArea>

class QVBoxLayout;

#include <string>
#include <vector>

namespace bemotion {

class EditorContext;
struct Layer;
namespace ui {
class DragValue;
class Tile;
} // namespace ui

// How clips come in and go away (core/Transitions.h). On top, what the selected clip has (its
// in and out transitions, their durations, a way to take them off); below, the categories:
// click one for the selected clips, or drag it onto a clip edge.
class TransitionsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit TransitionsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // enabled when clips are selected

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuild();
    void applyToSelection(const std::string& id); // a click on one: the selected clips' edges and cuts
    void buildApplied(QVBoxLayout* col); // the selected clip's transitions
    std::string selectionKey() const;
    std::vector<Layer*> shownLayers() const; // the selected clips, or the picked transition's clip          // what the top part shows (rebuilt when it changes)
    EditorContext& ctx_;
    std::vector<ui::Tile*> tiles_;
    std::vector<std::string> tileIds_; // a category name or a transition id, per tile
    std::string shownKey_;
    ui::DragValue* durations_[2] = {}; // in, out (null: none shown)
    int columns_ = 0;
    std::string category_; // the open category ("": the list of categories)
};

} // namespace bemotion
