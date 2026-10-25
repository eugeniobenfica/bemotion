#pragma once

// The editing workspace: menus, dockable panels (Qt Advanced Docking System:
// drag tabs to rearrange, pin a panel to auto-hide it on the window edge,
// double-click a tab to maximize), keyboard shortcuts and the update loop
// (playback, media, audio, undo commits).

#include "ui/EditorContext.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QPointF>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QTimer;
namespace ads {
class CDockAreaWidget;
class CDockManager;
class CDockWidget;
}

namespace bemotion {

class RecentProjects;
class ObjectsPanel;
class MediaPanel;
class ToolsPanel;
class TransitionsPanel;
class AnimationsPanel;
class ToolsPopover;
class ViewportPanel;
class PropertiesPanel;
class EffectsPanel;
class TimelinePanel;
namespace ui {
class PreviewWindow;
class IconButton;
class AudioMeter;
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
    // Opened from a crash-recovery copy: the changes count as unsaved.
    void markRecovered();

signals:
    void goHome();
    void newProjectRequested();                            // after the user agreed to close this one
    void openProjectRequested(std::filesystem::path path); // likewise
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
    void runPluginCommand(const std::string& id, bool pressed = false);
    // A Lua script (the plugin `doc` API) on the open project, as one undo step.
    void runScriptFile(const std::filesystem::path& file);
    void arrange(bool stagger, bool vertical = false);
    void setStatus(const QString& text); // errors and notes next to the menus (empty clears)
    bool copyToClipboard();
    void pasteFromClipboard();
    void keepRecoveryCopy();
    void autosave();
    void openPreview(); // full screen, only the picture, playing
    void exportFrame(bool toClipboard = false); // the frame at the playhead, as a PNG
    void resetLayout();
    void maximizeDock(ads::CDockWidget* dock);
    void toggleViewportFullscreen();
    void minimizeArea(ads::CDockAreaWidget* area);
    void restoreMinimized(size_t index);
    void showTools();
    void placeToolbar(); // timeline, free viewport position, or a window edge
    QRect toolbarBounds() const;
    void dockToolbar(QPointF position);
    // Moving the toolbar by its grip: the places it can go, shown over the window.
    void toolbarDragStarted(QPoint grab);
    void toolbarDragMoved(QPoint global);
    void toolbarDragFinished(QPoint global, bool dropped);
    QWidget* toolbarOverlay_ = nullptr;
    void placeRestoreHandles();
    void decorateArea(ads::CDockAreaWidget* area); // its minimize and maximize buttons
    void updateAreaButtons();
    int edgeOf(ads::CDockAreaWidget* area, float* along) const; // 0 left, 1 right, 2 top, 3 bottom
    QPoint handlePos(int edge, float along, QSize size) const;
    QAction* action(ui::Action a, const QString& text, std::function<void()> fn);

    EditorContext ctx_;
    RecentProjects& recents_;

    ads::CDockManager* docks_ = nullptr;
    QByteArray defaultLayout_;
    QByteArray beforeMaximize_; // restored by the next maximize toggle
    QByteArray viewportFullscreenLayout_;
    bool viewportFullscreen_ = false;
    bool windowWasFullscreen_ = false;
    QByteArray windowGeometryBeforeFullscreen_;
    ObjectsPanel* objects_ = nullptr;
    MediaPanel* media_ = nullptr;
    ToolsPanel* tools_ = nullptr;
    ToolsPopover* toolsPopover_ = nullptr;
    ViewportPanel* viewport_ = nullptr;
    PropertiesPanel* properties_ = nullptr;
    EffectsPanel* effects_ = nullptr;
    TransitionsPanel* transitions_ = nullptr;
    AnimationsPanel* animations_ = nullptr;
    TimelinePanel* timeline_ = nullptr;
    std::map<std::string, ads::CDockWidget*> dockWidgets_;
    // Minimized panels: hidden, with a handle on the window edge that brings them back.
    struct Minimized {
        std::vector<ads::CDockWidget*> docks;
        ads::CDockWidget* current = nullptr;
        int edge = 0;       // 0 left, 1 right, 2 top, 3 bottom
        float along = 0.5f; // handle position along that edge (0..1)
        int size = 0;       // its width / height, given back on restore
        QWidget* handle = nullptr;
    };
    std::vector<Minimized> minimized_;

    std::map<ui::Action, QAction*> actions_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    QPointer<ui::PreviewWindow> preview_; // the full screen preview, while open
    QElapsedTimer sinceChange_;    // autosave: a moment after the last change
    uint64_t seenRevision_ = 0, triedRevision_ = ~0ull; // once per change, even when saving fails
    ui::AudioMeter* meters_ = nullptr; // beside the timeline (View › Audio Meters)
    QLabel* status_ = nullptr; // errors next to the menus
    QString lastTitle_;
    double recordingStart_ = -1.0;
    uint64_t recordingScope_ = 0;
    bool spaceDown_ = false;
};

} // namespace bemotion
