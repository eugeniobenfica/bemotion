#pragma once

// Menus the Layer menu, the timeline's and the viewport's right click share: a clip's time
// (speed, reverse, time remap, freeze), its blending mode, its track matte and its masks,
// and the Speed / Duration dialog.

#include "core/Layer.h"

class QComboBox;
class QMenu;
class QWidget;

namespace bemotion {

class EditorContext;

namespace ui {

void fillTimeMenu(QMenu* menu, EditorContext& ctx, QWidget* parent);
void fillBlendMenu(QMenu* menu, EditorContext& ctx); // the mode under the mouse shows on the picture
// The blending modes of the selected layers, in families as the menu has them; the one under the
// mouse shows on the picture while the list is open. showBlendMode() sets what it shows.
QComboBox* blendModeCombo(EditorContext& ctx);
void showBlendMode(QComboBox* combo, BlendMode mode);
void fillMatteMenu(QMenu* menu, EditorContext& ctx);
void fillMaskMenu(QMenu* menu, EditorContext& ctx);
// "Separate Audio" for the selected videos with sound (their sound becomes clips of its own),
// and "Link Audio" for selected videos without it (a sound-only clip of their media goes back in).
void addSeparateAudio(QMenu& menu, EditorContext& ctx);
// Automatic sound fixes for the selected clips with sound: Auto Level (one loudness for all of
// them) and Remove Silence (cuts their quiet stretches out), with the options of the Tool
// Settings panel (Settings::tools).
void addAudioFixes(QMenu& menu, EditorContext& ctx, QWidget* parent);
void autoLevelSelected(EditorContext& ctx);
void removeSilence(EditorContext& ctx, QWidget* parent);
// Rectangle / ellipse masks fitted to the selected layers.
void addFittedMask(EditorContext& ctx, bool ellipse);
// Speed (percent) and duration of the selected video / audio clips, reverse, ripple (modal).
void showSpeedDialog(EditorContext& ctx, QWidget* parent);
// Shapes become Bézier paths, keeping their outline (After Effects' Convert to Bezier Path).
void convertSelectedToPaths(EditorContext& ctx);

} // namespace ui
} // namespace bemotion
