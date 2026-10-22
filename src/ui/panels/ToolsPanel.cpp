#include "ui/panels/ToolsPanel.h"

#include "app/Settings.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/ObjectsPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace bemotion {

namespace {

bool inToolbar(const char* id) {
    const auto& bar = settings().toolbar;
    return std::find(bar.begin(), bar.end(), id) != bar.end();
}

void drawSection(EditorContext& ctx, bool modal, ImVec2 tile, int cols) {
    const auto& tools = ui::toolCatalog();
    int column = 0;
    for (int i = 0; i < static_cast<int>(tools.size()); ++i) {
        const ui::ToolInfo& tool = tools[i];
        if (tool.modal != modal) continue;
        if (column++ % cols != 0) ImGui::SameLine();
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool enabled = ui::toolEnabled(ctx, tool);
        // Unavailable now (e.g. nothing selected): dimmed, but it can still be dragged to the toolbar.
        if (ui::objectTile("##tool", tool.icon, tool.label, tile) && enabled) ui::runTool(ctx, tool);
        if (!enabled)
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + tile.x, p.y + tile.y), theme::withAlpha(theme::kBgPanel, 140), 8.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n\nDrag it into the timeline's toolbar to keep it at hand.", ui::toolTooltip(tool).c_str());
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(ui::kToolPayload, &i, sizeof(i));
            const ImVec2 q = ImGui::GetCursorScreenPos();
            const float s = ImGui::GetFrameHeight();
            ImGui::Dummy(ImVec2(s, s));
            ui::drawIcon(ImGui::GetWindowDrawList(), tool.icon, ImVec2(q.x + s * 0.5f, q.y + s * 0.5f), s * 0.6f, theme::kAccent);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  ·  drop it in the timeline's toolbar", tool.label);
            ImGui::EndDragDropSource();
        }
        // Already in the toolbar: a small accent dot in the corner.
        if (inToolbar(tool.id))
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + tile.x - 9, p.y + 9), 3.0f, theme::kAccent);
        if (ui::toolActive(ctx, tool))
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + tile.x, p.y + tile.y), theme::kAccent, 8.0f, 0, 1.5f);
        ImGui::PopID();
    }
}

} // namespace

void ToolsPanel::draw(EditorContext& ctx) {
    if (!open || ui::panelHidden(kWindowName)) return; // minimized: only its restore handle shows
    // New in this version: existing layouts get the tab next to Objects.
    if (ImGuiWindow* objects = ImGui::FindWindowByName(ObjectsPanel::kWindowName); objects && objects->DockId)
        ImGui::SetNextWindowDockID(objects->DockId, ImGuiCond_FirstUseEver);
    ui::nextPanelWindowClass();
    if (!ImGui::Begin(kWindowName, &open)) {
        ImGui::End();
        return;
    }
    if (ui::panelIsMinimized()) {
        ui::panelMinimizeButton();
        ImGui::End();
        return;
    }

    // Same tile grid as the Objects panel.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(2, static_cast<int>((avail + spacing) / (84.0f + spacing)));
    const float tileW = std::max(56.0f, (avail - spacing * (cols - 1)) / cols);
    const ImVec2 tile(tileW, std::clamp(tileW * 0.8f, 56.0f, 84.0f));

    ui::sectionLabel("TOOLS");
    drawSection(ctx, true, tile, cols);
    ImGui::Dummy(ImVec2(0, 6));
    ui::sectionLabel("ACTIONS");
    drawSection(ctx, false, tile, cols);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
    ImGui::TextWrapped("Drag tools into the timeline's toolbar. Drag them along it to reorder, out of it (or right-click) "
                       "to remove. A dot marks the ones already there.");
    ImGui::PopStyleColor();
    if (ImGui::Button("Reset Toolbar")) {
        settings().toolbar = ui::defaultToolbar();
        saveSettings();
    }

    ui::panelMinimizeButton();
    ImGui::End();
}

} // namespace bemotion
