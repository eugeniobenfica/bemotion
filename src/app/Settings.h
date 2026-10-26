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
    float timelineLayerRow = 46.0f;    // clip rows
    float timelinePropertyRow = 38.0f; // keyframe rows (group headers derive from it)
    float keyframeSize = 6.0f;         // keyframe glyph radius
    float textScale = 1.0f;            // UI text size multiplier
    std::string uiFont = "Inter";      // interface font family (any installed one)
    int arrowFrames = 1;               // frames stepped by the arrow keys
    int bigArrowFrames = 10;           // ... with Shift (big step)
    int autosaveMinutes = 5;           // 0: off; else open projects save after every change (the name is older)
    bool showIntro = true;             // the opening animation at startup
    bool showRulers = false;           // the viewport's rulers (View > Rulers)
    int previewQuality = 1;            // raster preview divisor: 1, 2 or 4 (export is unaffected)
    bool welcomeAdded = false;         // the Welcome project was put in the projects folder once
    bool welcomeRemoved = false;       // the user deleted the Welcome project (Settings › Interface brings it back)
    std::string projectsFolder;        // where projects live, chosen by the user (empty: not yet)
    std::string speechLanguage = "auto"; // speech to text: "auto" or a language code ("en", "pt"...)
    std::vector<std::string> toolbar = ui::defaultToolbar(); // tool ids in the timeline's toolbar (ui/Tools.h)
    bool toolbarInViewport = false;      // the toolbar floats outside the timeline ...
    bool toolbarInWindow = false;        // window-relative floating position; older viewport positions stay unchanged
    float toolbarX = 0.5f, toolbarY = 0.0f; // ... with its top-left here (fractions of the free space)
    std::vector<std::string> disabledPlugins; // plugin ids
    std::vector<std::string> recentColors;    // the color picker's last colors, "#RRGGBBAA", newest first
    bool showMeters = true;                   // the left / right audio level meters beside the timeline
    // What the interface shows, one by one (Settings › Interface › Show); Friendly starts with
    // the first two off.
    bool showExpressionButtons = true; // the ƒ button beside each property (one with an expression keeps it)
    bool showGraphButton = true;       // the timeline's graph editor button
    bool showSnapshotButton = true;    // the viewport's Export Frame button
    bool showTimelineScrollbar = true; // the timeline's horizontal scrollbar
    // How much the interface shows: "friendly" (the essentials), "balanced" or "expert" (all of
    // it). Chosen on the first start (ui/ExperienceScreen), changed in Settings › Interface.
    std::string experience = "balanced";
    bool essentialsOnly = false; // plugins off (no charts, maps, 3D models, recorder): the core alone
    // Playback (Settings › Editor › Playback).
    bool hardwareDecoding = false; // videos read by the graphics card (media/MediaIO.h); off: a fast processor often beats it
    bool useProxies = true;       // the editor shows videos' proxies when they have one
    int cacheMegabytes = 1024;    // memory for cached frames: half decoded video, half rendered previews
    bool scaledVideoPreview = true; // the editor decodes videos no bigger than it shows them (exports: full size)
    bool autoProxies = false;       // imported videos bigger than 1280 x 720 get a proxy at once
    bool showFilmstrips = true;     // video clips show their frames on the timeline
    // The interface language: "auto" (the system's, when there is a file for it), "en", or a
    // language file's code ("pt-BR"); see ui/Language.h. Takes effect at the next start.
    std::string language = "auto";
    bool firstRun = false;       // no settings file yet: this is bemotion's first start (not saved)
};

Settings& settings();
// The experience's presets: the toolbar, the extras (rulers, audio meters), the advanced
// options. The rest of the interface asks friendlyMode() / expertMode().
void applyExperience(const std::string& experience);
bool friendlyMode();
bool expertMode();
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
