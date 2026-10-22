#pragma once

#include "core/Layer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

struct Composition {
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    double duration = 10.0; // seconds
    Color background = Color::fromHex(0x101012);

    std::vector<Layer> layers; // index 0 is the top-most layer
    uint64_t nextLayerId = 1;

    double frameDuration() const { return 1.0 / fps; }
    int frameCount() const;
    int frameAt(double t) const;
    double timeOfFrame(int frame) const { return frame / fps; }
    double snapToFrame(double t) const { return timeOfFrame(frameAt(t)); }
    Vec2 center() const { return {width * 0.5f, height * 0.5f}; }

    // Inserts a layer at `index` (default: top), assigning it a fresh id.
    Layer& addLayer(Layer layer, int index = 0);
    Layer* findLayer(uint64_t id);
    const Layer* findLayer(uint64_t id) const;
    int indexOf(uint64_t id) const;
    void removeLayer(uint64_t id);
    std::string uniqueLayerName(const std::string& base) const;

    // Moves `ids` as one block, keeping their relative order, so the block
    // starts at `targetIndex` of the list *without* the moved layers.
    void moveLayers(const std::vector<uint64_t>& ids, int targetIndex);
    // Moves each of `ids` `delta` steps in the stack (negative = towards the top).
    void shiftLayers(const std::vector<uint64_t>& ids, int delta);
    // Cuts a layer in two at time `t`. The original keeps [in, t), a copy
    // placed right above it gets [t, out). Returns the copy's id, or 0 when
    // `t` is not strictly inside the layer.
    uint64_t splitLayer(uint64_t id, double t);
};

struct Project {
    static constexpr int kFormatVersion = 1;

    std::string name = "Untitled";
    Composition comp;
};

} // namespace bemotion
