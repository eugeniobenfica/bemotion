#include "render/Fonts.h"

#include "app/Platform.h"

#include <imgui.h>

namespace bemotion {

namespace {
ImFont* g_weights[static_cast<int>(FontWeight::Count)] = {};

constexpr const char* kFiles[] = {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"};
} // namespace

void Fonts::load(const std::filesystem::path& assetsDir) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    for (int i = 0; i < static_cast<int>(FontWeight::Count); ++i) {
        const auto file = assetsDir / "fonts" / kFiles[i];
        std::error_code ec;
        ImFont* font = nullptr;
        if (std::filesystem::exists(file, ec))
            font = io.Fonts->AddFontFromFileTTF(platform::toUtf8(file).c_str(), kUiSize, &cfg);
        g_weights[i] = font;
    }
    // Fall back to the built-in font if the assets are missing.
    ImFont* fallback = g_weights[0] ? g_weights[0] : io.Fonts->AddFontDefault();
    for (auto& f : g_weights)
        if (!f) f = fallback;
    io.FontDefault = g_weights[0];
}

ImFont* Fonts::ui() { return g_weights[static_cast<int>(FontWeight::Regular)]; }
ImFont* Fonts::uiMedium() { return g_weights[static_cast<int>(FontWeight::Medium)]; }
ImFont* Fonts::uiBold() { return g_weights[static_cast<int>(FontWeight::SemiBold)]; }

ImFont* Fonts::forWeight(FontWeight weight) {
    const int i = static_cast<int>(weight);
    return (i >= 0 && i < static_cast<int>(FontWeight::Count)) ? g_weights[i] : g_weights[0];
}

} // namespace bemotion
