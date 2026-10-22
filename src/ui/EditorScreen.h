#pragma once

#include "ui/EditorContext.h"
#include "ui/ExportDialog.h"
#include "ui/panels/ObjectsPanel.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/PropertiesPanel.h"
#include "ui/panels/TimelinePanel.h"
#include "ui/panels/ViewportPanel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

class RecentProjects;

// The main editing workspace: menu bar + dockable panels.
class EditorScreen {
public:
    enum class Request { None, GoHome, Quit };

    EditorScreen(Project project, std::filesystem::path path, RecentProjects& recents);

    // Draws one frame. Returns a request once the user confirmed leaving.
    Request draw(double dt);

    // Called when the OS window is asked to close.
    void requestQuit() { askLeave(Request::Quit); }

    std::string windowTitle() const;
    const Project& project() const { return ctx_.project; }
    double time() const { return ctx_.time; }
    bool playing() const { return ctx_.playing; }
    // Files dropped on the window: imported into the Media panel and added at the playhead.
    void dropFiles(const std::vector<std::filesystem::path>& files);

private:
    void drawMenuBar();
    void handleShortcuts();
    void buildDefaultLayout(unsigned int dockspaceId);
    void drawUnsavedModal();
    void askLeave(Request request);

    bool save();
    bool saveAs();
    void jumpToKeyframe(int direction);

    EditorContext ctx_;
    RecentProjects& recents_;

    ObjectsPanel objects_;
    MediaPanel media_;
    ViewportPanel viewport_;
    PropertiesPanel properties_;
    EffectsPanel effects_;
    TimelinePanel timeline_;
    ExportDialog exportDialog_;

    Request pendingLeave_ = Request::None;
    Request confirmedLeave_ = Request::None;
    bool openUnsavedModal_ = false;
    bool resetLayout_ = false;
    std::string error_;
};

} // namespace bemotion
