#pragma once

#include "app/RecentProjects.h"
#include "core/Project.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace bemotion {

// Start screen: recent projects grid and the "New Project" dialog.
class HomeScreen {
public:
    struct Result {
        enum class Action { None, Open, Create } action = Action::None;
        std::filesystem::path path;
        Project project; // for Create
    };

    // Reloads project previews (call when the screen becomes visible).
    void refresh(const RecentProjects& recents);

    Result draw(RecentProjects& recents);
    void setError(std::string message) { error_ = std::move(message); }

private:
    void drawSidebar(Result& result, RecentProjects& recents, float width);
    void drawProjects(Result& result, RecentProjects& recents);
    void drawNewProjectModal(Result& result);
    void openNewProjectModal();

    std::map<std::filesystem::path, std::optional<Project>> previews_;
    std::string search_;
    std::string error_;

    // New project form.
    bool openModal_ = false;
    std::string newName_;
    int preset_ = 0;
    int width_ = 1920, height_ = 1080;
    int fpsIndex_ = 2;
    float duration_ = 10.0f;
    Color background_ = Color::fromHex(0x101012);
    std::filesystem::path folder_;
};

} // namespace bemotion
