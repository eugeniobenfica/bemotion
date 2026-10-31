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
// through automationEnvironment() (app/Services.h).

#include "app/Services.h"

#include <string>
#include <vector>

namespace bemotion {

bool isAutomationCommand(const std::string& arg);
// args[0] is the program, args[1] the command. Returns the exit code.
int runAutomation(const std::vector<std::string>& args);

// GUI-subsystem executable on Windows: print to the terminal that launched it, if any.
void attachParentConsole();

} // namespace bemotion
