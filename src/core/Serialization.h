#pragma once

// Project file format (.bemotion): UTF-8 JSON, versioned via Project::kFormatVersion.

#include "core/Project.h"
#include "core/Validate.h"

#include <filesystem>
#include <string>

namespace bemotion {

constexpr const char* kProjectExtension = ".bemotion";

std::string serializeProject(const Project& project);
// Loading repairs whatever breaks the rules (core/Validate.h); `repairs` lists what was fixed.
bool deserializeProject(const std::string& text, Project& out, std::string* error = nullptr,
                        std::vector<Issue>* repairs = nullptr);

bool saveProject(const Project& project, const std::filesystem::path& path, std::string* error = nullptr);
bool loadProject(const std::filesystem::path& path, Project& out, std::string* error = nullptr,
                 std::vector<Issue>* repairs = nullptr);

} // namespace bemotion
