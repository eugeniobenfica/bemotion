#pragma once

// The tools and editing actions that can live in the timeline's toolbar. The
// Tools popover (the toolbar's "+") lists all of them; the user drags the ones
// they want into the toolbar (Settings::toolbar keeps the choice).

#include "ui/Keymap.h"
#include "ui/Widgets.h"

#include <QCursor>

#include <string>
#include <string_view>
#include <vector>

namespace bemotion {

class EditorContext;
enum class Tool;

namespace ui {

// Drag-and-drop payload of a tool: its index in toolCatalog() (int).
inline constexpr const char* kToolPayload = "BEMOTION_TOOL";

struct ToolInfo {
    const char* id;          // stable id, saved in the settings
    const char* label;
    Icon icon;
    const char* description;
    Action shortcut = Action::Count; // shown in the tooltip (follows Settings > Shortcuts)
    bool modal = false;      // a tool you work with (selection, cut) rather than a one-shot action
};

const std::vector<ToolInfo>& toolCatalog();
int toolIndex(std::string_view id); // -1 when unknown
std::vector<std::string> defaultToolbar();
std::string toolTooltip(const ToolInfo& tool);

// Whether the tool's built-in feature is on (ui/Modules.h): off, it is not offered anywhere.
bool toolAvailable(const ToolInfo& tool);
bool toolEnabled(EditorContext& ctx, const ToolInfo& tool);
bool toolActive(const EditorContext& ctx, const ToolInfo& tool); // the current modal tool
// Runs the tool. Call it where layers may change (not while timeline rows are being drawn).
void runTool(EditorContext& ctx, const ToolInfo& tool);
// The mouse pointer while a tool is on: its icon (the arrow for Selection), where the tool
// works (the viewport, the timeline's clips).
QCursor toolCursor(Tool tool);

} // namespace ui
} // namespace bemotion
