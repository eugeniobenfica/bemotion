#pragma once

// Tool Settings: the tool in use, whichever it is (its name, shortcut and what it does), and its
// options (Selection's snapping, Brush, Eraser, Puppet, Warp, Track). Kept in Settings::tools
// between sessions; the viewport reads them. The sound fixes live in a clip's Properties.

#include <QScrollArea>

#include <functional>
#include <string>
#include <vector>

namespace bemotion {

class EditorContext;

class ToolSettingsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ToolSettingsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync();

private:
    void rebuild();
    std::string structureKey() const;
    EditorContext& ctx_;
    std::string key_;
    std::vector<std::function<void()>> refreshers_;
};

} // namespace bemotion
