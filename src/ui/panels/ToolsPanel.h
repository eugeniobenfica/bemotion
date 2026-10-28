#pragma once

#include <QFrame>
#include <QScrollArea>

#include <vector>

namespace bestyle {
class Tile;
} // namespace bestyle

namespace bemotion {

class EditorContext;
namespace ui {
using bestyle::Tile;
}

// Every tool and editing action that fits in the timeline's toolbar. Click one
// to use it, or drag it into the toolbar to keep it at hand (see ui/Tools.h).
// Shown in a ToolsPopover, opened from the toolbar's "+".
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

// A floating card over the editor (not a window: tiles drag straight into the toolbar).
// Closes on Escape or a click elsewhere.
class ToolsPopover : public QFrame {
    Q_OBJECT
public:
    ToolsPopover(ToolsPanel* panel, QWidget* host);
    // Opens next to `anchor` (host coordinates): above it when there is room, else below.
    void popup(QPoint anchor);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* e) override;
    void hideEvent(QHideEvent* e) override;
};

} // namespace bemotion
