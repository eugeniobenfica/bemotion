#include "ui/EditorWindow.h"

#include "core/Expressions.h"
#include "core/Lottie.h"

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
#include "ui/LayerMenus.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/PreviewWindow.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/ColorPanel.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ScopesPanel.h"
#include "ui/panels/ObjectsPanel.h"
#include "ui/panels/PropertiesPanel.h"
#include "ui/panels/TimelinePanel.h"
#include "ui/panels/ToolsPanel.h"
#include "ui/panels/AnimationsPanel.h"
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
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
#include <QStackedWidget>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#include <cfloat>
#include <fstream>
#include <sstream>

namespace bemotion {

using ui::Action;

namespace {

std::filesystem::path layoutFile() { return platform::appDataDir() / "layout.ads"; }

// While the toolbar's grip is dragged: the places it can go (the window edges, the viewport, the
// timeline), the one under the mouse lit with its name, and the bar's picture where it would land.
class ToolbarOverlay : public QWidget {
public:
    struct Zone {
        QRect rect;
        QString label;
    };
    explicit ToolbarOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    std::vector<Zone> zones;
    int hot = -1;          // the zone under the mouse
    QRect landing;         // where the bar goes (empty: nowhere, it stays)
    QPixmap picture;       // the bar, horizontal
    QPixmap uprightPicture; // ... and vertical (left / right edges)
    bool upright = false;
    QPoint cursorAt;       // the picture follows the mouse when it is over no zone

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
            const QRectF r = QRectF(zones[i].rect).adjusted(1.5, 1.5, -1.5, -1.5);
            const ui::Pt a(r.topLeft()), b(r.bottomRight());
            if (i == hot) {
                d.rectFilled(a, b, theme::withAlpha(theme::kAccent, 34), 8.0f);
                d.rect(a, b, theme::kAccent, 8.0f, 2.0f);
            } else {
                d.rect(a, b, theme::withAlpha(theme::kAccent, 90), 8.0f, 1.0f);
            }
        }
        // The name of the place under the mouse, in a chip by the landing spot.
        if (hot >= 0) {
            const QString label = zones[hot].label;
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            const float w = d.textWidth(label) + 20.0f, h = d.textHeight() + 10.0f;
            const QRect near = landing.isEmpty() ? zones[hot].rect : landing;
            float x = near.center().x() - w * 0.5f, y = near.bottom() + 8.0f;
            if (y + h > height() - 4) y = near.top() - h - 8.0f;
            x = std::clamp(x, 4.0f, std::max(4.0f, width() - w - 4.0f));
            d.rectFilled({x, y}, {x + w, y + h}, theme::kAccent, h * 0.5f);
            d.text({x + 10.0f, y + 5.0f}, theme::kOnAccent, label);
        }
        const QPixmap& pic = upright ? uprightPicture : picture;
        if (!landing.isEmpty()) {
            p.setOpacity(0.95);
            p.drawPixmap(landing.topLeft(), pic);
            p.setOpacity(1.0);
            d.rect(ui::Pt(landing.left() - 1.5f, landing.top() - 1.5f), ui::Pt(landing.right() + 2.5f, landing.bottom() + 2.5f), theme::kAccent, 8.0f, 1.5f);
        } else {
            p.setOpacity(0.55);
            p.drawPixmap(cursorAt, picture);
        }
    }
};

// Layers on the system clipboard: our own type, plus the same text for other apps.
constexpr const char* kLayersMime = "application/x-bemotion-layers";

// True while the user is in the middle of an edit: mouse held, or typing in a field.
bool interacting() {
    if (QApplication::mouseButtons() != Qt::NoButton) return true;
    if (QApplication::activePopupWidget()) return true; // a menu or list open (it may be previewing)
    QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QPlainTextEdit*>(f));
}

bool typingInField() {
    QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QPlainTextEdit*>(f));
}

// The tab a minimized panel leaves on the window edge: its names (upright on the side edges, like
// a book's spine) and a chevron pointing where it comes back from. A click brings it back.
class RestoreHandle : public QAbstractButton {
public:
    RestoreHandle(int edge, const QString& names, QWidget* parent) : QAbstractButton(parent), edge_(edge) {
        setText(names);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        const int along = std::clamp(fontMetrics().horizontalAdvance(names) + 40, 64, 320);
        setFixedSize(edge_ < 2 ? QSize(24, along) : QSize(along, 24));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        const bool hot = underMouse();
        const ui::Pt a(0.5f, 0.5f), b(width() - 0.5f, height() - 0.5f);
        d.rectFilled(a, b, hot ? theme::withAlpha(theme::kAccent, 40) : theme::kBgRaised, 6.0f);
        d.rect(a, b, hot ? theme::kAccent : theme::kBorder, 6.0f);
        const int to = edge_ ^ 1; // toward the inside: where the panel comes back
        const ui::Icon icon = to == 1 ? ui::Icon::ChevronRight : to == 0 ? ui::Icon::ChevronLeft
                              : to == 3 ? ui::Icon::ChevronDown : ui::Icon::ChevronUp;
        const ui::Rgba ink = hot ? theme::kText : theme::kTextDim;
        // Laid out as if horizontal, then turned for the side edges (reading upward on the left,
        // downward on the right).
        const float len = static_cast<float>(edge_ < 2 ? height() : width()), across = static_cast<float>(edge_ < 2 ? width() : height());
        p.save();
        if (edge_ == 0) {
            p.translate(0, height());
            p.rotate(-90);
        } else if (edge_ == 1) {
            p.translate(width(), 0);
            p.rotate(90);
        }
        ui::Draw r(p);
        r.text({12.0f, (across - r.textHeight()) * 0.5f}, ink, p.fontMetrics().elidedText(text(), Qt::ElideRight, static_cast<int>(len - 40)));
        p.restore();
        const ui::Pt chevron = edge_ == 0 ? ui::Pt(across * 0.5f, 14.0f) : edge_ == 1 ? ui::Pt(across * 0.5f, len - 14.0f) : ui::Pt(len - 14.0f, across * 0.5f);
        ui::drawIcon(d, icon, chevron, 11.0f, hot ? theme::kAccent : theme::kTextDim);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    int edge_;
};

// A project opened in a new context (history from its saved state).
EditorContext opened(Project project, std::filesystem::path path) {
    EditorContext ctx;
    ctx.open(std::move(project), std::move(path));
    return ctx;
}
} // namespace

EditorWindow::EditorWindow(Project project, std::filesystem::path path, RecentProjects& recents, QWidget* parent)
    : EditorWindow(opened(std::move(project), std::move(path)), recents, parent) {}

EditorWindow::EditorWindow(EditorContext context, RecentProjects& recents, QWidget* parent)
    : QMainWindow(parent), ctx_(std::move(context)), recents_(recents) {
    setWindowFlags(Qt::Widget); // lives inside the application window
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
    sinceChange_.start();
    // Unsaved changes are kept aside (crash recovery); autosave() saves them after each change.
    auto* keeper = new QTimer(this);
    connect(keeper, &QTimer::timeout, this, [this] { keepRecoveryCopy(); });
    keeper->start(20000);
}

EditorWindow::~EditorWindow() {
    qApp->removeEventFilter(this);
    media::MediaManager::get().setPreview(nullptr);
    while (!minimized_.empty()) restoreMinimized(0); // minimized panels come back with the next project
    if (!handedOver_) recovery::discard(ctx_.path, ctx_.project.name); // closed normally: nothing to recover
    if (media::Recorder::get().recording()) media::Recorder::get().cancel(); // a recording left running ends with the project
    // The layout is remembered for the next project: the whole one, not the viewport alone (the
    // window comes back from full screen too) or a panel maximized.
    if (viewportFullscreen_) toggleViewportFullscreen();
    const QByteArray state = beforeMaximize_.isEmpty() ? docks_->saveState() : beforeMaximize_;
    std::ofstream out(layoutFile(), std::ios::binary | std::ios::trunc);
    out.write(state.constData(), state.size());
    media::AudioEngine::get().update(Composition{}, 0.0, false);
    setExpressionComposition(nullptr); // this composition goes with the window
}

QString EditorWindow::title() const {
    return QString::fromStdString(ctx_.project.name) + (ctx_.isDirty() ? " •" : "") + tr("  —  bemotion");
}

// --- docks -----------------------------------------------------------------------------

void EditorWindow::buildDocks() {
    // No close, undock, tabs-menu or pin buttons: a panel header has its tabs and a minimize button.
    // Panels still move by dragging their tab; View > Panels shows or hides them.
    ads::CDockManager::setConfigFlags(ads::CDockManager::OpaqueSplitterResize | ads::CDockManager::DockAreaHideDisabledButtons);
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::AutoHideFeatureEnabled); // used by maximize
    docks_ = new ads::CDockManager(this);
    docks_->installEventFilter(this); // restore handles follow its size
    connect(docks_, &ads::CDockManager::dockAreaCreated, this, &EditorWindow::decorateArea);
    // The docking library's own style sheet beats the application's: our colors go on top of it.
    const QString adsStyle = docks_->styleSheet();
    docks_->setStyleSheet(adsStyle + theme::dockStyleSheet());
    connect(&ui::AppSignals::get(), &ui::AppSignals::appearanceChanged, this,
            [this, adsStyle] {
                docks_->setStyleSheet(adsStyle + theme::dockStyleSheet());
                if (viewport_) viewport_->setPreviewQuality(settings().previewQuality);
                if (meters_) meters_->setVisible(settings().showMeters);
                ctx_.showRulers = settings().showRulers;
            });
    connect(&ui::AppSignals::get(), &ui::AppSignals::interfaceResetRequested, this, [this] { resetLayout(); });

    objects_ = new ObjectsPanel(ctx_);
    media_ = new MediaPanel(ctx_);
    media::MediaManager::get().setPreview(&ctx_.comp()); // its videos may show their proxies
    tools_ = new ToolsPanel(ctx_);
    viewport_ = new ViewportPanel(ctx_);
    viewport_->setPreviewQuality(settings().previewQuality);
    properties_ = new PropertiesPanel(ctx_);
    effects_ = new EffectsPanel(ctx_);
    color_ = new ColorPanel(ctx_);
    animations_ = new AnimationsPanel(ctx_);
    scopes_ = new ScopesPanel(ctx_);
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
            timeline_->setToolPillVertical(false);
            QWidget* pill = timeline_->toolPill();
            const QSize free = viewport_->size() - pill->sizeHint();
            if (at.x() < 0) at = QPoint(free.width() / 2, 10); // its default place: top center
            s.toolbarX = std::clamp(static_cast<float>(at.x()) / std::max(1, free.width()), 0.0f, 1.0f);
            s.toolbarY = std::clamp(static_cast<float>(at.y()) / std::max(1, free.height()), 0.0f, 1.0f);
        }
        saveSettings();
        placeToolbar();
    };
    connect(timeline_, &TimelinePanel::toolbarDragStarted, this, &EditorWindow::toolbarDragStarted);
    connect(timeline_, &TimelinePanel::toolbarDragMoved, this, &EditorWindow::toolbarDragMoved);
    connect(timeline_, &TimelinePanel::toolbarDragFinished, this, &EditorWindow::toolbarDragFinished);
    connect(timeline_, &TimelinePanel::toolbarDockRequested, this, &EditorWindow::dockToolbar);
    connect(viewport_, &ViewportPanel::previewQualityChanged, this, [](int divisor) {
        settings().previewQuality = divisor;
        saveSettings();
    });
    // The viewport's full screen button: the preview (or, in the viewport-only layout, back).
    connect(viewport_, &ViewportPanel::viewportFullscreenRequested, this, [this] {
        if (viewportFullscreen_) toggleViewportFullscreen();
        else openPreview();
    });
    connect(viewport_, &ViewportPanel::layerMenuRequested, timeline_, &TimelinePanel::showTrackMenu);
    connect(viewport_, &ViewportPanel::snapshotRequested, this, [this] { exportFrame(); });
    connect(properties_, &PropertiesPanel::panelRequested, this, [this](const QString& name) {
        // Transitions live in Animate.
        auto it = dockWidgets_.find(name == "Transitions" ? std::string("AnimateLeft") : name.toStdString());
        if (it == dockWidgets_.end()) return;
        if (it->second->isClosed()) it->second->toggleView(true); // (a minimized panel comes back too)
        it->second->setAsCurrentTab();
        it->second->raise();
    });
    connect(timeline_, &TimelinePanel::toolbarMoveRequested, this, moveToolbar);
    connect(&ui::AppSignals::get(), &ui::AppSignals::toolbarChanged, this, [this] { QTimer::singleShot(0, this, [this] { placeToolbar(); }); });
    QTimer::singleShot(0, this, [this] { placeToolbar(); });
    connect(timeline_, &TimelinePanel::toolsPanelRequested, this, &EditorWindow::showTools);

    // `id`: what saved layouts know the panel by (English, never translated); its name by default.
    auto dock = [&](const char* name, QWidget* w, const char* id = nullptr) {
        auto* d = new ads::CDockWidget(docks_, id ? id : name); // the name stays English: saved layouts know panels by it
        d->setWindowTitle(tr(name));
        d->setWidget(w, ads::CDockWidget::ForceNoScrollArea);
        d->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
        dockWidgets_[id ? id : name] = d;
        d->tabWidget()->installEventFilter(this); // double-click a tab: maximize / restore
        // Shown again some other way (View > Panels, Reset Layout): its restore handle goes.
        connect(d, &ads::CDockWidget::viewToggled, this, [this, d](bool open) {
            QTimer::singleShot(0, this, [this] { fitSideAreas(); }); // a tab more or less
            if (!open) return;
            for (size_t i = 0; i < minimized_.size(); ++i)
                if (std::find(minimized_[i].docks.begin(), minimized_[i].docks.end(), d) != minimized_[i].docks.end()) {
                    restoreMinimized(i);
                    return;
                }
        });
        return d;
    };
    auto* vp = dock(TR_NOOP("Viewport"), viewport_);
    vp->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    auto* center = docks_->setCentralWidget(vp);
    auto* left = docks_->addDockWidget(ads::LeftDockWidgetArea, dock(TR_NOOP("Objects"), objects_));
    docks_->addDockWidgetTabToArea(dock(TR_NOOP("Media"), media_), left);
    // Animate, beside the objects: animations dragged onto clips like objects are. ("AnimateLeft":
    // a layout saved with it elsewhere, or with transitions apart, comes back as the default one.)
    docks_->addDockWidgetTabToArea(dock(TR_NOOP("Animate"), animations_, "AnimateLeft"), left);
    docks_->addDockWidgetTabToArea(dock(TR_NOOP("Scopes"), scopes_), left);       // beside the Effects tab while grading
    left->setCurrentIndex(0);
    auto* right = docks_->addDockWidget(ads::RightDockWidgetArea, dock(TR_NOOP("Properties"), properties_));
    docks_->addDockWidgetTabToArea(dock(TR_NOOP("Effects"), effects_), right);
    docks_->addDockWidgetTabToArea(dock(TR_NOOP("Color"), color_), right); // grading: LUT, curves, correction
    connect(color_, &ColorPanel::showScopesRequested, this, [this] {
        ads::CDockWidget* scopes = dockWidgets_["Scopes"];
        scopes->toggleView(true);
        scopes->setAsCurrentTab();
    });
    right->setCurrentIndex(0);
    // The timeline with the audio meters at its right.
    auto* timelineHost = new QWidget;
    auto* timelineRow = new QHBoxLayout(timelineHost);
    timelineRow->setContentsMargins(0, 0, 0, 0);
    timelineRow->setSpacing(0);
    timelineRow->addWidget(timeline_, 1);
    meters_ = new ui::AudioMeter;
    meters_->setContentsMargins(0, 0, 0, 0);
    timelineRow->addWidget(meters_);
    meters_->setVisible(settings().showMeters);
    auto* bottom = docks_->addDockWidget(ads::BottomDockWidgetArea, dock(TR_NOOP("Timeline"), timelineHost));
    (void)center;
    QTimer::singleShot(0, this, [this, left, right, bottom] {
        docks_->setSplitterSizes(left, {width() * 17 / 100, width() * 57 / 100, width() * 26 / 100});
        docks_->setSplitterSizes(bottom, {height() * 64 / 100, height() * 36 / 100});
        (void)right;
        dockWidgets_["Scopes"]->toggleView(false); // a pro tool: View > Panels opens it (in the left area)
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
        fitSideAreas();
    });
}

void EditorWindow::fitSideAreas() {
    // The side panels stay wide enough for their tabs' whole names (a narrower saved layout
    // widens): the open tabs, the minimize and maximize buttons.
    for (const char* name : {"Objects", "Properties"}) {
        ads::CDockAreaWidget* area = dockWidgets_[name]->dockAreaWidget();
        if (!area) continue;
        int tabs = 0;
        for (ads::CDockWidget* d : area->openedDockWidgets()) tabs += d->tabWidget()->sizeHint().width() + 2; // its margins
        area->setMinimumWidth(std::max(275, tabs + 56));
    }
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

namespace {
// What a toolbar drop would do: go back to the timeline, float over the viewport at `at`
// (viewport coordinates), or sit on a window edge at `edge` (Settings' fractions).
struct ToolbarDrop {
    enum Kind { None, Timeline, Viewport, Edge } kind = None;
    QPoint at;
    QPointF edge;
    QRect landing; // in the editor window
};
ToolbarDrop g_toolbarDrop;
QPoint g_toolbarGrab;
} // namespace

void EditorWindow::toolbarDragStarted(QPoint grab) {
    g_toolbarGrab = grab;
    g_toolbarDrop = {};
    auto* overlay = static_cast<ToolbarOverlay*>(toolbarOverlay_);
    if (!overlay) toolbarOverlay_ = overlay = new ToolbarOverlay(this);
    QWidget* pill = timeline_->toolPill();
    const bool wasUpright = pill->width() < pill->height();
    // Its pictures both ways, as it looks (the grab is taken before it fades).
    timeline_->setToolPillVertical(false);
    pill->resize(pill->sizeHint());
    overlay->picture = pill->grab();
    timeline_->setToolPillVertical(true);
    pill->resize(pill->sizeHint());
    overlay->uprightPicture = pill->grab();
    timeline_->setToolPillVertical(wasUpright);
    pill->resize(pill->sizeHint());
    if (wasUpright) g_toolbarGrab = QPoint(g_toolbarGrab.y(), g_toolbarGrab.x()); // held at the same spot of the bar
    overlay->setGeometry(rect());
    overlay->show();
    overlay->raise();
}

void EditorWindow::toolbarDragMoved(QPoint global) {
    auto* overlay = static_cast<ToolbarOverlay*>(toolbarOverlay_);
    if (!overlay) return;
    const QPoint m = mapFromGlobal(global);
    const QRect bounds = toolbarBounds();
    const QSize flat = overlay->picture.size() / std::max(1.0, overlay->picture.devicePixelRatio());
    const QSize upright = overlay->uprightPicture.size() / std::max(1.0, overlay->uprightPicture.devicePixelRatio());
    constexpr int kBand = 52; // how near an edge counts as "on" it
    overlay->zones.clear();
    std::vector<ToolbarDrop> drops;
    auto zone = [&](QRect r, const QString& label, ToolbarDrop drop) {
        overlay->zones.push_back({r, label});
        drops.push_back(drop);
    };
    // Back in the timeline, where its bar starts.
    const bool floating = settings().toolbarInViewport;
    if (floating && timeline_->isVisible()) {
        const QRect r(timeline_->mapTo(this, QPoint(4, 2)), QSize(std::max(flat.width() + 8, 200), flat.height() + 6));
        zone(r, tr("Back to the timeline"), {ToolbarDrop::Timeline, {}, {}, QRect(r.topLeft() + QPoint(4, 3), flat)});
    }
    // Along an edge: where the mouse is along it, the middle and the ends snap.
    auto along = [](double u) {
        u = std::clamp(u, 0.0, 1.0);
        if (std::fabs(u - 0.5) < 0.04) return 0.5;
        return u < 0.02 ? 0.0 : u > 0.98 ? 1.0 : u;
    };
    const int innerW = std::max(1, bounds.width() - flat.width()), innerH = std::max(1, bounds.height() - upright.height());
    const double ux = along((m.x() - g_toolbarGrab.x() - bounds.left()) / static_cast<double>(innerW));
    const double uy = along((m.y() - g_toolbarGrab.x() - bounds.top()) / static_cast<double>(innerH));
    const double flatU = std::clamp(ux, 0.0005, 0.9995); // exactly 0 or 1 across means a side edge (upright)
    const QPoint top(bounds.left() + static_cast<int>(flatU * innerW), bounds.top());
    const QPoint bottom(top.x(), bounds.bottom() - flat.height() + 1);
    const QPoint left(bounds.left(), bounds.top() + static_cast<int>(uy * innerH));
    const QPoint right(bounds.right() - upright.width() + 1, left.y());
    zone(QRect(bounds.left(), bounds.top(), bounds.width(), kBand), tr("Top edge"), {ToolbarDrop::Edge, {}, {flatU, 0}, QRect(top, flat)});
    zone(QRect(bounds.left(), bounds.bottom() - kBand + 1, bounds.width(), kBand), tr("Bottom edge"),
         {ToolbarDrop::Edge, {}, {flatU, 1}, QRect(bottom, flat)});
    zone(QRect(bounds.left(), bounds.top() + kBand, kBand, bounds.height() - 2 * kBand), tr("Left edge"),
         {ToolbarDrop::Edge, {}, {0, uy}, QRect(left, upright)});
    zone(QRect(bounds.right() - kBand + 1, bounds.top() + kBand, kBand, bounds.height() - 2 * kBand), tr("Right edge"),
         {ToolbarDrop::Edge, {}, {1, uy}, QRect(right, upright)});
    // Anywhere over the viewport.
    const QRect vp(viewport_->mapTo(this, QPoint()), viewport_->size());
    if (viewport_->isVisible()) {
        const QRect inside = vp.adjusted(6, 6, -6 - flat.width(), -6 - flat.height());
        const QPoint at(std::clamp(m.x() - g_toolbarGrab.x(), inside.left(), std::max(inside.left(), inside.right())),
                        std::clamp(m.y() - g_toolbarGrab.y(), inside.top(), std::max(inside.top(), inside.bottom())));
        zone(vp.adjusted(4, 4, -4, -4), tr("Float over the viewport"), {ToolbarDrop::Viewport, at - vp.topLeft() - QPoint(6, 6), {}, QRect(at, flat)});
    }
    // The first zone under the mouse wins (the timeline, then the edges, then the viewport).
    overlay->hot = -1;
    g_toolbarDrop = {};
    for (int i = 0; i < static_cast<int>(drops.size()); ++i)
        if (overlay->zones[i].rect.contains(m)) {
            overlay->hot = i;
            g_toolbarDrop = drops[i];
            break;
        }
    overlay->landing = g_toolbarDrop.landing;
    overlay->upright = g_toolbarDrop.kind == ToolbarDrop::Edge && (g_toolbarDrop.edge.x() == 0.0 || g_toolbarDrop.edge.x() == 1.0);
    overlay->cursorAt = m - g_toolbarGrab;
    overlay->update();
}

void EditorWindow::toolbarDragFinished(QPoint global, bool dropped) {
    if (dropped) toolbarDragMoved(global);
    if (toolbarOverlay_) toolbarOverlay_->hide();
    const ToolbarDrop drop = std::exchange(g_toolbarDrop, {});
    if (!dropped) return;
    // Moved after this event returns: the bar being dragged is the one that changes parents.
    QTimer::singleShot(0, this, [this, drop] {
        switch (drop.kind) {
        case ToolbarDrop::Timeline:
            settings().toolbarInViewport = false;
            settings().toolbarInWindow = false;
            saveSettings();
            placeToolbar();
            break;
        case ToolbarDrop::Edge: dockToolbar(drop.edge); break;
        case ToolbarDrop::Viewport: {
            Settings& s = settings();
            s.toolbarInViewport = true;
            s.toolbarInWindow = false;
            timeline_->setToolPillVertical(false);
            const QSize free = viewport_->size() - timeline_->toolPill()->sizeHint() - QSize(12, 12);
            s.toolbarX = std::clamp(static_cast<float>(drop.at.x()) / std::max(1, free.width()), 0.0f, 1.0f);
            s.toolbarY = std::clamp(static_cast<float>(drop.at.y()) / std::max(1, free.height()), 0.0f, 1.0f);
            saveSettings();
            placeToolbar();
            break;
        }
        case ToolbarDrop::None: break; // over nothing: it stays where it was
        }
    });
}

void EditorWindow::placeToolbar() {
    QWidget* pill = timeline_->toolPill();
    if (!settings().toolbarInViewport) {
        timeline_->dockToolPill();
        return;
    }
    timeline_->setToolPillVertical(settings().toolbarInWindow && (settings().toolbarX == 0.0f || settings().toolbarX == 1.0f));
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

void EditorWindow::decorateArea(ads::CDockAreaWidget* area) {
    // At the right of each panel header: minimize (to a tab on the window edge) and maximize
    // (the panel fills the window; again, or a double-click on a tab, brings the layout back).
    ads::CDockAreaTitleBar* bar = area->titleBar();
    if (!bar) return;
    auto* min = new ui::IconButton(ui::Icon::Minimize, tr("Minimize panel"), 22);
    auto* max = new ui::IconButton(ui::Icon::Maximize, tr("Maximize panel (or double-click its tab)"), 22);
    min->setObjectName("bemotionMinimize");
    max->setObjectName("bemotionMaximize");
    QPointer<ads::CDockAreaWidget> guard(area);
    connect(min, &QAbstractButton::clicked, this, [this, guard] {
        if (guard) QTimer::singleShot(0, this, [this, guard] { if (guard) minimizeArea(guard); });
    });
    connect(max, &QAbstractButton::clicked, this, [this, guard] {
        if (guard && guard->currentDockWidget())
            QTimer::singleShot(0, this, [this, guard] { if (guard && guard->currentDockWidget()) maximizeDock(guard->currentDockWidget()); });
    });
    const int at = bar->indexOf(bar->button(ads::TitleBarButtonTabsMenu));
    bar->insertWidget(at < 0 ? bar->layout()->count() : at, min);
    bar->insertWidget(at < 0 ? bar->layout()->count() : at + 1, max);
}

void EditorWindow::updateAreaButtons() {
    const bool maximized = !beforeMaximize_.isEmpty();
    for (ads::CDockAreaWidget* a : docks_->openedDockAreas()) {
        ads::CDockAreaTitleBar* bar = a->titleBar();
        if (!bar) continue;
        auto* min = bar->findChild<ui::IconButton*>("bemotionMinimize", Qt::FindDirectChildrenOnly);
        auto* max = bar->findChild<ui::IconButton*>("bemotionMaximize", Qt::FindDirectChildrenOnly);
        if (!min || !max) continue;
        // The viewport stays (it has its own full screen); a maximized panel can only come back.
        const bool regular = !a->isCentralWidgetArea() && a->dockContainer() == docks_;
        if (min->isVisible() != (regular && !maximized)) min->setVisible(regular && !maximized);
        if (max->isVisible() != regular) max->setVisible(regular);
        if (max->active() != maximized) {
            max->setActive(maximized);
            max->setIcon(maximized ? ui::Icon::Restore : ui::Icon::Maximize);
            max->setToolTip(maximized ? tr("Restore the layout (or double-click the tab)") : tr("Maximize panel (or double-click its tab)"));
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
    auto* handle = new RestoreHandle(m.edge, names.join("  ·  "), docks_);
    handle->setToolTip(tr("Show ") + names.join(", ") + tr(" again"));
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
    // Where each panel was along its edge; tabs on the same edge do not overlap.
    std::vector<QRect> placed;
    for (const Minimized& m : minimized_) {
        QRect r(handlePos(m.edge, m.along, m.handle->size()), m.handle->size());
        for (bool moved = true; moved;) {
            moved = false;
            for (const QRect& other : placed)
                if (other.intersects(r.adjusted(-4, -4, 4, 4))) {
                    if (m.edge < 2) r.moveTop(other.bottom() + 8);
                    else r.moveLeft(other.right() + 8);
                    moved = true;
                }
        }
        placed.push_back(r);
        m.handle->move(r.topLeft());
        m.handle->raise();
    }
}

void EditorWindow::resetLayout() {
    if (viewportFullscreen_) toggleViewportFullscreen();
    while (!minimized_.empty()) restoreMinimized(0);
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
    // The viewport is the center and stays: the panel takes nearly all of the space beside it.
    QPointer<ads::CDockWidget> guard(dock);
    QTimer::singleShot(0, this, [guard] {
        ads::CDockAreaWidget* area = guard ? guard->dockAreaWidget() : nullptr;
        auto* splitter = area ? qobject_cast<QSplitter*>(area->parentWidget()) : nullptr;
        if (!splitter || guard->objectName() == "Viewport") return;
        QList<int> sizes = splitter->sizes();
        const int i = splitter->indexOf(area);
        if (i < 0) return;
        int total = 0;
        for (int s : sizes) total += s;
        for (int k = 0; k < sizes.size(); ++k) sizes[k] = k == i ? total - 40 * (static_cast<int>(sizes.size()) - 1) : 40;
        splitter->setSizes(sizes);
    });
}

void EditorWindow::toggleViewportFullscreen() {
    if (viewportFullscreen_) {
        docks_->restoreState(viewportFullscreenLayout_);
        menuBar()->setVisible(true);
        viewportFullscreen_ = false;
        viewport_->setViewportFullscreen(false);
        if (!windowWasFullscreen_) {
            window()->showNormal();
            window()->restoreGeometry(windowGeometryBeforeFullscreen_);
        }
        return;
    }
    viewportFullscreenLayout_ = docks_->saveState();
    windowWasFullscreen_ = window()->isFullScreen();
    windowGeometryBeforeFullscreen_ = window()->saveGeometry();
    for (auto& [name, dock] : dockWidgets_)
        if (name != "Viewport" && !dock->isClosed()) dock->toggleView(false);
    menuBar()->setVisible(false);
    viewportFullscreen_ = true;
    viewport_->setViewportFullscreen(true);
    viewport_->requestFit();
    window()->showFullScreen();
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
    auto* home = new ui::IconButton(ui::Icon::Logo, tr("Back to projects"), 28);
    connect(home, &QAbstractButton::clicked, this, [this] {
        if (confirmClose()) emit goHome();
    });
    bar->setCornerWidget(home, Qt::TopLeftCorner);

    QMenu* file = bar->addMenu(tr("File"));
    file->addAction(action(Action::NewProject, tr("New Project..."), [this] {
        if (confirmClose()) emit newProjectRequested();
    }));
    file->addAction(action(Action::OpenProject, tr("Open Project..."), [this] {
        if (auto p = platform::openFileDialog(TR_NOOP("Open bemotion Project"), TR_NOOP("bemotion Project"), kProjectExtension); p && confirmClose())
            emit openProjectRequested(*p);
    }));
    QMenu* recent = file->addMenu(tr("Open Recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        int shown = 0;
        for (const RecentProject& r : recents_.items()) {
            std::error_code ec;
            if (r.path == ctx_.path || !std::filesystem::exists(r.path, ec)) continue;
            const std::filesystem::path path = r.path;
            QAction* a = recent->addAction(QString::fromStdString(r.name.empty() ? platform::toUtf8(path.stem()) : r.name), this, [this, path] {
                if (confirmClose()) emit openProjectRequested(path);
            });
            a->setToolTip(QString::fromStdString(platform::toUtf8(path)));
            if (++shown == 10) break;
        }
        if (!shown) recent->addAction(tr("No other projects yet"))->setEnabled(false);
    });
    recent->setToolTipsVisible(true);
    file->addSeparator();
    file->addAction(action(Action::Save, tr("Save"), [this] { save(); }));
    file->addAction(action(Action::SaveAs, tr("Save As..."), [this] { saveAs(); }));
    // The same settings as the Properties panel with nothing selected, and the length limit.
    file->addAction(tr("Composition Settings..."), this, [this] {
        const Composition& c = ctx_.comp();
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Composition Settings"));
        auto* form = new QFormLayout;
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(8);
        auto* sizeRow = new QHBoxLayout;
        auto* w = new ui::DragValue;
        auto* h = new ui::DragValue;
        for (ui::DragValue* v : {w, h}) {
            v->setRange(16.0f, 16384.0f);
            v->setSpeed(1.0f);
            v->setFormat("%.0f px");
            v->setMinimumWidth(110);
        }
        w->setValue(static_cast<float>(c.width));
        h->setValue(static_cast<float>(c.height));
        sizeRow->addWidget(w);
        sizeRow->addWidget(new QLabel("×"));
        sizeRow->addWidget(h);
        form->addRow(tr("Size"), sizeRow);
        auto* fps = new ui::DragValue;
        fps->setRange(1.0f, 240.0f);
        fps->setSpeed(0.1f);
        fps->setFormat("%.2f fps");
        fps->setValue(static_cast<float>(c.fps));
        form->addRow(tr("Frame rate"), fps);
        auto* bg = new ui::ColorButton;
        bg->setAlphaEnabled(false);
        bg->setColor(c.background);
        form->addRow(tr("Background"), bg);
        double limit = c.maxDuration;
        form->addRow(tr("Length"), ui::lengthLimitRow(limit, [&limit](double value) { limit = value; }));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 18);
        layout->addLayout(form);
        layout->addSpacing(10);
        layout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted) return;
        Composition& comp = ctx_.comp();
        comp.width = static_cast<int>(std::lround(w->value()));
        comp.height = static_cast<int>(std::lround(h->value()));
        comp.fps = fps->value();
        const Color b = bg->color();
        comp.background = {b.r, b.g, b.b, 1.0f};
        comp.maxDuration = limit;
        comp.syncCameras();
        comp.fitDuration();
        ctx_.setTime(ctx_.time);
        ctx_.touch();
    });
    file->addSeparator();
    file->addAction(action(Action::ImportMedia, tr("Import Media..."), [this] { media_->openImportDialog(); }));
    file->addAction(ui::menuIcon(ui::Icon::Pen, theme::kTextDim), tr("Import SVG..."), this, [this] {
        // A drawing: shape layers at the playhead, each outline a path (core/Svg.h).
        if (auto f = platform::openFileDialog(TR_NOOP("Import SVG"), TR_NOOP("SVG drawing"), ".svg")) media_->importFiles({*f}, true);
    });
    file->addAction(tr("Show in Folder"), this, [this] {
        if (!ctx_.path.empty()) platform::revealInFileManager(ctx_.path);
    });
    file->addSeparator();
    file->addAction(action(Action::Export, tr("Export Video..."), [this] { ui::showExportDialog(ctx_, this); }));
    file->addAction(action(Action::ExportFrame, tr("Export Frame..."), [this] { exportFrame(); }));
    file->addAction(tr("Export Lottie (JSON)..."), this, [this] { exportLottieFile(); });
    file->addAction(tr("Copy Frame"), this, [this] { exportFrame(true); });
    file->addSeparator();
    file->addAction(action(Action::CloseProject, tr("Close Project"), [this] {
        if (confirmClose()) emit goHome();
    }));
    file->addAction(action(Action::Quit, "Quit", [this] { emit quitRequested(); }));

    QMenu* edit = bar->addMenu(tr("Edit"));
    edit->addAction(action(Action::Undo, tr("Undo"), [this] { ctx_.undo(); }));
    edit->addAction(action(Action::Redo, tr("Redo"), [this] { ctx_.redo(); }));
    edit->addSeparator();
    edit->addAction(action(Action::Cut, tr("Cut"), [this] {
        if (copyToClipboard()) ctx_.deleteSelected();
    }));
    edit->addAction(action(Action::Copy, tr("Copy"), [this] { copyToClipboard(); }));
    edit->addAction(action(Action::Paste, tr("Paste"), [this] { pasteFromClipboard(); }));
    edit->addAction(action(Action::Duplicate, tr("Duplicate"), [this] { ctx_.duplicateSelected(); }));
    edit->addAction(action(Action::Delete, tr("Delete"), [this] {
        ctx_.deletePicked(); // a transition, a Quick Animation or a mask picked: it, not its layer
    }));
    edit->addAction(action(Action::RippleDelete, tr("Ripple Delete"), [this] { ctx_.rippleDeleteSelected(); }));
    edit->addSeparator();
    QMenu* markers = edit->addMenu(ui::menuIcon(ui::Icon::Marker, theme::kTextDim), tr("Markers"));
    markers->addAction(action(Action::AddMarker, tr("Add Marker"), [this] { ctx_.addMarkerAtPlayhead(); }));
    markers->addAction(action(Action::NextMarker, tr("Next Marker"), [this] { ctx_.jumpToMarker(true); }));
    markers->addAction(action(Action::PrevMarker, tr("Previous Marker"), [this] { ctx_.jumpToMarker(false); }));
    markers->addSeparator();
    QAction* clearMarkers = markers->addAction(tr("Remove All Markers"), this, [this] {
        ctx_.comp().markers.clear();
        ctx_.touch();
    });
    connect(markers, &QMenu::aboutToShow, this, [this, clearMarkers] { clearMarkers->setEnabled(!ctx_.comp().markers.empty()); });
    edit->addSeparator();
    edit->addAction(action(Action::SelectAll, tr("Select All"), [this] { ctx_.selectAll(); }));
    edit->addAction(action(Action::Deselect, tr("Deselect"), [this] {
        if (ctx_.tool != Tool::Select) ctx_.tool = Tool::Select;
        else if (!ctx_.selection.empty()) ctx_.select(0);
        else ctx_.exitScope(); // nothing selected: leave the open compound
    }));
    edit->addSeparator();
    edit->addAction(action(Action::Settings, tr("Settings..."), [] { ui::openSettings(); }));

    QMenu* layer = bar->addMenu(tr("Layer"));
    layer->addAction(action(Action::Split, tr("Split at Playhead"), [this] { ctx_.splitSelectedAtPlayhead(); }));
    // Separate Audio: made when shown, for the selection then (the action is added in its place).
    QAction* separateSpot = layer->addSeparator();
    separateSpot->setVisible(false);
    connect(layer, &QMenu::aboutToShow, this, [this, layer, separateSpot] {
        for (QAction* a : layer->actions())
            if (a->property("separate").toBool()) delete a;
        QMenu scratch;
        ui::addSeparateAudio(scratch, ctx_);
        for (QAction* a : scratch.actions()) {
            a->setParent(layer);
            a->setProperty("separate", true);
            layer->insertAction(separateSpot, a);
        }
    });
    // Time, blending, track matte and masks: filled when shown, for the selection then.
    QMenu* timeMenu = layer->addMenu(ui::menuIcon(ui::Icon::Speed, theme::kTextDim), tr("Time"));
    connect(timeMenu, &QMenu::aboutToShow, this, [this, timeMenu] {
        timeMenu->clear();
        ui::fillTimeMenu(timeMenu, ctx_, this);
    });
    layer->addSeparator();
    QMenu* blendMenu = layer->addMenu(tr("Blending Mode"));
    connect(blendMenu, &QMenu::aboutToShow, this, [this, blendMenu] {
        blendMenu->clear();
        ui::fillBlendMenu(blendMenu, ctx_);
    });
    QMenu* matteMenu = layer->addMenu(tr("Track Matte"));
    connect(matteMenu, &QMenu::aboutToShow, this, [this, matteMenu] {
        matteMenu->clear();
        ui::fillMatteMenu(matteMenu, ctx_);
    });
    QMenu* maskMenu = layer->addMenu(ui::menuIcon(ui::Icon::Mask, theme::kTextDim), tr("Mask"));
    connect(maskMenu, &QMenu::aboutToShow, this, [this, maskMenu] {
        maskMenu->clear();
        ui::fillMaskMenu(maskMenu, ctx_);
    });
    layer->addAction(ui::menuIcon(ui::Icon::PathShape, theme::kTextDim), tr("Convert to Bézier Path"), this, [this] { ui::convertSelectedToPaths(ctx_); });
    layer->addSeparator();
    layer->addAction(action(Action::CreateCompound, tr("Create Compound"), [this] { ctx_.createCompound(); }));
    layer->addAction(action(Action::ReleaseCompound, tr("Release Compound"), [this] { ctx_.releaseSelectedCompounds(); }));
    QMenu* fx = layer->addMenu(tr("Effect"));
    connect(fx, &QMenu::aboutToShow, this, [this, fx] {
        fx->clear();
        fillEffectMenu(fx, ctx_);
    });
    layer->addSeparator();
    layer->addAction(action(Action::CutTool, tr("Cut Tool"), [this] { ctx_.tool = Tool::Razor; }));
    layer->addAction(action(Action::CropTool, tr("Crop Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Crop ? Tool::Select : Tool::Crop; }));
    layer->addAction(action(Action::SelectTool, tr("Selection Tool"), [this] { ctx_.tool = Tool::Select; }));
    layer->addAction(action(Action::PenTool, tr("Pen Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Pen ? Tool::Select : Tool::Pen; }));
    layer->addAction(action(Action::RippleTool, tr("Ripple Edit Tool"), [this] { ctx_.tool = Tool::Ripple; }));
    layer->addAction(action(Action::RollTool, tr("Rolling Edit Tool"), [this] { ctx_.tool = Tool::Roll; }));
    layer->addAction(action(Action::SlipTool, tr("Slip Tool"), [this] { ctx_.tool = Tool::Slip; }));
    layer->addSeparator();
    layer->addAction(action(Action::BringToFront, tr("Bring to Front"), [this] { ctx_.shiftSelected(-1000); }));
    layer->addAction(action(Action::BringForward, tr("Bring Forward"), [this] { ctx_.shiftSelected(-1); }));
    layer->addAction(action(Action::SendBackward, tr("Send Backward"), [this] { ctx_.shiftSelected(1); }));
    layer->addAction(action(Action::SendToBack, tr("Send to Back"), [this] { ctx_.shiftSelected(1000); }));
    layer->addSeparator();
    QMenu* arrangeMenu = layer->addMenu(tr("Arrange"));
    arrangeMenu->setToolTipsVisible(true);
    arrangeMenu->addAction(tr("Distribute Horizontally"), this, [this] { arrange(false, false); })
        ->setToolTip(tr("Even spacing from left to right (two layers spread across the frame)"));
    arrangeMenu->addAction(tr("Distribute Vertically"), this, [this] { arrange(false, true); })
        ->setToolTip(tr("Even spacing from top to bottom"));
    arrangeMenu->addAction(tr("Stagger Start Times"), this, [this] { arrange(true); })
        ->setToolTip(tr("Each selected layer starts 0.1 s after the one above it"));
    connect(layer, &QMenu::aboutToShow, this, [this] {
        const bool any = !ctx_.selection.empty();
        for (Action a : {Action::CreateCompound, Action::BringToFront, Action::BringForward, Action::SendBackward, Action::SendToBack})
            actions_[a]->setEnabled(any);
        actions_[Action::ReleaseCompound]->setEnabled(ctx_.selectionHasCompound());
        actions_[Action::CutTool]->setCheckable(true);
        actions_[Action::CutTool]->setChecked(ctx_.tool == Tool::Razor);
        actions_[Action::CropTool]->setCheckable(true);
        actions_[Action::CropTool]->setChecked(ctx_.tool == Tool::Crop);
        actions_[Action::SelectTool]->setCheckable(true);
        actions_[Action::SelectTool]->setChecked(ctx_.tool == Tool::Select);
        const std::pair<Action, Tool> tools[] = {{Action::PenTool, Tool::Pen}, {Action::RippleTool, Tool::Ripple},
                                                 {Action::RollTool, Tool::Roll}, {Action::SlipTool, Tool::Slip}};
        for (const auto& [a, t] : tools) {
            actions_[a]->setCheckable(true);
            actions_[a]->setChecked(ctx_.tool == t);
        }
    });
    connect(layer, &QMenu::aboutToHide, this, [this] {
        for (auto& [a, act] : actions_) act->setEnabled(true); // shortcuts stay usable (they check themselves)
    });

    // The same objects as the Objects panel, in its order.
    QMenu* add = bar->addMenu(tr("Add"));
    add->addAction(action(Action::AddText, tr("Text"), [this] { ctx_.addText(); }));
    add->addAction(tr("Counter"), this, [this] { ctx_.addCounter(); });
    add->addAction(tr("Subtitles"), this, [this] { ctx_.addSubtitles(); });
    add->addSeparator();
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i)
        add->addAction(tr(shapeKindLabel(static_cast<ShapeKind>(i))), this, [this, i] { ctx_.addShape(static_cast<ShapeKind>(i)); });
    add->addSeparator();
    add->addAction(tr("Camera"), this, [this] { ctx_.addCamera(); });
    add->addAction(tr("Background"), this, [this] { ctx_.addBackground(); });
    add->addSeparator();
    // Plugin objects, a submenu per plugin (rebuilt as plugins change).
    QAction* pluginsMark = add->addSeparator();
    connect(add, &QMenu::aboutToShow, this, [this, add, pluginsMark] {
        for (QAction* a : add->actions())
            if (a->property("pluginMenu").toBool()) add->removeAction(a), a->deleteLater();
        std::string lastPlugin;
        QMenu* sub = nullptr;
        for (const PluginObjectType* type : PluginHost::get().objectTypes()) {
            if (type->pluginId != lastPlugin || !sub) {
                lastPlugin = type->pluginId;
                const Plugin* plugin = PluginHost::get().findPlugin(type->pluginId);
                sub = new QMenu(QString::fromStdString(plugin ? plugin->name : type->pluginId), add);
                sub->menuAction()->setProperty("pluginMenu", true);
                add->insertMenu(pluginsMark, sub);
            }
            const std::string id = type->id;
            sub->addAction(QString::fromStdString(type->name), this, [this, id] {
                if (const PluginObjectType* t = PluginHost::get().findType(id)) ctx_.addPluginObject(*t);
            });
        }
    });
    QMenu* mediaMenu = add->addMenu(tr("Media"));
    connect(mediaMenu, &QMenu::aboutToShow, this, [this, mediaMenu] {
        mediaMenu->clear();
        QAction* imp = mediaMenu->addAction(tr("Import Media..."), this, [this] { media_->openImportDialog(); });
        imp->setShortcut(ui::sequenceOf(Action::ImportMedia));
        if (!ctx_.comp().media.empty()) mediaMenu->addSeparator();
        for (const MediaItem& item : ctx_.comp().media) {
            const uint64_t id = item.id;
            mediaMenu->addAction(QString::fromStdString(item.name + "  (" + mediaKindLabel(item.kind) + ")"), this,
                                 [this, id] { ctx_.addMedia(id); });
        }
        if (ctx_.comp().media.empty()) mediaMenu->addAction(tr("Nothing imported yet"))->setEnabled(false);
    });

    QMenu* view = bar->addMenu(tr("View"));
    view->addAction(action(Action::FitView, tr("Fit Composition"), [this] { viewport_->requestFit(); }));
    view->addAction(action(Action::ActualSize, tr("Actual Size (100%)"), [this] { viewport_->setZoom(1.0f); }));
    view->addAction(action(Action::ZoomIn, tr("Zoom In"), [this] { viewport_->zoomBy(1.25f); }));
    view->addAction(action(Action::ZoomOut, tr("Zoom Out"), [this] { viewport_->zoomBy(1.0f / 1.25f); }));
    view->addAction(action(Action::FullScreenPreview, tr("Full Screen Preview"), [this] { openPreview(); }));
    view->addAction(action(Action::ViewportFullscreen, tr("Maximize Viewport"), [this] { toggleViewportFullscreen(); }));
    view->addSeparator();
    QAction* snap = action(Action::ToggleSnapping, tr("Snapping"), [this] { ctx_.snapping = !ctx_.snapping; });
    snap->setCheckable(true);
    view->addAction(snap);
    QAction* rulers = view->addAction(tr("Rulers"), this, [this] {
        ctx_.showRulers = !ctx_.showRulers;
        settings().showRulers = ctx_.showRulers; // remembered for the next projects
        saveSettings();
    });
    rulers->setCheckable(true);
    QMenu* guides = view->addMenu(tr("Guides"));
    QAction* showGuides = guides->addAction(tr("Show Guides"), this, [this] { ctx_.showGuides = !ctx_.showGuides; });
    showGuides->setCheckable(true);
    QAction* lockGuides = guides->addAction(tr("Lock Guides"), this, [this] { ctx_.lockGuides = !ctx_.lockGuides; });
    lockGuides->setCheckable(true);
    guides->addAction(tr("Add Center Guides"), this, [this] {
        Composition& c = ctx_.comp();
        c.guides.push_back({true, c.width * 0.5f});
        c.guides.push_back({false, c.height * 0.5f});
        ctx_.touch();
    });
    QAction* clearGuides = guides->addAction(tr("Clear Guides"), this, [this] {
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
    QAction* meters = view->addAction(tr("Audio Meters"), this, [this](bool on) {
        settings().showMeters = on;
        saveSettings();
        meters_->setVisible(on);
    });
    meters->setCheckable(true);
    meters->setChecked(settings().showMeters);
    connect(view, &QMenu::aboutToShow, meters, [meters] { meters->setChecked(settings().showMeters); });
    QMenu* panels = view->addMenu(tr("Panels"));
    view->addAction(tr("Tools..."), this, [this] { showTools(); });
    for (const char* name : {"Objects", "Media", "Scopes", "Properties", "Effects", "AnimateLeft", "Color", "Timeline"})
        panels->addAction(dockWidgets_[name]->toggleViewAction());
    view->addAction(tr("Reset Layout"), this, [this] { resetLayout(); });
    view->addSeparator();
    QAction* full = action(Action::FullScreen, tr("Full Screen"), [this] { ui::toggleFullScreen(window()); });
    full->setCheckable(true);
    view->addAction(full);

    // Plugins: their commands (rebuilt when plugins change) and the plugin manager.
    QMenu* plugins = bar->addMenu(tr("Plugins"));
    auto fillPlugins = [this, plugins] {
        plugins->clear();
        plugins->setToolTipsVisible(true);
        const auto commands = PluginHost::get().commands();
        for (const PluginCommand* c : commands) {
            const std::string id = c->id;
            QAction* a = plugins->addAction(QString::fromStdString(c->name), this, [this, id] { runPluginCommand(id); });
            a->setToolTip(QString::fromStdString(c->description));
        }
        if (commands.empty()) plugins->addAction(tr("No plugin commands installed"))->setEnabled(false);
        plugins->addSeparator();
        // Scripts, like After Effects' File > Scripts: Lua files with the plugin `doc` API, the
        // bundled ones and the user's own (app data / scripts).
        QMenu* scripts = plugins->addMenu(tr("Scripts"));
        scripts->setToolTipsVisible(true);
        const std::filesystem::path userScripts = platform::appDataDir() / "scripts";
        int listed = 0;
        for (const auto& folder : {platform::assetsDir() / "scripts", userScripts}) {
            std::error_code ec;
            if (!std::filesystem::is_directory(folder, ec)) continue;
            std::vector<std::filesystem::path> files;
            for (const auto& e : std::filesystem::directory_iterator(folder, ec))
                if (e.path().extension() == ".lua") files.push_back(e.path());
            std::sort(files.begin(), files.end());
            if (listed && !files.empty()) scripts->addSeparator();
            for (const auto& file : files) {
                QAction* a = scripts->addAction(QString::fromStdString(platform::toUtf8(file.stem())), this, [this, file] { runScriptFile(file); });
                // Its first comment line says what it does.
                std::ifstream in(file);
                std::string first;
                std::getline(in, first);
                if (first.rfind("--", 0) == 0) a->setToolTip(QString::fromStdString(first.substr(first.find_first_not_of("- "))));
                ++listed;
            }
        }
        if (listed) scripts->addSeparator();
        scripts->addAction(tr("Run Script File..."), this, [this] {
            if (auto file = platform::openFileDialog(TR_NOOP("Run Script"), TR_NOOP("Lua script"), ".lua")) runScriptFile(*file);
        });
        scripts->addAction(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("Open Scripts Folder"), this, [userScripts] {
            std::error_code ec;
            std::filesystem::create_directories(userScripts, ec); // asked for: the folder is made then
            platform::revealInFileManager(userScripts);
        });
        plugins->addSeparator();
        plugins->addAction(tr("Manage Plugins..."), this, [] { ui::openSettings(ui::SettingsTab::Plugins); });
    };
    fillPlugins();
    connect(plugins, &QMenu::aboutToShow, this, fillPlugins);

    // Help: the same pages as the Help window, then the outside links.
    QMenu* help = bar->addMenu(tr("Help"));
    QAction* keys = action(Action::Help, tr("Keyboard Shortcuts"), [] { ui::openHelp(ui::HelpTab::Shortcuts); });
    keys->setIcon(ui::menuIcon(ui::Icon::Keyboard, theme::kTextDim));
    help->addAction(keys);
    help->addAction(ui::menuIcon(ui::Icon::Help, theme::kTextDim), tr("Learn bemotion"), this, [] { ui::openHelp(ui::HelpTab::Learn); });
    help->addAction(ui::menuIcon(ui::Icon::Expression, theme::kTextDim), tr("Expressions Guide"), this,
                    [] { platform::openWithDefaultApp("https://github.com/eugeniobenfica/bemotion/blob/main/docs/EXPRESSIONS.md"); });
    help->addSeparator();
    help->addAction(ui::menuIcon(ui::Icon::Logo, theme::kTextDim), tr("About bemotion"), this, [] { ui::openHelp(ui::HelpTab::About); });
    help->addAction(ui::menuIcon(ui::Icon::Star, theme::kTextDim), tr("Credits"), this, [] { ui::openHelp(ui::HelpTab::Credits); });
    help->addAction(ui::menuIcon(ui::Icon::Lock, theme::kTextDim), tr("License"), this, [] { ui::openHelp(ui::HelpTab::License); });
    help->addSeparator();
    help->addAction(ui::menuIcon(ui::Icon::Help, theme::kTextDim), tr("Report an Issue..."), this,
                    [] { platform::openWithDefaultApp("https://github.com/eugeniobenfica/bemotion/issues"); });
    help->addAction(ui::menuIcon(ui::Icon::Coffee, theme::kAccent), tr("Support bemotion (Buy Me a Coffee)..."), this,
                    [] { platform::openWithDefaultApp("https://buymeacoffee.com/eugeniobenfica"); });

    // Shortcuts without a menu entry.
    action(Action::PlayPause, tr("Play / Pause"), [this] {
        ctx_.playing = !ctx_.playing;
        ctx_.playRate = 1.0;
    });
    action(Action::ShuttleBack, tr("Play Backwards"), [this] { ctx_.shuttle(-1); });
    action(Action::ShuttleStop, tr("Stop"), [this] { ctx_.shuttle(0); });
    action(Action::ShuttleForward, tr("Play Forward"), [this] { ctx_.shuttle(1); });
    action(Action::RippleTrimStart, tr("Ripple Trim Start to Playhead"), [this] { ctx_.rippleTrimToPlayhead(ClipEdge::In); });
    action(Action::RippleTrimEnd, tr("Ripple Trim End to Playhead"), [this] { ctx_.rippleTrimToPlayhead(ClipEdge::Out); });
    action(Action::SpeedDuration, tr("Speed / Duration"), [this] { ui::showSpeedDialog(ctx_, this); });
    action(Action::PrevFrame, tr("Step Back"), [this] { ctx_.stepFrames(-settings().arrowFrames); });
    action(Action::NextFrame, tr("Step Forward"), [this] { ctx_.stepFrames(settings().arrowFrames); });
    action(Action::PrevFrames, tr("Big Step Back"), [this] { ctx_.stepFrames(-settings().bigArrowFrames); });
    action(Action::NextFrames, tr("Big Step Forward"), [this] { ctx_.stepFrames(settings().bigArrowFrames); });
    action(Action::GoToStart, tr("Go to Start"), [this] { ctx_.setTime(0.0); });
    action(Action::GoToEnd, tr("Go to End"), [this] { ctx_.setTime(ctx_.comp().duration); });
    action(Action::PrevKeyframe, tr("Previous Keyframe"), [this] { jumpToKeyframe(-1); });
    action(Action::NextKeyframe, tr("Next Keyframe"), [this] { jumpToKeyframe(+1); });
    action(Action::EasyEase, tr("Easy Ease"), [this] { TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EasyEase); });
    action(Action::EaseIn, tr("Ease In"), [this] { TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EaseIn); });
    action(Action::EaseOut, tr("Ease Out"), [this] { TimelinePanel::applyEase(ctx_, TimelinePanel::Ease::EaseOut); });

    // Right side of the menu bar: errors, settings, Export.
    auto* corner = new QWidget;
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 2, 8, 2);
    row->setSpacing(8);
    status_ = new QLabel;
    ui::setTone(status_, ui::Tone::Danger);
    row->addWidget(status_);
    auto* gear = new ui::IconButton(ui::Icon::Gear, tr("Settings"), 28);
    connect(gear, &QAbstractButton::clicked, this, [] { ui::openSettings(); });
    row->addWidget(gear);
    auto* exportButton = ui::accentButton(tr("Export"));
    exportButton->setIcon(ui::makeIcon(ui::Icon::Export, theme::kOnAccent, 18));
    exportButton->setToolTip(tr("Export video"));
    connect(exportButton, &QPushButton::clicked, this, [this] { ui::showExportDialog(ctx_, this); });
    row->addWidget(exportButton);
    bar->setCornerWidget(corner, Qt::TopRightCorner);
}

bool EditorWindow::eventFilter(QObject* watched, QEvent* event) {
    // Esc leaves the viewport-only layout. It is caught before shortcuts are (Esc also deselects).
    if (viewportFullscreen_ && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && !key->isAutoRepeat() && QApplication::activeModalWidget() == nullptr) {
            toggleViewportFullscreen();
            key->accept();
            return true;
        }
    }
    if (watched == docks_ && event->type() == QEvent::Resize) {
        placeRestoreHandles();
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
                if (!viewport_->setSpaceHeld(false)) ctx_.playing = !ctx_.playing, ctx_.playRate = 1.0; // not used to pan: play / pause
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// --- update loop ------------------------------------------------------------------------------

void EditorWindow::tick() {
    const double dt = std::min(0.25, clock_.restart() / 1000.0);
    setExpressionComposition(&ctx_.comp()); // what expressions read in panels, handles and the timeline
    auto& recorder = media::Recorder::get();
    if (recorder.recording() && recordingStart_ >= 0.0) {
        auto preview = recorder.preview();
        ctx_.time = recordingStart_ + preview.seconds;
        ctx_.playing = true;
        timeline_->setRecordingPreview(recordingStart_, recordingScope_, std::move(preview));
    } else {
        recordingStart_ = -1.0;
        timeline_->clearRecordingPreview();
        ctx_.tick(dt);
        // The picture follows the sound's clock (no drift, no audible resync), unless the
        // playhead really jumped (a loop).
        // (Sound plays only at normal speed: shuttling faster or backwards is silent.)
        if (double audio = 0.0; ctx_.playing && ctx_.playRate == 1.0 && media::AudioEngine::get().clock(audio) && std::fabs(audio - ctx_.time) < 0.5)
            ctx_.time = audio;
    }
    media::MediaManager::get().update(ctx_.comp());
    media::AudioEngine::get().update(ctx_.comp(), ctx_.time, ctx_.playing && ctx_.playRate == 1.0);

    objects_->sync();
    media_->sync();
    tools_->sync();
    viewport_->sync();
    properties_->sync();
    effects_->sync();
    // A transition or a Quick Animation just picked on the timeline: Animate in front (what it
    // is and a way to take it off; Properties has nothing to show).
    if (const std::pair<uint64_t, uint64_t> picked{ctx_.transitionLayer, ctx_.animationLayer}; picked != motionPicked_) {
        motionPicked_ = picked;
        if (ads::CDockWidget* motion = dockWidgets_["AnimateLeft"]; (picked.first || picked.second) && !motion->isClosed()) motion->setAsCurrentTab();
    }
    color_->sync();
    animations_->sync();
    scopes_->sync();
    timeline_->sync();
    if (preview_) preview_->sync();
    if (meters_ && meters_->isVisible()) {
        const auto levels = media::AudioEngine::get().takeLevels();
        meters_->feed(levels.left, levels.right, levels.clipped, static_cast<float>(dt));
    }

    ctx_.commitIfIdle(interacting());
    autosave();
    updateAreaButtons();
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
        setStatus(tr("Save failed: %1").arg(QString::fromStdString(err)));
        return;
    }
    platform::log("saved " + platform::toUtf8(ctx_.path));
    setStatus({});
    recents_.touch(ctx_.path, ctx_.project.name);
    recovery::discard(ctx_.path, ctx_.project.name);
}

void EditorWindow::saveAs() {
    const auto suggested = ctx_.path.empty()
                               ? projectsFolderOrDocuments() / platform::fromUtf8(ctx_.project.name + kProjectExtension)
                               : ctx_.path;
    auto file = platform::saveFileDialog(TR_NOOP("Save Project As"), suggested, TR_NOOP("bemotion Project"), kProjectExtension);
    if (!file) return;
    std::string err;
    if (!ctx_.saveTo(*file, &err)) {
        setStatus(tr("Save failed: %1").arg(QString::fromStdString(err)));
        return;
    }
    setStatus({});
    recents_.touch(ctx_.path, ctx_.project.name);
    recovery::discard(ctx_.path, ctx_.project.name);
}

bool EditorWindow::confirmClose() {
    ctx_.commitIfIdle(false);
    if (!ctx_.isDirty()) return true;
    QMessageBox box(this);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("Save changes to \"%1\" before closing?").arg(QString::fromStdString(ctx_.project.name)));
    box.setInformativeText(tr("Your changes will be lost if you don't save them."));
    QPushButton* saveButton = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    saveButton->setProperty("accent", true);
    box.addButton(tr("Don't Save"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
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
        setStatus(tr("Arrange: %1").arg(QString::fromStdString(error)));
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
    if (ctx_.revision() != seenRevision_) { // a change: wait for the next pause
        seenRevision_ = ctx_.revision();
        sinceChange_.restart();
        return;
    }
    // Every change, once it is done (not mid-drag, not mid-word: a short pause after it). A
    // project never saved has no file yet: recovery keeps it until the user saves it.
    if (settings().autosaveMinutes <= 0 || ctx_.path.empty() || !ctx_.isDirty() || interacting() || ctx_.playing ||
        media::Recorder::get().recording() || sinceChange_.elapsed() < 700 || triedRevision_ == ctx_.revision())
        return;
    triedRevision_ = ctx_.revision();
    save();
}

void EditorWindow::openPreview() {
    if (preview_) {
        preview_->close();
        return;
    }
    if (viewportFullscreen_) toggleViewportFullscreen();
    preview_ = new ui::PreviewWindow(ctx_, this);
    // On the screen the editor is on; from the playhead (from the start when it is at the end).
    if (QScreen* screen = window()->screen()) preview_->setGeometry(screen->geometry());
    const Composition& c = ctx_.comp();
    if (ctx_.time >= c.timeOfFrame(c.frameCount() - 1) - 1e-6) ctx_.setTime(0.0);
    preview_->showFullScreen();
    preview_->activateWindow();
    preview_->setFocus();
    ctx_.playing = true;
}

void EditorWindow::exportLottieFile() {
    const std::string stem = ctx_.project.name.empty() ? std::string("animation") : ctx_.project.name;
    const std::filesystem::path folder = ctx_.path.empty() ? std::filesystem::path() : ctx_.path.parent_path();
    const auto file = platform::saveFileDialog(TR_NOOP("Export Lottie"), folder / platform::fromUtf8(stem + ".json"), TR_NOOP("Lottie animation"), "json");
    if (!file) return;
    std::vector<std::string> leftOut;
    std::string error;
    if (!exportLottie(ctx_.comp(), *file, &leftOut, &error)) {
        QMessageBox::warning(this, tr("Export Lottie"), QString::fromStdString(error));
        return;
    }
    if (leftOut.empty()) {
        setStatus(tr("Exported %1").arg(QString::fromStdString(platform::toUtf8(file->filename()))));
        return;
    }
    // What Lottie has no place for, said once.
    QString list;
    for (const std::string& what : leftOut) list += QStringLiteral("\n•  ") + QString::fromStdString(what);
    QMessageBox::information(this, tr("Export Lottie"),
                             tr("Exported %1. Lottie has no place for these, left out:%2").arg(QString::fromStdString(platform::toUtf8(file->filename())), list));
}

void EditorWindow::exportFrame(bool toClipboard) {
    // The frame at the playhead as it exports (full size, through the camera, no guides).
    const Composition& c = ctx_.comp();
    const double t = c.snapToFrame(ctx_.time);
    std::vector<uint8_t> png;
    std::string error;
    if (!renderFramesPng(c, {t}, c.width, png, error)) {
        setStatus(tr("Could not render the frame: %1").arg(QString::fromStdString(error)));
        return;
    }
    if (toClipboard) {
        QImage image;
        image.loadFromData(png.data(), static_cast<int>(png.size()), "PNG");
        QApplication::clipboard()->setImage(image);
        setStatus(tr("Frame %1 copied").arg(c.frameAt(t)));
        return;
    }
    const std::string stem = (ctx_.project.name.empty() ? tr("Frame").toStdString() : ctx_.project.name) + tr(" - frame ").toStdString() + std::to_string(c.frameAt(t));
    const std::filesystem::path folder = ctx_.path.empty() ? projectsFolderOrDocuments() : ctx_.path.parent_path();
    const auto file = platform::saveFileDialog(TR_NOOP("Export Frame"), folder / platform::fromUtf8(stem + ".png"), TR_NOOP("PNG Image"), "png");
    if (!file) return;
    std::ofstream out(*file, std::ios::binary | std::ios::trunc);
    if (!out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())))
        setStatus(tr("Could not save %1").arg(QString::fromStdString(platform::toUtf8(*file))));
}

void EditorWindow::markRecovered() {
    ctx_.touch(); // unsaved until the user saves
    setStatus(tr("Recovered: save to keep the changes"));
}

void EditorWindow::runPluginCommand(const std::string& id, bool pressed) {
    auto& recorder = media::Recorder::get();
    const bool wasRecording = recorder.recording();
    const double start = ctx_.time;
    std::string error;
    ctx_.playing = false;
    platform::log("running plugin command " + id);
    PluginHost::get().setProjectFolder(ctx_.path.empty() ? std::filesystem::path() : ctx_.path.parent_path());
    CommandOutcome outcome;
    if (!PluginHost::get().runCommand(id, ctx_.project, ctx_.time, ctx_.selection, automationEnvironment(), error, pressed, &outcome)) {
        platform::log("plugin command failed: " + error);
        setStatus(tr("Plugin: %1").arg(QString::fromStdString(error)));
        timeline_->setPluginButtonPressed(id, false); // a toggle that failed stays up
        if (!wasRecording && recorder.recording()) recorder.cancel();
        if (!recorder.recording()) timeline_->clearRecordingPreview();
        return;
    }
    if (!wasRecording && recorder.recording()) {
        recordingStart_ = start;
        // The Recorder plugin imports its finished clip into the root composition.
        recordingScope_ = 0;
        ctx_.scope = recordingScope_;
        timeline_->setRecordingPreview(start, recordingScope_, recorder.preview());
    } else if (!recorder.recording()) {
        recordingStart_ = -1.0;
        timeline_->clearRecordingPreview();
    }
    if (outcome.changed) {
        ctx_.pruneSelection();
        ctx_.touch(); // one undo step
    }
    if (outcome.play >= 0) ctx_.playing = outcome.play == 1;
    setStatus({});
}

void EditorWindow::runScriptFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream code;
    code << in.rdbuf();
    if (!in) {
        setStatus(tr("Could not read the script"));
        return;
    }
    ctx_.playing = false;
    // On a copy: a script that fails half-way leaves the project as it was.
    Project copy = ctx_.project;
    std::vector<std::string> log;
    std::string error;
    const std::string name = platform::toUtf8(file.filename());
    platform::log("running script " + name);
    if (!PluginHost::runScript(code.str(), name, copy, ctx_.time, automationEnvironment(), log, error, ctx_.selection)) {
        QMessageBox::warning(this, tr("Script"), QString::fromStdString(name + ":\n" + error));
        return;
    }
    if (serializeProject(copy) != serializeProject(ctx_.project)) {
        ctx_.project.comp = std::move(copy.comp);
        ctx_.pruneSelection();
        ctx_.touch(); // one undo step
    }
    std::string said;
    for (const std::string& line : log) said += (said.empty() ? "" : "  ·  ") + line;
    setStatus(said.empty() ? tr("Ran %1").arg(QString::fromStdString(platform::toUtf8(file.stem()))) : QString::fromStdString(said)); // what it print()ed, if anything
}

void EditorWindow::dropFiles(const std::vector<std::filesystem::path>& files, bool onMediaPanel) {
    // Like After Effects: dropped on the Media panel they are only imported; anywhere else they
    // also become layers at the playhead.
    for (const auto& f : files) platform::log("importing " + platform::toUtf8(f));
    media_->importFiles(files, !onMediaPanel);
}

} // namespace bemotion
