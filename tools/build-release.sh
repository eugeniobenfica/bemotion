#!/usr/bin/env bash
# One command from source to the files people download (Windows, MSYS2 UCRT64 shell):
#
#   tools/build-release.sh            (or double-click build-release.bat)
#
# Configures and builds bemotion in build-release/ (Release), runs the core tests,
# packages it (tools/package-windows.sh) and checks the package with
# tools/smoke-test.sh. The results land in dist/:
#   dist/bemotion-<version>-setup.exe      installer
#   dist/bemotion-<version>-portable.zip   portable folder, zipped
#   dist/SHA256SUMS.txt                    their sums: upload it with them (the updater checks it)
#
# Skia is looked for in $SKIA_ROOT, third_party/skia, then ~/bemotion-deps/skia;
# build it once with tools/skia/build-skia.sh if you have none.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
build="${BUILD_DIR:-build-release}"

skia="${SKIA_ROOT:-}"
for candidate in "$skia" "$root/third_party/skia" "$HOME/bemotion-deps/skia" "/c/Users/$USERNAME/bemotion-deps/skia"; do
    if [ -n "$candidate" ] && [ -f "$candidate/lib/libskia.a" ]; then skia="$candidate"; break; fi
done
if [ ! -f "$skia/lib/libskia.a" ]; then
    echo "Skia not found: build it once with tools/skia/build-skia.sh (or set SKIA_ROOT)."
    exit 1
fi
command -v makensis >/dev/null || { echo "NSIS missing: pacman -S mingw-w64-ucrt-x86_64-nsis"; exit 1; }

if [ "${BEMOTION_BUNDLE_MODELS:-}" = 1 ]; then
    echo "== speech model (downloaded once, to go inside)"
    tools/fetch-speech-model.sh
fi
echo "== configure ($build, Skia: $skia)"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKIA_ROOT="$(cygpath -m "$skia" 2>/dev/null || echo "$skia")" >/dev/null
echo "== build"
cmake --build "$build"
echo "== core tests"
"$build/bemotion_core_tests" | tail -1
echo "== package"
tools/package-windows.sh "$build" dist
echo "== check the package"
tools/smoke-test.sh dist/bemotion "$build/smoke" | tail -1
echo "== sums"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
suffix="$(sed -n 's/^set(BEMOTION_VERSION_SUFFIX "\(.*\)")$/\1/p' CMakeLists.txt | head -1)"
fullversion="$version${suffix:+-$suffix}"
(cd dist && sha256sum -b "bemotion-$fullversion-setup.exe" "bemotion-$fullversion-portable.zip" >SHA256SUMS.txt)
echo
echo "Done (tag the release v$fullversion; upload these three files):"
ls -1 "dist/bemotion-$fullversion-setup.exe" "dist/bemotion-$fullversion-portable.zip" dist/SHA256SUMS.txt
