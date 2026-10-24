#include "ui/EditorWindow.h"

#include "app/Automation.h"
#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Recovery.h"
#include "app/RecentProjects.h"
#include "app/Settings.h"
#include "core/Arrange.h"
#include "core/Clipboard.h"
#include "core/Serialization.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "media/Recorder.h"
#include "ui/AppSignals.h"
#include "ui/Dialogs.h"
#include "ui/Keymap.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ObjectsPanel.h"
#include "ui/panels/PropertiesPanel.h"
#include "ui/panels/TimelinePanel.h"
#include "ui/panels/ToolsPanel.h"
#include "ui/panels/TransitionsPanel.h"
#include "ui/panels/ViewportPanel.h"

#include <AutoHideDockContainer.h>
#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSplitter>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

#include <cfloat>
#include <fstream>
#include <sstream>

namespace bemotion {

using ui::Action;

namespace {

std::filesystem::path layoutFile() { return platform::appDataDir() / "layout.ads"; }

// Real drop targets stay above the floating toolbar while its grip is dragged.
class ToolbarTarget : public QPushButton {
public:
    ToolbarTarget(const char* label, QWidget* parent) : QPushButton(label, parent) { setAcceptDrops(true); }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (!e->mimeData()->hasFormat(ui::kToolbarMime)) return e->ignore();
        setDown(true);
        e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->mimeData()->hasFormat(ui::kToolbarMime)) e->acceptProposedAction();
        else e->ignore();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override { setDown(false); }
    void dropEvent(QDropEvent* e) override {
        if (!e->mimeData()->hasFormat(ui::kToolbarMime)) return e->ignore();
        setDown(false);
        e->acceptProposedAction();
        click();
    }
};

// Layers on the system clipboard: our own type, plus the same text for other apps.
constexpr const char* kLayersMime = "application/x-bemotion-layers";

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

// The tab a minimized panel leaves on the window edge; the chevron points where it comes back from.
// The same handle, pointing the other way, minimizes a panel: it shows where the restore handle
// will be, when the mouse comes near.
class RestoreHandle : public QAbstractButton {
public:
    RestoreHandle(int edge, QWidget* parent, bool minimizes = false) : QAbstractButton(parent), edge_(edge), minimizes_(minimizes) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        const bool upright = edge_ < 2;
        setFixedSize(upright ? QSize(18, 56) : QSize(56, 18));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        const bool hot = underMouse();
        const ui::Pt a(0.5f, 0.5f), b(width() - 0.5f, height() - 0.5f);
        d.rectFilled(a, b, hot ? theme::kAccent : theme::withAlpha(theme::kBgHover, 240), 5.0f);
        d.rect(a, b, hot ? theme::kAccent : theme::kBorder, 5.0f);
        const int to = minimizes_ ? edge_ : edge_ ^ 1; // where the chevron points: away from or toward the edge
        const ui::Icon icon = to == 1 ? ui::Icon::ChevronRight : to == 0 ? ui::Icon::ChevronLeft
                              : to == 3 ? ui::Icon::ChevronDown : ui::Icon::ChevronUp;
        ui::drawIcon(d, icon, ui::Pt(width() * 0.5f, height() * 0.5f), 13.0f, hot ? theme::kOnAccent : theme::kText);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    int edge_;
    bool minimizes_;
};

} // namespace

EditorWindow::EditorWindow(Project project, std::filesystem::path path, RecentProjects& recents, QWidget* parent)
    : QMainWindow(parent), recents_(recents) {
    setWindowFlags(Qt::Widget); // lives inside the application window
    ctx_.open(std::move(project), std::move(path));
    ctx_.showRulers = settings().showRulers;

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
    sinceSave_.start();
    // Unsaved changes are kept aside (crash recovery) and, if the user wants, saved regularly.
    auto* keeper = new QTimer(this);
    connect(keeper, &QTimer::timeout, this, [this] {
        keepRecoveryCopy();
        autosave();
    });
    keeper->start(20000);
}

EditorWindow::~EditorWindow() {
    qApp->removeEventFilter(this);
    while (!minimized_.empty()) restoreMinimized(0); // minimized panels come back with the next project
    recovery::discard(ctx_.path, ctx_.project.name); // closed normally: nothing to recover
    if (media::Recorder::get().recording()) media::Recorder::get().cancel(); // a recording left running ends with the project
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
    // No close, undock, tabs-menu or pin buttons: a panel header has its tabs and a minimize button.
    // Panels still move by dragging their tab; View > Panels shows or hides them.
    ads::CDockManager::setConfigFlags(ads::CDockManager::OpaqueSplitterResize | ads::CDockManager::DockAreaHideDisabledButtons);
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::AutoHideFeatureEnabled); // used by maximize
    docks_ = new ads::CDockManager(this);
    docks_->installEventFilter(this); // restore handles follow its size
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
    transitions_ = new TransitionsPanel(ctx_);
    timeline_ = new TimelinePanel(ctx_);
    toolsPopover_ = new ToolsPopover(tools_, this);
    connect(timeline_, &TimelinePanel::pluginButtonClicked, this, [this](const std::string& id, bool pressed) { runPluginCommand(id, pressed); });
    // The grip can drop on a window edge, freely over the viewport, or back in the timeline.
    viewport_->installEventFilter(this);
    auto moveToolbar = [this](bool toViewport, QPoint at) {
        Settings& s = settings();
        s.toolbarInViewport = toViewport;
        s.toolbarInWindow = false;
        if (toViewport) {
            QWidget* pill = timeline_->toolPill();
            const QSize free = viewport_->size() - pill->sizeHint();
            if (at.x() < 0) at = QPoint(free.width() / 2, 10); // its default place: top center
            s.toolbarX = std::clamp(static_cast<float>(at.x()) / std::max(1, free.width()), 0.0f, 1.0f);
            s.toolbarY = std::clamp(static_cast<float>(at.y()) / std::max(1, free.height()), 0.0f, 1.0f);
        }
        saveSettings();
        placeToolbar();
    };
    connect(timeline_, &TimelinePanel::toolbarDragActive, this, &EditorWindow::showToolbarTargets);
    connect(timeline_, &TimelinePanel::toolbarDockRequested, this, &EditorWindow::dockToolbar);
    connect(viewport_, &ViewportPanel::toolbarDropped, this, [moveToolbar](QPoint at) { moveToolbar(true, at); });
    connect(timeline_, &TimelinePanel::toolbarMoveRequested, this, moveToolbar);
    connect(&ui::AppSignals::get(), &ui::AppSignals::toolbarChanged, this, [this] { QTimer::singleShot(0, this, [this] { placeToolbar(); }); });
    QTimer::singleShot(0, this, [this] { placeToolbar(); });
    connect(timeline_, &TimelinePanel::toolsPanelRequested, this, &EditorWindow::showTools);

    auto dock = [&](const char* name, QWidget* w) {
        auto* d = new ads::CDockWidget(docks_, name);
        d->setWidget(w, ads::CDockWidget::ForceNoScrollArea);
        d->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
        dockWidgets_[name] = d;
        d->tabWidget()->installEventFilter(this); // double-click a tab: maximize / restore
        // Shown again some other way (View > Panels, Reset Layout): its restore handle goes.
        connect(d, &ads::CDockWidget::viewToggled, this, [this, d](bool open) {
            if (!open) return;
            for (size_t i = 0; i < minimized_.size(); ++i)
                if (std::find(minimized_[i].docks.begin(), minimized_[i].docks.end(), d) != minimized_[i].docks.end()) {
                    restoreMinimized(i);
                    return;
                }
        });
        return d;
    };
    auto* vp = dock("Viewport", viewport_);
    vp->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    auto* center = docks_->setCentralWidget(vp);
    auto* left = docks_->addDockWidget(ads::LeftDockWidgetArea, dock("Objects", objects_));
    docks_->addDockWidgetTabToArea(dock("Media", media_), left);
    left->setCurrentIndex(0);
    auto* right = docks_->addDockWidget(ads::RightDockWidgetArea, dock("Properties", properties_));
    docks_->addDockWidgetTabToArea(dock("Effects", effects_), right);
    docks_->addDockWidgetTabToArea(dock("Transitions", transitions_), right);
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
        // A layout saved before a panel existed would leave that panel out: then the default one is used.
        bool complete = !saved.empty();
        for (const auto& [name, d] : dockWidgets_) complete = complete && saved.find("\"" + name + "\"") != std::string::npos;
        if (complete) docks_->restoreState(QByteArray(saved.data(), static_cast<qsizetype>(saved.size())));
    });
}

void EditorWindow::dockToolbar(QPointF position) {
    settings().toolbarInViewport = true;
    settings().toolbarInWindow = true;
    settings().toolbarX = static_cast<float>(position.x());
    settings().toolbarY = static_cast<float>(position.y());
    saveSettings();
    placeToolbar();
}

QRect EditorWindow::toolbarBounds() const {
    // The dock workspace spans the window below its main header.
    return QRect(docks_->mapTo(this, QPoint()), docks_->size()).adjusted(8, 8, -8, -8);
}

void EditorWindow::showToolbarTargets(bool visible) {
    const char* labels[] = {"Top", "Bottom", "Left", "Right"};
    const QPointF positions[] = {{0.5, 0}, {0.5, 1}, {0, 0.5}, {1, 0.5}};
    for (int i = 0; i < 4; ++i) {
        if (!toolbarTargets_[i]) {
            auto* target = new ToolbarTarget(labels[i], this);
            target->setToolTip("Drop the toolbar here");
            connect(target, &QPushButton::clicked, this, [this, position = positions[i]] {
                dockToolbar(position);
                showToolbarTargets(false);
            });
            toolbarTargets_[i] = target;
        }
    }
    positionToolbarTargets();
    for (auto* target : toolbarTargets_) {
        target->setVisible(visible);
        if (visible) target->raise();
    }
}

void EditorWindow::positionToolbarTargets() {
    const QRect bounds = toolbarBounds();
    const QPointF positions[] = {{0.5, 0}, {0.5, 1}, {0, 0.5}, {1, 0.5}};
    for (int i = 0; i < 4; ++i) {
        auto* target = toolbarTargets_[i];
        if (!target) continue;
        target->resize(target->sizeHint().expandedTo(QSize(76, 32)));
        target->move(bounds.topLeft() + QPoint(
            static_cast<int>(positions[i].x() * std::max(0, bounds.width() - target->width())),
            static_cast<int>(positions[i].y() * std::max(0, bounds.height() - target->height()))));
    }
}

void EditorWindow::placeToolbar() {
    QWidget* pill = timeline_->toolPill();
    if (!settings().toolbarInViewport) {
        timeline_->dockToolPill();
        return;
    }
    QWidget* host = settings().toolbarInWindow ? static_cast<QWidget*>(this) : viewport_;
    if (pill->parentWidget() != host) {
        timeline_->releaseToolPill();
        pill->setParent(host);
    }
    pill->resize(pill->sizeHint());
    const QRect bounds = settings().toolbarInWindow ? toolbarBounds() : viewport_->rect().adjusted(6, 6, -6, -6);
    pill->move(bounds.topLeft() + QPoint(
        static_cast<int>(settings().toolbarX * std::max(0, bounds.width() - pill->width())),
        static_cast<int>(settings().toolbarY * std::max(0, bounds.height() - pill->height()))));
    pill->show();
    pill->raise();
}

void EditorWindow::showTools() {
    // Follow the toolbar, including when it floats at a window edge.
    QWidget* pill = timeline_->toolPill();
    const QPoint anchor = pill->isVisible() ? pill->mapTo(this, QPoint()) : QPoint(width() / 2 - 210, height() / 2 + 200);
    toolsPopover_->popup(anchor);
}

int EditorWindow::edgeOf(ads::CDockAreaWidget* area, float* along) const {
    // The window edge a panel minimizes to: the side of the viewport it sits on.
    auto rectIn = [this](QWidget* w) { return QRect(w->mapTo(docks_, QPoint(0, 0)), w->size()); };
    const QRect r = rectIn(area);
    QRect center = docks_->rect();
    const auto vp = dockWidgets_.find("Viewport");
    if (vp != dockWidgets_.end() && vp->second->dockAreaWidget()) center = rectIn(vp->second->dockAreaWidget());
    int edge = 2;
    if (r.right() <= center.left() + 4) edge = 0;
    else if (r.left() >= center.right() - 4) edge = 1;
    else if (r.top() >= center.bottom() - 4) edge = 3;
    if (along)
        *along = edge < 2 ? static_cast<float>(r.center().y()) / std::max(1, docks_->height())
                          : static_cast<float>(r.center().x()) / std::max(1, docks_->width());
    return edge;
}

QPoint EditorWindow::handlePos(int edge, float along, QSize size) const {
    const QRect area = docks_->rect();
    if (edge < 2)
        return {edge == 0 ? area.left() + 2 : area.right() - size.width() - 1,
                std::clamp(static_cast<int>(area.height() * along) - size.height() / 2, 4, std::max(4, area.height() - size.height() - 4))};
    return {std::clamp(static_cast<int>(area.width() * along) - size.width() / 2, 4, std::max(4, area.width() - size.width() - 4)),
            edge == 2 ? area.top() + 2 : area.bottom() - size.height() - 1};
}

void EditorWindow::updateMinimizeHandles() {
    // Each panel's minimize handle sits where its restore handle will be, shown when the mouse is near.
    // The pointer: the cursor when it moved, else the last mouse event (scripted input moves no cursor).
    if (const QPoint cursor = QCursor::pos(); cursor != lastCursor_) pointer_ = lastCursor_ = cursor;
    const QPoint mouse = docks_->mapFromGlobal(pointer_);
    const bool idle = QApplication::mouseButtons() == Qt::NoButton && !QApplication::activePopupWidget() &&
                      QApplication::activeModalWidget() == nullptr && isVisible();
    std::vector<ads::CDockAreaWidget*> areas;
    for (ads::CDockAreaWidget* a : docks_->openedDockAreas())
        if (!a->isCentralWidgetArea() && a->dockContainer() == docks_ && a->isVisible()) areas.push_back(a);
    std::erase_if(minimizeHandles_, [&](const MinimizeHandle& h) {
        if (h.area && std::find(areas.begin(), areas.end(), h.area.data()) != areas.end()) return false;
        h.handle->deleteLater();
        return true;
    });
    for (ads::CDockAreaWidget* a : areas) {
        auto it = std::find_if(minimizeHandles_.begin(), minimizeHandles_.end(), [&](const MinimizeHandle& h) { return h.area == a; });
        float along = 0.5f;
        const int edge = edgeOf(a, &along);
        if (it == minimizeHandles_.end() || it->edge != edge) {
            if (it != minimizeHandles_.end()) {
                it->handle->deleteLater();
                minimizeHandles_.erase(it);
            }
            auto* h = new RestoreHandle(edge, docks_, true);
            h->setToolTip("Minimize panel  (double-click a tab to maximize)");
            QPointer<ads::CDockAreaWidget> area(a);
            connect(h, &QAbstractButton::clicked, this, [this, area, h] {
                h->hide();
                if (area) minimizeArea(area);
            });
            minimizeHandles_.push_back({area, h, edge});
            it = std::prev(minimizeHandles_.end());
        }
        QWidget* h = it->handle;
        const QPoint pos = handlePos(edge, along, h->size());
        if (h->pos() != pos) h->move(pos);
        const bool near = QRect(pos, h->size()).adjusted(-48, -48, 48, 48).contains(mouse);
        const bool show = (idle && near) || (h->isVisible() && h->underMouse());
        if (show != h->isVisible()) {
            h->setVisible(show);
            if (show) h->raise();
        }
    }
}

void EditorWindow::minimizeArea(ads::CDockAreaWidget* area) {
    if (!area || area->isCentralWidgetArea()) return;
    Minimized m;
    m.edge = edgeOf(area, &m.along);
    m.current = area->currentDockWidget();
    m.size = m.edge < 2 ? area->width() : area->height();
    const QList<ads::CDockWidget*> open = area->openedDockWidgets();
    m.docks.assign(open.begin(), open.end());
    QStringList names;
    for (ads::CDockWidget* d : m.docks) names << d->windowTitle();
    auto* handle = new RestoreHandle(m.edge, docks_);
    handle->setToolTip("Restore " + names.join(", "));
    m.handle = handle;
    connect(handle, &QAbstractButton::clicked, this, [this, handle] {
        for (size_t i = 0; i < minimized_.size(); ++i)
            if (minimized_[i].handle == handle) return restoreMinimized(i);
    });
    minimized_.push_back(m);
    for (ads::CDockWidget* d : m.docks) d->toggleView(false);
    placeRestoreHandles();
    handle->show();
}

void EditorWindow::restoreMinimized(size_t index) {
    if (index >= minimized_.size()) return;
    const Minimized m = minimized_[index];
    minimized_.erase(minimized_.begin() + static_cast<std::ptrdiff_t>(index)); // first: toggleView reports back here
    m.handle->deleteLater();
    for (ads::CDockWidget* d : m.docks) d->toggleView(true);
    if (m.current) {
        m.current->setAsCurrentTab();
        // Back at the size it had (hidden, its place shrank to nothing).
        auto resize = [this](ads::CDockAreaWidget* area, int size) {
            auto* splitter = qobject_cast<QSplitter*>(area->parentWidget());
            if (!splitter || size <= 0) return;
            QList<int> sizes = splitter->sizes();
            const int i = splitter->indexOf(area);
            if (i < 0 || i >= sizes.size()) return;
            // The space comes from the largest neighbor (usually the viewport).
            int from = -1;
            for (int k = 0; k < sizes.size(); ++k)
                if (k != i && (from < 0 || sizes[k] > sizes[from])) from = k;
            if (from < 0) return;
            const int give = std::clamp(size - sizes[i], -sizes[i], std::max(0, sizes[from] - 80));
            sizes[i] += give;
            sizes[from] -= give;
            splitter->setSizes(sizes);
        };
        if (ads::CDockAreaWidget* area = m.current->dockAreaWidget()) {
            resize(area, m.size);
            QPointer<ads::CDockAreaWidget> guard(area);
            QTimer::singleShot(0, this, [guard, resize, size = m.size] {
                if (guard) resize(guard, size);
            });
        }
    }
    placeRestoreHandles();
}

void EditorWindow::placeRestoreHandles() {
    for (const Minimized& m : minimized_) {
        m.handle->move(handlePos(m.edge, m.along, m.handle->size()));
        m.handle->raise();
    }
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
    // Holding a key repeats only what is meant to repeat (stepping, zooming), not paste or duplicate.
    const Action repeating[] = {Action::PrevFrame, Action::NextFrame, Action::PrevFrames, Action::NextFrames, Action::Undo,
                                Action::Redo, Action::ZoomIn, Action::ZoomOut, Action::PrevKeyframe, Action::NextKeyframe};
    act->setAutoRepeat(std::find(std::begin(repeating), std::end(repeating), a) != std::end(repeating));
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
    edit->addAction(action(Action::Cut, "Cut", [this] {
        if (copyToClipboard()) ctx_.deleteSelected();
    }));
    edit->addAction(action(Action::Copy, "Copy", [this] { copyToClipboard(); }));
    edit->addAction(action(Action::Paste, "Paste", [this] { pasteFromClipboard(); }));
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
    layer->addSeparator();
    QMenu* arrangeMenu = layer->addMenu("Arrange");
    arrangeMenu->setToolTipsVisible(true);
    arrangeMenu->addAction("Distribute Horizontally", this, [this] { arrange(false, false); })
        ->setToolTip("Even spacing from left to right (two layers spread across the frame)");
    arrangeMenu->addAction("Distribute Vertically", this, [this] { arrange(false, true); })
        ->setToolTip("Even spacing from top to bottom");
    arrangeMenu->addAction("Stagger Start Times", this, [this] { arrange(true); })
        ->setToolTip("Each selected layer starts 0.1 s after the one above it");
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
    QAction* rulers = view->addAction("Rulers", this, [this] {
        ctx_.showRulers = !ctx_.showRulers;
        settings().showRulers = ctx_.showRulers; // remembered for the next projects
        saveSettings();
    });
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
        actions_[Action::FullScreen]->setChecked(window()->isFullScreen());
    });
    view->addSeparator();
    QMenu* panels = view->addMenu("Panels");
    view->addAction("Tools...", this, [this] { showTools(); });
    for (const char* name : {"Objects", "Media", "Properties", "Effects", "Transitions", "Timeline"})
        panels->addAction(dockWidgets_[name]->toggleViewAction());
    view->addAction("Reset Layout", this, [this] { resetLayout(); });
    view->addSeparator();
    QAction* full = action(Action::FullScreen, "Full Screen", [this] {
        QWidget* w = window();
        if (w->isFullScreen()) w->showMaximized();
        else w->showFullScreen();
    });
    full->setCheckable(true);
    view->addAction(full);

    // Plugins: their commands (rebuilt when plugins change) and the plugin manager.
    QMenu* plugins = bar->addMenu("Plugins");
    auto fillPlugins = [this, plugins] {
        plugins->clear();
        plugins->setToolTipsVisible(true);
        const auto commands = PluginHost::get().commands();
        for (const PluginCommand* c : commands) {
            const std::string id = c->id;
            QAction* a = plugins->addAction(QString::fromStdString(c->name), this, [this, id] { runPluginCommand(id); });
            a->setToolTip(QString::fromStdString(c->description));
        }
        if (commands.empty()) plugins->addAction("No plugin commands installed")->setEnabled(false);
        plugins->addSeparator();
        plugins->addAction("Manage Plugins...", this, [] { ui::openSettings(ui::SettingsTab::Plugins); });
    };
    fillPlugins();
    connect(plugins, &QMenu::aboutToShow, this, fillPlugins);

    QMenu* help = bar->addMenu("Help");
    help->addAction(action(Action::Help, "Keyboard Shortcuts", [] { ui::openHelp(ui::HelpTab::Shortcuts); }));
    help->addSeparator();
    help->addAction("About bemotion", this, [] { ui::openHelp(ui::HelpTab::About); });
    help->addAction("Credits", this, [] { ui::openHelp(ui::HelpTab::Credits); });
    help->addAction("License", this, [] { ui::openHelp(ui::HelpTab::License); });
    help->addSeparator();
    help->addAction(ui::menuIcon(ui::Icon::Coffee, theme::kAccent), "Support bemotion (Buy Me a Coffee)...", this,
                    [] { platform::openWithDefaultApp("https://buymeacoffee.com/eugeniobenfica"); });

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
    ui::setTone(status_, ui::Tone::Danger);
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
    if (event->type() == QEvent::MouseMove) pointer_ = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
    if (watched == docks_ && event->type() == QEvent::Resize) {
        placeRestoreHandles();
        positionToolbarTargets();
        if (timeline_ && settings().toolbarInViewport && settings().toolbarInWindow) placeToolbar();
    }
    if (watched == viewport_ && event->type() == QEvent::Resize && settings().toolbarInViewport) placeToolbar();
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
    transitions_->sync();
    timeline_->sync();

    ctx_.commitIfIdle(interacting());
    updateMinimizeHandles();
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
        platform::log("save failed: " + err);
        setStatus(QString::fromStdString("Save failed: " + err));
        return;
    }
    platform::log("saved " + platform::toUtf8(ctx_.path));
    setStatus({});
    recents_.touch(ctx_.path, ctx_.project.name);
    recovery::discard(ctx_.path, ctx_.project.name);
    sinceSave_.restart();
}

void EditorWindow::saveAs() {
    const auto suggested = ctx_.path.empty()
                               ? projectsFolderOrDocuments() / platform::fromUtf8(ctx_.project.name + kProjectExtension)
                               : ctx_.path;
    auto file = platform::saveFileDialog("Save Project As", suggested, "bemotion Project", kProjectExtension);
    if (!file) return;
    std::string err;
    if (!ctx_.saveTo(*file, &err)) {
        setStatus(QString::fromStdString("Save failed: " + err));
        return;
    }
    setStatus({});
    recents_.touch(ctx_.path, ctx_.project.name);
    recovery::discard(ctx_.path, ctx_.project.name);
    sinceSave_.restart();
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

void EditorWindow::setStatus(const QString& text) {
    // The menu bar's corner keeps the size it had: it is laid out again for the new text.
    status_->setText(text);
    status_->setToolTip(text);
    status_->parentWidget()->adjustSize();
    menuBar()->adjustSize();
    menuBar()->updateGeometry();
    menuBar()->update();
}

void EditorWindow::arrange(bool stagger, bool vertical) {
    ctx_.playing = false;
    std::string error;
    const bool ok = stagger ? staggerLayers(ctx_.comp(), ctx_.selection, 0.1, &error)
                            : distributeLayers(ctx_.comp(), ctx_.selection, vertical, ctx_.time, &error);
    if (!ok) {
        setStatus(QString::fromStdString("Arrange: " + error));
        return;
    }
    setStatus({});
    ctx_.touch();
}

bool EditorWindow::copyToClipboard() {
    const std::string text = ctx_.copySelected();
    if (text.empty()) return false;
    auto* mime = new QMimeData;
    const QByteArray bytes = QByteArray::fromStdString(text);
    mime->setData(kLayersMime, bytes);
    mime->setText(QString::fromUtf8(bytes));
    QApplication::clipboard()->setMimeData(mime);
    return true;
}

void EditorWindow::pasteFromClipboard() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    std::string text;
    if (mime->hasFormat(kLayersMime)) text = mime->data(kLayersMime).toStdString();
    else if (mime->hasText()) text = mime->text().toStdString();
    ctx_.playing = false;
    if (!text.empty() && ctx_.paste(text)) return;
    // Files copied in the file manager: imported and placed at the playhead.
    std::vector<std::filesystem::path> files;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile()) files.push_back(platform::fromUtf8(url.toLocalFile().toStdString()));
    if (!files.empty()) dropFiles(files, false);
}

void EditorWindow::keepRecoveryCopy() {
    // Only while there is something to lose; saving or closing removes it.
    if (!ctx_.isDirty() || interacting()) return;
    std::string err;
    if (!recovery::keep(ctx_.project, ctx_.path, &err)) platform::log("could not keep a recovery copy: " + err);
}

void EditorWindow::autosave() {
    const int minutes = settings().autosaveMinutes;
    if (minutes <= 0 || ctx_.path.empty() || !ctx_.isDirty() || interacting()) return;
    if (sinceSave_.isValid() && sinceSave_.elapsed() < minutes * 60000LL) return;
    platform::log("autosaving");
    save();
}

void EditorWindow::markRecovered() {
    ctx_.touch(); // unsaved until the user saves
    setStatus("Recovered: save to keep the changes");
}

void EditorWindow::runPluginCommand(const std::string& id, bool pressed) {
    std::string error;
    ctx_.playing = false;
    platform::log("running plugin command " + id);
    PluginHost::get().setProjectFolder(ctx_.path.empty() ? std::filesystem::path() : ctx_.path.parent_path());
    CommandOutcome outcome;
    if (!PluginHost::get().runCommand(id, ctx_.project, ctx_.time, ctx_.selection, automationEnvironment(), error, pressed, &outcome)) {
        platform::log("plugin command failed: " + error);
        setStatus(QString::fromStdString("Plugin: " + error));
        timeline_->setPluginButtonPressed(id, false); // a toggle that failed stays up
        return;
    }
    if (outcome.changed) {
        ctx_.pruneSelection();
        ctx_.touch(); // one undo step
    }
    if (outcome.play >= 0) ctx_.playing = outcome.play == 1;
    setStatus({});
}

void EditorWindow::dropFiles(const std::vector<std::filesystem::path>& files, bool onMediaPanel) {
    // Like After Effects: dropped on the Media panel they are only imported; anywhere else they
    // also become layers at the playhead.
    for (const auto& f : files) platform::log("importing " + platform::toUtf8(f));
    media_->importFiles(files, !onMediaPanel);
}

} // namespace bemotion
