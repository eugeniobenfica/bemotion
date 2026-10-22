#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

class EditorContext;
struct MediaItem;

// The Media tab (next to Objects), like After Effects' Project panel or
// Premiere's Project bin: imported images, videos and audio files. Items are
// dragged onto the canvas or the timeline (or double-clicked) to become layers.
class MediaPanel {
public:
    static constexpr const char* kWindowName = "Media";
    bool open = true;
    void draw(EditorContext& ctx);

    // Imports files (Import button, files dropped on the window). With `addLayers`,
    // each one also becomes a layer at the playhead.
    void importFiles(EditorContext& ctx, const std::vector<std::filesystem::path>& files, bool addLayers);
    void openImportDialog(EditorContext& ctx);
    bool hovered() const { return hovered_; } // the mouse was over the panel last frame

private:
    void drawItem(EditorContext& ctx, MediaItem& item, float rowH);
    void drawItemMenu(EditorContext& ctx, MediaItem& item);
    void relink(EditorContext& ctx, MediaItem& item);
    void drawRemoveModal(EditorContext& ctx);

    std::string search_;
    std::string message_;
    uint64_t selected_ = 0;
    uint64_t removing_ = 0; // waiting for the confirmation
    bool openRemoveModal_ = false;
    bool hovered_ = false;
};

} // namespace bemotion
