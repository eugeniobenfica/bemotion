#pragma once

// bemotion for scripts and AI tools, from the command line:
//
//   bemotion describe <project> [--json]              what is in it (ids, names, timing, animation)
//   bemotion validate <project> [--fix]               checks the rules; --fix saves the repaired file
//   bemotion apply <project> <ops.json | -> [--out <file>] [--dry-run] [--describe]
//   bemotion apply <project> --ops '<json>' ...       operations inline (docs/OPERATIONS.md)
//   bemotion render <project> [--at t1,t2,...] [--count N] [--width W] [--each] --out <file.png>
//                                                     frames to look at: one contact sheet, or one file each
//   bemotion script <project> <file.lua | -> [--time T] [--out <file>] [--dry-run]
//                                                     Lua with the plugin `doc` API (docs/PLUGINS.md)
//   bemotion new <project> [--width W --height H --fps F]  an empty project
//   bemotion mcp                                      MCP server for AI assistants (app/McpServer.h)
//
// Results go to stdout (JSON for apply, text for describe / validate); the exit
// code is 0 on success. The same operations run inside the app (Lua plugins)
// through automationEnvironment().

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

bool isAutomationCommand(const std::string& arg);
// args[0] is the program, args[1] the command. Returns the exit code.
int runAutomation(const std::vector<std::string>& args);

// GUI-subsystem executable on Windows: print to the terminal that launched it, if any.
void attachParentConsole();

} // namespace bemotion
