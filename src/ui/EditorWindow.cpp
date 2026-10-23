#include "ui/EditorWindow.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/RecentProjects.h"
#include "app/Settings.h"
#include "core/Serialization.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "ui/AppSignals.h"
#include "ui/Dialogs.h"
#include "ui/Keymap.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ObjectsPanel.h"
#include "ui/panels/PropertiesPanel.h"
#include "ui/panels/TimelinePanel.h"
#include "ui/panels/ToolsPanel.h"
#include "ui/panels/ViewportPanel.h"

#include <AutoHideDockContainer.h>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

#include <cfloat>
#include <fstream>
#include <sstream>

namespace bemotion {

using ui::Action;

namespace {

std::filesystem::path layoutFile() { return platform::appDataDir() / "layout.ads"; }

// True while the user is in the middle of an edit: mouse held, or typing in a field.
bool interacting() {
    if (QApplication::mouseButtons() != Qt::NoButton) return true;
    QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QPlainTextEdit*>(f));
}

bool typingInField() {
    QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QPlainTextEdit*>(f));
}

} // namespace

EditorWindow::EditorWindow(Project project, std::filesystem::path path, RecentProjects& recents, QWidget* parent)
    : QMainWindow(parent), recents_(recents) {
    setWindowFlags(Qt::Widget); // lives inside the application window
    ctx_.open(std::move(project), std::move(path));

    buildDocks();
    buildMenus();
    applyShortcuts();
    connect(&ui::AppSignals::get(), &ui::AppSignals::shortcutsChanged, this, &EditorWindow::applyShortcuts);

    qApp->installEventFilter(this); // Space: tap to play, hold to pan
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &EditorWindow::tick);
    timer_->start(16);
    clock_.start();
}

EditorWindow::~EditorWindow() {
    qApp->removeEventFilter(this);
    // The layout is remembered for the next project.
    const QByteArray state = docks_->saveState();
    std::ofstream out(layoutFile(), std::ios::binary | std::ios::trunc);
    out.write(state.constData(), state.size());
    media::AudioEngine::get().update(Composition{}, 0.0, false);
}

QString EditorWindow::title() const {
    return QString::fromStdString(ctx_.project.name) + (ctx_.isDirty() ? " •" : "") + QString("  —  bemotion");
}

// --- docks -----------------------------------------------------------------------------

void EditorWindow::buildDocks() {
    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig | ads::CDockManager::FocusHighlighting |
                                      ads::CDockManager::DockAreaHasCloseButton | ads::CDockManager::AllTabsHaveCloseButton |
                                      ads::CDockManager::DoubleClickUndocksWidget);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DoubleClickUndocksWidget, false); // double-click maximizes instead
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);
    docks_ = new ads::CDockManager(this);
    // The docking library's own style sheet beats the application's: our colors go on top of it.
    const QString adsStyle = docks_->styleSheet();
    docks_->setStyleSheet(adsStyle + theme::dockStyleSheet());
    connect(&ui::AppSignals::get(), &ui::AppSignals::appearanceChanged, this,
            [this, adsStyle] { docks_->setStyleSheet(adsStyle + theme::dockStyleSheet()); });

    objects_ = new ObjectsPanel(ctx_);
    media_ = new MediaPanel(ctx_);
    tools_ = new ToolsPanel(ctx_);
    viewport_ = new ViewportPanel(ctx_);
    properties_ = new PropertiesPanel(ctx_);
    effects_ = new EffectsPanel(ctx_);
    timeline_ = new TimelinePanel(ctx_);
    connect(timeline_, &TimelinePanel::toolsPanelRequested, this, [this] {
        if (auto* d = dockWidgets_["Tools"]) {
            d->toggleView(true);
            d->setAsCurrentTab();
            d->raise();
        }
    });

    auto dock = [&](const char* name, QWidget* w) {
        auto* d = new ads::CDockWidget(docks_, name);
        d->setWidget(w, ads::CDockWidget::ForceNoScrollArea);
        d->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
        dockWidgets_[name] = d;
        d->tabWidget()->installEventFilter(this); // double-click a tab: maximize / restore
        return d;
    };
    auto* vp = dock("Viewport", viewport_);
    vp->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    auto* center = docks_->setCentralWidget(vp);
    auto* left = docks_->addDockWidget(ads::LeftDockWidgetArea, dock("Objects", objects_));
    docks_->addDockWidgetTabToArea(dock("Media", media_), left);
    docks_->addDockWidgetTabToArea(dock("Tools", tools_), left);
    left->setCurrentIndex(0);
    auto* right = docks_->addDockWidget(ads::RightDockWidgetArea, dock("Properties", properties_));
    docks_->addDockWidgetTabToArea(dock("Effects", effects_), right);
    right->setCurrentIndex(0);
    auto* bottom = docks_->addDockWidget(ads::BottomDockWidgetArea, dock("Timeline", timeline_));
    (void)center;
    QTimer::singleShot(0, this, [this, left, right, bottom] {
        docks_->setSplitterSizes(left, {width() * 15 / 100, width() * 59 / 100, width() * 26 / 100});
        docks_->setSplitterSizes(bottom, {height() * 64 / 100, height() * 36 / 100});
        (void)right;
        defaultLayout_ = docks_->saveState();
        // The last layout, when there is one.
        std::ifstream in(layoutFile(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string saved = ss.str();
        if (!saved.empty()) docks_->restoreState(QByteArray(saved.data(), static_cast<qsizetype>(saved.size())));
    });
}

void EditorWindow::resetLayout() {
    if (!defaultLayout_.isEmpty()) docks_->restoreState(defaultLayout_);
    beforeMaximize_.clear();
    for (auto& [name, d] : dockWidgets_) d->toggleView(true);
}

void EditorWindow::maximizeDock(ads::CDockWidget* dock) {
    // Like After Effects' "~": the panel fills the window; the same gesture brings the layout back.
    if (!beforeMaximize_.isEmpty()) {
        docks_->restoreState(beforeMaximize_);
        beforeMaximize_.clear();
        return;
    }
    beforeMaximize_ = docks_->saveState();
    for (auto& [name, d] : dockWidgets_)
        if (d != dock && name != "Viewport" && !d->isAutoHide() && !d->isClosed()) d->setAutoHide(true);
    if (dock->objectName() != "Viewport" && dock->isAutoHide()) dock->setAutoHide(false);
}

// --- menus and shortcuts ------------------------------------------------------------------

QAction* EditorWindow::action(Action a, const QString& text, std::function<void()> fn) {
    auto* act = new QAction(text, this);
    act->setShortcutContext(Qt::WindowShortcut);
    connect(act, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
    addAction(act);
    actions_[a] = act;
    return act;
}

void EditorWindow::applyShortcuts() {
    for (auto& [a, act] : actions_) {
        QKeySequence seq = ui::sequenceOf(a);
        // Space is handled by the event filter (tap: play, hold: pan the viewport).
        if (a == Action::PlayPause && ui::chordOf(a) == Qt::Key_Space) seq = QKeySequence();
        if (a == Action::Delete) act->setShortcuts({seq, QKeySequence(Qt::Key_Backspace)});
        else act->setShortcut(seq);
    }
}

void EditorWindow::buildMenus() {
    QMenuBar* bar = menuBar();

    // Brand mark: back to the projects.
    auto* home = new ui::IconButton(ui::Icon::Logo, "Back to projects", 28);
    connect(home, &QAbstractButton::clicked, this, [this] {
        if (confirmClose()) emit goHome();
    });
    bar->setCornerWidget(home, Qt::TopLeftCorner);

    QMenu* file = bar->addMenu("File");
    file->addAction(action(Action::Save, "Save", [this] { save(); }));
    file->addAction(action(Action::SaveAs, "Save As...", [this] { saveAs(); }));
    file->addSeparator();
    file->addAction(action(Action::ImportMedia, "Import Media...", [this] { media_->openImportDialog(); }));
    file->addAction("Show in Folder", this, [this] {
        if (!ctx_.path.empty()) platform::revealInFileManager(ctx_.path);
    });
    file->addSeparator();
    file->addAction(action(Action::Export, "Export Video...", [this] { ui::showExportDialog(ctx_, this); }));
    file->addSeparator();
    file->addAction(action(Action::CloseProject, "Close Project", [this] {
        if (confirmClose()) emit goHome();
    }));
    file->addAction(action(Action::Quit, "Quit", [this] { emit quitRequested(); }));

    QMenu* edit = bar->addMenu("Edit");
    edit->addAction(action(Action::Undo, "Undo", [this] { ctx_.undo(); }));
    edit->addAction(action(Action::Redo, "Redo", [this] { ctx_.redo(); }));
    edit->addSeparator();
    edit->addAction(action(Action::Duplicate, "Duplicate", [this] { ctx_.duplicateSelected(); }));
    edit->addAction(action(Action::Delete, "Delete", [this] {
        if (!ctx_.selectedKeys.empty()) ctx_.deleteSelectedKeys();
        else ctx_.deleteSelected();
    }));
    edit->addSeparator();
    edit->addAction(action(Action::SelectAll, "Select All", [this] { ctx_.selectAll(); }));
    edit->addAction(action(Action::Deselect, "Deselect", [this] {
        if (ctx_.tool != Tool::Select) ctx_.tool = Tool::Select;
        else if (!ctx_.selection.empty()) ctx_.select(0);
        else ctx_.exitScope(); // nothing selected: leave the open compound
    }));
    edit->addSeparator();
    edit->addAction(action(Action::Settings, "Settings...", [] { ui::openSettings(); }));
    edit->addAction("Plugins...", this, [] { ui::openSettings(ui::SettingsTab::Plugins); });

    QMenu* layer = bar->addMenu("Layer");
    layer->addAction(action(Action::Split, "Split at Playhead", [this] { ctx_.splitSelectedAtPlayhead(); }));
    layer->addSeparator();
    layer->addAction(action(Action::CreateCompound, "Create Compound", [this] { ctx_.createCompound(); }));
    layer->addAction(action(Action::ReleaseCompound, "Release Compound", [this] { ctx_.releaseSelectedCompounds(); }));
    QMenu* fx = layer->addMenu("Effect");
    connect(fx, &QMenu::aboutToShow, this, [this, fx] {
        fx->clear();
        fillEffectMenu(fx, ctx_);
    });
    layer->addAction(action(Action::CutTool, "Cut Tool", [this] { ctx_.tool = Tool::Razor; }));
    layer->addAction(action(Action::SelectTool, "Selection Tool", [this] { ctx_.tool = Tool::Select; }));
    layer->addSeparator();
    layer->addAction(action(Action::BringToFront, "Bring to Front", [this] { ctx_.shiftSelected(-1000); }));
    layer->addAction(action(Action::BringForward, "Bring Forward", [this] { ctx_.shiftSelected(-1); }));
    layer->addAction(action(Action::SendBackward, "Send Backward", [this] { ctx_.shiftSelected(1); }));
    layer->addAction(action(Action::SendToBack, "Send to Back", [this] { ctx_.shiftSelected(1000); }));
    connect(layer, &QMenu::aboutToShow, this, [this] {
        const bool any = !ctx_.selection.empty();
        for (Action a : {Action::CreateCompound, Action::BringToFront, Action::BringForward, Action::SendBackward, Action::SendToBack})
            actions_[a]->setEnabled(any);
        actions_[Action::ReleaseCompound]->setEnabled(ctx_.selectionHasCompound());
        actions_[Action::CutTool]->setCheckable(true);
        actions_[Action::CutTool]->setChecked(ctx_.tool == Tool::Razor);
        actions_[Action::SelectTool]->setCheckable(true);
        actions_[Action::SelectTool]->setChecked(ctx_.tool == Tool::Select);
    });
    connect(layer, &QMenu::aboutToHide, this, [this] {
        for (auto& [a, act] : actions_) act->setEnabled(true); // shortcuts stay usable (they check themselves)
    });

    QMenu* add = bar->addMenu("Add");
    add->addAction(action(Action::AddText, "Text", [this] { ctx_.addText(); }));
    add->addAction("Camera", this, [this] { ctx_.addCamera(); });
    add->addSeparator();
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i)
        add->addAction(shapeKindLabel(static_cast<ShapeKind>(i)), this, [this, i] { ctx_.addShape(static_cast<ShapeKind>(i)); });
    add->addSeparator();
    QMenu* mediaMenu = add->addMenu("Media");
    connect(mediaMenu, &QMenu::aboutToShow, this, [this, mediaMenu] {
        mediaMenu->clear();
        QAction* imp = mediaMenu->addAction("Import Media...", this, [this] { media_->openImportDialog(); });
        imp->setShortcut(ui::sequenceOf(Action::ImportMedia));
        if (!ctx_.comp().media.empty()) mediaMenu->addSeparator();
        for (const MediaItem& item : ctx_.comp().media) {
            const uint64_t id = item.id;
            mediaMenu->addAction(QString::fromStdString(item.name + "  (" + mediaKindLabel(item.kind) + ")"), this,
                                 [this, id] { ctx_.addMedia(id); });
        }
        if (ctx_.comp().media.empty()) mediaMenu->addAction("Nothing imported yet")->setEnabled(false);
    });

    QMenu* view = bar->addMenu("View");
    view->addAction(action(Action::FitView, "Fit Composition", [this] { viewport_->requestFit(); }));
    view->addAction(action(Action::ActualSize, "Actual Size (100%)", [this] { viewport_->setZoom(1.0f); }));
    view->addAction(action(Action::ZoomIn, "Zoom In", [this] { viewport_->zoomBy(1.25f); }));
    view->addAction(action(Action::ZoomOut, "Zoom Out", [this] { viewport_->zoomBy(1.0f / 1.25f); }));
    view->addSeparator();
    QAction* snap = action(Action::ToggleSnapping, "Snapping", [this] { ctx_.snapping = !ctx_.snapping; });
    snap->setCheckable(true);
    view->addAction(snap);
    QAction* rulers = view->addAction("Rulers", this, [this] { ctx_.showRulers = !ctx_.showRulers; });
    rulers->setCheckable(true);
    QMenu* guides = view->addMenu("Guides");
    QAction* showGuides = guides->addAction("Show Guides", this, [this] { ctx_.showGuides = !ctx_.showGuides; });
    showGuides->setCheckable(true);
    QAction* lockGuides = guides->addAction("Lock Guides", this, [this] { ctx_.lockGuides = !ctx_.lockGuides; });
    lockGuides->setCheckable(true);
    guides->addAction("Add Center Guides", this, [this] {
        Composition& c = ctx_.comp();
        c.guides.push_back({true, c.width * 0.5f});
        c.guides.push_back({false, c.height * 0.5f});
        ctx_.touch();
    });
    QAction* clearGuides = guides->addAction("Clear Guides", this, [this] {
        ctx_.comp().guides.clear();
        ctx_.touch();
    });
    connect(view, &QMenu::aboutToShow, this, [=, this] {
        snap->setChecked(ctx_.snapping);
        rulers->setChecked(ctx_.showRulers);
        showGuides->setChecked(ctx_.showGuides);
        lockGuides->setChecked(ctx_.lockGuides);
        clearGuides->setEnabled(!ctx_.comp().guides.empty());
    });
    view->addSeparator();
    QMenu* panels = view->addMenu("Panels");
    for (const char* name : {"Objects", "Media", "Tools", "Properties", "Effects", "Timeline"})
        panels->addAction(dockWidgets_[name]->toggleViewAction());
    view->addAction("Reset Layout", this, [this] { resetLayout(); });

    QMenu* help = bar->addMenu("Help");
    help->addAction(action(Action::Help, "Keyboard Shortcuts", [] { ui::openHelp(ui::HelpTab::Shortcuts); }));
    help->addSeparator();
    help->addAction("About bemotion", this, [] { ui::openHelp(ui::HelpTab::About); });
    help->addAction("Credits", this, [] { ui::openHelp(ui::HelpTab::Credits); });
    help->addAction("License", this, [] { ui::openHelp(ui::HelpTab::License); });

    // Shortcuts without a menu entry.
    action(Action::PlayPause, "Play / Pause", [this] { ctx_.playing = !ctx_.playing; });
    action(Action::PrevFrame, "Step Back", [this] { ctx_.stepFrames(-settings().arrowFrames); });
    action(Action::NextFrame, "Step Forward", [this] { ctx_.stepFrames(settings().arrowFrames); });
    action(Action::PrevFrames, "Big Step Back", [this] { ctx_.stepFrames(-settings().bigArrowFrames); });
    action(Action::NextFrames, "Big Step Forward", [this] { ctx_.stepFrames(settings().bigArrowFrames); });
    action(Action::GoToStart, "Go to Start", [this] { ctx_.setTime(0.0); });
    action(Action::GoToEnd, "Go to End", [this] { ctx_.setTime(ctx_.comp().duration); });
    action(Action::PrevKeyframe, "Previous Keyframe", [this] { jumpToKeyframe(-1); });
    action(Action::NextKeyframe, "Next Keyframe", [this] { jumpToKeyframe(+1); });
    action(Action::EasyEase, "Easy Ease", [this] { TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EasyEase); });
    action(Action::EaseIn, "Ease In", [this] { TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EaseIn); });
    action(Action::EaseOut, "Ease Out", [this] { TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EaseOut); });

    // Right side of the menu bar: errors, settings, Export.
    auto* corner = new QWidget;
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 2, 8, 2);
    row->setSpacing(8);
    status_ = new QLabel;
    status_->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kDanger).name()));
    row->addWidget(status_);
    auto* gear = new ui::IconButton(ui::Icon::Gear, "Settings", 28);
    connect(gear, &QAbstractButton::clicked, this, [] { ui::openSettings(); });
    row->addWidget(gear);
    auto* exportButton = ui::accentButton("Export");
    exportButton->setIcon(ui::makeIcon(ui::Icon::Export, theme::kOnAccent, 18));
    exportButton->setToolTip("Export video");
    connect(exportButton, &QPushButton::clicked, this, [this] { ui::showExportDialog(ctx_, this); });
    row->addWidget(exportButton);
    bar->setCornerWidget(corner, Qt::TopRightCorner);
}

bool EditorWindow::eventFilter(QObject* watched, QEvent* event) {
    // Double-click a panel tab: maximize it (or bring the layout back).
    if (event->type() == QEvent::MouseButtonDblClick)
        if (auto* tab = qobject_cast<ads::CDockWidgetTab*>(watched)) {
            maximizeDock(tab->dockWidget());
            return true;
        }
    // Space: a tap toggles playback, holding it lets the viewport pan.
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && isVisible() &&
        ui::chordOf(Action::PlayPause) == Qt::Key_Space) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Space && !key->isAutoRepeat() && !typingInField() && QApplication::activeModalWidget() == nullptr &&
            QApplication::activeWindow() == window()) {
            if (event->type() == QEvent::KeyPress && !spaceDown_) {
                spaceDown_ = true;
                viewport_->setSpaceHeld(true);
            } else if (event->type() == QEvent::KeyRelease && spaceDown_) {
                spaceDown_ = false;
                if (!viewport_->setSpaceHeld(false)) ctx_.playing = !ctx_.playing; // not used to pan: play / pause
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// --- update loop ------------------------------------------------------------------------------

void EditorWindow::tick() {
    const double dt = std::min(0.25, clock_.restart() / 1000.0);
    ctx_.tick(dt);
    PluginHost::get().processPendingReload();
    media::MediaManager::get().update(ctx_.comp());
    media::AudioEngine::get().update(ctx_.comp(), ctx_.time, ctx_.playing);

    objects_->sync();
    media_->sync();
    tools_->sync();
    viewport_->sync();
    properties_->sync();
    effects_->sync();
    timeline_->sync();

    ctx_.commitIfIdle(interacting());
    const QString t = title();
    if (t != lastTitle_) {
        lastTitle_ = t;
        emit titleChanged();
    }
}

void EditorWindow::jumpToKeyframe(int direction) {
    double best = direction > 0 ? DBL_MAX : -DBL_MAX;
    auto consider = [&](Layer& layer) {
        for (const PropertyRef& p : layer.properties())
            for (int i = 0; i < p.anim->keyCount(); ++i) {
                const double t = p.anim->keyTime(i);
                if (direction > 0 && t > ctx_.time + kTimeEpsilon) best = std::min(best, t);
                if (direction < 0 && t < ctx_.time - kTimeEpsilon) best = std::max(best, t);
            }
    };
    if (Layer* sel = ctx_.selected()) consider(*sel);
    else
        for (Layer& l : ctx_.comp().layers) consider(l);
    if (best != DBL_MAX && best != -DBL_MAX) {
        ctx_.playing = false;
        ctx_.setTime(best);
    }
}

// --- files ------------------------------------------------------------------------------------

void EditorWindow::save() {
    if (ctx_.path.empty()) return saveAs();
    std::string err;
    if (!ctx_.save(&err)) {
        status_->setText(QString::fromStdString("Save failed: " + err));
        return;
    }
    status_->clear();
    recents_.touch(ctx_.path, ctx_.project.name);
}

void EditorWindow::saveAs() {
    const auto suggested = ctx_.path.empty()
                               ? platform::defaultProjectsDir() / platform::fromUtf8(ctx_.project.name + kProjectExtension)
                               : ctx_.path;
    auto file = platform::saveFileDialog("Save Project As", suggested, "bemotion Project", kProjectExtension);
    if (!file) return;
    std::string err;
    if (!ctx_.saveTo(*file, &err)) {
        status_->setText(QString::fromStdString("Save failed: " + err));
        return;
    }
    status_->clear();
    recents_.touch(ctx_.path, ctx_.project.name);
}

bool EditorWindow::confirmClose() {
    ctx_.commitIfIdle(false);
    if (!ctx_.isDirty()) return true;
    QMessageBox box(this);
    box.setWindowTitle("Unsaved Changes");
    box.setText(QString("Save changes to \"%1\" before closing?").arg(QString::fromStdString(ctx_.project.name)));
    box.setInformativeText("Your changes will be lost if you don't save them.");
    QPushButton* saveButton = box.addButton("Save", QMessageBox::AcceptRole);
    saveButton->setProperty("accent", true);
    box.addButton("Don't Save", QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();
    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == saveButton) {
        save();
        return !ctx_.isDirty();
    }
    return true;
}

void EditorWindow::dropFiles(const std::vector<std::filesystem::path>& files, bool onMediaPanel) {
    // Like After Effects: dropped on the Media panel they are only imported; anywhere else they
    // also become layers at the playhead.
    media_->importFiles(files, !onMediaPanel);
}

} // namespace bemotion
