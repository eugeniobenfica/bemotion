#include "ui/panels/AnimationsPanel.h"

#include "app/Platform.h"
#include "app/PresetLibrary.h"
#include "core/Presets.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace bemotion {

namespace {

// A picture for a preset, from its name (imported ones too).
ui::Icon iconOf(const std::string& name) {
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

} // namespace

AnimationsPanel::AnimationsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

void AnimationsPanel::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    if (std::max(2, (viewport()->width() - 16) / 92) != columns_) rebuild();
}

void AnimationsPanel::rebuild() {
    columns_ = std::max(2, (viewport()->width() - 16) / 92);
    const int tileW = std::max(56, (viewport()->width() - 16 - 8 * (columns_ - 1)) / columns_);
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.78f), 50, 66));
    presetCount_ = PresetLibrary::all().size();

    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    tiles_.clear();
    auto* about = new QLabel(tr("Quick Animations: click one for the selected layers (it starts at the playhead), or drag it onto a clip."));
    about->setWordWrap(true);
    ui::setTone(about, ui::Tone::TextFaint);
    col->addWidget(about);

    // Built-in categories first, then whatever imported presets use.
    std::vector<std::string> categories = {TR_NOOP("Entrance"), TR_NOOP("Emphasis"), TR_NOOP("Exit"), TR_NOOP("Letters")};
    for (const auto& lp : PresetLibrary::all())
        if (std::find(categories.begin(), categories.end(), lp.preset.category) == categories.end()) categories.push_back(lp.preset.category);
    for (const std::string& category : categories) {
        QGridLayout* grid = nullptr;
        int n = 0;
        for (const auto& lp : PresetLibrary::all()) {
            if (lp.preset.category != category) continue;
            if (!grid) {
                col->addSpacing(4);
                col->addWidget(ui::sectionLabel(QString::fromStdString(category)));
                grid = new QGridLayout;
                grid->setSpacing(8);
                grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                col->addLayout(grid);
            }
            const AnimationPreset preset = lp.preset;
            auto* t = new ui::Tile(iconOf(preset.name), trs(preset.name));
            t->setTileSize(tile);
            t->setToolTip(tr("%1 · %2 s%3\n\nClick: the selected layers, from the playhead. Or drag it onto a clip.")
                              .arg(trs(preset.name))
                              .arg(preset.duration, 0, 'g', 2)
                              .arg(preset.appliesTo == "text" ? tr(" · text") : preset.appliesTo == "shape" ? tr(" · shapes") : QString()));
            connect(t, &QAbstractButton::clicked, this, [this, preset] {
                bool any = false;
                for (Layer* l : ctx_.selectedLayers())
                    if (presetAppliesTo(preset, *l)) applyQuickAnimation(preset, *l, ctx_.time), any = true;
                if (any) ctx_.touch();
            });
            t->setDragData([preset] { return ui::makeTextMime(ui::kAnimationMime, presetToJson(preset)); });
            if (!lp.file.empty()) { // the user's own: shown, deleted from here
                const std::filesystem::path file = lp.file;
                t->setMarked(true);
                t->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(t, &QWidget::customContextMenuRequested, this, [this, t, file](QPoint p) {
                    QMenu menu;
                    menu.addAction(tr("Show File"), [file] { platform::revealInFileManager(file); });
                    menu.addAction(ui::menuIcon(ui::Icon::Trash, theme::kDanger), tr("Delete Preset"), [this, file] {
                        PresetLibrary::remove(file);
                        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
                    });
                    menu.exec(t->mapToGlobal(p));
                });
            }
            grid->addWidget(t, n / columns_, n % columns_);
            tiles_.emplace_back(t, preset.name);
            ++n;
        }
    }

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
    setWidget(content);
    sync();
}

void AnimationsPanel::sync() {
    if (!isVisible()) return;
    if (PresetLibrary::all().size() != presetCount_) { // imported or saved elsewhere
        rebuild();
        return;
    }
    // Dimmed when no selected layer can take it (nothing selected: they can still be dragged).
    const auto selected = ctx_.selectedLayers();
    for (auto& [tile, name] : tiles_) {
        const LibraryPreset* lp = nullptr;
        for (const auto& p : PresetLibrary::all())
            if (p.preset.name == name) lp = &p;
        bool fits = selected.empty();
        for (Layer* l : selected) fits = fits || (lp && presetAppliesTo(lp->preset, *l));
        tile->setDimmed(!fits);
    }
}

} // namespace bemotion
