#include "ui/panels/ObjectsPanel.h"

#include "app/PluginHost.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QGridLayout>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>

namespace bemotion {

namespace {

constexpr int kTileWidth = 72;

// A grid of tiles under a section title.
struct Section {
    QGridLayout* grid;
    int cols;
    int count = 0;
    void add(QWidget* w) {
        grid->addWidget(w, count / cols, count % cols);
        ++count;
    }
};

} // namespace

ObjectsPanel::ObjectsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

void ObjectsPanel::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    const int cols = std::max(2, (viewport()->width() - 16) / (kTileWidth + 8));
    if (cols != columns_) rebuild();
}

void ObjectsPanel::sync() {
    if (PluginHost::get().objectTypes().size() != pluginTypes_) rebuild();
}

void ObjectsPanel::rebuild() {
    const auto types = PluginHost::get().objectTypes();
    pluginTypes_ = types.size();
    columns_ = std::max(2, (viewport()->width() - 16) / (kTileWidth + 8));
    const int tileW = std::max(56, (viewport()->width() - 16 - 8 * (columns_ - 1)) / columns_);
    const QSize tile(tileW, std::clamp(static_cast<int>(tileW * 0.78f), 50, 66));

    content_ = new QWidget;
    auto* col = new QVBoxLayout(content_);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    auto section = [&](const QString& title) {
        col->addWidget(ui::sectionLabel(title));
        auto* grid = new QGridLayout;
        grid->setSpacing(8);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        col->addLayout(grid);
        return Section{grid, columns_};
    };
    auto makeTile = [&](ui::Icon icon, const QString& label, const QString& tip, int kind, std::function<void()> add) {
        auto* t = new ui::Tile(icon, label);
        t->setTileSize(tile);
        t->setToolTip(tip);
        t->setDragData([kind] { return ui::makeMime(ui::kObjectMime, kind); });
        connect(t, &QAbstractButton::clicked, this, std::move(add));
        return t;
    };

    Section text = section("Text");
    text.add(makeTile(ui::Icon::Text, "Text", "Click to add at the center, or drag onto the canvas (T)", -1, [this] { ctx_.addText(); }));
    text.add(makeTile(ui::Icon::Counter, "Counter", "Animated number from 0 to 100; edit its Value keyframes in Properties or the timeline",
                      ui::kDropCounter, [this] { ctx_.addCounter(); }));
    text.add(makeTile(ui::Icon::Subtitles, "Subtitles",
                      "Captions at the bottom of the frame, one after another: type them, import an .srt, or transcribe the speech of a clip",
                      ui::kDropSubtitles, [this] { ctx_.addSubtitles(); }));
    col->addSpacing(4);

    Section graphics = section("Graphics");
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) {
        const auto kind = static_cast<ShapeKind>(i);
        graphics.add(makeTile(ui::iconForShape(kind), shapeKindLabel(kind), "Click to add at the center, or drag onto the canvas", i,
                              [this, kind] { ctx_.addShape(kind); }));
    }
    col->addSpacing(4);

    Section scene = section("Scene");
    scene.add(makeTile(ui::Icon::Camera, "Camera", "A camera frames the scene: animate its position, rotation and zoom",
                       ui::kDropCamera, [this] { ctx_.addCamera(); }));
    scene.add(makeTile(ui::Icon::Background, "Background",
                       "Fills the whole frame, under everything: a color, a gradient, a picture or a video",
                       ui::kDropBackground, [this] { ctx_.addBackground(); }));

    // Object types from plugins (charts, ...), grouped by plugin.
    std::string lastPlugin;
    Section* current = nullptr;
    std::vector<Section> sections;
    sections.reserve(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        const PluginObjectType& type = *types[i];
        if (type.pluginId != lastPlugin) {
            lastPlugin = type.pluginId;
            const Plugin* plugin = PluginHost::get().findPlugin(type.pluginId);
            col->addSpacing(4);
            sections.push_back(section(QString::fromStdString(plugin ? plugin->name : type.pluginId)));
            current = &sections.back();
        }
        const std::string id = type.id;
        current->add(makeTile(ui::iconForPlugin(type.icon), QString::fromStdString(type.name),
                              QString::fromStdString(type.description.empty() ? "Click to add at the center, or drag onto the canvas"
                                                                              : type.description),
                              ui::kDropPlugin - static_cast<int>(i), [this, id] {
                                  if (const PluginObjectType* t = PluginHost::get().findType(id)) ctx_.addPluginObject(*t);
                              }));
    }
    col->addStretch();
    setWidget(content_);
}

} // namespace bemotion
