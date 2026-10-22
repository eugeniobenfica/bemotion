#pragma once

// Built-in + user effect presets (effect stacks). User presets are JSON files
// in <app data>/effects; importing copies a file there.

#include "core/Effects.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

struct LibraryEffectPreset {
    EffectPreset preset;
    std::filesystem::path file; // empty for built-ins
};

class EffectLibrary {
public:
    static const std::vector<LibraryEffectPreset>& all();
    static void reload();
    static std::filesystem::path userDir();

    static bool importFile(const std::filesystem::path& file, std::string* error = nullptr);
    static bool save(const EffectPreset& preset, std::string* error = nullptr);
    static void remove(const std::filesystem::path& file);
};

} // namespace bemotion
