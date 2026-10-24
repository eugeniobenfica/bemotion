#pragma once

#include <QScrollArea>

#include <string>
#include <vector>

namespace bemotion {

class EditorContext;
namespace ui {
class DragValue;
class Tile;
} // namespace ui

// How clips come in and go away (core/Transitions.h): open a category and drag
// one onto a clip edge. Its duration and deletion are handled on the timeline.
class TransitionsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit TransitionsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // enabled when clips are selected

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuild();
    EditorContext& ctx_;
    std::vector<ui::Tile*> tiles_;
    int columns_ = 0;
    std::string category_; // the open category ("": the list of categories)
};

} // namespace bemotion
