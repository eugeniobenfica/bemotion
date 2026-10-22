#include "ui/SettingsWindow.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <string>

namespace bemotion::ui {

namespace {

bool g_open = false;
bool g_focus = false;
int g_recenter = 0; // frames left to re-center (auto-resize settles a frame late)
std::string g_message; // result of the last import/export
std::string g_saveName;

constexpr ImU32 kAccentChoices[] = {
    IM_COL32(232, 185, 49, 255),  // gold
    IM_COL32(242, 140, 40, 255),  // orange
    IM_COL32(239, 111, 94, 255),  // coral
    IM_COL32(233, 85, 154, 255),  // pink
    IM_COL32(155, 108, 240, 255), // violet
    IM_COL32(76, 141, 246, 255),  // blue
    IM_COL32(46, 196, 182, 255),  // teal
    IM_COL32(92, 196, 106, 255),  // green
};

void changed() {
    applySettings();
    saveSettings();
}

// Theme card: a tiny preview of panel, raised surface, text and accent.
bool themeCard(const theme::Palette& p, bool selected) {
    const ImVec2 size(112, 74);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + size.x, a.y + size.y);
    ImGui::PushID(p.name.c_str());
    const bool clicked = ImGui::InvisibleButton("##theme", size);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, p.bgDeep, 8.0f);
    dl->AddRectFilled(ImVec2(a.x + 8, a.y + 8), ImVec2(a.x + 52, b.y - 24), p.bgPanel, 4.0f);
    dl->AddRectFilled(ImVec2(a.x + 58, a.y + 8), ImVec2(b.x - 8, a.y + 24), p.bgRaised, 4.0f);
    dl->AddRectFilled(ImVec2(a.x + 58, a.y + 30), ImVec2(b.x - 24, a.y + 42), p.accent, 3.0f);
    dl->AddRectFilled(ImVec2(a.x + 14, a.y + 16), ImVec2(a.x + 44, a.y + 20), p.textDim, 2.0f);
    dl->AddRectFilled(ImVec2(a.x + 14, a.y + 26), ImVec2(a.x + 36, a.y + 30), p.textFaint, 2.0f);
    dl->AddText(ImVec2(a.x + 8, b.y - 20), p.text, p.name.c_str());
    if (selected || hovered)
        dl->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), selected ? theme::kAccent : theme::kBorder, 9.0f, 0,
                    selected ? 2.0f : 1.0f);
    return clicked;
}

bool swatch(const char* id, ImU32 color, bool selected) {
    const float d = 24.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(d, d));
    const ImVec2 c(a.x + d * 0.5f, a.y + d * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddCircle(c, d * 0.5f, theme::kText, 0, 2.0f);
    dl->AddCircleFilled(c, d * 0.5f - (selected ? 4.0f : 2.0f), color);
    if (ImGui::IsItemHovered() && !selected) dl->AddCircle(c, d * 0.5f - 1.0f, theme::ink(90), 0, 1.0f);
    return clicked;
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void drawAppearance(Settings& s) {
    sectionLabel("THEME");
    const float avail = ImGui::GetContentRegionAvail().x;
    float x = 0;
    for (const LibraryTheme& t : ThemeLibrary::all()) {
        if (x > 0 && x + 120 <= avail) ImGui::SameLine(0, 10);
        else if (x > 0) x = 0;
        if (themeCard(t.palette, !s.customPalette && s.theme == t.palette.name)) {
            s.theme = t.palette.name;
            s.customPalette = false;
            changed();
        }
        x += 122;
    }
    if (ImGui::Button("Import Theme...")) {
        if (auto file = platform::openFileDialog("Import Theme", "bemotion theme", "json")) {
            std::string name, error;
            if (ThemeLibrary::importFile(*file, &name, &error)) {
                s.theme = name;
                s.customPalette = false;
                changed();
                g_message = "Imported \"" + name + "\".";
            } else {
                g_message = "Could not import: " + error;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Themes Folder")) platform::revealInFileManager(ThemeLibrary::userDir());
    if (!g_message.empty()) hint(g_message.c_str());

    ImGui::Spacing();
    sectionLabel("ACCENT COLOR");
    const theme::Palette base = [&] {
        Settings copy = s;
        copy.customAccent = false;
        if (copy.customPalette) return copy.palette;
        const LibraryTheme* t = ThemeLibrary::find(copy.theme);
        return t ? t->palette : theme::darkPalette();
    }();
    if (swatch("##themeaccent", base.accent, !s.customAccent)) {
        s.customAccent = false;
        changed();
    }
    if (ImGui::IsItemHovered()) tooltip("Theme default");
    for (int i = 0; i < static_cast<int>(std::size(kAccentChoices)); ++i) {
        ImGui::SameLine(0, 8);
        ImGui::PushID(i);
        if (swatch("##accent", kAccentChoices[i], s.customAccent && s.accent == kAccentChoices[i])) {
            s.customAccent = true;
            s.accent = kAccentChoices[i];
            changed();
        }
        ImGui::PopID();
    }
    ImGui::SameLine(0, 12);
    ImVec4 custom = theme::vec(s.customAccent ? s.accent : base.accent);
    if (ImGui::ColorEdit3("Custom##accent", &custom.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        s.customAccent = true;
        s.accent = ImGui::ColorConvertFloat4ToU32(ImVec4(custom.x, custom.y, custom.z, 1.0f));
        changed();
    }
    if (ImGui::IsItemHovered()) tooltip("Pick any color");
}

void colorRow(const char* label, ImU32& color, Settings& s) {
    ImVec4 v = theme::vec(color);
    ImGui::TableNextColumn();
    if (ImGui::ColorEdit4(label, &v.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
        color = ImGui::ColorConvertFloat4ToU32(v);
        s.customPalette = true;
        changed();
    }
}

void drawAdvanced(Settings& s) {
    sectionLabel("COLORS");
    hint("Editing a color turns the current theme into your own custom palette.");
    if (!s.customPalette) s.palette = effectivePalette(); // start from what is on screen
    theme::Palette& p = s.palette;
    if (ImGui::BeginTable("##colors", 2, ImGuiTableFlags_SizingStretchSame)) {
        colorRow("Background", p.bgDeep, s);
        colorRow("Panels", p.bgPanel, s);
        colorRow("Inputs & cards", p.bgRaised, s);
        colorRow("Hover", p.bgHover, s);
        colorRow("Borders", p.border, s);
        colorRow("Canvas surround", p.canvas, s);
        colorRow("Text", p.text, s);
        colorRow("Secondary text", p.textDim, s);
        colorRow("Faint text", p.textFaint, s);
        colorRow("Danger", p.danger, s);
        colorRow("Snap lines", p.snap, s);
        colorRow("Guides", p.guide, s);
        ImGui::EndTable();
    }
    if (ImGui::Checkbox("Light theme (overlay strokes in black)", &p.light)) {
        s.customPalette = true;
        changed();
    }

    ImGui::Spacing();
    sectionLabel("SHAPE & BEHAVIOR");
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("Corner rounding", &p.rounding, 0.0f, 12.0f, "%.0f px")) {
        s.customPalette = true;
        changed();
    }
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("Snap distance", &s.snapDistance, 2.0f, 30.0f, "%.0f px")) changed();

    ImGui::Spacing();
    sectionLabel("SHARE");
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##themename", "Theme name", &g_saveName);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_saveName.empty());
    if (ImGui::Button("Save as Theme")) {
        theme::Palette out = effectivePalette();
        out.name = g_saveName;
        std::string error;
        if (ThemeLibrary::save(out, &error)) {
            s.theme = out.name;
            s.customPalette = false;
            s.customAccent = false;
            changed();
            g_message = "Saved \"" + out.name + "\".";
        } else {
            g_message = error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Export...")) {
        theme::Palette out = effectivePalette();
        if (!g_saveName.empty()) out.name = g_saveName;
        if (auto file = platform::saveFileDialog("Export Theme", platform::fromUtf8(out.name + ".json"), "bemotion theme", "json")) {
            std::string error;
            g_message = ThemeLibrary::exportTo(out, *file, &error) ? "Exported to " + platform::toUtf8(*file) : error;
        }
    }
    if (s.customPalette) {
        ImGui::SameLine();
        if (ImGui::Button("Reset to Theme")) {
            s.customPalette = false;
            changed();
        }
    }
}

} // namespace

void openSettings() {
    g_open = true;
    g_focus = true;
    g_recenter = 2;
}

void drawSettings() {
    if (!g_open) return;
    Settings& s = settings();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            g_recenter > 0 ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (g_recenter > 0) --g_recenter;
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 0), ImVec2(560, vp->WorkSize.y * 0.9f));
    if (g_focus) {
        ImGui::SetNextWindowFocus();
        g_focus = false;
    }
    if (!ImGui::Begin("Settings", &g_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                                ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_open = false;

    drawAppearance(s);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Checkbox("Advanced mode", &s.advanced)) {
        saveSettings();
        g_recenter = 2; // the window changes height
    }
    hint("Shows every color and fine-tuning option. Your choices above stay as they are.");
    if (s.advanced) {
        ImGui::Spacing();
        drawAdvanced(s);
    }
    ImGui::End();
}

} // namespace bemotion::ui
