#pragma once

// What the editor, the command line (app/Automation.h) and the MCP server share:
// the environment operations run in, frames as PNG, exporting a project.

#include "core/Ops.h"
#include "export/Export.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bemotion {

// What operations need from the modules above the core: media probing, plugin objects.
OpEnvironment automationEnvironment();

// `count` moments evenly spread over the composition (first and last frame included).
std::vector<double> spreadTimes(const Composition& comp, int count);
// Renders the moments as a PNG: one frame, or a contact sheet (2-3 columns) for several.
bool renderFramesPng(const Composition& comp, std::vector<double> times, int frameWidth, std::vector<uint8_t>& png,
                     std::string& error);
// Exports a video / image sequence (waits for audio to decode); progress(done, total) per step.
bool exportProject(Project project, exporting::Settings settings, const std::function<void(int, int)>& progress,
                   std::string& error);

} // namespace bemotion
