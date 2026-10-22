#pragma once

// Built-in + user Quick Animation presets. User presets are JSON files in
// <app data>/presets; importing copies a file there.

#include "core/Presets.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

struct LibraryPreset {
    AnimationPreset preset;
    std::filesystem::path file; // empty for built-ins
};

class PresetLibrary {
public:
    static const std::vector<LibraryPreset>& all();
    static void reload();
    static std::filesystem::path userDir();

    static bool importFile(const std::filesystem::path& file, std::string* error = nullptr);
    static bool save(const AnimationPreset& preset, std::string* error = nullptr);
    static void remove(const std::filesystem::path& file);
};

} // namespace bemotion
