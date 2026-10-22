#include "ui/EditorScreen.h"

#include "app/Platform.h"
#include "app/RecentProjects.h"
#include "app/Settings.h"
#include "ui/HelpWindow.h"
#include "ui/Keymap.h"
#include "ui/SettingsWindow.h"
#include "core/Serialization.h"
#include "render/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>

namespace bemotion {

namespace {


} // namespace

EditorScreen::EditorScreen(Project project, std::filesystem::path path, RecentProjects& recents) : recents_(recents) {
    ctx_.open(std::move(project), std::move(path));
}

std::string EditorScreen::windowTitle() const {
    return ctx_.project.name + (ctx_.isDirty() ? " •" : "") + "  —  bemotion";
}

void EditorScreen::dropFiles(const std::vector<std::filesystem::path>& files) {
    // Like After Effects: dropped on the Media panel they are only imported; anywhere else they also
    // become layers at the playhead.
    media_.importFiles(ctx_, files, !media_.hovered());
}

EditorScreen::Request EditorScreen::draw(double dt) {
    ctx_.tick(dt);
    drawMenuBar();

    const ImGuiID dockId = ImGui::GetID("bemotionDockspace");
    if (resetLayout_ || ImGui::DockBuilderGetNode(dockId) == nullptr) {
        buildDefaultLayout(dockId);
        resetLayout_ = false;
    }
    ImGui::DockSpaceOverViewport(dockId, ImGui::GetMainViewport());

    objects_.draw(ctx_);
    media_.draw(ctx_);
    viewport_.draw(ctx_);
    properties_.draw(ctx_);
    effects_.draw(ctx_);
    timeline_.draw(ctx_);

    handleShortcuts();
    drawUnsavedModal();
    exportDialog_.draw(ctx_);

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
    ImGui::DockBuilderDockWindow(MediaPanel::kWindowName, left); // a tab beside Objects
    ImGui::DockBuilderDockWindow(ViewportPanel::kWindowName, center);
    ImGui::DockBuilderDockWindow(PropertiesPanel::kWindowName, right);
    ImGui::DockBuilderDockWindow(EffectsPanel::kWindowName, right); // a tab beside Properties
    ImGui::DockBuilderDockWindow(TimelinePanel::kWindowName, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
    viewport_.requestFit();
}

namespace {
// The shortcut shown next to a menu item (follows Settings > Shortcuts).
const char* keys(ui::Action a) {
    static std::string names[static_cast<int>(ui::Action::Count)];
    names[static_cast<int>(a)] = ui::chordName(ui::chordOf(a));
    return names[static_cast<int>(a)].c_str();
}
} // namespace

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
        if (ImGui::MenuItem("Save", keys(ui::Action::Save))) save();
        if (ImGui::MenuItem("Save As...", keys(ui::Action::SaveAs))) saveAs();
        if (ImGui::MenuItem("Import Media...", keys(ui::Action::ImportMedia))) media_.openImportDialog(ctx_);
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Folder", nullptr, false, !ctx_.path.empty()))
            platform::revealInFileManager(ctx_.path);
        ImGui::Separator();
        if (ImGui::MenuItem("Export Video...", keys(ui::Action::Export))) exportDialog_.open(ctx_);
        ImGui::Separator();
        if (ImGui::MenuItem("Close Project", keys(ui::Action::CloseProject))) askLeave(Request::GoHome);
        if (ImGui::MenuItem("Quit", keys(ui::Action::Quit))) askLeave(Request::Quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", keys(ui::Action::Undo), false, ctx_.history.canUndo() || ctx_.isDirty())) ctx_.undo();
        if (ImGui::MenuItem("Redo", keys(ui::Action::Redo), false, ctx_.history.canRedo())) ctx_.redo();
        ImGui::Separator();
        const bool any = !ctx_.selection.empty();
        if (ImGui::MenuItem("Duplicate", keys(ui::Action::Duplicate), false, any)) ctx_.duplicateSelected();
        if (ImGui::MenuItem("Delete", keys(ui::Action::Delete), false, any)) ctx_.deleteSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", keys(ui::Action::SelectAll))) ctx_.selectAll();
        if (ImGui::MenuItem("Deselect", keys(ui::Action::Deselect))) ctx_.select(0);
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...", keys(ui::Action::Settings))) ui::openSettings();
        if (ImGui::MenuItem("Plugins...")) ui::openSettings(ui::SettingsTab::Plugins);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layer")) {
        const bool any = !ctx_.selection.empty();
        if (ImGui::MenuItem("Split at Playhead", keys(ui::Action::Split))) ctx_.splitSelectedAtPlayhead();
        ImGui::Separator();
        if (ImGui::MenuItem("Create Compound", keys(ui::Action::CreateCompound), false, any)) ctx_.createCompound();
        if (ImGui::MenuItem("Release Compound", keys(ui::Action::ReleaseCompound), false, ctx_.selectionHasCompound()))
            ctx_.releaseSelectedCompounds();
        if (ImGui::BeginMenu("Effect", any)) {
            effectMenuItems(ctx_);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Cut Tool", keys(ui::Action::CutTool), ctx_.tool == Tool::Razor)) ctx_.tool = Tool::Razor;
        if (ImGui::MenuItem("Selection Tool", keys(ui::Action::SelectTool), ctx_.tool == Tool::Select)) ctx_.tool = Tool::Select;
        ImGui::Separator();
        if (ImGui::MenuItem("Bring to Front", keys(ui::Action::BringToFront), false, any)) ctx_.shiftSelected(-1000);
        if (ImGui::MenuItem("Bring Forward", keys(ui::Action::BringForward), false, any)) ctx_.shiftSelected(-1);
        if (ImGui::MenuItem("Send Backward", keys(ui::Action::SendBackward), false, any)) ctx_.shiftSelected(1);
        if (ImGui::MenuItem("Send to Back", keys(ui::Action::SendToBack), false, any)) ctx_.shiftSelected(1000);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Text", keys(ui::Action::AddText))) ctx_.addText();
        if (ImGui::MenuItem("Camera")) ctx_.addCamera();
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i)
            if (ImGui::MenuItem(shapeKindLabel(static_cast<ShapeKind>(i)))) ctx_.addShape(static_cast<ShapeKind>(i));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit Composition", keys(ui::Action::FitView))) viewport_.requestFit();
        if (ImGui::MenuItem("Actual Size (100%)", keys(ui::Action::ActualSize))) viewport_.setZoom(1.0f);
        if (ImGui::MenuItem("Zoom In", keys(ui::Action::ZoomIn))) viewport_.zoomBy(1.25f);
        if (ImGui::MenuItem("Zoom Out", keys(ui::Action::ZoomOut))) viewport_.zoomBy(1.0f / 1.25f);
        ImGui::Separator();
        ImGui::MenuItem("Snapping", keys(ui::Action::ToggleSnapping), &ctx_.snapping);
        ImGui::MenuItem("Rulers", nullptr, &ctx_.showRulers);
        if (ImGui::BeginMenu("Guides")) {
            ImGui::MenuItem("Show Guides", nullptr, &ctx_.showGuides);
            ImGui::MenuItem("Lock Guides", nullptr, &ctx_.lockGuides);
            if (ImGui::MenuItem("Add Center Guides")) {
                const Composition& c = ctx_.comp();
                ctx_.comp().guides.push_back({true, c.width * 0.5f});
                ctx_.comp().guides.push_back({false, c.height * 0.5f});
                ctx_.touch();
            }
            if (ImGui::MenuItem("Clear Guides", nullptr, false, !ctx_.comp().guides.empty())) {
                ctx_.comp().guides.clear();
                ctx_.touch();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Panels")) {
            ImGui::MenuItem(ObjectsPanel::kWindowName, nullptr, &objects_.open);
            ImGui::MenuItem(MediaPanel::kWindowName, nullptr, &media_.open);
            ImGui::MenuItem(ViewportPanel::kWindowName, nullptr, &viewport_.open);
            ImGui::MenuItem(PropertiesPanel::kWindowName, nullptr, &properties_.open);
            ImGui::MenuItem(EffectsPanel::kWindowName, nullptr, &effects_.open);
            ImGui::MenuItem(TimelinePanel::kWindowName, nullptr, &timeline_.open);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset Layout")) {
            resetLayout_ = true;
            objects_.open = media_.open = viewport_.open = properties_.open = effects_.open = timeline_.open = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts", keys(ui::Action::Help))) ui::openHelp(ui::HelpTab::Shortcuts);
        ImGui::Separator();
        if (ImGui::MenuItem("About bemotion")) ui::openHelp(ui::HelpTab::About);
        if (ImGui::MenuItem("Credits")) ui::openHelp(ui::HelpTab::Credits);
        if (ImGui::MenuItem("License")) ui::openHelp(ui::HelpTab::License);
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

    // Export, always one click away in the top-right corner.
    const float exportW = 104.0f;
    const float edge = ImGui::GetStyle().WindowPadding.x + 6.0f; // stay clear of the window edge
    ImGui::SameLine(ImGui::GetWindowWidth() - exportW - edge - 36);
    if (ui::iconButton("##settings", ui::Icon::Gear, 26.0f, "Settings (Ctrl+,)")) ui::openSettings();
    ImGui::SameLine(ImGui::GetWindowWidth() - exportW - edge);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
    const ImVec2 bp = ImGui::GetCursorScreenPos();
    if (ui::accentButton("     Export", ImVec2(exportW, 0))) exportDialog_.open(ctx_);
    ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Export, ImVec2(bp.x + 20, bp.y + ImGui::GetFrameHeight() * 0.5f),
                 14.0f, theme::kOnAccent);
    ui::tooltip("Export video (Ctrl+M)");
    ImGui::PopStyleVar();
    ImGui::EndMainMenuBar();
}

void EditorScreen::handleShortcuts() {
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    using ui::Action;
    auto pressed = [](Action a, bool repeat = false) { return ui::shortcutPressed(a, repeat); };

    if (pressed(Action::Save)) save();
    if (pressed(Action::SaveAs)) saveAs();
    if (pressed(Action::Undo, true)) ctx_.undo();
    if (pressed(Action::Redo, true) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) ctx_.redo();
    if (pressed(Action::Duplicate)) ctx_.duplicateSelected();
    if (pressed(Action::CloseProject)) askLeave(Request::GoHome);
    if (pressed(Action::Quit)) askLeave(Request::Quit);
    if (pressed(Action::FitView)) viewport_.requestFit();
    if (pressed(Action::ActualSize)) viewport_.setZoom(1.0f);
    if (pressed(Action::ZoomIn, true)) viewport_.zoomBy(1.25f);
    if (pressed(Action::ZoomOut, true)) viewport_.zoomBy(1.0f / 1.25f);
    if (pressed(Action::Export)) exportDialog_.open(ctx_);
    if (pressed(Action::ImportMedia)) media_.openImportDialog(ctx_);
    if (pressed(Action::Split)) ctx_.splitSelectedAtPlayhead();
    if (pressed(Action::CreateCompound)) ctx_.createCompound();
    if (pressed(Action::ReleaseCompound)) ctx_.releaseSelectedCompounds();
    if (pressed(Action::EasyEase)) TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EasyEase);
    if (pressed(Action::EaseIn)) TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EaseIn);
    if (pressed(Action::EaseOut)) TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EaseOut);
    if (pressed(Action::BringForward)) ctx_.shiftSelected(-1);
    if (pressed(Action::SendBackward)) ctx_.shiftSelected(1);
    if (pressed(Action::BringToFront)) ctx_.shiftSelected(-1000);
    if (pressed(Action::SendToBack)) ctx_.shiftSelected(1000);
    if (pressed(Action::SelectAll)) ctx_.selectAll();

    // Play / pause on Space: a tap toggles playback, holding it turns the mouse into a hand (viewport pan).
    const ImGuiIO& io = ImGui::GetIO();
    if (ui::chordOf(Action::PlayPause) == ImGuiKey_Space) {
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) viewport_.takeSpaceUsed();
            if (ImGui::IsKeyReleased(ImGuiKey_Space) && !viewport_.takeSpaceUsed()) ctx_.playing = !ctx_.playing;
        }
    } else if (pressed(Action::PlayPause)) {
        ctx_.playing = !ctx_.playing;
    }

    const Settings& st = settings();
    if (pressed(Action::PrevFrame, true)) ctx_.stepFrames(-st.arrowFrames);
    if (pressed(Action::NextFrame, true)) ctx_.stepFrames(st.arrowFrames);
    if (pressed(Action::PrevFrames, true)) ctx_.stepFrames(-st.bigArrowFrames);
    if (pressed(Action::NextFrames, true)) ctx_.stepFrames(st.bigArrowFrames);
    if (pressed(Action::GoToStart)) ctx_.setTime(0.0);
    if (pressed(Action::GoToEnd)) ctx_.setTime(ctx_.comp().duration);
    if (pressed(Action::PrevKeyframe, true)) jumpToKeyframe(-1);
    if (pressed(Action::NextKeyframe, true)) jumpToKeyframe(+1);
    if (pressed(Action::SelectTool)) ctx_.tool = Tool::Select;
    if (pressed(Action::CutTool)) ctx_.tool = Tool::Razor;
    if (pressed(Action::ToggleSnapping)) ctx_.snapping = !ctx_.snapping;
    if (pressed(Action::AddText)) ctx_.addText();
    if (pressed(Action::Deselect)) {
        if (ctx_.tool != Tool::Select) ctx_.tool = Tool::Select;
        else if (!ctx_.selection.empty()) ctx_.select(0);
        else ctx_.exitScope(); // nothing selected: leave the open compound
    }
    const bool backspace = !io.WantTextInput && !ImGui::IsAnyItemActive() && !io.KeyCtrl &&
                           ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
    if (pressed(Action::Delete) || backspace) {
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
    auto file = platform::saveFileDialog("Save Project As", suggested, "bemotion Project", kProjectExtension);
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

} // namespace bemotion
