#include "ui/Keymap.h"

#include <nlohmann/json.hpp>

#include <array>
#include <map>

namespace bemotion::ui {

namespace {

using json = nlohmann::json;

constexpr int Ctrl = static_cast<int>(Qt::ControlModifier), Shift = static_cast<int>(Qt::ShiftModifier);

constexpr ActionInfo kActions[] = {
    {"new_project", "New Project", "Files", Ctrl | Qt::Key_N},
    {"open_project", "Open Project", "Files", Ctrl | Qt::Key_O},
    {"save", "Save", "Files", Ctrl | Qt::Key_S},
    {"save_as", "Save As", "Files", Ctrl | Shift | Qt::Key_S},
    {"import_media", "Import Media", "Files", Ctrl | Qt::Key_I},
    {"export", "Export Video", "Files", Ctrl | Qt::Key_M},
    {"close_project", "Close Project", "Files", Ctrl | Qt::Key_W},
    {"quit", "Quit", "Files", Ctrl | Qt::Key_Q},
    {"settings", "Settings", "Files", Ctrl | Qt::Key_Comma},
    {"help", "Help", "Files", Chord(Qt::Key_F1)},
    {"undo", "Undo", "Editing", Ctrl | Qt::Key_Z},
    {"redo", "Redo", "Editing", Ctrl | Shift | Qt::Key_Z},
    {"cut", "Cut", "Editing", Ctrl | Qt::Key_X},
    {"copy", "Copy", "Editing", Ctrl | Qt::Key_C},
    {"paste", "Paste", "Editing", Ctrl | Qt::Key_V},
    {"duplicate", "Duplicate", "Editing", Ctrl | Qt::Key_D},
    {"delete", "Delete Keyframes / Layers", "Editing", Chord(Qt::Key_Delete)},
    {"select_all", "Select All Layers", "Editing", Ctrl | Qt::Key_A},
    {"deselect", "Deselect / Exit Compound", "Editing", Chord(Qt::Key_Escape)},
    {"create_compound", "Create Compound", "Editing", Ctrl | Qt::Key_G},
    {"release_compound", "Release Compound", "Editing", Ctrl | Shift | Qt::Key_G},
    {"bring_forward", "Bring Forward", "Editing", Ctrl | Qt::Key_BracketRight},
    {"send_backward", "Send Backward", "Editing", Ctrl | Qt::Key_BracketLeft},
    {"bring_to_front", "Bring to Front", "Editing", Ctrl | Shift | Qt::Key_BracketRight},
    {"send_to_back", "Send to Back", "Editing", Ctrl | Shift | Qt::Key_BracketLeft},
    {"play_pause", "Play / Pause", "Playback", Chord(Qt::Key_Space)},
    {"prev_frame", "Step Back", "Playback", Chord(Qt::Key_Left)},
    {"next_frame", "Step Forward", "Playback", Chord(Qt::Key_Right)},
    {"prev_frames", "Big Step Back", "Playback", Shift | Qt::Key_Left},
    {"next_frames", "Big Step Forward", "Playback", Shift | Qt::Key_Right},
    {"go_to_start", "Go to Start", "Playback", Chord(Qt::Key_Home)},
    {"go_to_end", "Go to End", "Playback", Chord(Qt::Key_End)},
    {"prev_keyframe", "Previous Keyframe", "Playback", Chord(Qt::Key_J)},
    {"next_keyframe", "Next Keyframe", "Playback", Chord(Qt::Key_K)},
    {"select_tool", "Selection Tool", "Tools", Chord(Qt::Key_V)},
    {"cut_tool", "Cut Tool", "Tools", Chord(Qt::Key_C)},
    {"split", "Split at Playhead", "Tools", Ctrl | Qt::Key_K},
    {"toggle_snapping", "Snapping On / Off", "Tools", Chord(Qt::Key_S)},
    {"add_text", "Add Text", "Tools", Chord(Qt::Key_T)},
    {"easy_ease", "Easy Ease", "Keyframes", Chord(Qt::Key_F9)},
    {"ease_in", "Ease In", "Keyframes", Shift | Qt::Key_F9},
    {"ease_out", "Ease Out", "Keyframes", Ctrl | Shift | Qt::Key_F9},
    {"fit_view", "Fit Composition", "View", Ctrl | Qt::Key_0},
    {"actual_size", "Actual Size (100%)", "View", Ctrl | Qt::Key_1},
    {"zoom_in", "Zoom In", "View", Ctrl | Qt::Key_Equal},
    {"zoom_out", "Zoom Out", "View", Ctrl | Qt::Key_Minus},
    {"full_screen", "Full Screen", "View", Chord(Qt::Key_F11)},
    {"viewport_fullscreen", "Viewport Fullscreen", "View", Ctrl | Qt::Key_F11},
};
static_assert(std::size(kActions) == static_cast<size_t>(Action::Count));

std::array<Chord, static_cast<size_t>(Action::Count)>& bindings() {
    static std::array<Chord, static_cast<size_t>(Action::Count)> b = [] {
        std::array<Chord, static_cast<size_t>(Action::Count)> d{};
        for (size_t i = 0; i < d.size(); ++i) d[i] = kActions[i].defaultChord;
        return d;
    }();
    return b;
}

// Names written by earlier versions ("LeftArrow", "RightBracket", ...) to Qt's.
std::string modernName(std::string s) {
    static const std::map<std::string, std::string> renamed = {
        {"LeftArrow", "Left"}, {"RightArrow", "Right"}, {"UpArrow", "Up"}, {"DownArrow", "Down"},
        {"LeftBracket", "["},  {"RightBracket", "]"},   {"Equal", "="},    {"Minus", "-"},
        {"Comma", ","},        {"Period", "."},         {"Delete", "Del"}, {"Escape", "Esc"},
        {"Super", "Meta"},     {"Enter", "Return"},     {"Apostrophe", "'"}, {"Semicolon", ";"},
        {"Slash", "/"},        {"Backslash", "\\"},     {"GraveAccent", "`"}};
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t plus = s.find('+', start + 1);
        if (plus == std::string::npos) plus = s.size();
        std::string part = s.substr(start, plus - start);
        if (auto it = renamed.find(part); it != renamed.end()) part = it->second;
        out += (out.empty() ? "" : "+") + part;
        start = plus + 1;
    }
    return out;
}

Chord parseChord(const std::string& text) {
    if (text.empty()) return 0;
    const QKeySequence seq = QKeySequence::fromString(QString::fromStdString(modernName(text)), QKeySequence::PortableText);
    return seq.isEmpty() ? 0 : seq[0].toCombined();
}

} // namespace

const ActionInfo& actionInfo(Action a) { return kActions[static_cast<size_t>(a)]; }
Chord chordOf(Action a) { return bindings()[static_cast<size_t>(a)]; }
QKeySequence sequenceOf(Action a) { return chordOf(a) ? QKeySequence(QKeyCombination::fromCombined(chordOf(a))) : QKeySequence(); }
void setChord(Action a, Chord chord) { bindings()[static_cast<size_t>(a)] = chord; }
void resetChord(Action a) { setChord(a, actionInfo(a).defaultChord); }

std::string chordName(Chord chord) {
    if (!chord) return {};
    return QKeySequence(QKeyCombination::fromCombined(chord)).toString(QKeySequence::PortableText).toStdString();
}

Action actionWithChord(Chord chord, Action except) {
    for (int i = 0; i < static_cast<int>(Action::Count); ++i)
        if (static_cast<Action>(i) != except && chord && bindings()[i] == chord) return static_cast<Action>(i);
    return Action::Count;
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
