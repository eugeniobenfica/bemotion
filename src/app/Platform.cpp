#include "app/Platform.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

namespace bemotion::platform {

namespace {
void* g_window = nullptr;

fs::path ensureDir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) result = raw;
    CoTaskMemFree(raw);
    return result;
}

// "bemotion Project", ".bemotion" -> L"bemotion Project (*.bemotion)\0*.bemotion\0\0"
std::wstring makeFilter(const char* name, const char* ext) {
    std::wstring f = widen(std::string(name) + " (*" + ext + ")");
    f.push_back(L'\0');
    f += widen(std::string("*") + ext);
    f.push_back(L'\0');
    f.push_back(L'\0');
    return f;
}
#else
std::optional<fs::path> runDialogCommand(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::nullopt;
    char buffer[4096];
    std::string out;
    while (fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (out.empty()) return std::nullopt;
    return fs::path(out);
}
#endif
} // namespace

std::string toUtf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

fs::path fromUtf8(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

void setMainWindowHandle(void* handle) { g_window = handle; }

fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : exe.parent_path();
#endif
}

fs::path appDataDir() {
    // Developer aid: BEMOTION_APPDATA points settings, layout and caches elsewhere (scripted UI tests).
    if (const char* dir = std::getenv("BEMOTION_APPDATA"); dir && *dir) return ensureDir(fromUtf8(dir));
#ifdef _WIN32
    return ensureDir(knownFolder(FOLDERID_RoamingAppData) / "bemotion");
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return ensureDir(fs::path(home ? home : ".") / "Library/Application Support/bemotion");
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return ensureDir(fs::path(xdg) / "bemotion");
    const char* home = std::getenv("HOME");
    return ensureDir(fs::path(home ? home : ".") / ".config/bemotion");
#endif
}

fs::path defaultProjectsDir() {
#ifdef _WIN32
    fs::path docs = knownFolder(FOLDERID_Documents);
#else
    const char* home = std::getenv("HOME");
    fs::path docs = fs::path(home ? home : ".") / "Documents";
#endif
    return ensureDir(docs / "bemotion");
}

fs::path assetsDir() {
    std::error_code ec;
    fs::path nextToExe = executableDir() / "assets";
    if (fs::exists(nextToExe, ec)) return nextToExe;
#ifdef BEMOTION_SOURCE_ASSETS_DIR
    return fs::path(BEMOTION_SOURCE_ASSETS_DIR);
#else
    return nextToExe;
#endif
}

std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const char* extension) {
#ifdef _WIN32
    wchar_t file[MAX_PATH * 4] = L"";
    const std::wstring filter = makeFilter(filterName, extension);
    const std::wstring wtitle = widen(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(g_window);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return fs::path(file);
#elif defined(__APPLE__)
    (void)filterName;
    return runDialogCommand(std::string("osascript -e 'POSIX path of (choose file with prompt \"") + title +
                            "\" of type {\"" + (extension + 1) + "\"})' 2>/dev/null");
#else
    return runDialogCommand(std::string("zenity --file-selection --title=\"") + title + "\" --file-filter=\"" +
                            filterName + " | *" + extension + "\" 2>/dev/null");
#endif
}

std::vector<fs::path> openFilesDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions) {
    std::vector<fs::path> files;
#ifdef _WIN32
    std::string patterns;
    for (const auto& e : extensions) patterns += (patterns.empty() ? "*." : ";*.") + e;
    std::wstring filter = widen(std::string(filterName));
    filter.push_back(L'\0');
    filter += widen(patterns);
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    std::vector<wchar_t> buffer(64 * 1024, L'\0');
    const std::wstring wtitle = widen(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(g_window);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return files;
    // One file: its full path. Several: the folder, then each name, separated by NULs.
    const wchar_t* p = buffer.data();
    const fs::path first(p);
    p += wcslen(p) + 1;
    if (!*p) return {first};
    for (; *p; p += wcslen(p) + 1) files.push_back(first / p);
#else
    std::string patterns;
    for (const auto& e : extensions) patterns += " *." + e;
    const std::string cmd = std::string("zenity --file-selection --multiple --separator='\n' --title=\"") + title +
                            "\" --file-filter=\"" + filterName + " |" + patterns + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return files;
    char line[4096];
    while (fgets(line, sizeof(line), pipe)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) files.emplace_back(s);
    }
    pclose(pipe);
#endif
    return files;
}

std::optional<fs::path> saveFileDialog(const char* title, const fs::path& suggested, const char* filterName,
                                       const char* extension) {
#ifdef _WIN32
    wchar_t file[MAX_PATH * 4] = L"";
    const std::wstring name = suggested.filename().wstring();
    wcsncpy(file, name.c_str(), std::size(file) - 1);
    const std::wstring dir = suggested.parent_path().wstring();
    const std::wstring filter = makeFilter(filterName, extension);
    const std::wstring wtitle = widen(title);
    const std::wstring defExt = widen(extension + 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(g_window);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrDefExt = defExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return fs::path(file);
#else
    std::optional<fs::path> result;
#if defined(__APPLE__)
    (void)filterName;
    result = runDialogCommand(std::string("osascript -e 'POSIX path of (choose file name with prompt \"") + title +
                              "\" default name \"" + toUtf8(suggested.filename()) + "\")' 2>/dev/null");
#else
    result = runDialogCommand(std::string("zenity --file-selection --save --confirm-overwrite --title=\"") + title +
                              "\" --filename=\"" + toUtf8(suggested) + "\" --file-filter=\"" + filterName + " | *" +
                              extension + "\" 2>/dev/null");
#endif
    if (result && result->extension() != extension) *result += extension;
    return result;
#endif
}

void revealInFileManager(const fs::path& path) {
#ifdef _WIN32
    const std::wstring args = L"/select,\"" + path.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open -R \"" + toUtf8(path) + "\"").c_str());
#else
    std::system(("xdg-open \"" + toUtf8(path.parent_path()) + "\" &").c_str());
#endif
}

bool moveToTrash(const fs::path& path, std::string* error) {
#ifdef _WIN32
    std::wstring from = path.wstring();
    from.push_back(L'\0'); // the list ends with an empty name
    SHFILEOPSTRUCTW op{};
    op.hwnd = static_cast<HWND>(g_window);
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const bool ok = SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
#elif defined(__APPLE__)
    const bool ok = std::system(("osascript -e 'tell application \"Finder\" to delete POSIX file \"" + toUtf8(path) +
                                 "\"' >/dev/null 2>&1").c_str()) == 0;
#else
    const bool ok = std::system(("gio trash \"" + toUtf8(path) + "\" >/dev/null 2>&1").c_str()) == 0;
#endif
    if (!ok && error) *error = "the file could not be moved to the trash.";
    return ok;
}

void openWithDefaultApp(const fs::path& path) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open \"" + toUtf8(path) + "\"").c_str());
#else
    std::system(("xdg-open \"" + toUtf8(path) + "\" &").c_str());
#endif
}

} // namespace bemotion::platform
