#pragma once

#include "core/Layer.h"

#include <QScrollArea>

#include <functional>
#include <string>
#include <vector>

class QComboBox;
class QVBoxLayout;

namespace bemotion {

class EditorContext;
class CollapsibleSection;
class PropertyRow;

// The Color tab (next to Transitions), like Premiere's Lumetri Color: the selected layer's
// grading in one place, in the order a colorist works: brightness and contrast, hue and
// saturation, a look (LUT) and curves. Each part is an effect of the layer's stack (the Effects
// tab shows them too): a part the layer does not have yet shows its default values, and the
// first change adds the effect. Scopes open from here.
class ColorPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ColorPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync();

signals:
    void showScopesRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    std::string structureKey();
    void rebuild();
    // One part: its effect's rows when the layer has it, else editable defaults that add it.
    void buildPart(QVBoxLayout* col, Layer& layer, EffectType type);
    void buildLook(CollapsibleSection* section, Layer& layer, int index);
    void buildCurves(CollapsibleSection* section, Layer& layer, int index);
    // The layer's first effect of `type`, added when `create` (before the parts that come after
    // it in the panel).
    Effect* effectOf(uint64_t layerId, EffectType type, bool create);

    EditorContext& ctx_;
    std::string key_;
    bool editing_ = false; // a default being dragged or typed: its rows wait
    std::vector<PropertyRow*> rows_;
    std::vector<std::function<void()>> refreshers_;
};

namespace ui {
// The LUT choices (None, the bundled looks, the current file, Choose File...) with `file` shown.
void fillLutChoices(QComboBox* combo, const std::string& file);
// The file the choice at `index` stands for (a dialog for Choose File...); false when the user
// cancels it.
bool lutOfChoice(QComboBox* combo, int index, std::string& file);
} // namespace ui

} // namespace bemotion
