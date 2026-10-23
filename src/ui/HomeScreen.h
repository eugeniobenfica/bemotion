#pragma once

#include "app/RecentProjects.h"
#include "core/Project.h"

#include <QImage>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

class QFileSystemWatcher;
class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace bemotion {

// Start screen: the projects grid (every project in the projects folder plus the
// ones opened from elsewhere) and the "New Project" dialog.
class HomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit HomeScreen(RecentProjects& recents, QWidget* parent = nullptr);

    // Set by the application.
    std::function<void(const std::filesystem::path&)> onOpen;
    std::function<void(const std::filesystem::path&, Project)> onCreate;

    // Reloads project previews (call when the screen becomes visible).
    void refresh();
    void setError(const std::string& message);
    void openNewProjectDialog();

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    struct Preview {
        std::optional<Project> project;
        QImage thumbnail;
        int64_t stamp = 0; // file time when loaded
    };
    void buildUi();
    void rebuildGrid();
    void confirmDelete(const RecentProject& item);

    RecentProjects& recents_;
    std::vector<RecentProject> entries_; // shown, newest first (lastOpened: opened or saved, whichever is later)
    std::map<std::filesystem::path, Preview> previews_;
    QWidget* content_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* error_ = nullptr;
    QWidget* errorRow_ = nullptr;
    QLabel* count_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* grid_ = nullptr;
    int columns_ = 0;
};

} // namespace bemotion
