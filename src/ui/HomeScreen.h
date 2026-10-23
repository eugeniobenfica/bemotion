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

class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace bemotion {

// Start screen: recent projects grid and the "New Project" dialog.
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

private:
    struct Preview {
        std::optional<Project> project;
        QImage thumbnail;
    };
    void rebuildGrid();
    void confirmDelete(const RecentProject& item);

    RecentProjects& recents_;
    std::map<std::filesystem::path, Preview> previews_;
    QLineEdit* search_ = nullptr;
    QLabel* error_ = nullptr;
    QWidget* errorRow_ = nullptr;
    QLabel* count_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* grid_ = nullptr;
    int columns_ = 0;
};

} // namespace bemotion
