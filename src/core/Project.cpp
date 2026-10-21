#include "core/Project.h"

#include <algorithm>
#include <cmath>

namespace mirage {

int Composition::frameCount() const { return std::max(1, static_cast<int>(std::lround(duration * fps))); }

int Composition::frameAt(double t) const {
    // Small bias so times exactly on a frame boundary never round down.
    return std::clamp(static_cast<int>(std::floor(t * fps + 1e-6)), 0, frameCount() - 1);
}

Layer& Composition::addLayer(Layer layer, int index) {
    layer.id = nextLayerId++;
    index = std::clamp(index, 0, static_cast<int>(layers.size()));
    return *layers.insert(layers.begin() + index, std::move(layer));
}

Layer* Composition::findLayer(uint64_t id) {
    for (auto& l : layers)
        if (l.id == id) return &l;
    return nullptr;
}

const Layer* Composition::findLayer(uint64_t id) const {
    for (const auto& l : layers)
        if (l.id == id) return &l;
    return nullptr;
}

int Composition::indexOf(uint64_t id) const {
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i].id == id) return static_cast<int>(i);
    return -1;
}

void Composition::removeLayer(uint64_t id) {
    std::erase_if(layers, [id](const Layer& l) { return l.id == id; });
}

std::string Composition::uniqueLayerName(const std::string& base) const {
    auto taken = [&](const std::string& n) {
        return std::any_of(layers.begin(), layers.end(), [&](const Layer& l) { return l.name == n; });
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
}

} // namespace mirage
