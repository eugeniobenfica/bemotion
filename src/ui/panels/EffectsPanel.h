#pragma once

#include "core/Layer.h"

#include <string>

namespace bemotion {

class EditorContext;

// The Effects tab (next to Properties), like After Effects' Effect Controls
// plus Effects & Presets: the selected layer's effect stack with animatable
// parameters, and a searchable browser of effects by category and of effect
// presets (import / save).
class EffectsPanel {
public:
    static constexpr const char* kWindowName = "Effects";
    bool open = true;
    void draw(EditorContext& ctx);

private:
    void drawStack(EditorContext& ctx, Layer& layer);
    void drawBrowser(EditorContext& ctx);

    std::string search_;
    std::string category_; // browser page; empty: the category grid
    std::string message_;
    std::string saveName_;
};

// "Effect" menu contents (categories as submenus, then presets): applies the
// choice to the selected layers. Used by the timeline's layer menu and Layer > Effect.
void effectMenuItems(EditorContext& ctx);

} // namespace bemotion
