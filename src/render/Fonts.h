#pragma once

// Fonts for text layers and plugin text, through Skia.
//
// The bundled Inter family (Regular / Medium / SemiBold / Bold) is always
// there; every installed family comes from the system font manager
// (DirectWrite on Windows, CoreText on macOS, fontconfig on Linux).
// Missing characters fall back to other installed fonts while shaping.

#include "core/Project.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

class Fonts {
public:
    static constexpr const char* kDefaultFamily = "Inter";

    static void load(const std::filesystem::path& assetsDir); // the bundled Inter faces
    static sk_sp<SkFontMgr> manager();

    // The face for a family and style name ("Bold", "SemiBold Italic", ...); Inter when the
    // family is not installed, the closest weight / slant when the style is not.
    static sk_sp<SkTypeface> typeface(const std::string& family, const std::string& style);
    static sk_sp<SkTypeface> forWeight(FontWeight weight); // Inter

    static std::vector<std::string> families();                         // sorted, Inter first
    static std::vector<std::string> stylesOf(const std::string& family); // regular first
    static bool hasFamily(const std::string& family);

    static std::filesystem::path interFile(const std::string& style); // for the UI (Qt)
};

} // namespace bemotion
