#pragma once

// A compact, readable summary of a project: what an AI (or a person in a
// terminal) needs to understand it before editing, in a few lines per layer:
// ids and names to refer to, types, timing, where things are, what is animated.
// The details of one layer come from the "query.layer" operation (core/Ops.h).

#include "core/Project.h"

#include <string>

namespace bemotion {

std::string describeProject(const Project& project);

} // namespace bemotion
