#include "core/Project.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace bemotion {

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

void Composition::moveLayers(const std::vector<uint64_t>& ids, int targetIndex) {
    std::vector<Layer> moving, rest;
    for (auto& l : layers)
        (std::find(ids.begin(), ids.end(), l.id) != ids.end() ? moving : rest).push_back(std::move(l));
    targetIndex = std::clamp(targetIndex, 0, static_cast<int>(rest.size()));
    rest.insert(rest.begin() + targetIndex, std::make_move_iterator(moving.begin()),
                std::make_move_iterator(moving.end()));
    layers = std::move(rest);
}

void Composition::shiftLayers(const std::vector<uint64_t>& ids, int delta) {
    auto selected = [&](size_t i) { return std::find(ids.begin(), ids.end(), layers[i].id) != ids.end(); };
    const int n = static_cast<int>(layers.size());
    for (int step = 0; step < std::abs(delta); ++step) {
        bool moved = false;
        if (delta < 0) {
            for (int i = 1; i < n; ++i)
                if (selected(i) && !selected(i - 1)) std::swap(layers[i], layers[i - 1]), moved = true;
        } else {
            for (int i = n - 2; i >= 0; --i)
                if (selected(i) && !selected(i + 1)) std::swap(layers[i], layers[i + 1]), moved = true;
        }
        if (!moved) break;
    }
}

uint64_t Composition::splitLayer(uint64_t id, double t) {
    const int index = indexOf(id);
    if (index < 0) return 0;
    Layer& original = layers[index];
    const double eps = frameDuration() * 0.5;
    if (t <= original.inPoint + eps || t >= original.outPoint - eps) return 0;
    Layer tail = original; // keyframes are kept on both halves so motion stays continuous
    original.outPoint = t;
    tail.inPoint = t;
    return addLayer(std::move(tail), index).id;
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

} // namespace bemotion
