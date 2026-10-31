#pragma once

// Built-in features that can be turned off, as plugins can (Settings › Plugins › Built-in
// features): what needs native speed stays in the app, yet nobody has to see what they do not
// use. Off, a feature leaves the interface (its menus, tools and buttons); operations, the
// command line and MCP keep it, since scripts and files rely on them. Essentials only turns them
// all off. Saved as the ids of the ones turned off (Settings::disabledModules).

#include "ui/Widgets.h"

#include <string_view>
#include <vector>

namespace bemotion::ui {

inline constexpr const char* kSpeechToText = "speech_to_text"; // Transcribe (ui/Transcribe)
inline constexpr const char* kPointTracking = "point_tracking"; // the Track Point tool
inline constexpr const char* kPuppetWarp = "puppet_warp";       // the Puppet Pin and Warp tools
inline constexpr const char* kSvgImport = "svg_import";         // File › Import SVG, dropped .svg files
inline constexpr const char* kLottieExport = "lottie_export";   // File › Export Lottie
inline constexpr const char* kScopes = "scopes";                // the Scopes panel

struct Module {
    const char* id; // saved: never renamed
    const char* name;
    const char* description;
    Icon icon;
};

const std::vector<Module>& modules();
bool moduleEnabled(std::string_view id);
// Saves the choice and emits AppSignals::modulesChanged.
void setModuleEnabled(std::string_view id, bool on);

} // namespace bemotion::ui
