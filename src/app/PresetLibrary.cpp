#include "app/PresetLibrary.h"

#include "app/Platform.h"

#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

std::vector<LibraryPreset>& storage() {
    static std::vector<LibraryPreset> presets;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        PresetLibrary::reload();
    }
    return presets;
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string safeFileName(std::string name) {
    for (char& c : name)
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
    return name.empty() ? "preset" : name;
}

} // namespace

const std::vector<LibraryPreset>& PresetLibrary::all() { return storage(); }

std::filesystem::path PresetLibrary::userDir() {
    const auto dir = platform::appDataDir() / "presets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void PresetLibrary::reload() {
    auto& presets = storage();
    presets.clear();
    for (auto& p : builtinPresets()) presets.push_back({p, {}});
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(userDir(), ec)) {
        if (entry.path().extension() != ".json") continue;
        AnimationPreset p;
        if (parsePreset(readFile(entry.path()), p)) presets.push_back({std::move(p), entry.path()});
    }
}

bool PresetLibrary::importFile(const std::filesystem::path& file, std::string* error) {
    AnimationPreset p;
    if (!parsePreset(readFile(file), p, error)) return false;
    return save(p, error);
}

bool PresetLibrary::save(const AnimationPreset& preset, std::string* error) {
    const auto file = userDir() / platform::fromUtf8(safeFileName(preset.name) + ".json");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!(out << presetToJson(preset))) {
        if (error) *error = "Could not write " + platform::toUtf8(file);
        return false;
    }
    out.close();
    reload();
    return true;
}

void PresetLibrary::remove(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::remove(file, ec);
    reload();
}

} // namespace bemotion
