#pragma once

#include <QScrollArea>

#include <string>
#include <vector>

class QButtonGroup;

namespace bemotion {

class EditorContext;
namespace ui {
class DragValue;
class Tile;
} // namespace ui

// How clips come in and go away (core/Transitions.h): pick where (start, end
// or both) and how long, open a category, then click a transition to put it on
// the selected clips. Each one is keyframes at the clip's edges, editable
// afterwards.
class TransitionsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit TransitionsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // enabled when clips are selected

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuild();
    void apply(int index);
    EditorContext& ctx_;
    std::vector<ui::Tile*> tiles_;
    QButtonGroup* edge_ = nullptr;
    ui::DragValue* duration_ = nullptr;
    int columns_ = 0;
    int edgeChoice_ = 0; // 0 in, 1 out, 2 both
    std::string category_; // the open category ("": the list of categories)
    float seconds_ = 0.5f;
};

} // namespace bemotion
