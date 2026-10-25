#pragma once

// "Did you mean ...?": the closest of a few names to a mistyped one, for error messages
// (operations, expressions).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

inline size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (std::tolower(a[i - 1]) == std::tolower(b[j - 1]) ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// "; did you mean "x"?" for the closest candidate, or "; valid: a, b, c" (with `list`).
inline std::string suggest(const std::string& wrong, const std::vector<std::string>& candidates, bool list = true) {
    if (candidates.empty()) return "";
    std::string best;
    size_t bestD = SIZE_MAX;
    for (const auto& c : candidates)
        if (const size_t d = editDistance(wrong, c); d < bestD) bestD = d, best = c;
    if (bestD <= std::max<size_t>(2, wrong.size() / 3)) return "; did you mean \"" + best + "\"?";
    if (!list) return "";
    std::string all;
    for (size_t i = 0; i < candidates.size() && i < 40; ++i) all += (i ? ", " : "") + candidates[i];
    return "; valid: " + all;
}

} // namespace bemotion
