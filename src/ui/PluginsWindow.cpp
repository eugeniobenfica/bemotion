#include "ui/PluginsWindow.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <string>

namespace bemotion::ui {

namespace {

constexpr const char* kDocsUrl = "https://github.com/eugeniobenfica/bemotion/blob/main/docs/PLUGINS.md";

bool g_open = false;
std::string g_message;

void installPlugin() {
    if (auto manifest = platform::openFileDialog("Install Plugin (choose its plugin.json)", "bemotion plugin", "json")) {
        std::string error;
        g_message = PluginHost::get().install(*manifest, &error) ? "Plugin installed." : "Could not install: " + error;
        g_open = true;
    }
}

void faint(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

void pluginsMenuItems() {
    PluginHost& host = PluginHost::get();
    for (const Plugin& p : host.plugins()) {
        bool enabled = p.enabled;
        const std::string label = p.name + (p.error.empty() ? "" : "  (error)");
        if (ImGui::MenuItem(label.c_str(), p.version.c_str(), &enabled)) host.setEnabled(p.id, enabled);
    }
    if (host.plugins().empty()) ImGui::TextDisabled("No plugins installed");
    ImGui::Separator();
    if (ImGui::MenuItem("Plugin Manager...")) openPluginManager();
    if (ImGui::MenuItem("Install Plugin...")) installPlugin();
    if (ImGui::MenuItem("Open Plugins Folder")) platform::revealInFileManager(PluginHost::userDir());
    if (ImGui::MenuItem("Reload Plugins")) host.requestReload();
    ImGui::Separator();
    if (ImGui::MenuItem("Write a Plugin (docs)")) platform::openWithDefaultApp(kDocsUrl);
}

void openPluginManager() { g_open = true; }

void drawPluginManager() {
    if (!g_open) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    if (!ImGui::Begin("Plugins", &g_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_open = false;

    PluginHost& host = PluginHost::get();
    if (ui::accentButton("Install Plugin...")) installPlugin();
    ImGui::SameLine();
    if (ImGui::Button("Open Plugins Folder")) platform::revealInFileManager(PluginHost::userDir());
    ImGui::SameLine();
    if (ImGui::Button("Reload")) host.requestReload();
    ImGui::SameLine();
    if (ImGui::Button("Write a Plugin")) platform::openWithDefaultApp(kDocsUrl);
    if (!g_message.empty()) faint(g_message);
    faint("Plugins add new kinds of objects. They run in a sandbox: no access to your files or the network.");
    ImGui::Separator();

    ImGui::BeginChild("##list");
    for (const Plugin& p : host.plugins()) {
        ImGui::PushID(p.id.c_str());
        bool enabled = p.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) host.setEnabled(p.id, enabled);
        ui::tooltip(enabled ? "Enabled" : "Disabled");
        ImGui::SameLine();
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.1f);
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(theme::kTextFaint), "v%s%s%s", p.version.c_str(), p.author.empty() ? "" : "  ·  ",
                           p.author.c_str());
        if (p.builtin) {
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(theme::kAccent), "built-in");
        }
        ImGui::Indent(ImGui::GetFrameHeight() + 8);
        if (!p.description.empty()) ImGui::TextWrapped("%s", p.description.c_str());
        if (!p.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
            ImGui::TextWrapped("Failed to load: %s", p.error.c_str());
            ImGui::PopStyleColor();
        } else {
            std::string objects;
            for (const PluginObjectType* t : host.objectTypes())
                if (t->pluginId == p.id) objects += (objects.empty() ? "" : ", ") + t->name;
            if (!objects.empty()) faint("Objects: " + objects);
        }
        if (!p.drawError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
            ImGui::TextWrapped("Last error: %s", p.drawError.c_str());
            ImGui::PopStyleColor();
        }
        if (!p.log.empty() && ImGui::TreeNode("log", "Log (%d)", static_cast<int>(p.log.size()))) {
            for (const auto& line : p.log) ImGui::TextUnformatted(line.c_str());
            ImGui::TreePop();
        }
        if (ImGui::SmallButton("Show Folder")) platform::revealInFileManager(p.dir);
        ImGui::Unindent(ImGui::GetFrameHeight() + 8);
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace bemotion::ui
