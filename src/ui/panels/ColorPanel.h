#pragma once

#include "core/Layer.h"

#include <QImage>
#include <QScrollArea>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QVBoxLayout;

namespace bemotion {

class EditorContext;
class CollapsibleSection;
class PropertyRow;

// The Color tab (next to Transitions), like Premiere's Lumetri Color: the selected layer's
// grading in one place, in the order a colorist works: basic correction (brightness, contrast,
// hue, saturation, lightness on sliders whose track shows what they do), a look (LUTs as
// cards, each a swatch of what it does) and curves. Each part is an effect of the layer's
// stack (the Effects tab shows them too): a part the layer does not have yet shows its default
// values, and the first change adds the effect. Scopes open from here.
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
    void buildBasic(QVBoxLayout* col, Layer& layer);
    void buildLook(QVBoxLayout* col, Layer& layer);
    void buildCurves(QVBoxLayout* col, Layer& layer);
    // On / off and reset for the effects of a part (none: the part is at its defaults).
    QWidget* partTools(uint64_t layerId, const std::vector<EffectType>& types);
    // The layer's first effect of `type`, added when `create` (before the parts that come after
    // it in the panel).
    Effect* effectOf(uint64_t layerId, EffectType type, bool create);
    QImage swatch(const std::string& lut); // a look's card picture (cached)

    EditorContext& ctx_;
    std::string key_;
    bool editing_ = false; // a default being dragged or typed: its rows wait
    std::vector<PropertyRow*> rows_;
    std::vector<std::function<void()>> refreshers_;
    std::map<std::string, QImage> swatches_;
};

namespace ui {
// The LUT choices (None, the bundled looks, the current file, Choose File...) with `file` shown.
void fillLutChoices(QComboBox* combo, const std::string& file);
// The file the choice at `index` stands for (a dialog for Choose File...); false when the user
// cancels it.
bool lutOfChoice(QComboBox* combo, int index, std::string& file);
} // namespace ui

} // namespace bemotion
