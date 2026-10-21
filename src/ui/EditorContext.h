#pragma once

// State shared by every editor panel: the open project, selection, playhead
// and undo history. Panels read and mutate the project directly and call
// touch() after a change; the change becomes an undo step once the user
// releases the mouse / finishes typing (see commitIfIdle).

#include "core/History.h"
#include "core/Project.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mirage {

struct KeySelection {
    uint64_t layerId = 0;
    std::string propertyId;
    uint32_t keyUid = 0;
    bool operator==(const KeySelection&) const = default;
};

class EditorContext {
public:
    Project project;
    std::filesystem::path path; // empty for never-saved projects
    History history;

    uint64_t selectedLayer = 0;
    std::vector<KeySelection> selectedKeys;

    double time = 0.0;
    bool playing = false;
    bool loop = true;

    void open(Project p, std::filesystem::path file);

    Composition& comp() { return project.comp; }
    Layer* selected() { return project.comp.findLayer(selectedLayer); }

    void select(uint64_t layerId);
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
    Layer& addShape(ShapeKind kind);
    Layer& addText();
    void deleteSelected();
    void duplicateSelected();
    void moveLayer(uint64_t id, int newIndex);
    void deleteSelectedKeys();
    void setSelectedKeysEasing(Easing easing);

private:
    Layer& insertLayer(Layer layer);
    bool pendingCommit_ = false;
};

} // namespace mirage
