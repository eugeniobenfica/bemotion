#pragma once

#include "app/RecentProjects.h"
#include "core/Project.h"
#include "export/Export.h"

#include <QMainWindow>
#include <QObject>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QStackedWidget;

namespace bemotion {

class DevScript;
class EditorWindow;
class HomeScreen;
class IntroScreen;

// The application window: the home screen or the editor, with the intro on top
// while it plays. Asks before closing a project with unsaved changes and takes
// files dropped from the file manager.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    std::function<bool()> canClose;                                         // false: stay open
    std::function<void(const std::vector<std::filesystem::path>&)> onDrop; // files from the file manager
    QWidget* overlay = nullptr;                                             // kept over the whole window

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

// Owns the window and switches between the home screen and the editor.
class Application : public QObject {
    Q_OBJECT
public:
    Application();
    ~Application() override;

    int run(int argc, char** argv);

private:
    int runCommandLineExport(const std::filesystem::path& projectPath, exporting::Settings settings);
    void initShared(); // settings, fonts, plugins, media (GUI and command line)
    void buildWindow();
    void frame(); // developer aids, once per ~16 ms
    void scriptStep();

    void openProject(const std::filesystem::path& path);
    void createProject(Project project, const std::filesystem::path& path);
    void showEditor(Project project, const std::filesystem::path& path);
    void goHome();
    void updateTitle();
    void offerRecovery(); // projects a crash left unsaved (app/Recovery.h)

    RecentProjects recents_;
    MainWindow* window_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    HomeScreen* home_ = nullptr;
    IntroScreen* intro_ = nullptr;
    EditorWindow* editor_ = nullptr;
    std::unique_ptr<DevScript> script_; // BEMOTION_SCRIPT developer aid
    int frameIndex_ = 0;
};

} // namespace bemotion
