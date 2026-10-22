#pragma once

namespace bemotion {

class EditorContext;

// Left panel: the library of objects that can be added to the composition.
class ObjectsPanel {
public:
    static constexpr const char* kWindowName = "Objects";
    void draw(EditorContext& ctx);
};

} // namespace bemotion
