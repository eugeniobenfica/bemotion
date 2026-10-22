#pragma once

#include "ui/EditorContext.h"

#include <imgui.h>

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bemotion {

// Bottom panel: transport controls, time ruler, layer bars and keyframes.
class TimelinePanel {
public:
    static constexpr const char* kWindowName = "Timeline";
    bool open = true; // closed with the tab's x, reopened from View > Panels
    void draw(EditorContext& ctx);

private:
    // Each timeline row is a track that may hold several clips (layers).
    //   LayerName  drag the row name: reorders whole tracks
    //   LayerMove  drag a clip: moves it in time and, vertically, onto another
    //              track (drop in a row) or onto a new track (drop between rows)
    //   LayerIn/Out  trim a clip edge
    enum class Drag { None, Keys, LayerName, LayerMove, LayerIn, LayerOut };

    // Where a vertical drag would land.
    struct DropTarget {
        bool valid = false;
        bool newTrack = true; // true: new track at `gap`; false: join `track`
        int gap = 0;
        uint64_t track = 0;
        bool fits = true;     // joining is only possible without overlaps
    };

    void drawToolbar(EditorContext& ctx);
    void drawRuler(EditorContext& ctx, float x0, float y0, float width);
    void drawTrackRow(EditorContext& ctx, const TrackRow& row, int rowIndex, float x0, float width);
    // Handles a click on a clip honoring Ctrl/Shift multi-select.
    void clickLayer(EditorContext& ctx, uint64_t id);
    // Handles a click on a track name: selects all its clips.
    void clickTrack(EditorContext& ctx, const TrackRow& row);
    void beginLayerDrag(EditorContext& ctx, Drag mode, uint64_t id);
    void updateLayerDrag(EditorContext& ctx);
    void endLayerDrag(EditorContext& ctx);
    int dropGapAt(float y) const; // insertion gap (0..rowCount) under screen y
    DropTarget dropTargetAt(EditorContext& ctx, float y, Drag mode) const;
    void applyPendingDrop(EditorContext& ctx);
    void beginMarquee(EditorContext& ctx);
    void updateMarquee(EditorContext& ctx, ImDrawList* dl);
    void drawDropIndicator(ImDrawList* dl, const DropTarget& target, float x0, float width) const;
    // Collapsible "Transform" / "Shape" / ... header; returns whether it is open.
    bool drawGroupRow(uint64_t track, const char* group, float x0, float width, float indent = 0.0f);
    void drawPropertyRow(EditorContext& ctx, Layer& layer, const PropertyRef& prop, float x0, float width);
    void drawKeyContextMenu(EditorContext& ctx);

    float timeToX(double t) const { return trackX0_ + static_cast<float>((t - scroll_) * pxPerSec_); }
    double xToTime(float x) const { return scroll_ + (x - trackX0_) / pxPerSec_; }

    void beginKeyDrag(EditorContext& ctx);
    void updateKeyDrag(EditorContext& ctx);
    void endKeyDrag(EditorContext& ctx);

    float leftWidth_ = 300.0f;
    double pxPerSec_ = 0.0; // 0 = fit on next frame
    double scroll_ = 0.0;   // time at the left edge of the track area
    float trackX0_ = 0.0f;
    float trackW_ = 0.0f;
    bool animatedOnly_ = false;
    bool openKeyMenu_ = false;
    BezierEase curveEdit_; // curve being edited in the key menu

    std::set<uint64_t> expanded_; // track ids showing their property rows
    std::set<std::string> collapsedGroups_; // "track/group"
    uint64_t renaming_ = 0;       // layer id being renamed
    bool focusRename_ = false;

    struct RowRect {
        int index;      // row (track) position, 0 = top
        uint64_t track;
        float top;      // screen y of the track row
        float bottom;   // screen y below it (property rows excluded)
    };
    struct ClipRect {
        uint64_t id;
        float top, bottom, x1, x2;
    };

    Drag drag_ = Drag::None;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    uint64_t dragLayer_ = 0;
    double dragOrigIn_ = 0.0, dragOrigOut_ = 0.0, dragApplied_ = 0.0;
    bool restacking_ = false; // vertical drag past the threshold: show the drop gap
    // A restack is applied after every row is laid out, so drops below the
    // dragged layer see the rows that come after it.
    bool pendingDrop_ = false;
    float pendingDropY_ = 0.0f;
    Drag pendingDropMode_ = Drag::None;
    // Box selection by dragging on empty timeline space.
    bool marquee_ = false;
    ImVec2 marqueeStart_;
    std::vector<uint64_t> marqueeBase_;
    std::vector<std::pair<KeySelection, double>> dragKeys_;
    // Snapping: targets are gathered when a drag starts; snapLineTime_ marks
    // the time that snapped this frame (drawn as a pink line), or -1.
    void collectTimeSnapTargets(EditorContext& ctx, const std::vector<uint64_t>& movingClips, bool movingKeys);
    // Moves `base` times by `rawDelta`, snapped to a target if one is close, else to whole frames.
    double snappedDelta(EditorContext& ctx, const std::vector<double>& base, double rawDelta);
    std::vector<double> timeTargets_;
    double snapLineTime_ = -1.0;
    double playheadGrab_ = 0.0; // playhead time minus the time under the mouse when it was grabbed
    struct ClipStart {
        uint64_t id;
        double in, out;
    };
    std::vector<ClipStart> dragClips_;

    std::vector<RowRect> rows_;   // rebuilt every frame
    std::vector<ClipRect> clips_; // rebuilt every frame
    float rowsBottom_ = 0.0f;

    // --- graph editor ------------------------------------------------------------
    // Value curves of the primary layer's animated properties, like After
    // Effects' Graph Editor: drag keyframes (time / value) and the easing
    // handles of each segment (they edit its cubic-bezier timing curve).
    // Implemented in TimelineGraph.cpp.
public:
    enum class Ease { Linear, EasyEase, EaseIn, EaseOut, Hold };
    // Applies an easing to the selected keyframes (F9 = Easy Ease, like After Effects).
    static void applyEase(EditorContext& ctx, Ease ease);

private:
    void drawGraphEditor(EditorContext& ctx, float x0, float top, float height);
    bool graphMode_ = false;
    std::set<std::string> graphHidden_; // property ids switched off in the graph
    float graphMin_ = 0.0f, graphMax_ = 1.0f; // value range on screen
    struct GraphDrag {
        enum Kind { None, Key, Handle } kind = None;
        std::string prop;
        int comp = 0;        // component (x / y) being dragged
        uint32_t uid = 0;    // key (Key) or segment start key (Handle)
        int which = 0;       // Handle: 1 = outgoing (x1, y1), 2 = incoming (x2, y2)
        ImVec2 startMouse;
        double startTime = 0.0; // Key: where the key was when grabbed
        float startValue = 0.0f;
        bool moved = false;     // past the drag threshold (a plain click changes nothing)
    } graphDrag_;
};

} // namespace bemotion
