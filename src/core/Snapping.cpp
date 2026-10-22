#include "core/Snapping.h"

#include <cmath>

namespace bemotion {

SnapResult snap1D(const std::vector<double>& edges, const std::vector<double>& targets, double threshold) {
    SnapResult best;
    double bestDistance = threshold;
    for (double e : edges)
        for (double t : targets) {
            const double d = std::fabs(t - e);
            if (d <= bestDistance) {
                bestDistance = d;
                best = {true, t - e, t};
            }
        }
    return best;
}

} // namespace bemotion
