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
    bool showRulers = false; // from Settings::showRulers
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
    void pruneSelection(); // drops layers that no longer exist
    void clearKeySelection() { selectedKeys.clear(); }
    bool isKeySelected(const KeySelection& k) const;

    void setTime(double t);
    void stepFrames(int frames);
    void tick(double dt); // advances playback

    // Marks the document as modified; recorded as an undo step when idle.
    void touch() {
        pendingCommit_ = true;
        ++revision_;
    }
    // Changes whenever the document changes (touch, undo, redo, open): views compare it to know
    // when to redraw.
    uint64_t revision() const { return revision_; }
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
    Layer& addCounter(std::optional<Vec2> position = std::nullopt);
    Layer& addSubtitles(); // at the bottom of the frame, over the whole composition
    Layer& addCamera();
    Layer& addBackground(); // under everything, over the whole composition
    Layer& addPluginObject(const struct PluginObjectType& type, std::optional<Vec2> position = {});
    void deleteSelected();
    void duplicateSelected();
    // Copy / paste (core/Clipboard.h): the selection as text; pasted layers start at the
    // playhead, above the selection, and become the selection. False when nothing was pasted.
    std::string copySelected();
    bool paste(const std::string& text);
    // Compounds: group the selection (Ctrl+G) / put selected compounds' contents back (Ctrl+Shift+G).
    void createCompound();
    // The compound open in the timeline (0: the whole composition). New layers go
    // inside it; the timeline shows only its contents; clicks pick inside it.
    uint64_t scope = 0;
    void enterScope(uint64_t compoundId);
    void exitScope(); // one level up
    void releaseSelectedCompounds();
    // A layer for an imported media item (at the center / playhead unless given).
    Layer* addMedia(uint64_t mediaId, std::optional<Vec2> position = {}, std::optional<double> start = {});
    // Adds an object of the Objects panel (see kObjectPayload) at a scene position, starting at `start`.
    Layer* addObject(int kind, Vec2 position, double start);
    bool selectionHasCompound();
    // The selection plus everything inside selected compounds (what moves when they move in time).
    std::vector<uint64_t> selectionWithContents();
    std::vector<uint64_t> selectedTracks(); // tracks holding selected clips, top first
    void moveSelectedTracksTo(int targetRow);        // whole tracks, see Composition::moveTracks
    void moveSelectedClipsToNewTrack(int gapRow);    // clips onto a new track at a row gap
    bool moveSelectedClipsToTrack(uint64_t trackId); // false if they would overlap other clips
    void shiftSelected(int delta);                   // stacking order of tracks: negative = forward
    void splitSelectedAtPlayhead();        // splits all selected (or all) layers under the playhead
    void splitLayerAt(uint64_t id, double t);
    void deleteSelectedKeys();
    void setSelectedKeysEasing(Easing easing);
    void setSelectedKeysCurve(const BezierEase& curve); // also switches them to Easing::Custom
    void setSelectedKeysPath(PathMode mode);            // position-like (Vec2) keys only
    bool selectionHasPathKeys();
    BezierEase selectedKeysCurve(); // the first selected key's timing as a cubic-bezier

private:
    template <typename Fn>
    void forEachSelectedKey(Fn&& fn); // fn(PropertyRef&, keyIndex)
    Layer& insertLayer(Layer layer);
    bool pendingCommit_ = false;
    uint64_t revision_ = 0;
};

} // namespace bemotion
