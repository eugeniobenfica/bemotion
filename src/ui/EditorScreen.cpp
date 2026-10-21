#include "ui/EditorScreen.h"

#include "app/Platform.h"
#include "app/RecentProjects.h"
#include "core/Serialization.h"
#include "render/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>

namespace mirage {

namespace {

struct ShortcutHelp {
    const char* keys;
    const char* action;
};

constexpr ShortcutHelp kShortcuts[] = {
    {"Space", "Play / pause"},
    {"Left / Right", "Previous / next frame (Shift: 10 frames)"},
    {"Home / End", "Go to start / end"},
    {"J / K", "Previous / next keyframe"},
    {"T", "Add text"},
    {"Ctrl+Z / Ctrl+Shift+Z", "Undo / redo"},
    {"Ctrl+D", "Duplicate layer"},
    {"Delete", "Delete keyframes or layer"},
    {"Ctrl+S", "Save"},
    {"Ctrl+0", "Fit composition in viewport"},
    {"Wheel / Middle drag", "Zoom / pan the viewport"},
    {"Ctrl+Wheel", "Zoom the timeline"},
    {"Shift (while dragging)", "Constrain move / uniform scale / snap rotation"},
};

} // namespace

EditorScreen::EditorScreen(Project project, std::filesystem::path path, RecentProjects& recents) : recents_(recents) {
    ctx_.open(std::move(project), std::move(path));
}

std::string EditorScreen::windowTitle() const {
    return ctx_.project.name + (ctx_.isDirty() ? " •" : "") + "  —  Mirage";
}

EditorScreen::Request EditorScreen::draw(double dt) {
    ctx_.tick(dt);
    drawMenuBar();

    const ImGuiID dockId = ImGui::GetID("MirageDockspace");
    if (resetLayout_ || ImGui::DockBuilderGetNode(dockId) == nullptr) {
        buildDefaultLayout(dockId);
        resetLayout_ = false;
    }
    ImGui::DockSpaceOverViewport(dockId, ImGui::GetMainViewport());

    objects_.draw(ctx_);
    viewport_.draw(ctx_);
    properties_.draw(ctx_);
    timeline_.draw(ctx_);

    handleShortcuts();
    drawUnsavedModal();
    drawAboutModal();

    ctx_.commitIfIdle(ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left));

    const Request r = confirmedLeave_;
    confirmedLeave_ = Request::None;
    return r;
}

void EditorScreen::buildDefaultLayout(unsigned int dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID top = 0, bottom = 0, left = 0, rest = 0, right = 0, center = 0;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Down, 0.36f, &bottom, &top);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.15f, &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.26f, &right, &center);

    ImGui::DockBuilderDockWindow(ObjectsPanel::kWindowName, left);
    ImGui::DockBuilderDockWindow(ViewportPanel::kWindowName, center);
    ImGui::DockBuilderDockWindow(PropertiesPanel::kWindowName, right);
    ImGui::DockBuilderDockWindow(TimelinePanel::kWindowName, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
    viewport_.requestFit();
}

void EditorScreen::drawMenuBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    if (!ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::PopStyleVar();

    // Brand mark, doubles as a "home" button.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        if (ImGui::InvisibleButton("##home", ImVec2(h + 4, h))) askLeave(Request::GoHome);
        ui::tooltip("Back to projects");
        ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Logo, ImVec2(p.x + h * 0.5f + 2, p.y + h * 0.5f), h * 0.55f,
                     ImGui::IsItemHovered() ? theme::kAccentHover : theme::kAccent);
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save", "Ctrl+S")) save();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) saveAs();
        if (ImGui::MenuItem("Show in Folder", nullptr, false, !ctx_.path.empty()))
            platform::revealInFileManager(ctx_.path);
        ImGui::Separator();
        if (ImGui::MenuItem("Close Project", "Ctrl+W")) askLeave(Request::GoHome);
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) askLeave(Request::Quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, ctx_.history.canUndo() || ctx_.isDirty())) ctx_.undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, ctx_.history.canRedo())) ctx_.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate Layer", "Ctrl+D", false, ctx_.selected() != nullptr)) ctx_.duplicateSelected();
        if (ImGui::MenuItem("Delete Layer", "Del", false, ctx_.selected() != nullptr)) ctx_.deleteSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Deselect", "Esc")) ctx_.select(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Text", "T")) ctx_.addText();
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i)
            if (ImGui::MenuItem(shapeKindLabel(static_cast<ShapeKind>(i)))) ctx_.addShape(static_cast<ShapeKind>(i));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit Composition", "Ctrl+0")) viewport_.requestFit();
        if (ImGui::MenuItem("Reset Layout")) resetLayout_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts & About")) openAboutModal_ = true;
        ImGui::EndMenu();
    }

    // Centered project title.
    const std::string title = ctx_.project.name + (ctx_.isDirty() ? "  •" : "");
    const float tw = ImGui::CalcTextSize(title.c_str()).x;
    const float cx = (ImGui::GetWindowWidth() - tw) * 0.5f;
    if (cx > ImGui::GetCursorPosX() + 20) {
        ImGui::SetCursorPosX(cx);
        ImGui::TextColored(theme::vec(theme::kTextDim), "%s", title.c_str());
    }
    if (!error_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(theme::kDanger), "  %s", error_.c_str());
    }
    ImGui::EndMainMenuBar();
}

void EditorScreen::handleShortcuts() {
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const ImGuiIO& io = ImGui::GetIO();
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global)) save();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global)) saveAs();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global)) ctx_.undo();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global))
        ctx_.redo();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, global)) ctx_.duplicateSelected();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, global)) askLeave(Request::GoHome);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, global)) askLeave(Request::Quit);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, global)) viewport_.requestFit();

    // Single-key shortcuts must not fire while typing.
    if (io.WantTextInput || ImGui::IsAnyItemActive()) return;
    const bool shift = io.KeyShift;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) ctx_.playing = !ctx_.playing;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) ctx_.stepFrames(shift ? -10 : -1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) ctx_.stepFrames(shift ? 10 : 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) ctx_.setTime(0.0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) ctx_.setTime(ctx_.comp().duration);
    if (ImGui::IsKeyPressed(ImGuiKey_J)) jumpToKeyframe(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_K)) jumpToKeyframe(+1);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) ctx_.addText();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ctx_.select(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        if (!ctx_.selectedKeys.empty()) ctx_.deleteSelectedKeys();
        else ctx_.deleteSelected();
    }
}

void EditorScreen::jumpToKeyframe(int direction) {
    double best = direction > 0 ? DBL_MAX : -DBL_MAX;
    auto consider = [&](Layer& layer) {
        for (const PropertyRef& p : layer.properties())
            for (int i = 0; i < p.anim->keyCount(); ++i) {
                const double t = p.anim->keyTime(i);
                if (direction > 0 && t > ctx_.time + kTimeEpsilon) best = std::min(best, t);
                if (direction < 0 && t < ctx_.time - kTimeEpsilon) best = std::max(best, t);
            }
    };
    if (Layer* sel = ctx_.selected()) consider(*sel);
    else
        for (Layer& l : ctx_.comp().layers) consider(l);
    if (best != DBL_MAX && best != -DBL_MAX) {
        ctx_.playing = false;
        ctx_.setTime(best);
    }
}

bool EditorScreen::save() {
    if (ctx_.path.empty()) return saveAs();
    std::string err;
    if (!ctx_.save(&err)) {
        error_ = "Save failed: " + err;
        return false;
    }
    error_.clear();
    recents_.touch(ctx_.path, ctx_.project.name);
    return true;
}

bool EditorScreen::saveAs() {
    const auto suggested = ctx_.path.empty()
                               ? platform::defaultProjectsDir() / platform::fromUtf8(ctx_.project.name + kProjectExtension)
                               : ctx_.path;
    auto file = platform::saveFileDialog("Save Project As", suggested, "Mirage Project", kProjectExtension);
    if (!file) return false;
    std::string err;
    if (!ctx_.saveTo(*file, &err)) {
        error_ = "Save failed: " + err;
        return false;
    }
    error_.clear();
    recents_.touch(ctx_.path, ctx_.project.name);
    return true;
}

void EditorScreen::askLeave(Request request) {
    ctx_.commitIfIdle(false);
    if (ctx_.isDirty()) {
        pendingLeave_ = request;
        openUnsavedModal_ = true;
    } else {
        confirmedLeave_ = request;
    }
}

void EditorScreen::drawUnsavedModal() {
    if (openUnsavedModal_) {
        ImGui::OpenPopup("Unsaved Changes");
        openUnsavedModal_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Save changes to \"%s\" before closing?", ctx_.project.name.c_str());
        ImGui::TextColored(theme::vec(theme::kTextDim), "Your changes will be lost if you don't save them.");
        ImGui::Dummy(ImVec2(0, 10));
        if (ImGui::Button("Don't Save", ImVec2(110, 32))) {
            confirmedLeave_ = pendingLeave_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 32)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingLeave_ = Request::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::accentButton("Save", ImVec2(110, 32))) {
            if (save()) confirmedLeave_ = pendingLeave_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void EditorScreen::drawAboutModal() {
    if (openAboutModal_) {
        ImGui::OpenPopup("About Mirage");
        openAboutModal_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    bool open = true;
    if (ImGui::BeginPopupModal("About Mirage", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(Fonts::uiBold(), 22.0f);
        ImGui::TextColored(theme::vec(theme::kAccent), "Mirage");
        ImGui::PopFont();
        ImGui::TextColored(theme::vec(theme::kTextDim), "Version %s  ·  Free and open source (MIT)", MIRAGE_VERSION);
        ImGui::Dummy(ImVec2(0, 8));
        ui::sectionLabel("KEYBOARD SHORTCUTS");
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (const auto& s : kShortcuts) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(theme::vec(theme::kAccent), "%s", s.keys);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(s.action);
            }
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Close", ImVec2(100, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

} // namespace mirage
