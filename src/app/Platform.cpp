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
#include <QUrl>

#include <cstdlib>

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

fs::path fromUtf8(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

fs::path executableDir() {
    if (QCoreApplication::instance()) return fromQ(QCoreApplication::applicationDirPath());
    return fs::current_path();
}

fs::path appDataDir() {
    // Developer aid: BEMOTION_APPDATA points settings, layout and caches elsewhere (scripted UI tests).
    if (const char* dir = std::getenv("BEMOTION_APPDATA"); dir && *dir) return ensureDir(fromUtf8(dir));
#if defined(_WIN32)
    if (const char* roaming = std::getenv("APPDATA"); roaming && *roaming) return ensureDir(fromUtf8(roaming) / "bemotion");
    return ensureDir(fromQ(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)) / "bemotion");
#elif defined(__APPLE__)
    return ensureDir(fromQ(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)) / "bemotion"); // Application Support
#else
    return ensureDir(fromQ(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)) / "bemotion"); // ~/.config
#endif
}

fs::path defaultProjectsDir() {
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) docs = QDir::homePath() + "/Documents";
    return ensureDir(fromQ(docs) / "bemotion");
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
    const QString file = QFileDialog::getOpenFileName(dialogParent(), QString::fromUtf8(title), QString(),
                                                      filterFor(filterName, {withoutDot(extension)}));
    if (file.isEmpty()) return std::nullopt;
    return fromQ(file);
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

} // namespace bemotion::platform
