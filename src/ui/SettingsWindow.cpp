#include "ui/SettingsWindow.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "render/Fonts.h"
#include "app/PluginHost.h"
#include "ui/PluginsWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
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
    const float fs = ImGui::GetFontSize();
    const ImVec2 size(std::max(112.0f, fs * 7.5f), 50.0f + fs + 12.0f); // preview + a name row that fits the text
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + size.x, a.y + size.y);
    ImGui::PushID(p.name.c_str());
    const bool clicked = ImGui::InvisibleButton("##theme", size);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, p.bgDeep, 8.0f);
    dl->AddRectFilled(ImVec2(a.x + 8, a.y + 8), ImVec2(a.x + 52, a.y + 46), p.bgPanel, 4.0f);
    dl->AddRectFilled(ImVec2(a.x + 58, a.y + 8), ImVec2(b.x - 8, a.y + 24), p.bgRaised, 4.0f);
    dl->AddRectFilled(ImVec2(a.x + 58, a.y + 30), ImVec2(b.x - 24, a.y + 42), p.accent, 3.0f);
    dl->AddRectFilled(ImVec2(a.x + 14, a.y + 16), ImVec2(a.x + 44, a.y + 20), p.textDim, 2.0f);
    dl->AddRectFilled(ImVec2(a.x + 14, a.y + 26), ImVec2(a.x + 36, a.y + 30), p.textFaint, 2.0f);
    dl->PushClipRect(a, b, true);
    dl->AddText(ImVec2(a.x + 8, b.y - fs - 7), p.text, p.name.c_str());
    dl->PopClipRect();
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
    const float cardW = std::max(112.0f, ImGui::GetFontSize() * 7.5f) + 10.0f;
    for (const LibraryTheme& t : ThemeLibrary::all()) {
        if (x > 0 && x + cardW <= avail) ImGui::SameLine(0, 10);
        else if (x > 0) x = 0;
        if (themeCard(t.palette, !s.customPalette && s.theme == t.palette.name)) {
            s.theme = t.palette.name;
            s.customPalette = false;
            changed();
        }
        x += cardW;
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

SettingsTab g_tab = SettingsTab::Theme;
bool g_selectTab = false;

void openSettings(SettingsTab tab) {
    g_open = true;
    g_focus = true;
    g_recenter = 2;
    g_tab = tab;
    g_selectTab = true;
}

namespace {

void drawInterface(Settings& s) {
    sectionLabel("TIMELINE");
    ImGui::SetNextItemWidth(240);
    if (ImGui::SliderFloat("Clip row height", &s.timelineLayerRow, 22.0f, 80.0f, "%.0f px")) changed();
    ImGui::SetNextItemWidth(240);
    if (ImGui::SliderFloat("Keyframe row height", &s.timelinePropertyRow, 20.0f, 60.0f, "%.0f px")) changed();
    ImGui::SetNextItemWidth(240);
    if (ImGui::SliderFloat("Keyframe size", &s.keyframeSize, 3.0f, 10.0f, "%.1f px")) changed();
    if (ImGui::Button("Compact")) s.timelineLayerRow = 26, s.timelinePropertyRow = 22, s.keyframeSize = 4, changed();
    ImGui::SameLine();
    if (ImGui::Button("Comfortable")) s.timelineLayerRow = 36, s.timelinePropertyRow = 30, s.keyframeSize = 5, changed();
    ImGui::SameLine();
    if (ImGui::Button("Large")) s.timelineLayerRow = 48, s.timelinePropertyRow = 38, s.keyframeSize = 6.5f, changed();

    ImGui::Spacing();
    sectionLabel("TEXT");
    ImGui::SetNextItemWidth(240);
    float percent = s.textScale * 100.0f;
    if (ImGui::SliderFloat("Text size", &percent, 80.0f, 150.0f, "%.0f%%")) {
        s.textScale = percent / 100.0f;
        changed();
    }
    if (ImGui::IsItemHovered()) tooltip("Scales every label in the interface");
    hint("Colors, corner rounding and themes are in the Theme page.");
}

void drawEditor(Settings& s) {
    sectionLabel("SNAPPING");
    ImGui::SetNextItemWidth(240);
    if (ImGui::SliderFloat("Snap distance", &s.snapDistance, 2.0f, 30.0f, "%.0f px")) changed();
    hint("How close (on screen) an object, clip, keyframe or the playhead must get before it snaps. "
         "Toggle snapping with S or the magnet button; hold Ctrl while dragging to ignore it.");
}

// One sidebar entry: icon + label, highlighted when selected.
bool sidebarItem(const char* label, Icon icon, bool selected) {
    const float h = ImGui::GetFrameHeight() + 10.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), selected ? theme::kAccentSoft : theme::kBgHover, 6.0f);
    if (selected) dl->AddRectFilled(ImVec2(p.x, p.y + 8), ImVec2(p.x + 3, p.y + h - 8), theme::kAccent, 2.0f);
    const ImU32 col = selected ? theme::kText : theme::kTextDim;
    drawIcon(dl, icon, ImVec2(p.x + 22, p.y + h * 0.5f), 16.0f, selected ? theme::kAccent : col);
    dl->AddText(ImVec2(p.x + 42, p.y + (h - ImGui::GetFontSize()) * 0.5f), col, label);
    return clicked;
}

} // namespace

namespace {

// Page title with a quick "Reset" to that page's defaults on the right.
bool pageHeader(const char* title) {
    ImGui::PushFont(Fonts::uiBold(), ImGui::GetFontSize() * 1.15f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    const float w = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2 + 22.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button("    Reset");
    drawIcon(ImGui::GetWindowDrawList(), Icon::Reset, ImVec2(p.x + 14, p.y + ImGui::GetFrameHeight() * 0.5f), 12.0f,
             theme::kTextDim);
    tooltip("Restore this page's defaults");
    ImGui::Spacing();
    return clicked;
}

void enableAllPlugins() {
    for (const Plugin& p : PluginHost::get().plugins()) PluginHost::get().setEnabled(p.id, true);
    settings().disabledPlugins.clear();
}

void resetPage(SettingsTab tab) {
    Settings& s = settings();
    const Settings d; // defaults
    switch (tab) {
    case SettingsTab::Theme:
        s.theme = d.theme, s.customAccent = d.customAccent, s.accent = d.accent;
        s.customPalette = false, s.advanced = d.advanced;
        break;
    case SettingsTab::Interface:
        s.timelineLayerRow = d.timelineLayerRow, s.timelinePropertyRow = d.timelinePropertyRow;
        s.keyframeSize = d.keyframeSize, s.textScale = d.textScale;
        break;
    case SettingsTab::Editor: s.snapDistance = d.snapDistance; break;
    case SettingsTab::Plugins: enableAllPlugins(); break;
    }
    changed();
}

} // namespace

void drawSettings() {
    if (!g_open) return;
    Settings& s = settings();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Always fits the screen; pages scroll inside.
    const ImVec2 size(std::min(780.0f, vp->WorkSize.x * 0.92f), std::min(600.0f, vp->WorkSize.y * 0.88f));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            g_recenter > 0 ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (g_recenter > 0) --g_recenter;
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 360), vp->WorkSize);
    if (g_focus) {
        ImGui::SetNextWindowFocus();
        g_focus = false;
    }
    if (!ImGui::Begin("Settings", &g_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_open = false;

    struct Page {
        SettingsTab tab;
        const char* label;
        Icon icon;
    };
    static constexpr Page kPages[] = {{SettingsTab::Theme, "Theme", Icon::Palette},
                                      {SettingsTab::Interface, "Interface", Icon::Sliders},
                                      {SettingsTab::Editor, "Editor", Icon::Magnet},
                                      {SettingsTab::Plugins, "Plugins", Icon::Plugin}};
    ImGui::BeginChild("##sidebar", ImVec2(170, 0), ImGuiChildFlags_None);
    for (const Page& p : kPages)
        if (sidebarItem(p.label, p.icon, g_tab == p.tab)) g_tab = p.tab;
    // Everything back to how bemotion ships.
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - 8));
    if (ImGui::Button("Reset All", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##resetAll");
    tooltip("Restore every setting to its default");
    if (ImGui::BeginPopup("##resetAll")) {
        ImGui::TextUnformatted("Reset every setting to its default?");
        if (accentButton("Reset All")) {
            settings() = Settings{};
            enableAllPlugins();
            changed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + 6, a.y), ImVec2(a.x + 6, a.y + ImGui::GetContentRegionAvail().y),
                                            theme::kBorder);
        ImGui::Dummy(ImVec2(14, 0));
        ImGui::SameLine(0, 0);
    }
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_None);
    const char* titles[] = {"Theme", "Interface", "Editor", "Plugins"};
    if (pageHeader(titles[static_cast<int>(g_tab)])) resetPage(g_tab);
    switch (g_tab) {
    case SettingsTab::Theme:
        drawAppearance(s);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Checkbox("Advanced mode", &s.advanced)) saveSettings();
        hint("Shows every color and fine-tuning option. Your choices above stay as they are.");
        if (s.advanced) {
            ImGui::Spacing();
            drawAdvanced(s);
        }
        break;
    case SettingsTab::Interface: drawInterface(s); break;
    case SettingsTab::Editor: drawEditor(s); break;
    case SettingsTab::Plugins: drawPluginsTab(); break;
    }
    ImGui::EndChild();
    g_selectTab = false;
    ImGui::End();
}

} // namespace bemotion::ui
