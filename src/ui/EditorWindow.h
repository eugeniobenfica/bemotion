#pragma once

// The editing workspace: menus, dockable panels (Qt Advanced Docking System:
// drag tabs to rearrange, pin a panel to auto-hide it on the window edge,
// double-click a tab to maximize), keyboard shortcuts and the update loop
// (playback, media, audio, undo commits).

#include "ui/EditorContext.h"

#include <QElapsedTimer>
#include <QMainWindow>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

class QAction;
class QLabel;
class QMenu;
class QTimer;
namespace ads {
class CDockManager;
class CDockWidget;
}

namespace bemotion {

class RecentProjects;
class ObjectsPanel;
class MediaPanel;
class ToolsPanel;
class ViewportPanel;
class PropertiesPanel;
class EffectsPanel;
class TimelinePanel;
namespace ui {
enum class Action;
}

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    EditorWindow(Project project, std::filesystem::path path, RecentProjects& recents, QWidget* parent = nullptr);
    ~EditorWindow() override;

    const Project& project() const { return ctx_.project; }
    EditorContext& context() { return ctx_; }
    QString title() const;

    // Files dropped on the window: imported into the Media panel and added at the playhead
    // (only imported when dropped on the Media panel).
    void dropFiles(const std::vector<std::filesystem::path>& files, bool onMediaPanel);
    // Asks to save unsaved changes; false when the user cancels.
    bool confirmClose();
    void applyShortcuts(); // after the keymap changed

signals:
    void goHome();
    void quitRequested();
    void titleChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenus();
    void buildDocks();
    void tick();
    void save();
    void saveAs();
    void jumpToKeyframe(int direction);
    void runPluginCommand(const std::string& id);
    void resetLayout();
    void maximizeDock(ads::CDockWidget* dock);
    QAction* action(ui::Action a, const QString& text, std::function<void()> fn);

    EditorContext ctx_;
    RecentProjects& recents_;

    ads::CDockManager* docks_ = nullptr;
    QByteArray defaultLayout_;
    QByteArray beforeMaximize_; // restored by the next maximize toggle
    ObjectsPanel* objects_ = nullptr;
    MediaPanel* media_ = nullptr;
    ToolsPanel* tools_ = nullptr;
    ViewportPanel* viewport_ = nullptr;
    PropertiesPanel* properties_ = nullptr;
    EffectsPanel* effects_ = nullptr;
    TimelinePanel* timeline_ = nullptr;
    std::map<std::string, ads::CDockWidget*> dockWidgets_;

    std::map<ui::Action, QAction*> actions_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    QLabel* status_ = nullptr; // errors next to the menus
    QString lastTitle_;
    bool spaceDown_ = false;
};

} // namespace bemotion
