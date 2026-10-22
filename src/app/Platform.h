#pragma once

// Thin OS abstraction: well-known folders, native file dialogs, UTF-8 paths.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bemotion::platform {

namespace fs = std::filesystem;

std::string toUtf8(const fs::path& path);
fs::path fromUtf8(const std::string& utf8);

fs::path executableDir();
fs::path appDataDir();         // per-user settings, created on demand
fs::path defaultProjectsDir(); // Documents/bemotion, created on demand
fs::path assetsDir();

// Native window handle used to parent dialogs (HWND on Windows).
void setMainWindowHandle(void* handle);

std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const char* extension);
// Several files at once; `extensions` without dots ("png", "mp4", ...). Empty when cancelled.
std::vector<fs::path> openFilesDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions);
std::optional<fs::path> saveFileDialog(const char* title, const fs::path& suggested, const char* filterName,
                                       const char* extension);

void revealInFileManager(const fs::path& path);
void openWithDefaultApp(const fs::path& path);
// Moves a file to the Recycle Bin / Trash (never deletes it outright).
bool moveToTrash(const fs::path& path, std::string* error = nullptr);

} // namespace bemotion::platform
