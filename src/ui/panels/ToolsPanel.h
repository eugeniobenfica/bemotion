#pragma once

namespace bemotion {

class EditorContext;

// The Tools tab (next to Objects and Media): every tool and editing action
// that fits in the timeline's toolbar. Click one to use it, or drag it into
// the toolbar to keep it at hand (see ui/Tools.h).
class ToolsPanel {
public:
    static constexpr const char* kWindowName = "Tools";
    bool open = true;
    void draw(EditorContext& ctx);
};

} // namespace bemotion
