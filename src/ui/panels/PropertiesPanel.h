#pragma once

#include "core/Layer.h"

#include <string>

namespace bemotion {

class EditorContext;

// Right panel: every property of the selected layer (or the composition when
// nothing is selected), with keyframe toggles, effects and quick animation presets.
class PropertiesPanel {
public:
    static constexpr const char* kWindowName = "Properties";
    bool open = true; // closed with the tab's x, reopened from View > Panels
    void draw(EditorContext& ctx);

private:
    void drawComposition(EditorContext& ctx);
    void drawLayer(EditorContext& ctx, Layer& layer);
    void drawEffects(EditorContext& ctx, Layer& layer);
    void drawPresets(EditorContext& ctx, Layer& layer);

    std::string saveName_;
    std::string presetMessage_;
    std::string effectMessage_;
    std::string effectSaveName_;
    std::string fontFilter_;
};

// Building blocks shared with the timeline's property rows. Each returns true
// when the property changed.
bool propertyValueWidget(EditorContext& ctx, const PropertyRef& prop, float width);
bool keyframeToggle(EditorContext& ctx, const Layer& layer, const PropertyRef& prop);
bool resetButton(EditorContext& ctx, const Layer& layer, const PropertyRef& prop);

// Full table row: keyframe toggle, label, value editor, reset button.
bool propertyRow(EditorContext& ctx, Layer& layer, const PropertyRef& prop);

} // namespace bemotion
