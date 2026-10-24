#pragma once

// User preferences (appearance, editor tuning), persisted as
// <app data>/settings.json, and the theme library: built-in themes plus
// user themes in <app data>/themes (one JSON file each).

#include "ui/Theme.h"
#include "ui/Tools.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

struct Settings {
    std::string theme = "Dark"; // ThemeLibrary entry
    bool customAccent = false;  // false: use the theme's own accent
    theme::Rgba accent = theme::rgba(232, 185, 49);
    bool advanced = false;      // show every option in the settings window (all pages)
    // Advanced mode: colors edited by hand override the theme.
    bool customPalette = false;
    theme::Palette palette;
    float snapDistance = 8.0f; // screen pixels
    // Interface sizes.
    float timelineLayerRow = 36.0f;    // clip rows
    float timelinePropertyRow = 30.0f; // keyframe rows (group headers derive from it)
    float keyframeSize = 5.0f;         // keyframe glyph radius
    float textScale = 1.0f;            // UI text size multiplier
    std::string uiFont = "Inter";      // interface font family (any installed one)
    int arrowFrames = 1;               // frames stepped by the arrow keys
    int bigArrowFrames = 10;           // ... with Shift (big step)
    double maxDuration = 0.0;          // length limit of new projects, seconds (0: none)
    int autosaveMinutes = 5;           // saves open projects this often (0: off)
    bool showIntro = true;             // the opening animation at startup
    bool showRulers = false;           // the viewport's rulers (View > Rulers)
    int previewQuality = 1;            // raster preview divisor: 1, 2 or 4 (export is unaffected)
    bool welcomeAdded = false;         // the Welcome project was put in the projects folder once
    std::string projectsFolder;        // where projects live, chosen by the user (empty: not yet)
    std::vector<std::string> toolbar = ui::defaultToolbar(); // tool ids in the timeline's toolbar (ui/Tools.h)
    bool toolbarInViewport = false;      // the toolbar floats outside the timeline ...
    bool toolbarInWindow = false;        // window-relative floating position; older viewport positions stay unchanged
    float toolbarX = 0.5f, toolbarY = 0.0f; // ... with its top-left here (fractions of the free space)
    std::vector<std::string> disabledPlugins; // plugin ids
};

Settings& settings();
// The projects folder the user chose, if it still exists (else empty).
std::filesystem::path projectsFolder();
// Where file dialogs start: the projects folder, else Documents.
std::filesystem::path projectsFolderOrDocuments();
void loadSettings();
void saveSettings();

// The palette in effect: theme (or hand-edited colors) plus the accent choice.
theme::Palette effectivePalette();
// Applies effectivePalette() and the interface font / text size to the application.
void applySettings();

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
