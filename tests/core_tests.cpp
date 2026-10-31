// Minimal, dependency-free unit tests for the document model, one file per area.

#include "Check.h"

int g_failures = 0;

Tests animationTests();
Tests timelineTests();
Tests operationsTests();
Tests expressionTests();
Tests layerTests();
Tests fileTests();

int main() {
    for (Tests (*area)() : {animationTests, timelineTests, operationsTests, expressionTests, layerTests, fileTests})
        for (const auto& [name, fn] : area()) {
            const int before = g_failures;
            fn();
            std::printf("[%s] %s\n", g_failures == before ? " ok " : "FAIL", name);
        }
    std::printf("%s\n", g_failures == 0 ? "All tests passed." : "Some tests failed.");
    return g_failures == 0 ? 0 : 1;
}
