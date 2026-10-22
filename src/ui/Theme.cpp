#include "ui/Theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>

namespace bemotion::theme {

namespace {

using json = nlohmann::json;

Palette g_current;

int ch(ImU32 c, int shift) { return static_cast<int>((c >> shift) & 0xFF); }

// Linear blend of two colors (alpha included).
ImU32 mix(ImU32 a, ImU32 b, float t) {
    auto m = [&](int shift) { return static_cast<int>(ch(a, shift) + (ch(b, shift) - ch(a, shift)) * t + 0.5f); };
    return IM_COL32(m(IM_COL32_R_SHIFT), m(IM_COL32_G_SHIFT), m(IM_COL32_B_SHIFT), m(IM_COL32_A_SHIFT));
}

float luminance(ImU32 c) {
    return (0.2126f * ch(c, IM_COL32_R_SHIFT) + 0.7152f * ch(c, IM_COL32_G_SHIFT) + 0.0722f * ch(c, IM_COL32_B_SHIFT)) / 255.0f;
}

std::string toHex(ImU32 c) {
    char buf[16];
    if (ch(c, IM_COL32_A_SHIFT) == 255)
        std::snprintf(buf, sizeof buf, "#%02X%02X%02X", ch(c, IM_COL32_R_SHIFT), ch(c, IM_COL32_G_SHIFT), ch(c, IM_COL32_B_SHIFT));
    else
        std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", ch(c, IM_COL32_R_SHIFT), ch(c, IM_COL32_G_SHIFT),
                      ch(c, IM_COL32_B_SHIFT), ch(c, IM_COL32_A_SHIFT));
    return buf;
}

bool fromHex(const std::string& s, ImU32& out) {
    if (s.empty() || s[0] != '#' || (s.size() != 7 && s.size() != 9)) return false;
    unsigned v = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + static_cast<unsigned>(d);
    }
    if (s.size() == 7) v = (v << 8) | 0xFF;
    out = IM_COL32((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

// Every color field, for (de)serialization.
template <typename P, typename Fn>
void forEachColor(P& p, Fn&& fn) {
    fn("background", p.bgDeep);
    fn("panel", p.bgPanel);
    fn("raised", p.bgRaised);
    fn("hover", p.bgHover);
    fn("border", p.border);
    fn("text", p.text);
    fn("text_dim", p.textDim);
    fn("text_faint", p.textFaint);
    fn("accent", p.accent);
    fn("danger", p.danger);
    fn("snap", p.snap);
    fn("guide", p.guide);
    fn("canvas", p.canvas);
}

} // namespace

Palette darkPalette() { return {}; }

Palette lightPalette() {
    Palette p;
    p.name = "Light";
    p.light = true;
    p.bgDeep = IM_COL32(222, 222, 227, 255);
    p.bgPanel = IM_COL32(240, 240, 243, 255);
    p.bgRaised = IM_COL32(252, 252, 253, 255);
    p.bgHover = IM_COL32(228, 228, 234, 255);
    p.border = IM_COL32(206, 206, 213, 255);
    p.text = IM_COL32(28, 28, 32, 255);
    p.textDim = IM_COL32(88, 88, 98, 255);
    p.textFaint = IM_COL32(140, 140, 150, 255);
    p.accent = IM_COL32(214, 158, 14, 255);
    p.danger = IM_COL32(208, 68, 52, 255);
    p.snap = IM_COL32(226, 38, 136, 255);
    p.guide = IM_COL32(0, 146, 214, 220);
    p.canvas = IM_COL32(204, 204, 210, 255);
    return p;
}

Palette midnightPalette() {
    Palette p;
    p.name = "Midnight";
    p.bgDeep = IM_COL32(10, 13, 22, 255);
    p.bgPanel = IM_COL32(16, 20, 32, 255);
    p.bgRaised = IM_COL32(24, 29, 44, 255);
    p.bgHover = IM_COL32(33, 40, 58, 255);
    p.border = IM_COL32(38, 45, 64, 255);
    p.text = IM_COL32(226, 231, 245, 255);
    p.textDim = IM_COL32(140, 150, 175, 255);
    p.textFaint = IM_COL32(88, 98, 125, 255);
    p.accent = IM_COL32(110, 168, 255, 255);
    p.canvas = IM_COL32(22, 27, 40, 255);
    return p;
}

bool paletteFromJson(const std::string& text, Palette& out, std::string* error) {
    try {
        const json j = json::parse(text);
        if (!j.is_object()) throw std::runtime_error("expected a JSON object");
        Palette p = j.value("light", false) ? lightPalette() : darkPalette();
        p.name = j.value("name", std::string("Imported"));
        p.light = j.value("light", false);
        p.rounding = std::clamp(j.value("rounding", p.rounding), 0.0f, 12.0f);
        const json colors = j.value("colors", json::object());
        forEachColor(p, [&](const char* key, ImU32& c) {
            if (colors.contains(key) && colors[key].is_string() && !fromHex(colors[key].get<std::string>(), c))
                throw std::runtime_error(std::string("bad color for \"") + key + "\"");
        });
        out = p;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::string paletteToJson(const Palette& palette) {
    json colors = json::object();
    forEachColor(palette, [&](const char* key, const ImU32& c) { colors[key] = toHex(c); });
    json j = {{"name", palette.name}, {"light", palette.light}, {"rounding", palette.rounding}, {"colors", colors}};
    return j.dump(2);
}

ImU32 withAlpha(ImU32 c, int alpha) { return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT); }
ImU32 accent(int alpha) { return withAlpha(kAccent, alpha); }
ImU32 ink(int alpha) { return g_current.light ? IM_COL32(0, 0, 0, alpha) : IM_COL32(255, 255, 255, alpha); }
ImU32 shade(int alpha) { return IM_COL32(0, 0, 0, g_current.light ? alpha / 3 : alpha); }

const Palette& current() { return g_current; }

void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 4);
    s.IndentSpacing = 14;
    s.ScrollbarSize = 10;
    s.GrabMinSize = 10;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;
    s.DockingSeparatorSize = 2;
}

void apply(const Palette& p, float dpiScale) {
    g_current = p;
    kBgDeep = p.bgDeep;
    kBgPanel = p.bgPanel;
    kBgRaised = p.bgRaised;
    kBgHover = p.bgHover;
    kBorder = p.border;
    kText = p.text;
    kTextDim = p.textDim;
    kTextFaint = p.textFaint;
    kAccent = withAlpha(p.accent, 255);
    kAccentHover = mix(kAccent, IM_COL32(255, 255, 255, 255), 0.18f);
    kAccentActive = mix(kAccent, IM_COL32(0, 0, 0, 255), 0.12f);
    kAccentSoft = withAlpha(kAccent, 40);
    kOnAccent = luminance(kAccent) > 0.55f ? IM_COL32(20, 18, 12, 255) : IM_COL32(255, 255, 255, 255);
    kDanger = p.danger;
    kSnap = p.snap;
    kGuide = p.guide;
    kCanvas = p.canvas;

    ImGuiStyle& s = ImGui::GetStyle();
    const float r = std::clamp(p.rounding, 0.0f, 12.0f) * dpiScale;
    s.FrameRounding = r;
    s.WindowRounding = r * 1.6f;
    s.ChildRounding = r * 1.2f;
    s.PopupRounding = r * 1.2f;
    s.ScrollbarRounding = r * 1.6f;
    s.GrabRounding = r * 0.8f;
    s.TabRounding = r;

    const ImU32 popup = withAlpha(mix(p.bgPanel, p.bgRaised, 0.5f), 250);
    const ImU32 pressed = mix(p.bgHover, p.text, 0.08f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = vec(kText);
    c[ImGuiCol_TextDisabled] = vec(kTextFaint);
    c[ImGuiCol_WindowBg] = vec(kBgPanel);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = vec(popup);
    c[ImGuiCol_Border] = vec(kBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = vec(kBgRaised);
    c[ImGuiCol_FrameBgHovered] = vec(kBgHover);
    c[ImGuiCol_FrameBgActive] = vec(pressed);
    c[ImGuiCol_TitleBg] = vec(kBgDeep);
    c[ImGuiCol_TitleBgActive] = vec(kBgDeep);
    c[ImGuiCol_TitleBgCollapsed] = vec(kBgDeep);
    c[ImGuiCol_MenuBarBg] = vec(kBgDeep);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = vec(mix(p.border, p.text, 0.12f));
    c[ImGuiCol_ScrollbarGrabHovered] = vec(mix(p.border, p.text, 0.25f));
    c[ImGuiCol_ScrollbarGrabActive] = vec(kAccent);
    c[ImGuiCol_CheckMark] = vec(kAccent);
    c[ImGuiCol_SliderGrab] = vec(kAccent);
    c[ImGuiCol_SliderGrabActive] = vec(kAccentHover);
    c[ImGuiCol_Button] = vec(kBgRaised);
    c[ImGuiCol_ButtonHovered] = vec(kBgHover);
    c[ImGuiCol_ButtonActive] = vec(pressed);
    c[ImGuiCol_Header] = vec(ink(12));
    c[ImGuiCol_HeaderHovered] = vec(ink(20));
    c[ImGuiCol_HeaderActive] = vec(accent(50));
    c[ImGuiCol_Separator] = vec(kBorder);
    c[ImGuiCol_SeparatorHovered] = vec(kAccent);
    c[ImGuiCol_SeparatorActive] = vec(kAccent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = vec(kAccent);
    c[ImGuiCol_ResizeGripActive] = vec(kAccent);
    c[ImGuiCol_Tab] = vec(kBgDeep);
    c[ImGuiCol_TabHovered] = vec(kBgHover);
    c[ImGuiCol_TabSelected] = vec(kBgPanel);
    c[ImGuiCol_TabSelectedOverline] = vec(kAccent);
    c[ImGuiCol_TabDimmed] = vec(kBgDeep);
    c[ImGuiCol_TabDimmedSelected] = vec(kBgPanel);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = vec(accent(90));
    c[ImGuiCol_DockingEmptyBg] = vec(kBgDeep);
    c[ImGuiCol_TextSelectedBg] = vec(accent(70));
    c[ImGuiCol_DragDropTarget] = vec(kAccent);
    c[ImGuiCol_NavCursor] = vec(kAccent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, p.light ? 0.3f : 0.55f);
    c[ImGuiCol_TableHeaderBg] = vec(kBgRaised);
    c[ImGuiCol_TableBorderLight] = vec(kBorder);
    c[ImGuiCol_TableBorderStrong] = vec(kBorder);
}

} // namespace bemotion::theme
