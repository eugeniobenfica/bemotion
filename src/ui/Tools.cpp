#include "ui/Tools.h"

#include "ui/Language.h"

#include "ui/EditorContext.h"
#include "ui/panels/TimelinePanel.h"

namespace bemotion::ui {

const std::vector<ToolInfo>& toolCatalog() {
    static const std::vector<ToolInfo> tools = {
        {"select", TR_NOOP("Selection"), Icon::Pointer, TR_NOOP("Select, move and trim layers"), Action::SelectTool, true},
        {"razor", TR_NOOP("Cut"), Icon::Razor, TR_NOOP("Click a clip to split it there"), Action::CutTool, true},
        {"crop", TR_NOOP("Crop"), Icon::Crop, TR_NOOP("Drag the edges of a video or image to crop it (Esc: done)"), Action::CropTool, true},
        {"pen", TR_NOOP("Pen"), Icon::Pen,
         TR_NOOP("Click to place points, drag for curves; click the first point to close (Enter: done). With a layer selected "
         "that is not a shape: draws a mask on it"),
         Action::PenTool, true},
        {"ripple", TR_NOOP("Ripple Edit"), Icon::RippleEdit, TR_NOOP("Drag a clip's edge: the clips after it follow, no gap opens"), Action::RippleTool, true},
        {"roll", TR_NOOP("Rolling Edit"), Icon::RollEdit, TR_NOOP("Drag the cut between two clips: one gets longer, the other shorter"), Action::RollTool, true},
        {"slip", TR_NOOP("Slip"), Icon::SlipEdit, TR_NOOP("Drag a clip: it stays put and shows other frames of its footage"), Action::SlipTool, true},
        {"split", TR_NOOP("Split at Playhead"), Icon::SplitPlayhead, TR_NOOP("Split the selected layers at the playhead"), Action::Split},
        {"compound", TR_NOOP("Create Compound"), Icon::Compound, TR_NOOP("Combine the selected layers; double-click it to open its timeline"),
         Action::CreateCompound},
        {"release", TR_NOOP("Release Compound"), Icon::Ungroup, TR_NOOP("Put a compound's layers back, keeping their look"),
         Action::ReleaseCompound},
        {"duplicate", TR_NOOP("Duplicate"), Icon::Duplicate, TR_NOOP("Duplicate the selected layers"), Action::Duplicate},
        {"delete", TR_NOOP("Delete"), Icon::Trash, TR_NOOP("Delete the selected keyframes, or layers"), Action::Delete},
        {"easy_ease", TR_NOOP("Easy Ease"), Icon::Ease, TR_NOOP("Smooth in and out of the selected keyframes"), Action::EasyEase},
        {"forward", TR_NOOP("Bring Forward"), Icon::LayerUp, TR_NOOP("Move the selected layers up the stack"), Action::BringForward},
        {"backward", TR_NOOP("Send Backward"), Icon::LayerDown, TR_NOOP("Move the selected layers down the stack"), Action::SendBackward},
        {"text", TR_NOOP("Add Text"), Icon::Text, TR_NOOP("A new text layer at the playhead"), Action::AddText},
        {"camera", TR_NOOP("Add Camera"), Icon::Camera, TR_NOOP("A camera to pan, rotate and zoom the scene")},
    };
    return tools;
}

int toolIndex(std::string_view id) {
    const auto& tools = toolCatalog();
    for (size_t i = 0; i < tools.size(); ++i)
        if (id == tools[i].id) return static_cast<int>(i);
    return -1;
}

std::vector<std::string> defaultToolbar() { return {"select", "razor", "pen", "crop", "split", "compound", "delete"}; }

std::string toolTooltip(const ToolInfo& tool) {
    std::string tip = tr(tool.label).toStdString();
    if (tool.shortcut != Action::Count && chordOf(tool.shortcut)) tip += "  (" + chordName(chordOf(tool.shortcut)) + ")";
    return tip + "\n" + tr(tool.description).toStdString();
}

bool toolEnabled(EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    if (id == "split" || id == "compound" || id == "duplicate" || id == "forward" || id == "backward")
        return !ctx.selection.empty();
    if (id == "release") return ctx.selectionHasCompound();
    if (id == "delete") return ctx.hasPicked(); // a transition or Quick Animation picked counts
    if (id == "easy_ease") return !ctx.selectedKeys.empty();
    return true;
}

bool toolActive(const EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    return (id == "select" && ctx.tool == Tool::Select) || (id == "razor" && ctx.tool == Tool::Razor) ||
           (id == "crop" && ctx.tool == Tool::Crop) || (id == "pen" && ctx.tool == Tool::Pen) ||
           (id == "ripple" && ctx.tool == Tool::Ripple) || (id == "roll" && ctx.tool == Tool::Roll) || (id == "slip" && ctx.tool == Tool::Slip);
}

void runTool(EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    if (id == "select") ctx.tool = Tool::Select;
    else if (id == "razor") ctx.tool = Tool::Razor;
    else if (id == "crop") ctx.tool = ctx.tool == Tool::Crop ? Tool::Select : Tool::Crop;
    else if (id == "pen") ctx.tool = ctx.tool == Tool::Pen ? Tool::Select : Tool::Pen;
    else if (id == "ripple") ctx.tool = Tool::Ripple;
    else if (id == "roll") ctx.tool = Tool::Roll;
    else if (id == "slip") ctx.tool = Tool::Slip;
    else if (id == "split") ctx.splitSelectedAtPlayhead();
    else if (id == "compound") ctx.createCompound();
    else if (id == "release") ctx.releaseSelectedCompounds();
    else if (id == "duplicate") ctx.duplicateSelected();
    else if (id == "delete") ctx.deletePicked();
    else if (id == "easy_ease") TimelinePanel::applyEase(ctx, TimelinePanel::Ease::EasyEase);
    else if (id == "forward") ctx.shiftSelected(-1);
    else if (id == "backward") ctx.shiftSelected(1);
    else if (id == "text") ctx.addText();
    else if (id == "camera") ctx.addCamera();
}

} // namespace bemotion::ui
