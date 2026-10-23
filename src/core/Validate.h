#pragma once

// The rules a project must follow, checked in one place.
//
// Every editing path (the interface, operations from plugins, the command line
// or an AI) must leave the project valid; validate() says what is wrong and
// repair() fixes what can be fixed (loading a damaged or hand-edited file,
// cleaning up after a bug) without losing content.

#include "core/Project.h"

#include <string>
#include <vector>

namespace bemotion {

struct Issue {
    std::string where; // "composition", "layer 12 \"Title\"", "layer 12 \"Title\" / opacity"
    std::string what;  // human readable, also for AIs
};

std::vector<Issue> validate(const Project& project);

// Fixes every issue validate() can report and returns what it changed
// (empty: the project was already valid). Afterwards validate() is empty.
std::vector<Issue> repair(Project& project);

} // namespace bemotion
