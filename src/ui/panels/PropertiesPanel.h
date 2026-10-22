#pragma once

#include "core/Layer.h"

namespace bemotion {

class EditorContext;

// Right panel: every property of the selected layer (or the composition when
// nothing is selected), with keyframe toggles and quick animation presets.
class PropertiesPanel {
public:
    static constexpr const char* kWindowName = "Properties";
    void draw(EditorContext& ctx);

private:
    void drawComposition(EditorContext& ctx);
    void drawLayer(EditorContext& ctx, Layer& layer);
    void drawPresets(EditorContext& ctx, Layer& layer);
};

// Value editor + keyframe toggle for one animatable property. Shared with the
// timeline. Returns true when the property changed.
bool propertyRow(EditorContext& ctx, const PropertyRef& prop);

} // namespace bemotion
