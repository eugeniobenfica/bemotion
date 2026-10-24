#pragma once

#include "core/Project.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace bemotion {

// Writes a portable ZIP: Project.bemotion plus every project-linked media and
// plugin file. The existing output is replaced only after a complete package.
bool packageProject(const Project& project, const std::filesystem::path& projectFile,
                    const std::filesystem::path& output, std::string* error = nullptr,
                    const std::atomic_bool* cancelled = nullptr);

} // namespace bemotion
