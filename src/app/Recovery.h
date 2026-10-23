#pragma once

// Crash recovery: while a project has unsaved changes, the editor keeps a copy
// of it in <app data>/recovery (one per project, replaced each time). Saving or
// closing normally removes it, so whatever is left there when bemotion starts
// was lost by a crash and can be offered back.

#include "core/Project.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bemotion::recovery {

struct Entry {
    std::filesystem::path copy;     // the kept project
    std::filesystem::path original; // where it was being edited (may be empty)
    std::string name;
    int64_t savedAt = 0; // unix seconds
};

// Keeps `project` aside for `original` (replacing the previous copy).
bool keep(const Project& project, const std::filesystem::path& original, std::string* error = nullptr);
// Removes the copy kept for `original`.
void discard(const std::filesystem::path& original, const std::string& name);
void discard(const Entry& entry);
// Copies left by sessions that did not end normally, newest first.
std::vector<Entry> pending();

} // namespace bemotion::recovery
