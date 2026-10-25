#include "ui/Tools.h"

#include "ui/EditorContext.h"
#include "ui/panels/TimelinePanel.h"

namespace bemotion::ui {

const std::vector<ToolInfo>& toolCatalog() {
    static const std::vector<ToolInfo> tools = {
        {"select", "Selection", Icon::Pointer, "Select, move and trim layers", Action::SelectTool, true},
        {"razor", "Cut", Icon::Razor, "Click a clip to split it there", Action::CutTool, true},
        {"crop", "Crop", Icon::Crop, "Drag the edges of a video or image to crop it (Esc: done)", Action::CropTool, true},
        {"split", "Split at Playhead", Icon::SplitPlayhead, "Split the selected layers at the playhead", Action::Split},
        {"compound", "Create Compound", Icon::Compound, "Combine the selected layers; double-click it to open its timeline",
         Action::CreateCompound},
        {"release", "Release Compound", Icon::Ungroup, "Put a compound's layers back, keeping their look",
         Action::ReleaseCompound},
        {"duplicate", "Duplicate", Icon::Duplicate, "Duplicate the selected layers", Action::Duplicate},
        {"delete", "Delete", Icon::Trash, "Delete the selected keyframes, or layers", Action::Delete},
        {"easy_ease", "Easy Ease", Icon::Ease, "Smooth in and out of the selected keyframes", Action::EasyEase},
        {"forward", "Bring Forward", Icon::LayerUp, "Move the selected layers up the stack", Action::BringForward},
        {"backward", "Send Backward", Icon::LayerDown, "Move the selected layers down the stack", Action::SendBackward},
        {"text", "Add Text", Icon::Text, "A new text layer at the playhead", Action::AddText},
        {"camera", "Add Camera", Icon::Camera, "A camera to pan, rotate and zoom the scene"},
    };
    return tools;
}

int toolIndex(std::string_view id) {
    const auto& tools = toolCatalog();
    for (size_t i = 0; i < tools.size(); ++i)
        if (id == tools[i].id) return static_cast<int>(i);
    return -1;
}

std::vector<std::string> defaultToolbar() { return {"select", "razor", "crop", "split", "compound", "delete"}; }

std::string toolTooltip(const ToolInfo& tool) {
    std::string tip = tool.label;
    if (tool.shortcut != Action::Count && chordOf(tool.shortcut)) tip += "  (" + chordName(chordOf(tool.shortcut)) + ")";
    return tip + "\n" + tool.description;
}

bool toolEnabled(EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    if (id == "split" || id == "compound" || id == "duplicate" || id == "forward" || id == "backward")
        return !ctx.selection.empty();
    if (id == "release") return ctx.selectionHasCompound();
    if (id == "delete") return !ctx.selection.empty() || !ctx.selectedKeys.empty();
    if (id == "easy_ease") return !ctx.selectedKeys.empty();
    return true;
}

bool toolActive(const EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    return (id == "select" && ctx.tool == Tool::Select) || (id == "razor" && ctx.tool == Tool::Razor) ||
           (id == "crop" && ctx.tool == Tool::Crop);
}

void runTool(EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    if (id == "select") ctx.tool = Tool::Select;
    else if (id == "razor") ctx.tool = Tool::Razor;
    else if (id == "crop") ctx.tool = ctx.tool == Tool::Crop ? Tool::Select : Tool::Crop;
    else if (id == "split") ctx.splitSelectedAtPlayhead();
    else if (id == "compound") ctx.createCompound();
    else if (id == "release") ctx.releaseSelectedCompounds();
    else if (id == "duplicate") ctx.duplicateSelected();
    else if (id == "delete") {
        if (!ctx.selectedKeys.empty()) ctx.deleteSelectedKeys();
        else ctx.deleteSelected();
    } else if (id == "easy_ease") TimelinePanel::applyEase(ctx, TimelinePanel::Ease::EasyEase);
    else if (id == "forward") ctx.shiftSelected(-1);
    else if (id == "backward") ctx.shiftSelected(1);
    else if (id == "text") ctx.addText();
    else if (id == "camera") ctx.addCamera();
}

} // namespace bemotion::ui
