#pragma once

#include "app/RecentProjects.h"
#include "export/Export.h"
#include "ui/HomeScreen.h"

#include <filesystem>
#include <memory>
#include <string>

struct GLFWwindow;

namespace bemotion {

class EditorScreen;
class DevScript;

// Owns the OS window and the ImGui context, and switches between the home
// screen and the editor.
class Application {
public:
    Application();
    ~Application();

    int run(int argc, char** argv);

private:
    bool init(bool visible);
    int runCommandLineExport(const std::filesystem::path& projectPath, exporting::Settings settings);
    void shutdown();
    void frame(double dt);

    void openProject(const std::filesystem::path& path);
    void createProject(Project project, const std::filesystem::path& path);
    void goHome();
    void updateTitle();

    static void onWindowClose(GLFWwindow* window);

    GLFWwindow* window_ = nullptr;
    int frameIndex_ = 0;
    std::string iniPath_;
    std::string title_;
    RecentProjects recents_;
    HomeScreen home_;
    std::unique_ptr<EditorScreen> editor_;
    std::unique_ptr<DevScript> script_; // BEMOTION_SCRIPT developer aid
};

} // namespace bemotion
