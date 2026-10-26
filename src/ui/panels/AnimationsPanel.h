#pragma once

#include <QScrollArea>

#include <string>
#include <utility>
#include <vector>

class QLabel;

namespace bemotion {

class EditorContext;
struct AnimationPreset;
namespace ui {
class Tile;
} // namespace ui

// Quick Animations (core/Presets.h, app/PresetLibrary.h) by category: click one to
// animate the selected layers from the playhead, or drag it onto a clip on the
// timeline. Imported and saved presets show beside the built-in ones.
class AnimationsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit AnimationsPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // dims what the selection cannot take

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuild();
    std::string appliedKey(); // what the "on this clip" list shows (rebuilt when it changes)
    EditorContext& ctx_;
    std::string appliedKey_;
    std::vector<std::pair<ui::Tile*, std::string>> tiles_; // with the preset's name
    QLabel* message_ = nullptr;
    std::string messageText_;
    size_t presetCount_ = 0;
    int columns_ = 0;
};

} // namespace bemotion
