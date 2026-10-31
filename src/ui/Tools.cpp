#include "ui/Tools.h"

#include "ui/Language.h"

#include "ui/EditorContext.h"
#include "ui/Modules.h"
#include "ui/Theme.h"
#include "ui/panels/TimelinePanel.h"

#include <QPainter>
#include <QPixmap>

#include <map>

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
        {"brush", TR_NOOP("Brush"), Icon::Brush,
         TR_NOOP("Paint on the selected layer (or on a new one); its color and size are in the viewport's corner"), Action::BrushTool, true},
        {"eraser", TR_NOOP("Eraser"), Icon::Eraser, TR_NOOP("Erase the selected layer's picture and paint where you drag"), Action::EraserTool, true},
        {"puppet", TR_NOOP("Puppet Pin"), Icon::Puppet,
         TR_NOOP("Click the selected layer to pin it, drag pins to bend it (Alt+click a pin: remove it)"), Action::PuppetTool, true},
        {"warp", TR_NOOP("Warp"), Icon::Warp, TR_NOOP("Drag the points of a grid over the selected layer to bend it"), Action::WarpTool, true},
        {"track", TR_NOOP("Track Point"), Icon::Track,
         TR_NOOP("Click a detail of the selected video: it is followed frame by frame, and a layer moves with it"), Action::TrackTool, true},
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

bool toolAvailable(const ToolInfo& tool) {
    const std::string_view id = tool.id;
    if (id == "puppet" || id == "warp") return moduleEnabled(kPuppetWarp);
    if (id == "track") return moduleEnabled(kPointTracking);
    return true;
}

bool toolEnabled(EditorContext& ctx, const ToolInfo& tool) {
    const std::string_view id = tool.id;
    if (!toolAvailable(tool)) return false;
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
           (id == "ripple" && ctx.tool == Tool::Ripple) || (id == "roll" && ctx.tool == Tool::Roll) || (id == "slip" && ctx.tool == Tool::Slip) ||
           (id == "brush" && ctx.tool == Tool::Brush) || (id == "eraser" && ctx.tool == Tool::Eraser) ||
           (id == "puppet" && ctx.tool == Tool::Puppet) || (id == "warp" && ctx.tool == Tool::Warp) || (id == "track" && ctx.tool == Tool::Track);
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
    else if (id == "brush") ctx.tool = ctx.tool == Tool::Brush ? Tool::Select : Tool::Brush;
    else if (id == "eraser") ctx.tool = ctx.tool == Tool::Eraser ? Tool::Select : Tool::Eraser;
    else if (id == "puppet") ctx.tool = ctx.tool == Tool::Puppet ? Tool::Select : Tool::Puppet;
    else if (id == "warp") ctx.tool = ctx.tool == Tool::Warp ? Tool::Select : Tool::Warp;
    else if (id == "track") ctx.tool = ctx.tool == Tool::Track ? Tool::Select : Tool::Track;
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

QCursor toolCursor(Tool tool) {
    Icon icon = Icon::Pointer;
    QPoint hot(16, 16); // where it acts: the middle of its icon, the tip of the pen
    switch (tool) {
    case Tool::Select: return QCursor(Qt::ArrowCursor);
    case Tool::Razor: icon = Icon::Razor; break;
    case Tool::Crop: icon = Icon::Crop; break;
    case Tool::Pen: icon = Icon::Pen, hot = QPoint(7, 25); break;
    case Tool::Ripple: icon = Icon::RippleEdit; break;
    case Tool::Roll: icon = Icon::RollEdit; break;
    case Tool::Slip: icon = Icon::SlipEdit; break;
    case Tool::Brush:
    case Tool::Eraser: return QCursor(Qt::BlankCursor); // the viewport draws the brush's ring, nothing else
    case Tool::Puppet: icon = Icon::Puppet; break;
    case Tool::Warp: icon = Icon::Warp; break;
    case Tool::Track: icon = Icon::Track; break;
    }
    // Made once per tool and theme: its icon with a halo, readable on any picture.
    static std::map<std::pair<int, Rgba>, QCursor> made;
    const auto key = std::make_pair(static_cast<int>(tool), theme::kText);
    if (auto it = made.find(key); it != made.end()) return it->second;
    constexpr qreal kScale = 2.0; // sharp on high-density screens
    QPixmap pixmap(static_cast<int>(32 * kScale), static_cast<int>(32 * kScale));
    pixmap.fill(Qt::transparent);
    {
        QPainter p(&pixmap);
        const QPixmap halo = makeIcon(icon, theme::kBgDeep, 64).pixmap(static_cast<int>(22 * kScale));
        const QPixmap shape = makeIcon(icon, theme::kText, 64).pixmap(static_cast<int>(22 * kScale));
        const QPointF at(5 * kScale, 5 * kScale);
        for (int dx = -2; dx <= 2; dx += 2)
            for (int dy = -2; dy <= 2; dy += 2)
                if (dx || dy) p.drawPixmap(at + QPointF(dx, dy), halo);
        p.drawPixmap(at, shape);
    }
    pixmap.setDevicePixelRatio(kScale);
    return made[key] = QCursor(pixmap, hot.x(), hot.y());
}

} // namespace bemotion::ui
