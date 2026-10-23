#pragma once

#include "core/Layer.h"

#include <QScrollArea>

#include <functional>
#include <string>
#include <vector>

class QVBoxLayout;

namespace bemotion {

class EditorContext;
class CollapsibleSection;
class PropertyRow;

// Right panel: every property of the selected layer (or the composition when
// nothing is selected), with keyframe toggles and quick animation presets
// (effects have their own tab). Rebuilt when the selection or the layer's
// structure changes; otherwise only the values refresh.
class PropertiesPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit PropertiesPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync();

private:
    std::string structureKey();
    void rebuild();
    void buildComposition(QVBoxLayout* col);
    void buildLayer(QVBoxLayout* col, Layer& layer);
    void buildMediaSource(QVBoxLayout* col, Layer& layer);
    void buildPluginData(QVBoxLayout* col, Layer& layer);
    void buildPresets(QVBoxLayout* col, Layer& layer);

    EditorContext& ctx_;
    std::string key_;
    std::vector<PropertyRow*> rows_;
    std::vector<std::function<void()>> refreshers_; // non-animatable widgets
    std::string presetMessage_;
};

} // namespace bemotion
