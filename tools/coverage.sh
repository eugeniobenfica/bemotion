#!/usr/bin/env bash
# How much of the code the tests run (GCC's gcov counters, read by gcovr):
#
#   tools/coverage.sh [-DSKIA_ROOT=<skia>]     # configure / build the "coverage" preset, test, report
#
# Builds in build-coverage/ (CMakePresets.json), runs ctest (core tests, fuzz, golden images and
# the smoke-free checks) and writes:
#   build-coverage/coverage/summary.txt     line coverage per file, sorted, with the total
#   build-coverage/coverage/index.html      the same, browsable line by line
# Only src/ is counted (not the tests, libs/ or third_party/). Needs GCC and gcovr
# (pip install gcovr, or the distribution's package). The CI's coverage job runs it on Linux.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
command -v gcovr >/dev/null || { echo "gcovr missing: pip install gcovr"; exit 1; }

cmake --preset coverage "$@"
cmake --build --preset coverage
# Counters from an earlier run would add up with this one.
find build-coverage -name '*.gcda' -delete
ctest --preset coverage || echo "warning: some tests failed; coverage is reported anyway"

out=build-coverage/coverage
mkdir -p "$out"
gcovr --root . --filter 'src/' --exclude 'src/.*_autogen/' \
    --sort uncovered-percent --txt "$out/summary.txt" --html-details "$out/index.html" build-coverage
tail -n 3 "$out/summary.txt"
echo "Report: $out/index.html"
