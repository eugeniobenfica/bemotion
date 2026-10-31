#include "ui/EditorWindow.h"

// The editor window's menus and keyboard shortcuts (the rest: EditorWindow.cpp).

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
#include "ui/Modules.h"
#include "ui/PluginInstall.h"
#include "ui/Theme.h"
#include "ui/PreviewWindow.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/ColorPanel.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ScopesPanel.h"
#include "ui/panels/ToolSettingsPanel.h"
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
#include "ui/EditorInternal.h"

namespace bemotion {

using ui::Action;

using namespace editor_internal;

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
    // In a row with the same margins as the right corner, so the mark sits centered on the
    // menu titles, not against the window's edge.
    auto* homeCorner = new QWidget;
    auto* homeRow = new QHBoxLayout(homeCorner);
    homeRow->setContentsMargins(8, 2, 2, 2);
    homeRow->addWidget(home, 0, Qt::AlignVCenter);
    bar->setCornerWidget(homeCorner, Qt::TopLeftCorner);

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
    importSvg_ = file->addAction(ui::menuIcon(ui::Icon::Pen, theme::kTextDim), tr("Import SVG..."), this, [this] {
        // A drawing: shape layers at the playhead, each outline a path (core/Svg.h).
        if (auto f = platform::openFileDialog(TR_NOOP("Import SVG"), TR_NOOP("SVG drawing"), ".svg")) media_->importFiles({*f}, true);
    });
    file->addAction(tr("Show in Folder"), this, [this] {
        if (!ctx_.path.empty()) platform::revealInFileManager(ctx_.path);
    });
    file->addSeparator();
    file->addAction(action(Action::Export, tr("Export Video..."), [this] { ui::showExportDialog(ctx_, this); }));
    file->addAction(action(Action::ExportFrame, tr("Export Frame..."), [this] { exportFrame(); }));
    exportLottie_ = file->addAction(tr("Export Lottie (JSON)..."), this, [this] { exportLottieFile(); });
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
        if (ctx_.pickingSample()) ctx_.samplePick = nullptr; // a sample being picked: no more
        else if (ctx_.tool != Tool::Select) ctx_.tool = Tool::Select;
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
        ui::addAudioFixes(scratch, ctx_, this);
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
    layer->addAction(action(Action::BrushTool, tr("Brush Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Brush ? Tool::Select : Tool::Brush; }));
    layer->addAction(action(Action::EraserTool, tr("Eraser Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Eraser ? Tool::Select : Tool::Eraser; }));
    layer->addAction(action(Action::PuppetTool, tr("Puppet Pin Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Puppet ? Tool::Select : Tool::Puppet; }));
    layer->addAction(action(Action::WarpTool, tr("Warp Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Warp ? Tool::Select : Tool::Warp; }));
    layer->addAction(action(Action::TrackTool, tr("Track Point Tool"), [this] { ctx_.tool = ctx_.tool == Tool::Track ? Tool::Select : Tool::Track; }));
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
        const std::pair<Action, Tool> tools[] = {{Action::PenTool, Tool::Pen},       {Action::RippleTool, Tool::Ripple},
                                                 {Action::RollTool, Tool::Roll},     {Action::SlipTool, Tool::Slip},
                                                 {Action::BrushTool, Tool::Brush},   {Action::EraserTool, Tool::Eraser},
                                                 {Action::PuppetTool, Tool::Puppet}, {Action::WarpTool, Tool::Warp},
                                                 {Action::TrackTool, Tool::Track}};
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
    for (const char* name : {"Objects", "Media", "Scopes", "Properties", "Effects", "AnimateLeft", "Color", "ToolSettings", "Timeline"})
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
    exportButton->setIcon(ui::makeIcon(ui::Icon::Export, theme::kOnAccent, 20));
    exportButton->setIconSize(QSize(20, 20)); // the glyph is ~60% of it: as tall as the word beside it
    exportButton->setFixedHeight(28);         // as tall as the gear beside it, not the whole bar
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

} // namespace bemotion
