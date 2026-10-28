#pragma once

#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace bmstyle {
class DragValue;
class Tile;
} // namespace bmstyle

namespace bemotion {

class EditorContext;
struct AnimationPreset;
struct Layer;
namespace ui {
using bmstyle::DragValue;
using bmstyle::Tile;
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

private:
    struct TileInfo {
        ui::Tile* tile;
        std::string id;     // a transition's id, or a preset's name
        bool transition;
    };
    void rebuild();
    void buildApplied(QVBoxLayout* col); // what the clip has
    // Puts one on each selected clip: at `spot` (a MotionSpot), or (-1) where the playhead says.
    void placeOnSelection(const std::string& transition, const AnimationPreset* preset, int spot);
    QWidget* folderHeader(const std::string& folder, const QString& name, int count, bool closed, bool userFolder);
    void moveToFolder(const std::string& key, const std::string& folder); // "": back to its own
    void newFolder(const std::string& moveHere);
    static std::vector<std::string> builtInFolders();
    static bool isBuiltInFolder(const std::string& folder);
    static std::string defaultFolder(const AnimationPreset& preset);
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
    std::string dimmedFor_; // the clips the tiles were last dimmed and marked for (appliedKey)
    size_t dimmedTiles_ = 0;
};

} // namespace bemotion
