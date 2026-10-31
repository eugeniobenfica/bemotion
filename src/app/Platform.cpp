#include "app/Platform.h"

#include "core/Utf8.h"

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
#include <iterator>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define BEMOTION_HAS_BACKTRACE 1
#endif
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
    return QCoreApplication::translate("bemotion", "%1 (%2);;All files (*)").arg(QCoreApplication::translate("bemotion", name), patterns);
}

std::string withoutDot(const char* ext) { return ext && ext[0] == '.' ? ext + 1 : (ext ? ext : ""); }

} // namespace

std::string toUtf8(const fs::path& path) { return utf8::fromPath(path); }

fs::path fromUtf8(const std::string& utf8) { return bemotion::utf8::toPath(utf8); }

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
    const QString file = QFileDialog::getOpenFileName(dialogParent(), QCoreApplication::translate("bemotion", title), QString(),
                                                      filterFor(filterName, {withoutDot(extension)}));
    if (file.isEmpty()) return std::nullopt;
    return fromQ(file);
}

std::optional<fs::path> openFileDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions) {
    const QString file = QFileDialog::getOpenFileName(dialogParent(), QCoreApplication::translate("bemotion", title), QString(), filterFor(filterName, extensions));
    if (file.isEmpty()) return std::nullopt;
    return fromQ(file);
}

std::optional<fs::path> chooseFolderDialog(const char* title, const fs::path& start) {
    const QString dir = QFileDialog::getExistingDirectory(dialogParent(), QCoreApplication::translate("bemotion", title), qpath(start));
    if (dir.isEmpty()) return std::nullopt;
    return fromQ(dir);
}

std::vector<fs::path> openFilesDialog(const char* title, const char* filterName, const std::vector<std::string>& extensions) {
    std::vector<fs::path> out;
    for (const QString& f : QFileDialog::getOpenFileNames(dialogParent(), QCoreApplication::translate("bemotion", title), QString(),
                                                          filterFor(filterName, extensions)))
        out.push_back(fromQ(f));
    return out;
}

std::optional<fs::path> saveFileDialog(const char* title, const fs::path& suggested, const char* filterName,
                                       const char* extension) {
    const QString file = QFileDialog::getSaveFileName(dialogParent(), QCoreApplication::translate("bemotion", title), qpath(suggested),
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

// Crash reports (setCrashReports): what the crash handlers need, made ready beforehand, since a
// crashing process should not allocate or take locks.
bool g_crashReports = false;
std::string g_previousLog;           // the session before's log, read by startLog
std::vector<fs::path> g_newReports;  // kept from it at this start
std::string g_logPath;               // for the signal handler (UTF-8; POSIX paths are bytes)

std::string crashStamp(const QDateTime& when) { return when.toString("yyyy-MM-dd-hhmmss").toStdString(); }

#ifdef _WIN32
using MiniDumpWriteDumpFn = decltype(&MiniDumpWriteDump);
MiniDumpWriteDumpFn g_writeDump = nullptr;
std::wstring g_dumpPath; // crash-<start time>.dmp

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "CRASH: exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
    writeLog(buf, false);
    if (g_crashReports && g_writeDump && !g_dumpPath.empty()) {
        const HANDLE file = CreateFileW(g_dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION ex{GetCurrentThreadId(), info, FALSE};
            // The threads' stacks and the modules: enough for a stack trace, without the heap
            // (which could hold the user's pictures or text).
            g_writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &ex, nullptr, nullptr);
            CloseHandle(file);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting still gets it
}
#else
// Signals: a line and the stack into the log, with calls safe in a signal handler only, then
// the default action (the process ends as it would have).
void onSignal(int sig) {
    const int fd = g_logPath.empty() ? -1 : ::open(g_logPath.c_str(), O_WRONLY | O_APPEND);
    if (fd >= 0) {
        char line[] = "CRASH: signal 00\n"; // (no printf in a signal handler)
        line[14] = static_cast<char>('0' + sig / 10 % 10);
        line[15] = static_cast<char>('0' + sig % 10);
        if (::write(fd, line, sizeof line - 1) < 0) {}
#ifdef BEMOTION_HAS_BACKTRACE
        void* frames[64];
        backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
#endif
        ::close(fd);
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

} // namespace

fs::path logFile() { return appDataDir() / "bemotion.log"; }

void startLog() {
    {
        std::ifstream previous(logFile(), std::ios::binary);
        g_previousLog.assign(std::istreambuf_iterator<char>(previous), std::istreambuf_iterator<char>());
    }
    writeLog("bemotion " BEMOTION_VERSION " started", true);
    g_logPath = toUtf8(logFile());
#ifdef _WIN32
    SetUnhandledExceptionFilter(onCrash);
#else
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) std::signal(sig, onSignal);
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

fs::path crashFolder() { return appDataDir() / "crashes"; }

void setCrashReports(bool on) {
    g_crashReports = on;
    if (!on) return;
    std::error_code ec;
    fs::create_directories(crashFolder(), ec);
    // The session before ended in a crash: its log is the report (startLog read it).
    if (g_previousLog.find("CRASH:") != std::string::npos) {
        const fs::path report = crashFolder() / fromUtf8("crash-" + crashStamp(QDateTime::currentDateTime().addSecs(-1)) + ".log");
        std::ofstream(report, std::ios::binary) << g_previousLog;
        g_newReports.push_back(report);
        // A minidump that crash left (Windows) goes with it.
        for (const auto& entry : fs::directory_iterator(crashFolder(), ec))
            if (entry.path().extension() == ".dmp" && toUtf8(entry.path().stem()).ends_with("-pending")) {
                fs::path dump = report;
                dump.replace_extension(".dmp");
                fs::rename(entry.path(), dump, ec);
                if (!ec) g_newReports.push_back(dump);
            }
        g_previousLog.clear();
    }
#ifdef _WIN32
    if (!g_writeDump)
        if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll"))
            g_writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
    g_dumpPath = (crashFolder() / ("crash-" + crashStamp(QDateTime::currentDateTime()) + "-pending.dmp")).wstring();
#endif
}

std::vector<fs::path> newCrashReports() { return g_newReports; }

} // namespace bemotion::platform
