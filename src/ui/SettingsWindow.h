#pragma once

namespace bemotion::ui {

// The Settings window (appearance and editor preferences). Shared by the home
// screen and the editor; drawn once per frame by the application.
enum class SettingsTab { Theme, Interface, Editor, Plugins };
void openSettings(SettingsTab tab = SettingsTab::Theme);
void drawSettings();

} // namespace bemotion::ui
