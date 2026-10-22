#include "app/EffectLibrary.h"

#include "app/Platform.h"

#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

std::vector<LibraryEffectPreset>& storage() {
    static std::vector<LibraryEffectPreset> presets;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        EffectLibrary::reload();
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
    return name.empty() ? "effect" : name;
}

} // namespace

const std::vector<LibraryEffectPreset>& EffectLibrary::all() { return storage(); }

std::filesystem::path EffectLibrary::userDir() {
    const auto dir = platform::appDataDir() / "effects";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void EffectLibrary::reload() {
    auto& presets = storage();
    presets.clear();
    for (auto& p : builtinEffectPresets()) presets.push_back({p, {}});
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(userDir(), ec)) {
        if (entry.path().extension() != ".json") continue;
        EffectPreset p;
        if (parseEffectPreset(readFile(entry.path()), p)) presets.push_back({std::move(p), entry.path()});
    }
}

bool EffectLibrary::importFile(const std::filesystem::path& file, std::string* error) {
    EffectPreset p;
    if (!parseEffectPreset(readFile(file), p, error)) return false;
    return save(p, error);
}

bool EffectLibrary::save(const EffectPreset& preset, std::string* error) {
    const auto file = userDir() / platform::fromUtf8(safeFileName(preset.name) + ".json");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!(out << effectPresetToJson(preset))) {
        if (error) *error = "Could not write " + platform::toUtf8(file);
        return false;
    }
    out.close();
    reload();
    return true;
}

void EffectLibrary::remove(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::remove(file, ec);
    reload();
}

} // namespace bemotion
