#pragma once

// Thin OS abstraction over Qt: well-known folders, native file dialogs,
// the file manager, UTF-8 paths.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bemotion::platform {

namespace fs = std::filesystem;

std::string toUtf8(const fs::path& path);
fs::path fromUtf8(const std::string& utf8); // invalid UTF-8 falls back to the system code page

// The command line and environment as UTF-8. On Windows the C runtime hands them over in
// the legacy code page, which mangles names like "C:\Users\João": these read the Unicode ones.
std::vector<std::string> arguments(int argc, char** argv);
std::string env(const char* name); // "" when unset

fs::path executableDir();
fs::path appDataDir();         // per-user settings, created on demand (BEMOTION_APPDATA overrides it)
fs::path documentsDir(); // the user's Documents (where dialogs start when there is no projects folder yet)
fs::path assetsDir();

std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const char* extension);
std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions);
// Several files at once; `extensions` without dots ("png", "mp4", ...). Empty when cancelled.
std::vector<fs::path> openFilesDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions);
std::optional<fs::path> chooseFolderDialog(const char* title, const fs::path& start);
std::optional<fs::path> saveFileDialog(const char* title, const fs::path& suggested, const char* filterName,
                                       const char* extension);

void revealInFileManager(const fs::path& path);
void openWithDefaultApp(const fs::path& path);
// Moves a file to the Recycle Bin / Trash (never deletes it outright).
bool moveToTrash(const fs::path& path, std::string* error = nullptr);

// Diagnostics: <app data>/bemotion.log, started fresh by startLog() (which also records
// crashes there), one timestamped line per log() call. Users can attach it to bug reports.
void startLog();
void log(const std::string& line);
fs::path logFile();

} // namespace bemotion::platform
