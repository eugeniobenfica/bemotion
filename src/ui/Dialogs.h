#pragma once

// The application's dialogs (each in its own file).

class QWidget;

namespace bemotion {

class EditorContext;

namespace ui {

enum class SettingsTab { Theme, Interface, Editor, Performance, Shortcuts, Plugins }; // the pages, in order
enum class HelpTab { Shortcuts, About, Credits, License, Learn };

void openSettings(SettingsTab tab = SettingsTab::Theme); // SettingsWindow.cpp (one window, raised if open)
void openHelp(HelpTab tab = HelpTab::Shortcuts);         // HelpWindow.cpp
void showExportDialog(EditorContext& ctx, QWidget* parent); // ExportDialog.cpp (modal)

} // namespace ui
} // namespace bemotion
