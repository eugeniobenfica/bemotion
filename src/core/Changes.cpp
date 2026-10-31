#include "core/Changes.h"

#include "core/Serialization.h"

#include <functional>
#include <limits>
#include <string>
#include <unordered_map>

namespace bemotion {

namespace {

uint64_t hashOf(const std::string& text) { return std::hash<std::string>{}(text); }

bool hasExpression(Layer& layer) {
    for (const PropertyRef& p : layer.properties())
        if (p.anim->hasExpression()) return true;
    return false;
}

} // namespace

CompositionPrint printOf(const Composition& comp) {
    CompositionPrint print;
    // Everything of the composition but its layers and what is not drawn (markers, guides,
    // folders, names): the project file's text of it, with no layers.
    Project shell;
    shell.comp.width = comp.width;
    shell.comp.height = comp.height;
    shell.comp.fps = comp.fps;
    shell.comp.background = comp.background;
    shell.comp.media = comp.media;
    for (MediaItem& m : shell.comp.media)
        m.folder.clear(), m.name.clear();
    print.settings = hashOf(serializeProject(shell));
    print.layers.reserve(comp.layers.size());
    for (const Layer& l : comp.layers) {
        print.expressions =
            print.expressions || hasExpression(const_cast<Layer&>(l)); // properties() needs mutable layers
        print.layers.push_back({l.id, hashOf(serializeLayer(l)), l.shownFrom(), l.shownUntil()});
    }
    return print;
}

std::vector<TimeSpan> changedSpans(const CompositionPrint& before, const CompositionPrint& after) {
    const std::vector<TimeSpan> everything{{0.0, std::numeric_limits<double>::infinity()}};
    if (before.settings != after.settings || before.expressions || after.expressions) return everything;
    // Layers drawn in another order change every moment where both show: simpler to say all.
    std::vector<uint64_t> orderBefore, orderAfter;
    std::unordered_map<uint64_t, const CompositionPrint::LayerPrint*> old;
    for (const auto& l : before.layers)
        old[l.id] = &l;
    for (const auto& l : before.layers)
        orderBefore.push_back(l.id);
    std::vector<TimeSpan> spans;
    for (const auto& l : after.layers) {
        auto it = old.find(l.id);
        if (it == old.end()) {
            spans.push_back({l.from, l.until}); // added
            continue;
        }
        orderAfter.push_back(l.id);
        if (it->second->hash != l.hash) {
            spans.push_back({it->second->from, it->second->until});
            spans.push_back({l.from, l.until});
        }
        old.erase(it);
    }
    for (const auto& [id, l] : old)
        spans.push_back({l->from, l->until}); // removed
    std::erase_if(orderBefore, [&](uint64_t id) { return old.count(id) > 0; });
    if (orderBefore != orderAfter) return everything;
    return spans;
}

} // namespace bemotion
