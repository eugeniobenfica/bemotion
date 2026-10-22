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

std::string g_message;

void installPlugin() {
    if (auto manifest = platform::openFileDialog("Install Plugin (choose its plugin.json)", "bemotion plugin", "json")) {
        std::string error;
        g_message = PluginHost::get().install(*manifest, &error) ? "Plugin installed." : "Could not install: " + error;
    }
}

void faint(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

void drawPluginsTab() {
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

    ImGui::BeginChild("##list", ImVec2(0, 0), ImGuiChildFlags_None);
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
}

} // namespace bemotion::ui
