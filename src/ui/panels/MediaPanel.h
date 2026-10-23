#pragma once

#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace bemotion {

class EditorContext;
class MediaList;

// The Media tab (next to Objects), like After Effects' Project panel or
// Premiere's Project bin: imported images, videos and audio files. Items are
// dragged onto the canvas or the timeline (or double-clicked) to become layers.
class MediaPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync();

    // Imports files (Import button, files dropped on the window). With `addLayers`,
    // each one also becomes a layer at the playhead.
    void importFiles(const std::vector<std::filesystem::path>& files, bool addLayers);
    void openImportDialog();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void showMessage(const std::string& text);

    EditorContext& ctx_;
    QLineEdit* search_ = nullptr;
    QLabel* message_ = nullptr;
    QWidget* messageRow_ = nullptr;
    QWidget* empty_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    MediaList* list_ = nullptr;
};

} // namespace bemotion
