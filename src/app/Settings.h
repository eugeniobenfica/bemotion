#pragma once

// User preferences (appearance, editor tuning), persisted as
// <app data>/settings.json, and the theme library: built-in themes plus
// user themes in <app data>/themes (one JSON file each).

#include "ui/Theme.h"
#include "ui/Tools.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace bemotion {

// The tools' own options (the Tool Settings panel), kept between sessions.
struct ToolSettings {
    float brushColor[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // straight RGBA
    float brushSize = 16.0f;     // px
    float brushOpacity = 100.0f; // percent
    float brushSoftness = 0.0f;  // px of blurred edge
    float eraserSize = 32.0f;
    float eraserSoftness = 0.0f;
    bool pinAutoKey = true;      // dragging a pin keys it at the playhead (as After Effects does)
    bool pinStarch = false;      // new Puppet pins are Starch pins (stiffen, hold nothing)
    bool showMesh = true;        // the Puppet tool shows the picture's mesh
    float trackFeature = 24.0f;  // px: the detail's box with a plain click
    float trackSearch = 40.0f;   // px it may move between two frames
    float trackMatch = 60.0f;    // percent: below it, the point is lost
    bool trackBackward = false;  // from the playhead toward the clip's start
    float silenceThreshold = -40.0f; // dB
    float silenceMin = 0.5f;         // s
    float silencePadding = 0.1f;     // s
    bool silenceRipple = true;
    double silenceSampleFrom = -1.0, silenceSampleTo = -1.0; // composition seconds of only silence (-1: none)
    float levelTarget = -18.0f;  // dB
    float levelEvenOut = 50.0f;  // percent
};

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
    std::vector<std::string> disabledModules; // built-in features turned off (ui/Modules.h ids)
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
    bool essentialsOnly = false; // plugins and built-in features (ui/Modules.h) off: the core alone
    // Updates (Settings › General › Updates, app/Updater): off until the user turns them on.
    bool checkUpdates = false;        // look for a new version on GitHub, at most once a day
    std::string updateChannel;        // "stable", "beta"; empty: as this build (a beta follows betas)
    int64_t lastUpdateCheck = 0;      // when it last looked (seconds since 1970)
    std::string skippedVersion;       // "Skip this version": not offered again (a newer one is)
    // Playback (Settings › Editor › Playback).
    bool hardwareDecoding = false; // videos read by the graphics card (media/MediaIO.h); off: a fast processor often beats it
    bool useProxies = true;       // the editor shows videos' proxies when they have one
    int cacheMegabytes = 1024;    // memory for cached frames: half decoded video, half rendered previews
    bool scaledVideoPreview = true; // the editor decodes videos no bigger than it shows them (exports: full size)
    bool adaptivePlayback = true;   // playing, the viewport lowers its resolution when a frame takes too long (paused: full)
    bool gpuRendering = true;       // the editor and its exports draw on the graphics card when OpenGL works (render/Gpu.h)
    std::string renderEngine = "opengl"; // with gpuRendering: "opengl" or "vulkan" (ui/GpuContext)
    bool autoProxies = false;       // imported videos bigger than 1280 x 720 get a proxy at once
    bool showFilmstrips = true;     // video clips show their frames on the timeline
    // Animate's folders: the user's own (in order, after the built-in ones), where items were
    // moved ("t:<transition id>" / "p:<preset name>" -> folder) and the folders closed.
    std::vector<std::string> animationFolders;
    std::map<std::string, std::string> animationFolderOf;
    std::vector<std::string> closedAnimationFolders;
    std::vector<std::string> closedEffectCategories; // the Effects browser's, by their English name
    std::vector<std::string> closedObjectCategories;
    // Plugin settings (plugin.json "settings", Settings › Plugins › a plugin's gear): by plugin id,
    // then setting id, each value as JSON text ("true", "3", "\"Pastel\"").
    std::map<std::string, std::map<std::string, std::string>> pluginSettings;
    ToolSettings tools;
    int processorThreads = 0;    // decoding, encoding, transcribing: threads each may use (0: automatic)
    int gpuCacheMegabytes = 512; // the graphics card's memory kept for pictures between frames // the Objects panel's, by their English name (plugins: their id)
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
