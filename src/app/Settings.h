#pragma once

// User preferences (appearance, editor tuning), persisted as
// <app data>/settings.json, and the theme library: built-in themes plus
// user themes in <app data>/themes (one JSON file each).

#include "ui/Theme.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

struct Settings {
    std::string theme = "Dark"; // ThemeLibrary entry
    bool customAccent = false;  // false: use the theme's own accent
    ImU32 accent = IM_COL32(232, 185, 49, 255);
    bool advanced = false;      // show every option in the settings window
    // Advanced mode: colors edited by hand override the theme.
    bool customPalette = false;
    theme::Palette palette;
    float snapDistance = 8.0f; // screen pixels
    std::vector<std::string> disabledPlugins; // plugin ids
};

Settings& settings();
void loadSettings();
void saveSettings();

// The palette in effect: theme (or hand-edited colors) plus the accent choice.
theme::Palette effectivePalette();
// Applies effectivePalette() to the UI. `dpiScale` is remembered from the first call.
void applySettings(float dpiScale = 0.0f);

struct LibraryTheme {
    theme::Palette palette;
    std::filesystem::path file; // empty for built-ins
};

class ThemeLibrary {
public:
    static const std::vector<LibraryTheme>& all();
    static void reload();
    static std::filesystem::path userDir();
    static const LibraryTheme* find(const std::string& name);

    static bool importFile(const std::filesystem::path& file, std::string* importedName = nullptr,
                           std::string* error = nullptr);
    static bool save(const theme::Palette& palette, std::string* error = nullptr);
    static bool exportTo(const theme::Palette& palette, const std::filesystem::path& file, std::string* error = nullptr);
};

} // namespace bemotion
