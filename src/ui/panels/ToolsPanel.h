#pragma once

#include <QScrollArea>

#include <vector>

namespace bemotion {

class EditorContext;
namespace ui {
class Tile;
}

// The Tools tab (next to Objects and Media): every tool and editing action
// that fits in the timeline's toolbar. Click one to use it, or drag it into
// the toolbar to keep it at hand (see ui/Tools.h).
class ToolsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ToolsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // enabled / active / in-toolbar marks

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuild();
    EditorContext& ctx_;
    std::vector<ui::Tile*> tiles_; // by catalog index
    int columns_ = 0;
};

} // namespace bemotion
