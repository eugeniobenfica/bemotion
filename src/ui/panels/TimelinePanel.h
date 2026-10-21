#pragma once

#include "ui/EditorContext.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mirage {

// Bottom panel: transport controls, time ruler, layer bars and keyframes.
class TimelinePanel {
public:
    static constexpr const char* kWindowName = "Timeline";
    void draw(EditorContext& ctx);

private:
    enum class Drag { None, Scrub, Keys, LayerMove, LayerIn, LayerOut, Reorder };

    void drawToolbar(EditorContext& ctx);
    void drawRuler(EditorContext& ctx, float x0, float y0, float width);
    void drawLayerRow(EditorContext& ctx, Layer& layer, int index, float x0, float width);
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

    Drag drag_ = Drag::None;
    float dragStartX_ = 0.0f;
    uint64_t dragLayer_ = 0;
    double dragOrigIn_ = 0.0, dragOrigOut_ = 0.0, dragApplied_ = 0.0;
    std::vector<std::pair<KeySelection, double>> dragKeys_;
    std::vector<std::pair<int, float>> rowTops_; // layer index -> row top (screen y), for reordering
};

} // namespace mirage
