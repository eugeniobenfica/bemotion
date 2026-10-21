#include "ui/panels/ObjectsPanel.h"

#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>

namespace mirage {

void ObjectsPanel::draw(EditorContext& ctx) {
    if (!ImGui::Begin(kWindowName)) {
        ImGui::End();
        return;
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float tileW = std::max(64.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
    const ImVec2 tile(tileW, std::clamp(tileW * 0.78f, 64.0f, 110.0f));

    ui::sectionLabel("TEXT");
    if (ui::objectTile("##text", ui::Icon::Text, "Text", tile)) ctx.addText();
    ui::tooltip("Add a text layer (T)");

    ImGui::Dummy(ImVec2(0, 6));
    ui::sectionLabel("GRAPHICS");
    int column = 0;
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) {
        const auto kind = static_cast<ShapeKind>(i);
        if (column++ % 2 == 1) ImGui::SameLine();
        ImGui::PushID(i);
        if (ui::objectTile("##shape", ui::iconForShape(kind), shapeKindLabel(kind), tile)) ctx.addShape(kind);
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Click an object to add it to the center of the composition.");
    ImGui::PopStyleColor();

    ImGui::End();
}

} // namespace mirage
