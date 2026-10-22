#pragma once

// Customizable keyboard shortcuts. Every action has a default key chord
// (After Effects / industry conventions); users can rebind them in
// Settings > Shortcuts, and the bindings are saved with the settings.

#include <imgui.h>

#include <string>

namespace bemotion::ui {

enum class Action {
    // Files
    Save, SaveAs, ImportMedia, Export, CloseProject, Quit, Settings, Help,
    // Editing
    Undo, Redo, Duplicate, Delete, SelectAll, Deselect, CreateCompound, ReleaseCompound,
    BringForward, SendBackward, BringToFront, SendToBack,
    // Playback & time
    PlayPause, PrevFrame, NextFrame, PrevFrames, NextFrames, GoToStart, GoToEnd, PrevKeyframe, NextKeyframe,
    // Tools
    SelectTool, CutTool, Split, ToggleSnapping, AddText,
    // Keyframes
    EasyEase, EaseIn, EaseOut,
    // View
    FitView, ActualSize, ZoomIn, ZoomOut,
    Count
};

struct ActionInfo {
    const char* id;    // saved in settings.json
    const char* label; // shown in Settings and Help
    const char* group;
    ImGuiKeyChord defaultChord;
};

const ActionInfo& actionInfo(Action a);
ImGuiKeyChord chordOf(Action a);
void setChord(Action a, ImGuiKeyChord chord); // 0 removes the binding
void resetChord(Action a);
std::string chordName(ImGuiKeyChord chord);   // "Ctrl+Shift+Z", "" if none
Action actionWithChord(ImGuiKeyChord chord, Action except); // Action::Count if free

// True when the action's shortcut was pressed this frame. Single keys
// (no Ctrl/Alt) never fire while typing in a field.
bool shortcutPressed(Action a, bool repeat = false);

// Persistence (as part of settings.json): id -> chord name, only non-default bindings.
void loadKeymap(const std::string& json);
std::string saveKeymap();

} // namespace bemotion::ui
