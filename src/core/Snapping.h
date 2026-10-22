#pragma once

// 1D snapping used by the viewport (x and y separately) and the timeline (time).

#include <vector>

namespace bemotion {

struct SnapResult {
    bool snapped = false;
    double offset = 0.0; // add to the moving value(s)
    double target = 0.0; // the target that was hit
};

// Finds the smallest correction that puts any of `edges` exactly on any of
// `targets`, if it is within `threshold`.
SnapResult snap1D(const std::vector<double>& edges, const std::vector<double>& targets, double threshold);

} // namespace bemotion
