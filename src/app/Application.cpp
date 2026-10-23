#include "app/Application.h"

#include "app/AppIcon.h"
#include "app/Automation.h"
#include "app/DevScript.h"
#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Settings.h"
#include "core/Serialization.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "render/Fonts.h"
#include "ui/AppSignals.h"
#include "ui/Dialogs.h"
#include "ui/EditorWindow.h"
#include "ui/HomeScreen.h"
#include "ui/IntroScreen.h"
#include "ui/Keymap.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bemotion {

// --- window -------------------------------------------------------------------------------

MainWindow::MainWindow() {
    setAcceptDrops(true);
    setMinimumSize(960, 600);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (canClose && !canClose()) {
        e->ignore();
        return;
    }
    e->accept();
    // Settings, Help, ... are windows of their own: the application ends with this one.
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (overlay) overlay->setGeometry(rect());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    std::vector<std::filesystem::path> files;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile()) files.push_back(platform::fromUtf8(url.toLocalFile().toStdString()));
    if (!files.empty() && onDrop) onDrop(files);
    e->acceptProposedAction();
}

// --- application ----------------------------------------------------------------------------

Application::Application() = default;
Application::~Application() = default;

int Application::run(int argc, char** argv) {
    // Command line: bemotion [project] [--export <file> [--format mp4|png|mov|webm|gif] [--scale N] [--transparent]]
    std::filesystem::path projectArg;
    std::optional<exporting::Settings> cliExport;
    const std::vector<std::string> args = platform::arguments(argc, argv); // UTF-8, also on Windows
    const int count = static_cast<int>(args.size());
    QCoreApplication::setApplicationName("bemotion");
    QCoreApplication::setOrganizationName("bemotion");
    if (count > 1 && isAutomationCommand(args[1])) { // describe, validate, apply, render, new
        QCoreApplication core(argc, argv);
        attachParentConsole();
        initShared();
        const int code = runAutomation(args);
        media::MediaManager::get().release();
        PluginHost::get().shutdown();
        return code;
    }
    for (int i = 1; i < count; ++i) {
        const std::string& arg = args[i];
        auto next = [&]() { return i + 1 < count ? args[++i] : std::string(); };
        if (arg == "--write-icon" || arg == "--write-mark") { // developer aid: refresh the branding files
            const std::string file = next();
            return writeAppIcon(platform::fromUtf8(file), arg == "--write-mark") ? 0 : 1;
        }
        if (arg == "--export") {
            if (!cliExport) cliExport.emplace();
            cliExport->output = platform::fromUtf8(next());
        } else if (arg == "--format" || arg == "--scale" || arg == "--transparent") {
            if (!cliExport) cliExport.emplace();
            if (arg == "--transparent") cliExport->transparent = true;
            else if (arg == "--scale") cliExport->scale = std::strtof(next().c_str(), nullptr);
            else {
                const std::string f = next();
                for (int k = 0; k < static_cast<int>(exporting::Format::Count); ++k)
                    if ("." + f == exporting::formatExtension(static_cast<exporting::Format>(k)))
                        cliExport->format = static_cast<exporting::Format>(k);
            }
        } else {
            projectArg = platform::fromUtf8(arg);
        }
    }
    QCoreApplication::setApplicationName("bemotion");
    QCoreApplication::setOrganizationName("bemotion");
    QCoreApplication::setApplicationVersion(BEMOTION_VERSION);
    if (cliExport) {
        QCoreApplication core(argc, argv);
        return runCommandLineExport(projectArg, *cliExport);
    }

    QApplication app(argc, argv);
    platform::startLog();
    platform::log("Qt " + std::string(qVersion()) + " on " + QSysInfo::prettyProductName().toStdString() + ", screen scale " +
                  std::to_string(app.devicePixelRatio()));
    QApplication::setStyle("Fusion"); // the palette and style sheet do the rest
    initShared();
    // The interface font: Inter ships with bemotion (other families come from the system).
    for (const char* style : {"Regular", "Medium", "SemiBold", "Bold"})
        QFontDatabase::addApplicationFont(QString::fromStdString(platform::toUtf8(Fonts::interFile(style))));
    applySettings();
    media::AudioEngine::get().start();
    platform::log("fonts, plugins and media ready");
    buildWindow();
    platform::log("window shown");
    if (const std::string script = platform::env("BEMOTION_SCRIPT"); !script.empty()) {
        script_ = std::make_unique<DevScript>();
        if (!script_->load(platform::fromUtf8(script))) {
            platform::log("could not read the script " + script);
            script_.reset();
        }
    }
    auto* timer = new QTimer(window_);
    connect(timer, &QTimer::timeout, this, [this] { frame(); });
    timer->start(16);

    if (!projectArg.empty()) openProject(projectArg);
    else if (settings().showIntro) intro_->start(platform::assetsDir() / "welcome" / "Welcome.bemotion");

    const int result = QApplication::exec();
    platform::log("quitting");
    delete window_; // the editor first: it stops using media and plugins
    window_ = nullptr;
    media::AudioEngine::get().stop();
    media::MediaManager::get().release();
    PluginHost::get().shutdown();
    return result;
}

void Application::initShared() {
    loadSettings();
    Fonts::load(platform::assetsDir());
    PluginHost::get().load();
    media::MediaManager::get().install();
}

void Application::buildWindow() {
    window_ = new MainWindow;
    window_->setWindowTitle("bemotion");
    // Window / taskbar icon.
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        const std::vector<uint8_t> px = renderAppIcon(size);
        icon.addPixmap(QPixmap::fromImage(QImage(px.data(), size, size, QImage::Format_RGBA8888).copy()));
    }
    QApplication::setWindowIcon(icon);

    stack_ = new QStackedWidget;
    window_->setCentralWidget(stack_);
    recents_.load();
    if (!settings().welcomeAdded) { // first run: the Welcome project waits in the projects list
        const auto copy = platform::defaultProjectsDir() / "Welcome to bemotion.bemotion";
        std::error_code ec;
        if (!std::filesystem::exists(copy, ec))
            std::filesystem::copy_file(platform::assetsDir() / "welcome" / "Welcome.bemotion", copy, ec);
        if (!ec) recents_.touch(copy, "Welcome to bemotion");
        settings().welcomeAdded = true;
        saveSettings();
    }
    home_ = new HomeScreen(recents_);
    home_->onOpen = [this](const std::filesystem::path& p) { openProject(p); };
    home_->onCreate = [this](const std::filesystem::path& p, Project project) { createProject(std::move(project), p); };
    // Settings and Help work from the home screen too (the editor has its own menu actions).
    auto* settingsAction = new QAction(home_);
    auto* helpAction = new QAction(home_);
    settingsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    helpAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(settingsAction, &QAction::triggered, this, [] { ui::openSettings(); });
    connect(helpAction, &QAction::triggered, this, [] { ui::openHelp(); });
    home_->addAction(settingsAction);
    home_->addAction(helpAction);
    auto bindHome = [settingsAction, helpAction] {
        settingsAction->setShortcut(ui::sequenceOf(ui::Action::Settings));
        helpAction->setShortcut(ui::sequenceOf(ui::Action::Help));
    };
    bindHome();
    connect(&ui::AppSignals::get(), &ui::AppSignals::shortcutsChanged, this, bindHome);
    stack_->addWidget(home_);
    home_->refresh();

    intro_ = new IntroScreen(window_);
    window_->overlay = intro_;
    intro_->hide();
    connect(intro_, &IntroScreen::finished, this, [this] {
        if (editor_) editor_->setFocus();
        else home_->setFocus();
    });

    window_->canClose = [this] { return !editor_ || editor_->confirmClose(); };
    window_->onDrop = [this](const std::vector<std::filesystem::path>& files) {
        if (editor_) editor_->dropFiles(files, false);
        else if (files.size() == 1 && files[0].extension() == kProjectExtension) openProject(files[0]);
    };
    window_->resize(1440, 900);
    window_->showMaximized();
    intro_->setGeometry(window_->rect());
}

void Application::frame() {
    ++frameIndex_;
    // Each step runs on its own: a step that opens a modal dialog (exec) must not hold up the
    // next ones, which drive that dialog.
    if (script_) QTimer::singleShot(0, this, [this] { scriptStep(); });
    // Developer aid: BEMOTION_CAPTURE=<file> saves frame BEMOTION_CAPTURE_FRAME (default 60) and quits.
    if (const std::string capture = platform::env("BEMOTION_CAPTURE"); !capture.empty()) {
        const std::string at = platform::env("BEMOTION_CAPTURE_FRAME");
        if (frameIndex_ == (at.empty() ? 60 : std::atoi(at.c_str()))) {
            window_->grab().save(QString::fromStdString(capture));
            window_->canClose = nullptr;
            window_->close();
        }
    }
}

void Application::scriptStep() {
    if (!script_) return;
    script_->step(window_);
    if (!script_) return; // quit while that step ran
    for (auto& f : script_->takeDrops())
        if (window_->onDrop) window_->onDrop({f});
    if (const std::string shot = script_->takeCapture(); !shot.empty()) {
        // The window, plus any other open window (settings, dialogs) as <file>-<title>.<ext>.
        const QString file = QString::fromStdString(shot);
        window_->grab().save(file);
        const int dot = file.lastIndexOf('.');
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w == window_ || !w->isVisible()) continue;
            const QString title = qobject_cast<QMenu*>(w) ? QString("menu") : w->windowTitle().replace(' ', '_');
            if (!title.isEmpty()) w->grab().save(file.left(dot) + "-" + title + file.mid(dot));
        }
    }
    if (script_->wantsQuit()) {
        script_.reset();
        window_->canClose = nullptr; // scripted runs never ask
        window_->close();
    }
}

int Application::runCommandLineExport(const std::filesystem::path& projectPath, exporting::Settings settings) {
    attachParentConsole();
    Project project;
    std::string error;
    if (projectPath.empty() || settings.output.empty()) {
        std::fprintf(stderr, "usage: bemotion <project.bemotion> --export <output> [--format mp4|png|mov|webm|gif] "
                             "[--scale 0.5] [--transparent]\n");
        return 2;
    }
    if (!loadProject(projectPath, project, &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    initShared();
    int reported = -1;
    const bool ok = exportProject(std::move(project), settings, [&](int done, int total) {
        const int percent = done * 100 / std::max(1, total);
        if (percent / 10 == reported) return;
        reported = percent / 10;
        std::printf("exporting... %d%% (%d/%d frames)\n", percent, done, total);
        std::fflush(stdout);
    }, error);
    int result = 0;
    if (!ok) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        result = 1;
    } else {
        std::printf("done: %s\n", platform::toUtf8(settings.output).c_str());
    }
    media::MediaManager::get().release();
    PluginHost::get().shutdown();
    return result;
}

void Application::openProject(const std::filesystem::path& path) {
    Project project;
    std::string err;
    platform::log("opening " + platform::toUtf8(path));
    if (!loadProject(path, project, &err)) {
        platform::log("could not open it: " + err);
        home_->setError("Could not open \"" + platform::toUtf8(path.filename()) + "\": " + err);
        return;
    }
    recents_.touch(path, project.name);
    PluginHost::get().reconcile(project.comp);
    showEditor(std::move(project), path);
}

void Application::createProject(Project project, const std::filesystem::path& path) {
    std::string err;
    if (!saveProject(project, path, &err)) {
        home_->setError("Could not create the project: " + err);
        return;
    }
    recents_.touch(path, project.name);
    showEditor(std::move(project), path);
}

void Application::showEditor(Project project, const std::filesystem::path& path) {
    if (editor_) {
        stack_->removeWidget(editor_);
        editor_->deleteLater();
    }
    editor_ = new EditorWindow(std::move(project), path, recents_);
    stack_->addWidget(editor_);
    stack_->setCurrentWidget(editor_);
    connect(editor_, &EditorWindow::goHome, this, [this] { QTimer::singleShot(0, this, [this] { goHome(); }); });
    connect(editor_, &EditorWindow::quitRequested, window_, &QWidget::close);
    connect(editor_, &EditorWindow::titleChanged, this, [this] { updateTitle(); });
    updateTitle();
    if (intro_->isVisible()) intro_->raise();
    else editor_->setFocus();
}

void Application::goHome() {
    if (editor_) {
        stack_->removeWidget(editor_);
        editor_->deleteLater();
        editor_ = nullptr;
    }
    media::AudioEngine::get().update(Composition{}, 0.0, false);
    recents_.load();
    stack_->setCurrentWidget(home_);
    home_->refresh();
    updateTitle();
}

void Application::updateTitle() { window_->setWindowTitle(editor_ ? editor_->title() : QString("bemotion")); }

} // namespace bemotion
