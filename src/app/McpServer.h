#pragma once

// bemotion as an MCP server (Model Context Protocol): AI assistants such as
// Claude start `bemotion mcp` and call its tools to create, inspect, edit,
// look at and export projects. Messages are JSON-RPC 2.0, one per line, on
// stdin / stdout (the MCP "stdio" transport); nothing else is written to stdout.
//
// The tools are thin wrappers over the operations (core/Ops.h) and the
// automation helpers (app/Automation.h): every edit is validated and atomic,
// exactly as from the command line or a plugin. See docs/AUTOMATION.md for
// connecting an assistant.

namespace bemotion {

int runMcpServer(); // returns when stdin closes

} // namespace bemotion
