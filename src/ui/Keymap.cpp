#include "ui/Keymap.h"

#include "ui/Language.h"

#include <nlohmann/json.hpp>

#include <array>
#include <map>

namespace bemotion::ui {

namespace {

using json = nlohmann::json;

constexpr int Ctrl = static_cast<int>(Qt::ControlModifier), Shift = static_cast<int>(Qt::ShiftModifier);

constexpr ActionInfo kActions[] = {
    {"new_project", TR_NOOP("New Project"), TR_NOOP("Files"), Ctrl | Qt::Key_N},
    {"open_project", TR_NOOP("Open Project"), TR_NOOP("Files"), Ctrl | Qt::Key_O},
    {"save", TR_NOOP("Save"), TR_NOOP("Files"), Ctrl | Qt::Key_S},
    {"save_as", TR_NOOP("Save As"), TR_NOOP("Files"), Ctrl | Shift | Qt::Key_S},
    {"import_media", TR_NOOP("Import Media"), TR_NOOP("Files"), Ctrl | Qt::Key_I},
    {"export", TR_NOOP("Export Video"), TR_NOOP("Files"), Ctrl | Qt::Key_M},
    {"close_project", TR_NOOP("Close Project"), TR_NOOP("Files"), Ctrl | Qt::Key_W},
    {"quit", "Quit", TR_NOOP("Files"), Ctrl | Qt::Key_Q},
    {"settings", TR_NOOP("Settings"), TR_NOOP("Files"), Ctrl | Qt::Key_Comma},
    {"help", TR_NOOP("Help"), TR_NOOP("Files"), Chord(Qt::Key_F1)},
    {"undo", TR_NOOP("Undo"), TR_NOOP("Editing"), Ctrl | Qt::Key_Z},
    {"redo", TR_NOOP("Redo"), TR_NOOP("Editing"), Ctrl | Shift | Qt::Key_Z},
    {"cut", TR_NOOP("Cut"), TR_NOOP("Editing"), Ctrl | Qt::Key_X},
    {"copy", TR_NOOP("Copy"), TR_NOOP("Editing"), Ctrl | Qt::Key_C},
    {"paste", TR_NOOP("Paste"), TR_NOOP("Editing"), Ctrl | Qt::Key_V},
    {"duplicate", TR_NOOP("Duplicate"), TR_NOOP("Editing"), Ctrl | Qt::Key_D},
    {"delete", TR_NOOP("Delete Keyframes / Layers"), TR_NOOP("Editing"), Chord(Qt::Key_Delete)},
    {"select_all", TR_NOOP("Select All Layers"), TR_NOOP("Editing"), Ctrl | Qt::Key_A},
    {"deselect", TR_NOOP("Deselect / Exit Compound"), TR_NOOP("Editing"), Chord(Qt::Key_Escape)},
    {"create_compound", TR_NOOP("Create Compound"), TR_NOOP("Editing"), Ctrl | Qt::Key_G},
    {"release_compound", TR_NOOP("Release Compound"), TR_NOOP("Editing"), Ctrl | Shift | Qt::Key_G},
    {"bring_forward", TR_NOOP("Bring Forward"), TR_NOOP("Editing"), Ctrl | Qt::Key_BracketRight},
    {"send_backward", TR_NOOP("Send Backward"), TR_NOOP("Editing"), Ctrl | Qt::Key_BracketLeft},
    {"bring_to_front", TR_NOOP("Bring to Front"), TR_NOOP("Editing"), Ctrl | Shift | Qt::Key_BracketRight},
    {"send_to_back", TR_NOOP("Send to Back"), TR_NOOP("Editing"), Ctrl | Shift | Qt::Key_BracketLeft},
    {"play_pause", TR_NOOP("Play / Pause"), TR_NOOP("Playback"), Chord(Qt::Key_Space)},
    {"prev_frame", TR_NOOP("Step Back"), TR_NOOP("Playback"), Chord(Qt::Key_Left)},
    {"next_frame", TR_NOOP("Step Forward"), TR_NOOP("Playback"), Chord(Qt::Key_Right)},
    {"prev_frames", TR_NOOP("Big Step Back"), TR_NOOP("Playback"), Shift | Qt::Key_Left},
    {"next_frames", TR_NOOP("Big Step Forward"), TR_NOOP("Playback"), Shift | Qt::Key_Right},
    {"go_to_start", TR_NOOP("Go to Start"), TR_NOOP("Playback"), Chord(Qt::Key_Home)},
    {"go_to_end", TR_NOOP("Go to End"), TR_NOOP("Playback"), Chord(Qt::Key_End)},
    {"prev_keyframe", TR_NOOP("Previous Keyframe"), TR_NOOP("Playback"), Shift | Qt::Key_J},
    {"next_keyframe", TR_NOOP("Next Keyframe"), TR_NOOP("Playback"), Shift | Qt::Key_K},
    {"shuttle_back", TR_NOOP("Play Backwards (again: faster)"), TR_NOOP("Playback"), Chord(Qt::Key_J)},
    {"shuttle_stop", TR_NOOP("Stop"), TR_NOOP("Playback"), Chord(Qt::Key_K)},
    {"shuttle_forward", TR_NOOP("Play Forward (again: faster)"), TR_NOOP("Playback"), Chord(Qt::Key_L)},
    {"add_marker", TR_NOOP("Add Marker"), TR_NOOP("Playback"), Chord(Qt::Key_M)},
    {"next_marker", TR_NOOP("Next Marker"), TR_NOOP("Playback"), Shift | Qt::Key_M},
    {"prev_marker", TR_NOOP("Previous Marker"), TR_NOOP("Playback"), Ctrl | Shift | Qt::Key_M},
    {"select_tool", TR_NOOP("Selection Tool"), TR_NOOP("Tools"), Chord(Qt::Key_V)},
    {"cut_tool", TR_NOOP("Cut Tool"), TR_NOOP("Tools"), Chord(Qt::Key_C)},
    {"split", TR_NOOP("Split at Playhead"), TR_NOOP("Tools"), Ctrl | Qt::Key_K},
    {"toggle_snapping", TR_NOOP("Snapping On / Off"), TR_NOOP("Tools"), Chord(Qt::Key_S)},
    {"add_text", TR_NOOP("Add Text"), TR_NOOP("Tools"), Chord(Qt::Key_T)},
    {"crop_tool", TR_NOOP("Crop Tool"), TR_NOOP("Tools"), Shift | Qt::Key_C},
    {"pen_tool", TR_NOOP("Pen Tool"), TR_NOOP("Tools"), Chord(Qt::Key_G)},
    {"ripple_tool", TR_NOOP("Ripple Edit Tool"), TR_NOOP("Tools"), Chord(Qt::Key_B)},
    {"roll_tool", TR_NOOP("Rolling Edit Tool"), TR_NOOP("Tools"), Chord(Qt::Key_N)},
    {"slip_tool", TR_NOOP("Slip Tool"), TR_NOOP("Tools"), Chord(Qt::Key_Y)},
    {"ripple_delete", TR_NOOP("Ripple Delete"), TR_NOOP("Timeline"), Shift | Qt::Key_Delete},
    {"ripple_trim_start", TR_NOOP("Ripple Trim Start to Playhead"), TR_NOOP("Timeline"), Chord(Qt::Key_Q)},
    {"ripple_trim_end", TR_NOOP("Ripple Trim End to Playhead"), TR_NOOP("Timeline"), Chord(Qt::Key_W)},
    {"speed_duration", TR_NOOP("Speed / Duration"), TR_NOOP("Timeline"), Ctrl | Qt::Key_R},
    {"easy_ease", TR_NOOP("Easy Ease"), TR_NOOP("Keyframes"), Chord(Qt::Key_F9)},
    {"ease_in", TR_NOOP("Ease In"), TR_NOOP("Keyframes"), Shift | Qt::Key_F9},
    {"ease_out", TR_NOOP("Ease Out"), TR_NOOP("Keyframes"), Ctrl | Shift | Qt::Key_F9},
    {"fit_view", TR_NOOP("Fit Composition"), TR_NOOP("View"), Ctrl | Qt::Key_0},
    {"actual_size", TR_NOOP("Actual Size (100%)"), TR_NOOP("View"), Ctrl | Qt::Key_1},
    {"zoom_in", TR_NOOP("Zoom In"), TR_NOOP("View"), Ctrl | Qt::Key_Equal},
    {"zoom_out", TR_NOOP("Zoom Out"), TR_NOOP("View"), Ctrl | Qt::Key_Minus},
    {"full_screen", TR_NOOP("Full Screen"), TR_NOOP("View"), Chord(Qt::Key_F11)},
    {"viewport_fullscreen", TR_NOOP("Maximize Viewport"), TR_NOOP("View"), Ctrl | Qt::Key_F11},
    {"full_screen_preview", TR_NOOP("Full Screen Preview"), TR_NOOP("View"), Ctrl | Shift | Qt::Key_F},
    {"export_frame", TR_NOOP("Export Frame"), TR_NOOP("File"), Shift | Qt::Key_E},
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
