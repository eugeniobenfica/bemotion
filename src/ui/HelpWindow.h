#pragma once

namespace bemotion::ui {

// Help window: keyboard shortcuts (drawn as key caps), about the project,
// credits and the license. Drawn once per frame by the application.
enum class HelpTab { Shortcuts, About, Credits, License };
void openHelp(HelpTab tab = HelpTab::Shortcuts);
void drawHelp();

// Draws "Ctrl+Shift+Z / Ctrl+Y" style text as key caps on the current line.
void keyCaps(const char* keys);

} // namespace bemotion::ui
