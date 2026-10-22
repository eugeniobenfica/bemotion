#pragma once

// Fonts for the UI and for text layers.
//
// The bundled Inter family (Regular/Medium/SemiBold/Bold) is always there.
// System fonts are discovered in the background (family/style names read
// from each file's name table, cached in <app data>/fontcache.json) and
// loaded into ImGui's atlas the first time a text layer uses them. ImGui
// 1.92+ rasterizes glyphs on demand at any size, so one ImFont per face is
// enough.

#include "core/Project.h"

#include <filesystem>
#include <string>
#include <vector>

struct ImFont;

namespace bemotion {

struct FontFace {
    std::string family;
    std::string style;
    std::filesystem::path path;
    int index = 0;    // face inside a .ttc collection
    int weight = 400; // OS/2 weight class
    bool italic = false;
};

class Fonts {
public:
    static constexpr float kUiSize = 15.0f;
    static constexpr const char* kDefaultFamily = "Inter";

    static void load(const std::filesystem::path& assetsDir);
    // Starts scanning installed fonts on a background thread.
    static void startSystemScan(const std::filesystem::path& cacheFile);
    static bool systemScanReady();
    // Adds fonts requested since the last call to the atlas (call between frames).
    static void processPending();

    static ImFont* ui();
    static ImFont* uiMedium();
    static ImFont* uiBold();
    static ImFont* forWeight(FontWeight weight);

    // Font for a text layer; falls back to Inter while a face loads or if missing.
    static ImFont* forText(const TextProps& text);
    // Queues every face the composition uses; true once all are loaded (or
    // known to be missing). Exports wait for this so no frame uses a fallback.
    static bool ensureLoaded(const Composition& comp);

    static std::vector<std::string> families();                    // sorted, Inter first
    static std::vector<std::string> stylesOf(const std::string& family); // regular first
    static bool hasFamily(const std::string& family);
};

// Reads the faces of one font file (several for .ttc collections).
std::vector<FontFace> scanFontFile(const std::filesystem::path& file);

} // namespace bemotion
