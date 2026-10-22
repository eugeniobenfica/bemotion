#pragma once

// State shared by every editor panel: the open project, selection, playhead
// and undo history. Panels read and mutate the project directly and call
// touch() after a change; the change becomes an undo step once the user
// releases the mouse / finishes typing (see commitIfIdle).

#include "core/History.h"
#include "core/Project.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bemotion {

struct KeySelection {
    uint64_t layerId = 0;
    std::string propertyId;
    uint32_t keyUid = 0;
    bool operator==(const KeySelection&) const = default;
};

// Active editing tool. Razor cuts layers where you click in the timeline.
enum class Tool { Select, Razor };

class EditorContext {
public:
    Project project;
    std::filesystem::path path; // empty for never-saved projects
    History history;

    // Selected layer ids in click order; the last one is the "primary" layer
    // shown in the Properties panel. Keyframe selection belongs to it.
    std::vector<uint64_t> selection;
    std::vector<KeySelection> selectedKeys;

    Tool tool = Tool::Select;
    bool snapping = true;     // magnet: objects, clips and keyframes snap (hold Ctrl to bypass)
    bool showGuides = true;
    bool lockGuides = false;
    bool showRulers = true;
    double time = 0.0;
    bool playing = false;
    bool loop = true;

    void open(Project p, std::filesystem::path file);

    Composition& comp() { return project.comp; }

    // --- selection ------------------------------------------------------------
    uint64_t primaryId() const { return selection.empty() ? 0 : selection.back(); }
    Layer* selected() { return project.comp.findLayer(primaryId()); }
    bool isSelected(uint64_t layerId) const;
    std::vector<Layer*> selectedLayers(); // in stack order (top first)

    void select(uint64_t layerId); // replaces the selection; 0 clears it
    void toggleSelected(uint64_t layerId);
    void selectRange(uint64_t layerId); // from the primary layer to `layerId` in the stack
    void selectAll();
    void clearKeySelection() { selectedKeys.clear(); }
    bool isKeySelected(const KeySelection& k) const;

    void setTime(double t);
    void stepFrames(int frames);
    void tick(double dt); // advances playback

    // Marks the document as modified; recorded as an undo step when idle.
    void touch() { pendingCommit_ = true; }
    void commitIfIdle(bool interacting);

    void undo();
    void redo();
    bool isDirty() const { return history.isDirty() || pendingCommit_; }

    bool save(std::string* error = nullptr);
    bool saveTo(const std::filesystem::path& file, std::string* error = nullptr);

    // --- document operations ------------------------------------------------
    // New objects are centered unless a composition position is given.
    Layer& addShape(ShapeKind kind, std::optional<Vec2> position = std::nullopt);
    Layer& addText(std::optional<Vec2> position = std::nullopt);
    void deleteSelected();
    void duplicateSelected();
    std::vector<uint64_t> selectedTracks(); // tracks holding selected clips, top first
    void moveSelectedTracksTo(int targetRow);        // whole tracks, see Composition::moveTracks
    void moveSelectedClipsToNewTrack(int gapRow);    // clips onto a new track at a row gap
    bool moveSelectedClipsToTrack(uint64_t trackId); // false if they would overlap other clips
    void shiftSelected(int delta);                   // stacking order of tracks: negative = forward
    void splitSelectedAtPlayhead();        // splits all selected (or all) layers under the playhead
    void splitLayerAt(uint64_t id, double t);
    void deleteSelectedKeys();
    void setSelectedKeysEasing(Easing easing);

private:
    Layer& insertLayer(Layer layer);
    void pruneSelection();
    bool pendingCommit_ = false;
};

} // namespace bemotion
