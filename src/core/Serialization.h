#pragma once

// Project file format (.bemotion): UTF-8 JSON, versioned via Project::kFormatVersion.

#include "core/Project.h"
#include "core/Validate.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace bemotion {

constexpr const char* kProjectExtension = ".bemotion";

std::string serializeProject(const Project& project);
// One layer as the project file writes it (core/Changes.h compares layers this way).
std::string serializeLayer(const Layer& layer);
// Loading repairs whatever breaks the rules (core/Validate.h); `repairs` lists what was fixed.
bool deserializeProject(const std::string& text, Project& out, std::string* error = nullptr,
                        std::vector<Issue>* repairs = nullptr);

bool saveProject(const Project& project, const std::filesystem::path& path, std::string* error = nullptr);
bool loadProject(const std::filesystem::path& path, Project& out, std::string* error = nullptr,
                 std::vector<Issue>* repairs = nullptr);

// A Bézier path as {"closed": true, "vertices": [[x, y, in_x, in_y, out_x, out_y], ...]} (a
// vertex may be just [x, y]: a corner). Also what operations take for "path".
nlohmann::json pathToJson(const BezierPath& path);
bool pathFromJson(const nlohmann::json& j, BezierPath& out, std::string* error = nullptr);

} // namespace bemotion
