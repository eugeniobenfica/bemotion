#pragma once

#include <QScrollArea>

namespace bemotion {

class EditorContext;

// Left panel: the library of objects that can be added to the composition.
// Click a tile to add it at the center, or drag it onto the canvas or the timeline.
class ObjectsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ObjectsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // rebuilds when plugins change

private:
    void rebuild();
    EditorContext& ctx_;
    QWidget* content_ = nullptr;
    size_t pluginTypes_ = 0;
};

} // namespace bemotion
