// What every core test file shares: the CHECK macro, float comparison, running operations.
#pragma once

#include "core/Ops.h"
#include "core/Project.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

extern int g_failures; // core_tests.cpp

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

inline bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

inline bemotion::OpsResult run(bemotion::Project& p, const char* ops) {
    return bemotion::applyOps(p, nlohmann::json::parse(ops));
}

// Each area file lists its tests: a name to print and the function.
using Tests = std::vector<std::pair<const char*, std::function<void()>>>;
