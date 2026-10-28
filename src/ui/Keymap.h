#pragma once

// Customizable keyboard shortcuts. Every action has a default key chord
// (After Effects / industry conventions); users can rebind them in
// Settings > Shortcuts, and the bindings are saved with the settings.

#include <QKeySequence>

#include <string>

namespace bemotion::ui {

enum class Action {
    // Files
    NewProject, OpenProject, Save, SaveAs, ImportMedia, Export, CloseProject, Quit, Settings, Help,
    // Editing
    Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, SelectAll, Deselect, CreateCompound, ReleaseCompound,
    BringForward, SendBackward, BringToFront, SendToBack,
    // Playback & time
    PlayPause, PrevFrame, NextFrame, PrevFrames, NextFrames, GoToStart, GoToEnd, PrevKeyframe, NextKeyframe,
    ShuttleBack, ShuttleStop, ShuttleForward, AddMarker, NextMarker, PrevMarker,
    // Tools
    SelectTool, CutTool, Split, ToggleSnapping, AddText, CropTool, PenTool, RippleTool, RollTool, SlipTool,
    BrushTool, EraserTool, PuppetTool, WarpTool, TrackTool,
    // Timeline editing
    RippleDelete, RippleTrimStart, RippleTrimEnd, SpeedDuration,
    // Keyframes
    EasyEase, EaseIn, EaseOut,
    // View
    FitView, ActualSize, ZoomIn, ZoomOut, FullScreen, ViewportFullscreen, FullScreenPreview,
    ExportFrame,
    Count
};

// A key and its modifiers (QKeyCombination::toCombined()); 0 = no binding.
using Chord = int;

struct ActionInfo {
    const char* id;    // saved in settings.json
    const char* label; // shown in Settings and Help
    const char* group;
    Chord defaultChord;
};

const ActionInfo& actionInfo(Action a);
Chord chordOf(Action a);
QKeySequence sequenceOf(Action a);
void setChord(Action a, Chord chord); // 0 removes the binding
void resetChord(Action a);
std::string chordName(Chord chord);   // "Ctrl+Shift+Z", "" if none
Action actionWithChord(Chord chord, Action except); // Action::Count if free

// Persistence (as part of settings.json): id -> chord name, only non-default bindings.
void loadKeymap(const std::string& json);
std::string saveKeymap();

} // namespace bemotion::ui
