#pragma once

#include "core/Layer.h"

#include <filesystem>

struct ImFont;

namespace mirage {

// Fonts shared by the UI and the canvas. ImGui 1.92+ rasterizes glyphs on
// demand at any size, so a single ImFont per weight is enough.
class Fonts {
public:
    static constexpr float kUiSize = 15.0f;

    static void load(const std::filesystem::path& assetsDir);

    static ImFont* ui();
    static ImFont* uiMedium();
    static ImFont* uiBold();
    static ImFont* forWeight(FontWeight weight);
};

} // namespace mirage
