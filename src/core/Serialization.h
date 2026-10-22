#pragma once

// Project file format (.bemotion): UTF-8 JSON, versioned via Project::kFormatVersion.

#include "core/Project.h"

#include <filesystem>
#include <string>

namespace bemotion {

constexpr const char* kProjectExtension = ".bemotion";

std::string serializeProject(const Project& project);
bool deserializeProject(const std::string& text, Project& out, std::string* error = nullptr);

bool saveProject(const Project& project, const std::filesystem::path& path, std::string* error = nullptr);
bool loadProject(const std::filesystem::path& path, Project& out, std::string* error = nullptr);

} // namespace bemotion
