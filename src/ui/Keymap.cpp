#include "ui/Keymap.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstring>

namespace bemotion::ui {

namespace {

using json = nlohmann::json;

constexpr ImGuiKeyChord Ctrl = ImGuiMod_Ctrl, Shift = ImGuiMod_Shift;

constexpr ActionInfo kActions[] = {
    {"save", "Save", "Files", Ctrl | ImGuiKey_S},
    {"save_as", "Save As", "Files", Ctrl | Shift | ImGuiKey_S},
    {"export", "Export Video", "Files", Ctrl | ImGuiKey_M},
    {"close_project", "Close Project", "Files", Ctrl | ImGuiKey_W},
    {"quit", "Quit", "Files", Ctrl | ImGuiKey_Q},
    {"settings", "Settings", "Files", Ctrl | ImGuiKey_Comma},
    {"help", "Help", "Files", ImGuiKey_F1},
    {"undo", "Undo", "Editing", Ctrl | ImGuiKey_Z},
    {"redo", "Redo", "Editing", Ctrl | Shift | ImGuiKey_Z},
    {"duplicate", "Duplicate", "Editing", Ctrl | ImGuiKey_D},
    {"delete", "Delete Keyframes / Layers", "Editing", ImGuiKey_Delete},
    {"select_all", "Select All Layers", "Editing", Ctrl | ImGuiKey_A},
    {"deselect", "Deselect / Exit Compound", "Editing", ImGuiKey_Escape},
    {"create_compound", "Create Compound", "Editing", Ctrl | ImGuiKey_G},
    {"release_compound", "Release Compound", "Editing", Ctrl | Shift | ImGuiKey_G},
    {"bring_forward", "Bring Forward", "Editing", Ctrl | ImGuiKey_RightBracket},
    {"send_backward", "Send Backward", "Editing", Ctrl | ImGuiKey_LeftBracket},
    {"bring_to_front", "Bring to Front", "Editing", Ctrl | Shift | ImGuiKey_RightBracket},
    {"send_to_back", "Send to Back", "Editing", Ctrl | Shift | ImGuiKey_LeftBracket},
    {"play_pause", "Play / Pause", "Playback", ImGuiKey_Space},
    {"prev_frame", "Step Back", "Playback", ImGuiKey_LeftArrow},
    {"next_frame", "Step Forward", "Playback", ImGuiKey_RightArrow},
    {"prev_frames", "Big Step Back", "Playback", Shift | ImGuiKey_LeftArrow},
    {"next_frames", "Big Step Forward", "Playback", Shift | ImGuiKey_RightArrow},
    {"go_to_start", "Go to Start", "Playback", ImGuiKey_Home},
    {"go_to_end", "Go to End", "Playback", ImGuiKey_End},
    {"prev_keyframe", "Previous Keyframe", "Playback", ImGuiKey_J},
    {"next_keyframe", "Next Keyframe", "Playback", ImGuiKey_K},
    {"select_tool", "Selection Tool", "Tools", ImGuiKey_V},
    {"cut_tool", "Cut Tool", "Tools", ImGuiKey_C},
    {"split", "Split at Playhead", "Tools", Ctrl | ImGuiKey_K},
    {"toggle_snapping", "Snapping On / Off", "Tools", ImGuiKey_S},
    {"add_text", "Add Text", "Tools", ImGuiKey_T},
    {"easy_ease", "Easy Ease", "Keyframes", ImGuiKey_F9},
    {"ease_in", "Ease In", "Keyframes", Shift | ImGuiKey_F9},
    {"ease_out", "Ease Out", "Keyframes", Ctrl | Shift | ImGuiKey_F9},
    {"fit_view", "Fit Composition", "View", Ctrl | ImGuiKey_0},
    {"actual_size", "Actual Size (100%)", "View", Ctrl | ImGuiKey_1},
    {"zoom_in", "Zoom In", "View", Ctrl | ImGuiKey_Equal},
    {"zoom_out", "Zoom Out", "View", Ctrl | ImGuiKey_Minus},
};
static_assert(std::size(kActions) == static_cast<size_t>(Action::Count));

std::array<ImGuiKeyChord, static_cast<size_t>(Action::Count)>& bindings() {
    static std::array<ImGuiKeyChord, static_cast<size_t>(Action::Count)> b = [] {
        std::array<ImGuiKeyChord, static_cast<size_t>(Action::Count)> d{};
        for (size_t i = 0; i < d.size(); ++i) d[i] = kActions[i].defaultChord;
        return d;
    }();
    return b;
}

ImGuiKeyChord parseChord(const std::string& text) {
    if (text.empty()) return 0;
    ImGuiKeyChord chord = 0;
    size_t start = 0;
    while (start < text.size()) {
        size_t plus = text.find('+', start + 1); // "Ctrl++" style keys never occur: '+' is "Equal"
        if (plus == std::string::npos) plus = text.size();
        const std::string part = text.substr(start, plus - start);
        if (part == "Ctrl") chord |= ImGuiMod_Ctrl;
        else if (part == "Shift") chord |= ImGuiMod_Shift;
        else if (part == "Alt") chord |= ImGuiMod_Alt;
        else if (part == "Super") chord |= ImGuiMod_Super;
        else
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
                if (part == ImGui::GetKeyName(static_cast<ImGuiKey>(k))) chord |= k;
        start = plus + 1;
    }
    return (chord & ~ImGuiMod_Mask_) ? chord : 0; // a binding needs a key
}

} // namespace

const ActionInfo& actionInfo(Action a) { return kActions[static_cast<size_t>(a)]; }
ImGuiKeyChord chordOf(Action a) { return bindings()[static_cast<size_t>(a)]; }
void setChord(Action a, ImGuiKeyChord chord) { bindings()[static_cast<size_t>(a)] = chord; }
void resetChord(Action a) { setChord(a, actionInfo(a).defaultChord); }

std::string chordName(ImGuiKeyChord chord) {
    if (!chord) return {};
    std::string s;
    if (chord & ImGuiMod_Ctrl) s += "Ctrl+";
    if (chord & ImGuiMod_Shift) s += "Shift+";
    if (chord & ImGuiMod_Alt) s += "Alt+";
    if (chord & ImGuiMod_Super) s += "Super+";
    s += ImGui::GetKeyName(static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_));
    return s;
}

Action actionWithChord(ImGuiKeyChord chord, Action except) {
    for (int i = 0; i < static_cast<int>(Action::Count); ++i)
        if (static_cast<Action>(i) != except && chord && bindings()[i] == chord) return static_cast<Action>(i);
    return Action::Count;
}

bool shortcutPressed(Action a, bool repeat) {
    const ImGuiKeyChord chord = chordOf(a);
    if (!chord) return false;
    const ImGuiIO& io = ImGui::GetIO();
    const bool plain = !(chord & (ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super));
    if (plain && (io.WantTextInput || ImGui::IsAnyItemActive())) return false; // typing, or dragging a value
    ImGuiInputFlags flags = ImGuiInputFlags_RouteGlobal;
    if (repeat) flags |= ImGuiInputFlags_Repeat;
    return ImGui::Shortcut(chord, flags);
}

void loadKeymap(const std::string& text) {
    for (size_t i = 0; i < bindings().size(); ++i) bindings()[i] = kActions[i].defaultChord;
    try {
        const json j = json::parse(text);
        if (!j.is_object()) return;
        for (size_t i = 0; i < bindings().size(); ++i)
            if (const auto it = j.find(kActions[i].id); it != j.end() && it->is_string())
                bindings()[i] = parseChord(it->get<std::string>());
    } catch (const std::exception&) {
        // Keep the defaults.
    }
}

std::string saveKeymap() {
    json j = json::object();
    for (size_t i = 0; i < bindings().size(); ++i)
        if (bindings()[i] != kActions[i].defaultChord) j[kActions[i].id] = chordName(bindings()[i]);
    return j.dump();
}

} // namespace bemotion::ui
