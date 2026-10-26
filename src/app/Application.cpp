#include "app/Application.h"

#include "app/AppIcon.h"
#include "app/Automation.h"
#include "app/DevScript.h"
#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Recovery.h"
#include "app/Settings.h"
#include "core/Serialization.h"
#include "media/AudioEngine.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "media/Recorder.h"
#include "render/Fonts.h"
#include "render/SceneRenderer.h"
#include "ui/AppSignals.h"
#include "ui/Dialogs.h"
#include "ui/EditorWindow.h"
#include "ui/ExperienceScreen.h"
#include "ui/HomeScreen.h"
#include "ui/IntroScreen.h"
#include "ui/Language.h"
#include "ui/Keymap.h"
#include "ui/Style.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QDateTime>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
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
    QApplication::setStyle(new ui::Style); // Fusion with our check boxes; the palette and style sheet do the rest
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
    // The first start: how much the interface shows, asked once the intro is done.
    if (projectArg.empty() && settings().firstRun) {
        if (intro_->isVisible()) connect(intro_, &IntroScreen::finished, this, [this] { showFirstRun(); });
        else QTimer::singleShot(0, this, [this] { showFirstRun(); });
    }
    // Changes a crash left unsaved: offered once the window (and the intro) is out of the way.
    if (intro_->isVisible()) connect(intro_, &IntroScreen::finished, this, [this] { QTimer::singleShot(0, this, [this] { offerRecovery(); }); });
    else QTimer::singleShot(300, this, [this] { offerRecovery(); });

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
    ui::installLanguage(settings().language); // before any window reads its texts
    Fonts::load(platform::assetsDir());
    render::setLutFolder(platform::assetsDir() / "luts");
    PluginHost::get().load();
    media::MediaManager::get().install();
    media::setHardwareDecoding(settings().hardwareDecoding);
    media::setProxyFolder(platform::appDataDir() / "proxies");
    media::MediaManager::get().setUseProxies(settings().useProxies);
    media::MediaManager::get().setFrameCacheMegabytes(settings().cacheMegabytes / 2); // the other half: the viewport's
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
    recents_.load(); // the Welcome project joins the projects folder when the user picks one (HomeScreen)
    buildHome();
    home_->refresh();
    // Every text is read as a window is built: another language builds them again.
    connect(&ui::AppSignals::get(), &ui::AppSignals::languageChanged, this, [this] { QTimer::singleShot(0, this, [this] { retranslate(); }); });

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

void Application::buildHome() {
    home_ = new HomeScreen(recents_);
    home_->onOpen = [this](const std::filesystem::path& p) { openProject(p); };
    home_->onCreate = [this](const std::filesystem::path& p, Project project) { createProject(std::move(project), p); };
    // Settings and Help work from the home screen too (the editor has its own menu actions).
    auto* settingsAction = new QAction(home_);
    auto* helpAction = new QAction(home_);
    auto* fullAction = new QAction(home_);
    for (QAction* a : {settingsAction, helpAction, fullAction}) a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(settingsAction, &QAction::triggered, this, [] { ui::openSettings(); });
    connect(helpAction, &QAction::triggered, this, [] { ui::openHelp(); });
    connect(fullAction, &QAction::triggered, this, [this] { ui::toggleFullScreen(window_); });
    home_->addAction(settingsAction);
    home_->addAction(helpAction);
    home_->addAction(fullAction);
    auto bindHome = [settingsAction, helpAction, fullAction] {
        settingsAction->setShortcut(ui::sequenceOf(ui::Action::Settings));
        helpAction->setShortcut(ui::sequenceOf(ui::Action::Help));
        fullAction->setShortcut(ui::sequenceOf(ui::Action::FullScreen));
    };
    bindHome();
    connect(&ui::AppSignals::get(), &ui::AppSignals::shortcutsChanged, home_, bindHome);
    stack_->insertWidget(0, home_);
}

void Application::retranslate() {
    HomeScreen* old = home_;
    const bool homeShown = stack_->currentWidget() == old;
    buildHome();
    home_->refresh();
    if (homeShown) stack_->setCurrentWidget(home_);
    stack_->removeWidget(old);
    old->deleteLater();
    // A recording keeps its window: the editor follows once it ends (frame()).
    if (editor_) {
        if (media::Recorder::get().recording()) editorLanguagePending_ = true;
        else rebuildEditor();
    }
    updateTitle();
}

void Application::rebuildEditor() {
    editorLanguagePending_ = false;
    // The project, its undo history, selection and playhead go on in the new window; the old one
    // closes first (it saves the dock layout the new one opens with).
    EditorContext context = editor_->context();
    stack_->removeWidget(editor_);
    editor_->handOver();
    delete editor_;
    editor_ = new EditorWindow(std::move(context), recents_);
    attachEditor();
    // Its shortcuts work once the window is back in front (the old focus went with the old one).
    editor_->setFocus();
}

void Application::frame() {
    ++frameIndex_;
    if (editorLanguagePending_ && editor_ && !media::Recorder::get().recording()) rebuildEditor();
    PluginHost::get().processPendingReload(); // Settings also manages plugins from the home screen.
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
        std::map<QString, int> seen; // a submenu beside its menu: "menu", "menu2"
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w == window_ || !w->isVisible()) continue;
            QString title = qobject_cast<QMenu*>(w)                   ? QString("menu")
                            : w == QApplication::activePopupWidget() ? QString("popup") // a list's drop-down
                                                                     : w->windowTitle().replace(' ', '_');
            for (const QChar c : QString("/\\:*?\"<>|")) title.replace(c, '-'); // a file name ("Speed / Duration")
            if (title.isEmpty()) continue;
            if (const int n = ++seen[title]; n > 1) title += QString::number(n);
            w->grab().save(file.left(dot) + "-" + title + file.mid(dot));
        }
    }
    if (script_->wantsQuit()) {
        // Kept alive: an earlier step may still be running under this one (it opened a popup or a
        // dialog with its own event loop) and returns into it.
        retiredScript_ = std::move(script_);
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
        const std::string message = tr("Could not open \"%1\": %2").arg(QString::fromStdString(platform::toUtf8(path.filename())), QString::fromStdString(err)).toStdString();
        if (editor_) QMessageBox::warning(window_, tr("Open Project"), QString::fromStdString(message)); // from the editor's File menu
        else home_->setError(message);
        return;
    }
    recents_.touch(path, project.name);
    PluginHost::get().reconcile(project.comp);
    showEditor(std::move(project), path);
}

void Application::createProject(Project project, const std::filesystem::path& path) {
    std::string err;
    if (!saveProject(project, path, &err)) {
        home_->setError(tr("Could not create the project: %1").arg(QString::fromStdString(err)).toStdString());
        return;
    }
    recents_.touch(path, project.name);
    showEditor(std::move(project), path);
}

void Application::showEditor(Project project, const std::filesystem::path& path) {
    if (editor_) {
        // Now, not later: its closing (the layout it saves, the preview it lets go) comes before
        // the new window opens.
        stack_->removeWidget(editor_);
        delete editor_;
    }
    editor_ = new EditorWindow(std::move(project), path, recents_);
    attachEditor();
    if (intro_->isVisible()) intro_->raise();
    else editor_->setFocus();
}

void Application::attachEditor() {
    stack_->addWidget(editor_);
    stack_->setCurrentWidget(editor_);
    connect(editor_, &EditorWindow::goHome, this, [this] { QTimer::singleShot(0, this, [this] { goHome(); }); });
    connect(editor_, &EditorWindow::newProjectRequested, this, [this] {
        QTimer::singleShot(0, this, [this] {
            goHome();
            home_->openNewProjectDialog();
        });
    });
    connect(editor_, &EditorWindow::openProjectRequested, this, [this](std::filesystem::path path) {
        QTimer::singleShot(0, this, [this, path] { openProject(path); });
    });
    connect(editor_, &EditorWindow::quitRequested, window_, &QWidget::close);
    connect(editor_, &EditorWindow::titleChanged, this, [this] { updateTitle(); });
    updateTitle();
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

void Application::showFirstRun() {
    auto* screen = new ui::ExperienceScreen(window_);
    window_->overlay = screen;
    screen->setGeometry(window_->rect());
    screen->show();
    screen->raise();
    screen->setFocus();
    connect(screen, &ui::ExperienceScreen::finished, this, [this, screen] {
        window_->overlay = intro_;
        screen->deleteLater();
        if (editor_) editor_->setFocus();
        else home_->setFocus();
    });
}

void Application::offerRecovery() {
    const std::vector<recovery::Entry> entries = recovery::pending();
    if (entries.empty()) return;
    const recovery::Entry& e = entries.front();
    QMessageBox box(window_);
    box.setWindowTitle(tr("Recover Unsaved Changes"));
    box.setText(tr("bemotion closed before \"%1\" was saved.").arg(QString::fromStdString(e.name)));
    QString info = tr("Its changes up to %1 were kept. Recover them?")
                       .arg(QDateTime::fromSecsSinceEpoch(e.savedAt).toString("hh:mm, dd MMM"));
    if (entries.size() > 1) info += tr("\n\n%1 more project(s) can be recovered next time.").arg(entries.size() - 1);
    box.setInformativeText(info);
    QPushButton* recover = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
    recover->setProperty("accent", true);
    QPushButton* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(recover);
    box.exec();
    if (box.clickedButton() == discard) {
        recovery::discard(e);
        return;
    }
    if (box.clickedButton() != recover) return;
    Project project;
    std::string err;
    if (!loadProject(e.copy, project, &err)) {
        home_->setError(tr("Could not recover \"%1\": %2").arg(QString::fromStdString(e.name), QString::fromStdString(err)).toStdString());
        return;
    }
    platform::log("recovering " + e.name);
    if (editor_ && !editor_->confirmClose()) return;
    recovery::discard(e); // the editor keeps its own copy from now on
    if (!e.original.empty()) recents_.touch(e.original, project.name);
    PluginHost::get().reconcile(project.comp);
    showEditor(std::move(project), e.original);
    editor_->markRecovered();
}

void Application::updateTitle() { window_->setWindowTitle(editor_ ? editor_->title() : QString("bemotion")); }

} // namespace bemotion
