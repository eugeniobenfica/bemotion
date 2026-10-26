#pragma once

#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace bemotion {

class EditorContext;
struct Layer;
namespace ui {
class DragValue;
class Tile;
} // namespace ui

// Animate: everything that moves a clip for you, as one thing. Transitions (core/Transitions.h:
// at a clip's start and end, or across the cut between two clips) and Quick Animations
// (core/Presets.h, app/PresetLibrary.h: from the playhead) are both keyframes made for you; they
// share one list, one search, the same tiles. On top, what the clip has, in time order (its
// length, a way to take it off); click a tile for the selected clips or drag it onto a clip.
class AnimationsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnimationsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // the clip's list, dims what the selection cannot take

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    struct TileInfo {
        ui::Tile* tile;
        std::string id;     // a transition's id, or a preset's name
        bool transition;
    };
    void rebuild();
    void buildApplied(QVBoxLayout* col); // what the clip has
    void applyTransition(const std::string& id); // a click on one: the selected clips' edges and cuts
    std::vector<Layer*> shownLayers() const;     // the selected clips, or the picked one's clip
    std::string appliedKey() const;              // what the top part shows (rebuilt when it changes)
    EditorContext& ctx_;
    QLineEdit* search_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    std::string appliedKey_;
    std::vector<TileInfo> tiles_;
    std::vector<std::pair<ui::DragValue*, int>> lengths_; // shown lengths: -1 in, -2 out, else an animation's index
    QLabel* message_ = nullptr;
    std::string messageText_;
    size_t presetCount_ = 0;
    int columns_ = 0;
};

} // namespace bemotion
