#include "ui/panels/ObjectsPanel.h"

#include "app/PluginHost.h"
#include "app/Settings.h"
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

constexpr int kTileWidth = 84; // narrowest tile that fits "Background"

// A category of tiles, as Effects and Animate have them (ui::CategoryHeader over a CategoryBody):
// as many columns as fit, reflowed at every width; open or closed, remembered. A closed one
// keeps its tiles in a grid that is never shown.
struct Section {
    ui::ResponsiveGrid* grid;
    ui::CategoryHeader* header;
    int count = 0;
    void add(QWidget* w) {
        grid->addWidget(w);
        header->setCount(++count);
    }
};

} // namespace

ObjectsPanel::ObjectsPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

void ObjectsPanel::sync() {
    if (PluginHost::get().objectTypes().size() != pluginTypes_) rebuild();
}

void ObjectsPanel::rebuild() {
    const auto types = PluginHost::get().objectTypes();
    pluginTypes_ = types.size();

    content_ = new QWidget;
    auto* col = new QVBoxLayout(content_);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    auto section = [&](const std::string& key, ui::Icon icon, const QString& title) {
        const auto& shut = settings().closedObjectCategories;
        const bool closed = std::find(shut.begin(), shut.end(), key) != shut.end();
        auto* header = new ui::CategoryHeader(icon, title, 0, closed);
        header->setToolTip(tr("Click to open or close it"));
        header->toggled = [this, key] {
            std::vector<std::string>& s = settings().closedObjectCategories;
            if (std::find(s.begin(), s.end(), key) != s.end()) std::erase(s, key);
            else s.push_back(key);
            saveSettings();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        };
        col->addWidget(header);
        auto* grid = new ui::ResponsiveGrid(kTileWidth, 8);
        if (closed) {
            grid->setParent(content_); // kept (the tiles are made anyway), never shown
            grid->hide();
        } else {
            auto* body = new ui::CategoryBody(grid);
            col->addWidget(body);
        }
        return Section{grid, header};
    };
    auto makeTile = [&](ui::Icon icon, const QString& label, const QString& tip, int kind, std::function<void()> add) {
        auto* t = new ui::Tile(icon, label);
        t->setTileSize({kTileWidth, 62});
        t->setToolTip(tip);
        t->setDragData([kind] { return ui::makeMime(ui::kObjectMime, kind); });
        connect(t, &QAbstractButton::clicked, this, std::move(add));
        return t;
    };

    Section text = section("Text", ui::Icon::Text, tr("Text"));
    text.add(makeTile(ui::Icon::Text, tr("Text"), tr("Click to add at the center, or drag onto the canvas (T)"), -1, [this] { ctx_.addText(); }));
    text.add(makeTile(ui::Icon::Counter, tr("Counter"), tr("Animated number from 0 to 100; edit its Value keyframes in Properties or the timeline"),
                      ui::kDropCounter, [this] { ctx_.addCounter(); }));
    text.add(makeTile(ui::Icon::Subtitles, tr("Subtitles"),
                      tr("Captions at the bottom of the frame, one after another: type them, import an .srt, or transcribe the speech of a clip"),
                      ui::kDropSubtitles, [this] { ctx_.addSubtitles(); }));

    Section graphics = section("Graphics", ui::Icon::Polygon, tr("Graphics"));
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) {
        const auto kind = static_cast<ShapeKind>(i);
        graphics.add(makeTile(ui::iconForShape(kind), tr(shapeKindLabel(kind)), tr("Click to add at the center, or drag onto the canvas"), i,
                              [this, kind] { ctx_.addShape(kind); }));
    }

    Section scene = section("Scene", ui::Icon::Camera, tr("Scene"));
    scene.add(makeTile(ui::Icon::Camera, tr("Camera"), tr("A camera frames the scene: animate its position, rotation and zoom"),
                       ui::kDropCamera, [this] { ctx_.addCamera(); }));
    scene.add(makeTile(ui::Icon::Background, tr("Background"),
                       tr("Fills the whole frame, under everything: a color, a gradient, a picture or a video"),
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
            sections.push_back(section("plugin:" + type.pluginId, ui::iconForPlugin(type.icon),
                                       QString::fromStdString(plugin ? plugin->name : type.pluginId)));
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
