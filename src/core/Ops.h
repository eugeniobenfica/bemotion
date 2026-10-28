#pragma once

// Operations: the one way to change a project from outside the interface.
//
// An operation is a small JSON object naming what to do:
//
//   { "op": "layer.add", "type": "text", "text": "Hello", "position": [960, 540] }
//   { "op": "key.add", "layer": "Hello", "prop": "opacity", "time": 0, "value": 0, "easing": "ease_out" }
//
// Plugins (Lua), the command line and AI tools all send operations; the same
// rules apply to every one of them. A batch (an array, or {"ops": [...]}) is
// atomic: it runs on a copy of the project and replaces the original only if
// every operation succeeded and the result is valid (core/Validate.h). The
// interface records an applied batch as one undo step.
//
// The full reference, with every operation and its fields, is docs/OPERATIONS.md.

#include "core/Project.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace bemotion {

inline constexpr int kOpsVersion = 1;

// What the core cannot do on its own: reading media files and building plugin
// objects need modules above it. Unset hooks make those operations fail politely.
struct OpEnvironment {
    // Probes a file: fills `out` (kind, size, duration...) or explains why not.
    std::function<bool(const std::string& path, MediaItem& out, std::string& error)> probeMedia;
    // A layer of a plugin object type ("charts.bar") with its parameters at their defaults.
    std::function<bool(const std::string& objectType, Layer& out, std::string& error)> makePluginLayer;
    // A layer's bounds in its own space (texts need the renderer), for masks made to fit it.
    std::function<bool(const Composition& comp, const Layer& layer, double t, Vec2& mn, Vec2& mx)> layerBounds;
    // A media item's sound as loudness (RMS 0..1, one value every `blockSeconds` of the source),
    // for edits that find its quiet moments.
    std::function<bool(const MediaItem& item, std::vector<float>& loudness, double& blockSeconds, std::string& error)> mediaLoudness;
};

struct OpsResult {
    bool ok = false;
    std::string error;        // which operation failed and why (with suggestions)
    int failedIndex = -1;     // index of the failed operation in the batch
    nlohmann::json results;   // one entry per operation (ids of created things, query answers)
    bool changed = false;     // the project was modified
};

// "text", "rectangle", "camera", "compound", "media", "plugin:charts.bar"... (as in layer.add).
std::string layerTypeName(const Layer& layer);

// Every operation's name ("layer.add", "query.layer"...), in the reference's order.
std::vector<std::string> operationNames();

// Applies one operation or a batch. On failure `project` is left untouched.
OpsResult applyOps(Project& project, const nlohmann::json& ops, const OpEnvironment& env = {});

} // namespace bemotion
