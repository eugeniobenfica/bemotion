#include "ui/panels/AnimationsPanel.h"

#include "app/Platform.h"
#include "app/PresetLibrary.h"
#include "app/Settings.h"
#include "core/Presets.h"
#include "core/Transitions.h"
#include "ui/Draw.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace bemotion {

namespace {

// A picture for a Quick Animation, from its name (imported ones too).
ui::Icon presetIcon(const std::string& name) {
    auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
    if (has("Type") || has("Still")) return ui::Icon::Text;
    if (has("Blur")) return ui::Icon::FxBlur;
    if (has("Wave")) return ui::Icon::Wave;
    if (has("Jitter")) return ui::Icon::Zigzag;
    if (has("Random")) return ui::Icon::Fade;
    if (has("Jelly")) return ui::Icon::Bounce;
    if (has("Swing")) return ui::Icon::Reset;
    if (has("Glide")) return ui::Icon::ArrowDown;
    if (has("Grow")) return ui::Icon::ZoomIn;
    if (has("Shrink") || has("Zoom Out")) return ui::Icon::ZoomOut;
    if (has("Letters Out")) return ui::Icon::Fade;
    if (has("Draw")) return ui::Icon::Line;
    if (has("Spin") || has("Wobble")) return ui::Icon::Reset;
    if (has("Pop")) return ui::Icon::Burst;
    if (has("Pulse")) return ui::Icon::Ripple;
    if (has("Heartbeat")) return ui::Icon::Heartbeat;
    if (has("Shake")) return ui::Icon::Zigzag;
    if (has("Float")) return ui::Icon::Wave;
    if (has("Bounce")) return ui::Icon::Bounce;
    if (has("Zoom")) return ui::Icon::ZoomIn;
    if (has("Drop") || has("Sink")) return ui::Icon::ArrowDown;
    if (has("Up") || has("Rise")) return ui::Icon::ArrowUp;
    if (has("Slide")) return ui::Icon::ArrowRight;
    if (has("Blink")) return ui::Icon::Eye;
    if (has("Fade")) return ui::Icon::Fade;
    return ui::Icon::Ease;
}

// ... and for a transition, from its id.
ui::Icon transitionIcon(const std::string& id) {
    if (id == "fade") return ui::Icon::Fade;
    if (id == "fade_up") return ui::Icon::ArrowUp;
    if (id == "slide_left" || id == "whip_left") return ui::Icon::ArrowLeft;
    if (id == "slide_right" || id == "whip_right") return ui::Icon::ArrowRight;
    if (id == "slide_up") return ui::Icon::ArrowUp;
    if (id == "slide_down" || id == "fade_down" || id == "drop") return ui::Icon::ArrowDown;
    if (id == "zoom") return ui::Icon::ZoomIn;
    if (id == "zoom_out") return ui::Icon::ZoomOut;
    if (id == "pop" || id == "elastic") return ui::Icon::Burst;
    if (id == "stretch" || id == "squash") return ui::Icon::Ungroup;
    if (id == "spin" || id == "swing" || id == "roll") return ui::Icon::Reset;
    if (id == "turn" || id == "tilt" || id == "fly") return ui::Icon::Cube;
    return ui::Icon::Transition;
}

QLabel* faint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

// What a dragged tile is, as a folder knows its items ("t:<transition id>" / "p:<preset name>").
std::string itemKeyOf(const QMimeData* data) {
    std::string text;
    if (ui::readTextMime(data, ui::kTransitionMime, text)) return "t:" + text;
    AnimationPreset preset;
    if (ui::readTextMime(data, ui::kAnimationMime, text) && parsePreset(text, preset)) return "p:" + preset.name;
    return {};
}

// A category's picture: what its animations do (the user's own folders: a folder).
ui::Icon folderIcon(const std::string& folder) {
    if (folder == "Fade") return ui::Icon::Fade;
    if (folder == "Slide") return ui::Icon::ArrowRight;
    if (folder == "Scale") return ui::Icon::ZoomIn;
    if (folder == "Rotate") return ui::Icon::Reset;
    if (folder == "3D") return ui::Icon::Cube;
    if (folder == "Emphasis") return ui::Icon::Burst;
    if (folder == "Text") return ui::Icon::Text;
    if (folder == "Draw") return ui::Icon::Line;
    return ui::Icon::Folder;
}

bool matches(const QString& text, const QString& filter) { return filter.isEmpty() || text.contains(filter, Qt::CaseInsensitive); }

} // namespace

AnimationsPanel::AnimationsPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search animations"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    auto* top = new QHBoxLayout;
    top->setContentsMargins(8, 0, 8, 0);
    top->addWidget(search_);
    col->addLayout(top);
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col->addWidget(scroll_, 1);
    rebuild();
}

std::vector<Layer*> AnimationsPanel::shownLayers() const {
    auto& ctx = const_cast<EditorContext&>(ctx_);
    if (ctx.selection.empty()) // a transition or an animation picked on the timeline: its clip's
        for (const uint64_t id : {ctx.transitionLayer, ctx.animationLayer})
            if (Layer* l = id ? ctx.comp().findLayer(id) : nullptr) return {l};
    return ctx.selectedLayers();
}

std::string AnimationsPanel::appliedKey() const {
    std::string key = std::to_string(ctx_.transitionLayer) + (ctx_.transitionIncoming ? "i" : "o") + std::to_string(ctx_.animationLayer) +
                      "/" + std::to_string(ctx_.animationIndex) + ":";
    for (const Layer* l : shownLayers()) {
        key += std::to_string(l->id) + l->name + ":" + l->transitionIn.id + (l->transitionIn.joined ? "+" : "") + ":" + l->transitionOut.id +
               (l->transitionOut.joined ? "+" : "") + ":";
        for (const AppliedAnimation& a : l->animations) key += "|" + a.name + std::to_string(a.start);
        key += ",";
    }
    return key;
}

void AnimationsPanel::placeOnSelection(const std::string& transition, const AnimationPreset* preset, int spot) {
    Composition& comp = ctx_.comp();
    std::vector<uint64_t> ids;
    for (Layer* l : ctx_.selectedLayers()) ids.push_back(l->id);
    bool any = false;
    for (uint64_t id : ids) { // looked up each time: a cut transition reorders the stack
        Layer* l = comp.findLayer(id);
        if (!l) continue;
        const double length = preset ? presetDuration(*preset, *l) : 0.5;
        // At the playhead (inside the clip): on an end when near it.
        const double t = std::clamp(ctx_.time, l->inPoint, std::max(l->inPoint, l->outPoint - length));
        MotionSpot where = spot >= 0 ? static_cast<MotionSpot>(spot) : motionSpotAt(*l, ctx_.time, length);
        // A transition on the end that touches the next selected clip: across their cut.
        if (!preset && where == MotionSpot::Out)
            if (Layer* next = nextOnTrack(comp, *l); next && std::find(ids.begin(), ids.end(), next->id) != ids.end()) where = MotionSpot::Cut;
        any = placeMotion(comp, *l, transition, preset, where, t, where == MotionSpot::Cut ? 1.0 : length) || any;
    }
    if (any) ctx_.touch();
}

std::vector<std::string> AnimationsPanel::builtInFolders() {
    return {TR_NOOP("Fade"), TR_NOOP("Slide"), TR_NOOP("Scale"), TR_NOOP("Rotate"), "3D", TR_NOOP("Emphasis"), TR_NOOP("Text"), TR_NOOP("Draw")};
}

bool AnimationsPanel::isBuiltInFolder(const std::string& folder) {
    const auto all = builtInFolders();
    return std::find(all.begin(), all.end(), folder) != all.end() || folder == "Custom";
}

// Where a Quick Animation goes by default: with the transitions doing the same (Fade In with
// Fade), the rest by what it is.
std::string AnimationsPanel::defaultFolder(const AnimationPreset& p) {
    auto has = [&](const char* word) { return p.name.find(word) != std::string::npos; };
    if (p.category == "Letters" || p.appliesTo == "text") return "Text";
    if (has("Draw")) return "Draw";
    if (p.category == "Emphasis") return "Emphasis";
    if (p.category != "Entrance" && p.category != "Exit") return p.category.empty() ? std::string("Custom") : p.category;
    if (has("Fade")) return "Fade";
    if (has("Slide") || has("Drop") || has("Rise") || has("Glide") || has("Sink")) return "Slide";
    if (has("Pop") || has("Zoom") || has("Grow") || has("Shrink")) return "Scale";
    if (has("Spin") || has("Swing")) return "Rotate";
    return "Emphasis";
}

void AnimationsPanel::moveToFolder(const std::string& key, const std::string& folder) {
    Settings& st = settings();
    if (folder.empty()) st.animationFolderOf.erase(key);
    else st.animationFolderOf[key] = folder;
    std::erase(st.closedAnimationFolders, folder); // opened, to see it there
    saveSettings();
    QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
}

void AnimationsPanel::newFolder(const std::string& moveHere) {
    bool ok = false;
    const QString typed = QInputDialog::getText(this, tr("New Folder"), tr("Name"), QLineEdit::Normal, tr("My Animations"), &ok).trimmed();
    if (!ok || typed.isEmpty()) return;
    Settings& st = settings();
    const std::string name = typed.toStdString();
    if (std::find(st.animationFolders.begin(), st.animationFolders.end(), name) == st.animationFolders.end()) st.animationFolders.push_back(name);
    if (!moveHere.empty()) st.animationFolderOf[moveHere] = name;
    saveSettings();
    QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
}

QWidget* AnimationsPanel::folderHeader(const std::string& folder, const QString& name, int count, bool closed, bool userFolder) {
    auto* header = new ui::CategoryHeader(folderIcon(folder), name, count, closed);
    header->setToolTip(tr("Click to open or close it. Drop an animation here to move it in; right-click for folders of your own."));
    header->toggled = [this, folder] {
        Settings& st = settings();
        if (std::find(st.closedAnimationFolders.begin(), st.closedAnimationFolders.end(), folder) != st.closedAnimationFolders.end())
            std::erase(st.closedAnimationFolders, folder);
        else st.closedAnimationFolders.push_back(folder);
        saveSettings();
        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
    };
    // Something dragged from a tile onto the folder's name goes into it.
    header->accepts = [](const QMimeData* data) { return !itemKeyOf(data).empty(); };
    header->dropped = [this, folder](const QMimeData* data) { moveToFolder(itemKeyOf(data), folder); };
    header->menuRequested = [this, header, folder, userFolder](QPoint at) {
        QMenu menu;
        menu.addAction(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("New Folder..."), [this] { newFolder({}); });
        if (userFolder) {
            menu.addAction(tr("Rename..."), [this, folder] {
                bool ok = false;
                const QString typed = QInputDialog::getText(this, tr("Rename Folder"), tr("Name"), QLineEdit::Normal, QString::fromStdString(folder), &ok).trimmed();
                if (!ok || typed.isEmpty() || typed.toStdString() == folder) return;
                Settings& st = settings();
                for (std::string& f : st.animationFolders)
                    if (f == folder) f = typed.toStdString();
                for (auto& [key, f] : st.animationFolderOf)
                    if (f == folder) f = typed.toStdString();
                saveSettings();
                QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            });
            menu.addAction(ui::menuIcon(ui::Icon::Trash, theme::kDanger), tr("Delete Folder"), [this, folder] {
                Settings& st = settings();
                std::erase(st.animationFolders, folder);
                std::erase_if(st.animationFolderOf, [&](const auto& entry) { return entry.second == folder; }); // its items go back
                saveSettings();
                QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            });
        }
        menu.exec(header->mapToGlobal(at));
    };
    return header;
}

void AnimationsPanel::buildApplied(QVBoxLayout* col) {
    appliedKey_ = appliedKey();
    lengths_.clear();
    const auto shown = shownLayers();
    if (shown.size() > 1) {
        col->addWidget(new QLabel(tr("Animate  ·  %1 clips").arg(shown.size())));
        col->addWidget(faint(tr("What you click below goes to each of them.")));
        col->addSpacing(6);
        return;
    }
    if (shown.empty()) return;
    Layer* layer = shown.front();
    const uint64_t id = layer->id;
    col->addWidget(new ui::ElidedLabel(tr("Animate  ·  %1").arg(QString::fromStdString(layer->name)))); // a long name never widens the panel
    auto* card = new ui::SettingsCard;
    // A row: its length (drag it, or its band's end on the timeline) and a way to take it off.
    auto addRow = [&](const QString& name, bool picked, const QString& about, float length, int which, std::function<void()> remove) {
        auto* controls = new QWidget;
        auto* row = new QHBoxLayout(controls);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* value = new ui::DragValue;
        value->setRange(0.05f, 60.0f);
        value->setSpeed(0.01f);
        value->setFormat("%.2f s");
        value->setValue(length);
        value->setFixedWidth(84);
        value->setToolTip(tr("How long it lasts (or drag the end of its band on the timeline)"));
        connect(value, &ui::DragValue::valueChanged, this, [this, id, which](float v) {
            Layer* l = ctx_.comp().findLayer(id);
            if (!l) return;
            bool done = false;
            if (which == -1) done = resizeTransition(ctx_.comp(), *l, TransitionEdge::In, v);
            else if (which == -2) done = resizeTransition(ctx_.comp(), *l, TransitionEdge::Out, v);
            else if (which < static_cast<int>(l->animations.size()))
                done = retimeQuickAnimation(*l, static_cast<size_t>(which), l->animations[static_cast<size_t>(which)].start, v);
            if (done) ctx_.touch();
        });
        lengths_.emplace_back(value, which);
        row->addWidget(value);
        auto* trash = new ui::IconButton(ui::Icon::Trash, tr("Take it off (its keyframes go too)"), 26);
        connect(trash, &QAbstractButton::clicked, this, [this, remove] {
            remove();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        });
        row->addWidget(trash);
        card->addRow((picked ? QStringLiteral("▸ ") : QString()) + name, about, controls);
    };
    if (!canTakeTransition(*layer) && layer->animations.empty()) {
        card->addRow(tr("Nothing yet"), layer->type == LayerType::Camera ? tr("A camera takes Quick Animations (from the playhead), not ins and outs")
                                                                          : tr("Sound without a picture: fade its volume instead"),
                     nullptr);
    } else if (!layer->transitionIn.active() && !layer->transitionOut.active() && layer->animations.empty()) {
        card->addRow(tr("Nothing yet"), tr("Click one below, or drag it onto the clip"), nullptr);
    }
    auto transitionRow = [&](TransitionEdge edge) {
        const bool in = edge == TransitionEdge::In;
        const ClipTransition& clip = in ? layer->transitionIn : layer->transitionOut;
        if (!clip.active()) return;
        const TransitionInfo* info = findTransition(clip.id);
        const QString name = info ? tr(info->name) : QString::fromStdString(clip.id);
        QString about = in ? tr("In: at the clip's start") : tr("Out: at the clip's end");
        if (clip.joined) about = in ? tr("In: across the cut with the clip before") : tr("Out: across the cut with the clip after");
        addRow(name, ctx_.transitionLayer == id && ctx_.transitionIncoming == in, about, clip.duration, in ? -1 : -2, [this, id, edge] {
            if (Layer* l = ctx_.comp().findLayer(id); l && removeTransition(ctx_.comp(), *l, edge)) {
                if (ctx_.transitionLayer == id) ctx_.transitionLayer = 0;
                ctx_.touch();
            }
        });
    };
    // In time order: its in, the Quick Animations, its out.
    transitionRow(TransitionEdge::In);
    for (size_t i = 0; i < layer->animations.size(); ++i) {
        const AppliedAnimation& a = layer->animations[i];
        addRow(trs(a.name), ctx_.animationLayer == id && ctx_.animationIndex == static_cast<int>(i),
               tr("From %1 s (drag its band on the timeline)").arg(a.start - layer->inPoint, 0, 'f', 2), static_cast<float>(a.duration),
               static_cast<int>(i), [this, id, i] {
                   if (Layer* l = ctx_.comp().findLayer(id); l && removeQuickAnimation(*l, i)) {
                       if (ctx_.animationLayer == id) ctx_.animationLayer = 0;
                       ctx_.touch();
                   }
               });
    }
    transitionRow(TransitionEdge::Out);
    col->addWidget(card);
    col->addSpacing(6);
}

void AnimationsPanel::rebuild() {
    presetCount_ = PresetLibrary::all().size();
    const QString filter = search_->text().trimmed();
    Settings& st = settings();

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 4, 8, 8);
    col->setSpacing(6);
    tiles_.clear();
    dimmedFor_.clear();
    buildApplied(col);
    col->addWidget(faint(tr("Click one for the selected clips (at the playhead: near a clip's start or end, on it), or drag it "
                            "anywhere on a clip, onto its start or end, or between two clips. A dot marks what the clip has.")));

    // Everything, alike: transitions and Quick Animations, each in its folder.
    struct Entry {
        std::string key, folder;
        QString name, tip;
        ui::Icon icon;
        std::string transition;
        std::optional<AnimationPreset> preset;
        std::filesystem::path file; // the user's own preset
    };
    std::vector<Entry> entries;
    for (const TransitionInfo& info : transitions())
        entries.push_back({"t:" + std::string(info.id), info.category, tr(info.name), tr(info.about), transitionIcon(info.id), info.id, std::nullopt, {}});
    for (const auto& lp : PresetLibrary::all()) {
        // One already there by that name (a transition, which does both ways): not twice.
        if (lp.file.empty() && std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.name == trs(lp.preset.name); })) continue;
        entries.push_back({"p:" + lp.preset.name, defaultFolder(lp.preset), trs(lp.preset.name),
                           tr("%1 s%2").arg(lp.preset.duration, 0, 'g', 2).arg(lp.preset.appliesTo == "text"    ? tr(" · text")
                                                                              : lp.preset.appliesTo == "shape" ? tr(" · shapes")
                                                                                                               : QString()),
                           presetIcon(lp.preset.name), {}, lp.preset, lp.file});
    }
    // The folders: the built-in ones, imported presets' own, then the user's.
    std::vector<std::string> folders = builtInFolders();
    for (const Entry& e : entries)
        if (std::find(folders.begin(), folders.end(), e.folder) == folders.end()) folders.push_back(e.folder);
    for (const std::string& f : st.animationFolders)
        if (std::find(folders.begin(), folders.end(), f) == folders.end()) folders.push_back(f);
    for (Entry& e : entries)
        if (const auto it = st.animationFolderOf.find(e.key); it != st.animationFolderOf.end() &&
                                                              std::find(folders.begin(), folders.end(), it->second) != folders.end())
            e.folder = it->second;

    for (const std::string& folder : folders) {
        std::vector<const Entry*> inside;
        const QString folderName = isBuiltInFolder(folder) ? tr(folder.c_str()) : QString::fromStdString(folder);
        for (const Entry& e : entries)
            if (e.folder == folder && (matches(e.name, filter) || matches(QString::fromStdString(e.key.substr(2)), filter) || matches(folderName, filter)))
                inside.push_back(&e);
        const bool userFolder = std::find(st.animationFolders.begin(), st.animationFolders.end(), folder) != st.animationFolders.end();
        if (inside.empty() && (!filter.isEmpty() || !userFolder)) continue;
        const bool closed = filter.isEmpty() &&
                            std::find(st.closedAnimationFolders.begin(), st.closedAnimationFolders.end(), folder) != st.closedAnimationFolders.end();
        col->addWidget(folderHeader(folder, folderName, static_cast<int>(inside.size()), closed, userFolder));
        if (closed) continue;
        auto* grid = new ui::ResponsiveGrid(84);
        for (const Entry* e : inside) {
            auto* t = new ui::Tile(e->icon, e->name);
            t->setTileSize({84, 62});
            t->setToolTip(tr("%1\n\nClick: the selected clips, at the playhead (near a clip's start or end: on it). Drag: anywhere on a "
                             "clip, onto its start or end, or between two clips. Right-click: more places, its folder.")
                              .arg(e->tip.isEmpty() ? e->name : e->name + QStringLiteral(" · ") + e->tip));
            const std::string transition = e->transition;
            const std::optional<AnimationPreset> preset = e->preset;
            connect(t, &QAbstractButton::clicked, this, [this, transition, preset] { placeOnSelection(transition, preset ? &*preset : nullptr, -1); });
            if (preset) t->setDragData([preset] { return ui::makeTextMime(ui::kAnimationMime, presetToJson(*preset)); });
            else t->setDragData([transition] { return ui::makeTextMime(ui::kTransitionMime, transition); });
            t->setContextMenuPolicy(Qt::CustomContextMenu);
            const std::string key = e->key, current = e->folder;
            const std::filesystem::path file = e->file;
            connect(t, &QWidget::customContextMenuRequested, this, [this, t, transition, preset, key, current, file, folders](QPoint at) {
                QMenu menu;
                const AnimationPreset* p = preset ? &*preset : nullptr;
                menu.addAction(tr("At the Start"), [this, transition, p] { placeOnSelection(transition, p, static_cast<int>(MotionSpot::In)); });
                menu.addAction(tr("At the End"), [this, transition, p] { placeOnSelection(transition, p, static_cast<int>(MotionSpot::Out)); });
                menu.addAction(tr("At the Start and End"), [this, transition, p] {
                    placeOnSelection(transition, p, static_cast<int>(MotionSpot::In));
                    placeOnSelection(transition, p, static_cast<int>(MotionSpot::Out));
                });
                menu.addAction(tr("At the Playhead"), [this, transition, p] { placeOnSelection(transition, p, static_cast<int>(MotionSpot::At)); });
                menu.addSeparator();
                QMenu* move = menu.addMenu(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("Move to Folder"));
                for (const std::string& f : folders) {
                    QAction* a = move->addAction(isBuiltInFolder(f) ? tr(f.c_str()) : QString::fromStdString(f), [this, key, f] { moveToFolder(key, f); });
                    a->setCheckable(true);
                    a->setChecked(f == current);
                }
                move->addSeparator();
                move->addAction(tr("New Folder..."), [this, key] { newFolder(key); });
                if (settings().animationFolderOf.count(key)) menu.addAction(tr("Back to Its Folder"), [this, key] { moveToFolder(key, {}); });
                if (!file.empty()) { // the user's own: shown, deleted from here
                    menu.addSeparator();
                    menu.addAction(tr("Show File"), [file] { platform::revealInFileManager(file); });
                    menu.addAction(ui::menuIcon(ui::Icon::Trash, theme::kDanger), tr("Delete Preset"), [this, file] {
                        PresetLibrary::remove(file);
                        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
                    });
                }
                menu.exec(t->mapToGlobal(at));
            });
            grid->addWidget(t);
            tiles_.push_back({t, preset ? preset->name : transition, !preset});
        }
        auto* body = new ui::CategoryBody(grid); // the same as the Effects browser's categories
        if (inside.empty()) body->add(faint(tr("Empty: drag one onto the folder's name, or right-click one › Move to Folder.")));
        col->addWidget(body);
    }
    if (tiles_.empty() && !filter.isEmpty()) col->addWidget(faint(tr("Nothing matches your search.")));

    auto* row = new QHBoxLayout;
    auto* import = new QPushButton(tr("Import..."));
    import->setToolTip(tr("Add a Quick Animation from a .json preset file"));
    connect(import, &QPushButton::clicked, this, [this] {
        if (auto file = platform::openFileDialog(TR_NOOP("Import Quick Animation"), TR_NOOP("Quick Animation preset"), ".json")) {
            std::string err;
            messageText_ = PresetLibrary::importFile(*file, &err) ? "" : "Import failed: " + err;
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        }
    });
    auto* save = new QPushButton(tr("Save Selected..."));
    save->setToolTip(tr("Turn the selected layer's keyframes into a Quick Animation of your own"));
    connect(save, &QPushButton::clicked, this, [this] {
        Layer* l = ctx_.selected();
        if (!l) {
            messageText_ = "Select a layer with keyframes first.";
            message_->setText(QString::fromStdString(messageText_));
            message_->show();
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Quick Animation"), tr("Preset name"), QLineEdit::Normal,
                                                   QString::fromStdString(l->name + " animation"), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        AnimationPreset preset = presetFromLayer(*l, l->inPoint, l->outPoint, name.toStdString());
        std::string err;
        if (preset.tracks.empty()) messageText_ = "This layer has no keyframes to save.";
        else if (PresetLibrary::save(preset, &err)) messageText_ = "Saved \"" + name.toStdString() + "\" (Custom).";
        else messageText_ = err;
        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
    });
    row->addWidget(import);
    row->addWidget(save);
    col->addSpacing(4);
    col->addLayout(row);
    message_ = new QLabel(QString::fromStdString(messageText_));
    message_->setWordWrap(true);
    ui::setTone(message_, ui::Tone::TextDim);
    col->addWidget(message_);
    message_->setVisible(!messageText_.empty());
    col->addStretch();
    const int scrolled = scroll_->verticalScrollBar() ? scroll_->verticalScrollBar()->value() : 0;
    scroll_->setWidget(content);
    scroll_->verticalScrollBar()->setValue(scrolled);
    sync();
}

void AnimationsPanel::sync() {
    if (!isVisible()) return;
    if (PresetLibrary::all().size() != presetCount_ || appliedKey() != appliedKey_) { // imported or saved elsewhere; the clip's list
        rebuild();
        return;
    }
    const auto shown = shownLayers();
    // Lengths changed elsewhere (dragged on the timeline, undone) show here.
    if (shown.size() == 1)
        for (auto& [value, which] : lengths_) {
            if (value->editing()) continue;
            const Layer* l = shown.front();
            if (which == -1) value->setValue(l->transitionIn.duration);
            else if (which == -2) value->setValue(l->transitionOut.duration);
            else if (which < static_cast<int>(l->animations.size())) value->setValue(static_cast<float>(l->animations[static_cast<size_t>(which)].duration));
        }
    // Dimmed when no selected clip can take it (nothing selected: they can still be dragged); a
    // dot on what the clips have. (Looked at again only when the clips or what they have change.)
    if (dimmedFor_ == appliedKey_ && dimmedTiles_ == tiles_.size()) return;
    dimmedFor_ = appliedKey_;
    dimmedTiles_ = tiles_.size();
    for (const TileInfo& info : tiles_) {
        bool fits = shown.empty(), has = false;
        const LibraryPreset* lp = nullptr;
        if (!info.transition)
            for (const auto& p : PresetLibrary::all())
                if (p.preset.name == info.id) lp = &p;
        for (const Layer* l : shown) {
            if (info.transition) {
                fits = fits || canTakeTransition(*l, info.id);
                has = has || l->transitionIn.id == info.id || l->transitionOut.id == info.id;
            } else {
                fits = fits || (lp && presetAppliesTo(lp->preset, *l));
                for (const AppliedAnimation& a : l->animations) has = has || a.name == info.id;
            }
        }
        info.tile->setDimmed(!fits);
        info.tile->setMarked(has);
    }
}

} // namespace bemotion
