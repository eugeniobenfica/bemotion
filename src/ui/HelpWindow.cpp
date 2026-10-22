#include "ui/HelpWindow.h"

#include "app/Platform.h"
#include "render/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace bemotion::ui {

namespace {

constexpr const char* kRepoUrl = "https://github.com/eugeniobenfica/bemotion";

bool g_open = false;
HelpTab g_tab = HelpTab::Shortcuts;

struct Shortcut {
    const char* keys; // "+" joins keys, " / " separates alternatives
    const char* action;
};
struct ShortcutGroup {
    const char* name;
    std::vector<Shortcut> items;
};

const std::vector<ShortcutGroup>& shortcutGroups() {
    static const std::vector<ShortcutGroup> groups = {
        {"Playback",
         {{"Space", "Play / pause"},
          {"Left / Right", "Previous / next frame"},
          {"Shift+Left / Shift+Right", "Back / forward 10 frames"},
          {"Home / End", "Go to start / end"},
          {"J / K", "Previous / next keyframe"}}},
        {"Tools",
         {{"V", "Selection tool"},
          {"C", "Cut tool (click a clip to split it)"},
          {"Ctrl+K", "Split selected layers at the playhead"},
          {"T", "Add text"},
          {"S", "Snapping on / off"}}},
        {"Editing",
         {{"Ctrl+Z / Ctrl+Shift+Z", "Undo / redo"},
          {"Ctrl+D", "Duplicate layers"},
          {"Delete", "Delete keyframes, or the layers"},
          {"Ctrl+A", "Select all layers"},
          {"Shift+Click / Ctrl+Click", "Add to / toggle the selection"},
          {"Ctrl+] / Ctrl+[", "Bring forward / send backward"},
          {"Ctrl+Shift+] / Ctrl+Shift+[", "Bring to front / send to back"}}},
        {"Keyframes & motion",
         {{"F9", "Easy Ease on the selected keyframes"},
          {"Shift+F9 / Ctrl+Shift+F9", "Ease In / Ease Out"},
          {"Alt+Drag", "Break a motion path handle into a corner"},
          {"Shift+Drag", "Constrain a move, uniform scale, 15° rotation"},
          {"Ctrl+Drag", "Move without snapping"}}},
        {"View",
         {{"Wheel", "Zoom the viewport around the cursor"},
          {"Middle Drag / Space+Drag / Alt+Drag", "Pan the viewport"},
          {"Ctrl+0 / Ctrl+1", "Fit / 100% zoom"},
          {"Ctrl+= / Ctrl+-", "Zoom in / out"},
          {"Ctrl+Wheel / Shift+Wheel", "Zoom / scroll the timeline"}}},
        {"Files",
         {{"Ctrl+S / Ctrl+Shift+S", "Save / save as"},
          {"Ctrl+M", "Export video"},
          {"Ctrl+,", "Settings"},
          {"Ctrl+W / Ctrl+Q", "Close project / quit"}}},
    };
    return groups;
}

struct Credit {
    const char* name;
    const char* by;
    const char* license;
    const char* use;
};
constexpr Credit kCredits[] = {
    {"Dear ImGui", "Omar Cornut and contributors", "MIT", "the whole user interface"},
    {"GLFW", "Marcus Geelnard, Camilla Löwy", "zlib", "windows, input and OpenGL context"},
    {"nlohmann/json", "Niels Lohmann", "MIT", "project, settings, preset and theme files"},
    {"stb", "Sean Barrett", "MIT / Public Domain", "font scanning and PNG export"},
    {"Lua 5.4", "PUC-Rio (Lua.org)", "MIT", "the plugin scripting language"},
    {"Inter", "Rasmus Andersson", "SIL Open Font License 1.1", "the interface typeface"},
    {"FFmpeg (optional)", "The FFmpeg developers", "LGPL / GPL", "ProRes, WebM and GIF export when installed"},
};

constexpr const char* kLicense =
    "MIT License\n\n"
    "Copyright (c) 2026 The bemotion contributors\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated "
    "documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation "
    "the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to "
    "permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be included in all copies or substantial portions of "
    "the Software.\n\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO "
    "THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
    "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    "SOFTWARE.";

void paragraph(const char* text, ImU32 color = 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, color ? color : theme::kTextDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void title(const char* text) {
    ImGui::PushFont(Fonts::uiBold(), ImGui::GetFontSize() * 1.15f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::Spacing();
}

void drawShortcuts() {
    title("Keyboard shortcuts");
    for (const ShortcutGroup& g : shortcutGroups()) {
        sectionLabel(g.name);
        if (ImGui::BeginTable(g.name, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, 270.0f);
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
            for (const Shortcut& s : g.items) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                keyCaps(s.keys);
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(s.action);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
    }
}

void drawAbout() {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + 48, p.y + 48), theme::kAccent, 12.0f);
    drawIcon(dl, Icon::Logo, ImVec2(p.x + 24, p.y + 25), 28.0f, theme::kOnAccent);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 62, p.y + 2));
    ImGui::PushFont(Fonts::uiBold(), ImGui::GetFontSize() * 1.6f);
    ImGui::TextUnformatted("bemotion");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x + 62, ImGui::GetCursorScreenPos().y - 2));
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Version %s  ·  free and open source", BEMOTION_VERSION);
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(ImGui::GetCursorScreenPos().y, p.y + 56)));
    ImGui::Spacing();
    paragraph("bemotion is a minimal 2D motion graphics animator. Drop text, shapes and charts on the canvas, set "
              "keyframes, shape the motion with paths and curves, add effects and export a video.",
              theme::kText);
    ImGui::Spacing();
    paragraph("The goal is simplicity without giving up control: the common moves are one click away (quick "
              "animations, effect presets, snapping), while every property stays animatable and the interface "
              "follows the industry standards motion designers already know. Themes and plugins let anyone shape it "
              "to their taste and extend it.");
    ImGui::Spacing();
    paragraph("Written in C++20 with Dear ImGui and OpenGL. Plugins are sandboxed Lua scripts.");
    ImGui::Spacing();
    ImGui::Spacing();
    if (accentButton("View on GitHub")) platform::openWithDefaultApp(kRepoUrl);
    ImGui::SameLine();
    if (ImGui::Button("Report an Issue")) platform::openWithDefaultApp(std::string(kRepoUrl) + "/issues");
    ImGui::SameLine();
    if (ImGui::Button("Write a Plugin")) platform::openWithDefaultApp(std::string(kRepoUrl) + "/blob/main/docs/PLUGINS.md");
}

void drawCredits() {
    title("Credits");
    paragraph("Created by Eugênio Benfica and the bemotion contributors. bemotion stands on the shoulders of these "
              "open source projects:");
    ImGui::Spacing();
    for (const Credit& c : kCredits) {
        ImGui::PushFont(Fonts::uiBold(), 0.0f);
        ImGui::TextUnformatted(c.name);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(theme::kAccent), "%s", c.license);
        ImGui::TextColored(theme::vec(theme::kTextDim), "%s  ·  %s", c.by, c.use);
        ImGui::Spacing();
    }
    paragraph("Thank you to everyone who builds, tests, reports and shares.", theme::kTextFaint);
}

void drawLicense() {
    title("License");
    paragraph("bemotion is free software under the MIT license: use it for anything, including commercial work, "
              "and change and share it, as long as the notice below travels with it. Your projects and exported "
              "videos are entirely yours.",
              theme::kText);
    ImGui::Spacing();
    ImGui::BeginChild("##license", ImVec2(0, 0), ImGuiChildFlags_Borders);
    paragraph(kLicense);
    ImGui::EndChild();
}

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

// One key cap: rounded, slightly raised.
void keyCap(const std::string& key) {
    const ImVec2 ts = ImGui::CalcTextSize(key.c_str());
    const float h = ImGui::GetFrameHeight() - 2.0f;
    const float w = std::max(h, ts.x + 14.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h + 2.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = std::min(6.0f, h * 0.25f);
    dl->AddRectFilled(ImVec2(p.x, p.y + 2), ImVec2(p.x + w, p.y + h + 2), theme::kBorder, r); // the key's side
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::kBgRaised, r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), theme::kBorder, r);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), theme::kText, key.c_str());
}

} // namespace

void keyCaps(const char* keys) {
    const std::string text = keys;
    size_t start = 0;
    bool firstCombo = true;
    while (start <= text.size()) {
        size_t end = text.find(" / ", start);
        if (end == std::string::npos) end = text.size();
        const std::string combo = text.substr(start, end - start);
        if (!firstCombo) {
            ImGui::SameLine(0, 6);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::vec(theme::kTextFaint), "/");
            ImGui::SameLine(0, 6);
        }
        firstCombo = false;
        // "+" joins keys, except a key that is itself "+" or ends with it ("Ctrl+=" is fine, "Ctrl++" is not used).
        size_t k = 0;
        bool firstKey = true;
        while (k <= combo.size()) {
            size_t plus = combo.find('+', k + 1);
            if (plus == std::string::npos) plus = combo.size();
            const std::string key = combo.substr(k, plus - k);
            if (!firstKey) {
                ImGui::SameLine(0, 3);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::vec(theme::kTextFaint), "+");
                ImGui::SameLine(0, 3);
            }
            firstKey = false;
            keyCap(key);
            k = plus + 1;
            if (plus >= combo.size()) break;
        }
        start = end + 3;
        if (end >= text.size()) break;
    }
}

void openHelp(HelpTab tab) {
    g_open = true;
    g_tab = tab;
    ImGui::SetWindowFocus("Help");
}

void drawHelp() {
    if (!g_open) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(820.0f, vp->WorkSize.x * 0.92f), std::min(620.0f, vp->WorkSize.y * 0.88f)),
                             ImGuiCond_Appearing);
    if (!ImGui::Begin("Help", &g_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_open = false;

    struct Page {
        HelpTab tab;
        const char* label;
        Icon icon;
    };
    static constexpr Page kPages[] = {{HelpTab::Shortcuts, "Shortcuts", Icon::Text},
                                      {HelpTab::About, "About", Icon::Logo},
                                      {HelpTab::Credits, "Credits", Icon::Star},
                                      {HelpTab::License, "License", Icon::Lock}};
    ImGui::BeginChild("##helpSidebar", ImVec2(170, 0));
    for (const Page& p : kPages)
        if (sidebarItem(p.label, p.icon, g_tab == p.tab)) g_tab = p.tab;
    ImGui::EndChild();
    ImGui::SameLine(0, 14);
    ImGui::BeginChild("##helpPage");
    switch (g_tab) {
    case HelpTab::Shortcuts: drawShortcuts(); break;
    case HelpTab::About: drawAbout(); break;
    case HelpTab::Credits: drawCredits(); break;
    case HelpTab::License: drawLicense(); break;
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace bemotion::ui
