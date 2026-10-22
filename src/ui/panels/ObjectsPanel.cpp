#include "ui/panels/ObjectsPanel.h"

#include "app/PluginHost.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/ViewportPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace bemotion {

namespace {

// Lets a tile be dragged onto the viewport to drop the object at a precise spot.
// `kind` is -1 for text, otherwise a ShapeKind.
void dragSource(int kind, ui::Icon icon, const char* label) {
    if (!ImGui::BeginDragDropSource()) return;
    ImGui::SetDragDropPayload(kObjectPayload, &kind, sizeof(kind));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(s, s));
    ui::drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.6f, theme::kAccent);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  ·  drop on the canvas", label);
    ImGui::EndDragDropSource();
}

} // namespace

void ObjectsPanel::draw(EditorContext& ctx) {
    if (!open) return;
    ui::nextPanelWindowClass();
    if (!ImGui::Begin(kWindowName, &open)) {
        ImGui::End();
        return;
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float tileW = std::max(64.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
    const ImVec2 tile(tileW, std::clamp(tileW * 0.78f, 64.0f, 110.0f));

    ui::sectionLabel("TEXT");
    if (ui::objectTile("##text", ui::Icon::Text, "Text", tile)) ctx.addText();
    ui::tooltip("Click to add at the center, or drag onto the canvas (T)");
    dragSource(-1, ui::Icon::Text, "Text");

    ImGui::Dummy(ImVec2(0, 6));
    ui::sectionLabel("GRAPHICS");
    int column = 0;
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) {
        const auto kind = static_cast<ShapeKind>(i);
        if (column++ % 2 == 1) ImGui::SameLine();
        ImGui::PushID(i);
        if (ui::objectTile("##shape", ui::iconForShape(kind), shapeKindLabel(kind), tile)) ctx.addShape(kind);
        ui::tooltip("Click to add at the center, or drag onto the canvas");
        dragSource(i, ui::iconForShape(kind), shapeKindLabel(kind));
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, 6));
    ui::sectionLabel("SCENE");
    if (ui::objectTile("##camera", ui::Icon::Camera, "Camera", tile)) ctx.addCamera();
    ui::tooltip("A camera frames the scene: animate its position, rotation and zoom");
    dragSource(kDropCamera, ui::Icon::Camera, "Camera");

    // Object types from plugins (charts, ...), grouped by plugin.
    const auto types = PluginHost::get().objectTypes();
    std::string lastPlugin;
    column = 0;
    for (size_t i = 0; i < types.size(); ++i) {
        const PluginObjectType& type = *types[i];
        if (type.pluginId != lastPlugin) {
            lastPlugin = type.pluginId;
            const Plugin* plugin = PluginHost::get().findPlugin(type.pluginId);
            std::string title = plugin ? plugin->name : type.pluginId;
            for (char& ch : title) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            ImGui::Dummy(ImVec2(0, 6));
            ui::sectionLabel(title.c_str());
            column = 0;
        }
        if (column++ % 2 == 1) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i) + 1000);
        const ui::Icon icon = ui::iconForPlugin(type.icon);
        if (ui::objectTile("##plugin", icon, type.name.c_str(), tile)) ctx.addPluginObject(type);
        ui::tooltip(type.description.empty() ? "Click to add at the center, or drag onto the canvas"
                                             : type.description.c_str());
        dragSource(kDropPlugin - static_cast<int>(i), icon, type.name.c_str());
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Click an object to add it at the center, or drag it to any spot on the canvas.");
    ImGui::PopStyleColor();

    ImGui::End();
}

} // namespace bemotion
