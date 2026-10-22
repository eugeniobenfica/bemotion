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
    void draw(EditorContext& ctx);

private:
    // Name drags only restack; bar drags move in time and restack vertically.
    enum class Drag { None, Keys, LayerName, LayerMove, LayerIn, LayerOut };

    void drawToolbar(EditorContext& ctx);
    void drawRuler(EditorContext& ctx, float x0, float y0, float width);
    void drawLayerRow(EditorContext& ctx, Layer& layer, int index, float x0, float width);
    // Handles a click on a layer (name or bar) honoring Ctrl/Shift multi-select.
    void clickLayer(EditorContext& ctx, uint64_t id);
    void beginLayerDrag(EditorContext& ctx, Drag mode, uint64_t id);
    void updateLayerDrag(EditorContext& ctx);
    void endLayerDrag(EditorContext& ctx);
    int dropGapAt(float y) const; // insertion gap (0..layerCount) under screen y
    void applyPendingDrop(EditorContext& ctx);
    void beginMarquee(EditorContext& ctx);
    void updateMarquee(EditorContext& ctx, ImDrawList* dl);
    void drawDropIndicator(ImDrawList* dl, float x0, float width) const;
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

    std::set<uint64_t> expanded_;
    uint64_t renaming_ = 0;
    bool focusRename_ = false;

    struct LayerRow {
        int index;    // position in the layer stack
        uint64_t id;
        float top;    // screen y of the layer row
        float bottom; // screen y below the layer row (property rows excluded)
        float barX1;  // screen x range of the layer bar
        float barX2;
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
    // Box selection by dragging on empty timeline space.
    bool marquee_ = false;
    ImVec2 marqueeStart_;
    std::vector<uint64_t> marqueeBase_;
    std::vector<std::pair<KeySelection, double>> dragKeys_;
    std::vector<LayerRow> rows_; // rebuilt every frame
    float rowsBottom_ = 0.0f;
};

} // namespace bemotion
