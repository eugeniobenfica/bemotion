#include "app/Platform.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDateTime>
#include <QUrl>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace bemotion::platform {

namespace {

fs::path ensureDir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

QString qpath(const fs::path& p) { return QString::fromStdString(toUtf8(p)); }
fs::path fromQ(const QString& s) { return fromUtf8(s.toStdString()); }

QWidget* dialogParent() { return QApplication::activeWindow(); }

QString filterFor(const char* name, const std::vector<std::string>& extensions) {
    QString patterns;
    for (const auto& e : extensions) patterns += QString(patterns.isEmpty() ? "*." : " *.") + QString::fromStdString(e);
    return QString("%1 (%2);;All files (*)").arg(QString::fromUtf8(name), patterns);
}

std::string withoutDot(const char* ext) { return ext && ext[0] == '.' ? ext + 1 : (ext ? ext : ""); }

} // namespace

std::string toUtf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

fs::path fromUtf8(const std::string& utf8) {
    try {
        return fs::path(std::u8string(utf8.begin(), utf8.end()));
    } catch (const std::exception&) { // not UTF-8 after all: read it in the system code page
        return fs::path(utf8);
    }
}

std::vector<std::string> arguments(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int n = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 0; i < n; ++i) out.push_back(QString::fromWCharArray(wide[i]).toStdString());
        LocalFree(wide);
        return out;
    }
#endif
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

std::string env(const char* name) { return qEnvironmentVariable(name).toStdString(); }

fs::path executableDir() {
    if (QCoreApplication::instance()) return fromQ(QCoreApplication::applicationDirPath());
    return fs::current_path();
}

fs::path appDataDir() {
    // Developer aid: BEMOTION_APPDATA points settings, layout and caches elsewhere (scripted UI tests).
    if (const std::string dir = env("BEMOTION_APPDATA"); !dir.empty()) return ensureDir(fromUtf8(dir));
#if defined(_WIN32)
    if (const std::string roaming = env("APPDATA"); !roaming.empty()) return ensureDir(fromUtf8(roaming) / "bemotion");
    return ensureDir(fromQ(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)) / "bemotion");
#elif defined(__APPLE__)
    return ensureDir(fromQ(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)) / "bemotion"); // Application Support
#else
    return ensureDir(fromQ(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)) / "bemotion"); // ~/.config
#endif
}

fs::path documentsDir() {
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) docs = QDir::homePath();
    return fromQ(docs);
}

std::vector<fs::path> speechModelFolders() {
    std::vector<fs::path> folders;
    if (const std::string env = platform::env("BEMOTION_MODELS"); !env.empty()) folders.push_back(fromUtf8(env));
    folders.push_back(assetsDir() / "models");
    folders.push_back(appDataDir() / "models");
    return folders;
}

fs::path welcomeProject() {
    const fs::path copy = appDataDir() / "Welcome" / "Welcome to bemotion.bemotion";
    std::error_code ec;
    if (fs::exists(copy, ec)) return copy;
    fs::create_directories(copy.parent_path(), ec);
    fs::copy_file(assetsDir() / "welcome" / "Welcome.bemotion", copy, ec);
    return ec ? fs::path() : copy;
}

fs::path assetsDir() {
    std::error_code ec;
    fs::path nextToExe = executableDir() / "assets";
    if (fs::exists(nextToExe, ec)) return nextToExe;
    // A macOS app bundle: Contents/MacOS/bemotion, Contents/Resources/assets.
    if (fs::path bundled = executableDir().parent_path() / "Resources" / "assets"; fs::exists(bundled, ec)) return bundled;
#ifdef BEMOTION_SOURCE_ASSETS_DIR
    return fs::path(BEMOTION_SOURCE_ASSETS_DIR);
#else
    return nextToExe;
#endif
}

std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const char* extension) {
    const QString file = QFileDialog::getOpenFileName(dialogParent(), QString::fromUtf8(title), QString(),
                                                      filterFor(filterName, {withoutDot(extension)}));
    if (file.isEmpty()) return std::nullopt;
    return fromQ(file);
}

std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions) {
    const QString file = QFileDialog::getOpenFileName(dialogParent(), QString::fromUtf8(title), QString(), filterFor(filterName, extensions));
    if (file.isEmpty()) return std::nullopt;
    return fromQ(file);
}

std::optional<fs::path> chooseFolderDialog(const char* title, const fs::path& start) {
    const QString dir = QFileDialog::getExistingDirectory(dialogParent(), QString::fromUtf8(title), qpath(start));
    if (dir.isEmpty()) return std::nullopt;
    return fromQ(dir);
}

std::vector<fs::path> openFilesDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions) {
    std::vector<fs::path> out;
    for (const QString& f : QFileDialog::getOpenFileNames(dialogParent(), QString::fromUtf8(title), QString(),
                                                          filterFor(filterName, extensions)))
        out.push_back(fromQ(f));
    return out;
}

std::optional<fs::path> saveFileDialog(const char* title, const fs::path& suggested, const char* filterName,
                                       const char* extension) {
    const QString file = QFileDialog::getSaveFileName(dialogParent(), QString::fromUtf8(title), qpath(suggested),
                                                      filterFor(filterName, {withoutDot(extension)}));
    if (file.isEmpty()) return std::nullopt;
    fs::path p = fromQ(file);
    if (extension && *extension && p.extension().empty()) p += extension;
    return p;
}

void revealInFileManager(const fs::path& path) {
    const QString native = QDir::toNativeSeparators(qpath(path));
#if defined(_WIN32)
    QProcess::startDetached("explorer.exe", {"/select,", native});
#elif defined(__APPLE__)
    QProcess::startDetached("open", {"-R", native});
#else
    const QFileInfo info(native);
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
#endif
}

void openWithDefaultApp(const fs::path& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(qpath(path))); }

bool moveToTrash(const fs::path& path, std::string* error) {
    if (QFile::moveToTrash(qpath(path))) return true;
    if (error) *error = "the file could not be moved to the trash.";
    return false;
}

// --- diagnostics -----------------------------------------------------------------------------

namespace {

std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

void writeLog(const std::string& line, bool truncate) {
    const std::lock_guard<std::mutex> lock(logMutex());
    std::ofstream out(logFile(), truncate ? std::ios::trunc : std::ios::app);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz").toStdString() << "  " << line << '\n';
}

#ifdef _WIN32
LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "CRASH: exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
    writeLog(buf, false);
    return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting still gets it
}
#endif

} // namespace

fs::path logFile() { return appDataDir() / "bemotion.log"; }

void startLog() {
    writeLog("bemotion " BEMOTION_VERSION " started", true);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onCrash);
#endif
    std::set_terminate([] {
        std::string what = "unknown";
        try {
            if (auto e = std::current_exception()) std::rethrow_exception(e);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        writeLog("CRASH: unhandled exception: " + what, false);
        std::abort();
    });
}

void log(const std::string& line) { writeLog(line, false); }

} // namespace bemotion::platform
