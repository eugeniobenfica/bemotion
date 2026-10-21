#include "ui/HomeScreen.h"

#include "app/Platform.h"
#include "core/Serialization.h"
#include "render/Fonts.h"
#include "render/SceneRenderer.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace mirage {

namespace {

struct FormatPreset {
    const char* label;
    int width;
    int height;
};

constexpr FormatPreset kPresets[] = {
    {"Full HD  ·  1920 × 1080", 1920, 1080},
    {"4K UHD  ·  3840 × 2160", 3840, 2160},
    {"HD  ·  1280 × 720", 1280, 720},
    {"Vertical  ·  1080 × 1920  (Stories, Reels, Shorts)", 1080, 1920},
    {"Square  ·  1080 × 1080", 1080, 1080},
    {"Portrait  ·  1080 × 1350", 1080, 1350},
    {"Custom", 0, 0},
};
constexpr int kCustomPreset = static_cast<int>(std::size(kPresets)) - 1;
constexpr double kFpsOptions[] = {24, 25, 30, 50, 60};
constexpr const char* kFpsLabels[] = {"24 fps", "25 fps", "30 fps", "50 fps", "60 fps"};

std::string relativeTime(int64_t unixSeconds) {
    using namespace std::chrono;
    const int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const int64_t d = std::max<int64_t>(0, now - unixSeconds);
    char buf[64];
    if (d < 60) return "just now";
    if (d < 3600) std::snprintf(buf, sizeof(buf), "%lld min ago", static_cast<long long>(d / 60));
    else if (d < 86400) std::snprintf(buf, sizeof(buf), "%lld h ago", static_cast<long long>(d / 3600));
    else if (d < 86400 * 30) {
        const long long days = d / 86400;
        std::snprintf(buf, sizeof(buf), "%lld day%s ago", days, days == 1 ? "" : "s");
    } else {
        const std::time_t t = static_cast<std::time_t>(unixSeconds);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
    }
    return buf;
}

std::string sanitizeFileName(std::string name) {
    for (char& ch : name)
        if (std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos || static_cast<unsigned char>(ch) < 32)
            ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name.empty() ? "Untitled" : name;
}

std::filesystem::path uniqueProjectPath(const std::filesystem::path& folder, const std::string& name) {
    const std::string base = sanitizeFileName(name);
    std::error_code ec;
    auto candidate = folder / platform::fromUtf8(base + kProjectExtension);
    for (int i = 2; std::filesystem::exists(candidate, ec); ++i)
        candidate = folder / platform::fromUtf8(base + " " + std::to_string(i) + kProjectExtension);
    return candidate;
}

bool containsCaseInsensitive(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
}

void drawThumbnail(ImDrawList* dl, const Project* project, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 255), 8.0f, ImDrawFlags_RoundCornersTop);
    if (!project) {
        ui::drawIcon(dl, ui::Icon::Logo, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 36.0f, theme::kTextFaint);
        return;
    }
    const Composition& c = project->comp;
    const float pad = 12.0f;
    const float zoom = std::min((b.x - a.x - pad * 2) / c.width, (b.y - a.y - pad * 2) / c.height);
    const Vec2 origin{(a.x + b.x) * 0.5f - c.width * zoom * 0.5f, (a.y + b.y) * 0.5f - c.height * zoom * 0.5f};
    dl->PushClipRect(a, b, true);
    render::drawComposition(dl, c, std::min(c.duration * 0.5, 2.0), render::View{origin, zoom});
    dl->PopClipRect();
}

} // namespace

void HomeScreen::refresh(const RecentProjects& recents) {
    previews_.clear();
    for (const auto& r : recents.items()) {
        Project p;
        if (loadProject(r.path, p)) previews_[r.path] = std::move(p);
        else previews_[r.path] = std::nullopt;
    }
}

HomeScreen::Result HomeScreen::draw(RecentProjects& recents) {
    Result result;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::kBgDeep);
    ImGui::Begin("##home", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    drawSidebar(result, recents, 270.0f);
    ImGui::SameLine(0, 0);
    drawProjects(result, recents);
    drawNewProjectModal(result);

    ImGui::End();
    return result;
}

void HomeScreen::drawSidebar(Result& result, RecentProjects& recents, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kBgPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 26));
    ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    // Brand.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + 40, p.y + 40), theme::kAccent, 10.0f);
    ui::drawIcon(dl, ui::Icon::Logo, ImVec2(p.x + 20, p.y + 21), 24.0f, theme::kOnAccent);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 52, p.y + 1));
    ImGui::PushFont(Fonts::uiBold(), 22.0f);
    ImGui::TextUnformatted("Mirage");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x + 52, p.y + 26));
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Motion graphics  ·  v%s", MIRAGE_VERSION);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 64));

    const float w = ImGui::GetContentRegionAvail().x;
    if (ui::accentButton("+  New Project", ImVec2(w, 38))) openNewProjectModal();
    ImGui::Dummy(ImVec2(0, 2));
    if (ImGui::Button("Open Project...", ImVec2(w, 34))) {
        if (auto file = platform::openFileDialog("Open Mirage Project", "Mirage Project", kProjectExtension)) {
            result.action = Result::Action::Open;
            result.path = *file;
        }
    }

    // Footer.
    const float footerH = 74.0f;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 12, ImGui::GetWindowHeight() - footerH));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Show Projects Folder", ImVec2(w, 0)))
        platform::revealInFileManager(platform::defaultProjectsDir());
    ImGui::TextColored(theme::vec(theme::kTextFaint), "%d recent project%s", static_cast<int>(recents.items().size()),
                       recents.items().size() == 1 ? "" : "s");

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void HomeScreen::drawProjects(Result& result, RecentProjects& recents) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36, 30));
    ImGui::BeginChild("##projects", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    ImGui::PushFont(Fonts::uiBold(), 24.0f);
    ImGui::TextUnformatted("Projects");
    ImGui::PopFont();
    ImGui::SameLine();
    const float searchW = 260.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 20, ImGui::GetWindowWidth() - 36 - searchW));
    ImGui::SetNextItemWidth(searchW);
    ImGui::InputTextWithHint("##search", "Search projects", &search_);
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Recently opened");
    ImGui::Dummy(ImVec2(0, 10));

    if (!error_.empty()) {
        ImGui::TextColored(theme::vec(theme::kDanger), "%s", error_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) error_.clear();
        ImGui::Dummy(ImVec2(0, 6));
    }

    const auto& items = recents.items();
    if (items.empty()) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(0, std::max(0.0f, avail.y * 0.25f)));
        const char* line1 = "No projects yet";
        const char* line2 = "Create your first animation in a few clicks.";
        ImGui::PushFont(Fonts::uiBold(), 20.0f);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(line1).x) * 0.5f);
        ImGui::TextUnformatted(line1);
        ImGui::PopFont();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(line2).x) * 0.5f);
        ImGui::TextColored(theme::vec(theme::kTextDim), "%s", line2);
        ImGui::Dummy(ImVec2(0, 12));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 200) * 0.5f);
        if (ui::accentButton("Create a project", ImVec2(200, 40))) openNewProjectModal();
        ImGui::EndChild();
        return;
    }

    const float gap = 20.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, static_cast<int>((avail + gap) / (240.0f + gap)));
    const float cardW = (avail - gap * (cols - 1)) / cols;
    const float thumbH = cardW * 9.0f / 16.0f;
    const float cardH = thumbH + 62.0f;

    std::filesystem::path toRemove;
    int shown = 0;
    for (const auto& item : items) {
        if (!containsCaseInsensitive(item.name, search_)) continue;
        if (shown % cols != 0) ImGui::SameLine(0, gap);
        ++shown;

        ImGui::PushID(platform::toUtf8(item.path).c_str());
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + cardW, a.y + cardH);
        if (ImGui::InvisibleButton("##card", ImVec2(cardW, cardH))) {
            result.action = Result::Action::Open;
            result.path = item.path;
        }
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopupContextItem("##cardctx")) {
            if (ImGui::MenuItem("Open")) {
                result.action = Result::Action::Open;
                result.path = item.path;
            }
            if (ImGui::MenuItem("Show in Folder")) platform::revealInFileManager(item.path);
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from List")) toRemove = item.path;
            ImGui::EndPopup();
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, hovered ? theme::kBgHover : theme::kBgPanel, 8.0f);
        auto it = previews_.find(item.path);
        const Project* preview = (it != previews_.end() && it->second) ? &*it->second : nullptr;
        drawThumbnail(dl, preview, a, ImVec2(b.x, a.y + thumbH));
        dl->AddRect(a, b, hovered ? theme::kAccent : theme::kBorder, 8.0f, 0, hovered ? 1.5f : 1.0f);

        ImGui::PushFont(Fonts::uiBold(), 0.0f);
        dl->PushClipRect(ImVec2(a.x + 12, a.y), ImVec2(b.x - 12, b.y), true);
        dl->AddText(ImVec2(a.x + 14, a.y + thumbH + 12), theme::kText, item.name.c_str());
        ImGui::PopFont();
        char meta[128];
        if (preview)
            std::snprintf(meta, sizeof(meta), "%d × %d  ·  %.4g fps  ·  %s", preview->comp.width, preview->comp.height,
                          preview->comp.fps, relativeTime(item.lastOpened).c_str());
        else std::snprintf(meta, sizeof(meta), "Unreadable file  ·  %s", relativeTime(item.lastOpened).c_str());
        dl->AddText(ImVec2(a.x + 14, a.y + thumbH + 34), theme::kTextFaint, meta);
        dl->PopClipRect();
        ImGui::PopID();
        if (shown % cols == 0) ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    }
    if (shown == 0) ImGui::TextColored(theme::vec(theme::kTextDim), "No projects match \"%s\".", search_.c_str());
    if (!toRemove.empty()) {
        recents.remove(toRemove);
        previews_.erase(toRemove);
    }
    ImGui::EndChild();
}

void HomeScreen::openNewProjectModal() {
    openModal_ = true;
    newName_ = "Untitled";
    folder_ = platform::defaultProjectsDir();
}

void HomeScreen::drawNewProjectModal(Result& result) {
    if (openModal_) {
        ImGui::OpenPopup("New Project");
        openModal_ = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 22));
    if (!ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::PopStyleVar();

    const float labelW = 110.0f;
    auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::kTextDim), "%s", text);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    label("Name");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", &newName_, ImGuiInputTextFlags_EnterReturnsTrue);

    label("Format");
    if (ImGui::BeginCombo("##format", kPresets[preset_].label)) {
        for (int i = 0; i < static_cast<int>(std::size(kPresets)); ++i)
            if (ImGui::Selectable(kPresets[i].label, i == preset_)) {
                preset_ = i;
                if (kPresets[i].width > 0) {
                    width_ = kPresets[i].width;
                    height_ = kPresets[i].height;
                }
            }
        ImGui::EndCombo();
    }
    label("Size");
    int size[2] = {width_, height_};
    if (ImGui::InputInt2("##size", size)) {
        width_ = std::clamp(size[0], 16, 16384);
        height_ = std::clamp(size[1], 16, 16384);
        preset_ = kCustomPreset;
        for (int i = 0; i < kCustomPreset; ++i)
            if (kPresets[i].width == width_ && kPresets[i].height == height_) preset_ = i;
    }
    label("Frame rate");
    ImGui::Combo("##fps", &fpsIndex_, kFpsLabels, static_cast<int>(std::size(kFpsLabels)));
    label("Duration");
    ImGui::DragFloat("##duration", &duration_, 0.1f, 0.5f, 3600.0f, "%.1f seconds", ImGuiSliderFlags_AlwaysClamp);
    label("Background");
    ImGui::ColorEdit3("##bg", &background_.r, ImGuiColorEditFlags_DisplayHex);
    label("Location");
    const std::string folder = platform::toUtf8(folder_);
    ImGui::SetNextItemWidth(-90);
    ImGui::BeginDisabled();
    std::string folderCopy = folder;
    ImGui::InputText("##folder", &folderCopy, ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Change...", ImVec2(-FLT_MIN, 0))) {
        if (auto file = platform::saveFileDialog("Choose where to save the project",
                                                 uniqueProjectPath(folder_, newName_), "Mirage Project",
                                                 kProjectExtension)) {
            folder_ = file->parent_path();
            newName_ = platform::toUtf8(file->stem());
        }
    }

    ImGui::Dummy(ImVec2(0, 10));
    const float bw = 120.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 24 - bw * 2 - 8);
    if (ImGui::Button("Cancel", ImVec2(bw, 34)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine(0, 8);
    if (ui::accentButton("Create", ImVec2(bw, 34)) || enter) {
        Project p;
        p.name = newName_.empty() ? "Untitled" : newName_;
        p.comp.width = width_;
        p.comp.height = height_;
        p.comp.fps = kFpsOptions[fpsIndex_];
        p.comp.duration = duration_;
        p.comp.background = background_;
        result.action = Result::Action::Create;
        result.path = uniqueProjectPath(folder_, p.name);
        result.project = std::move(p);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace mirage
