#!/usr/bin/env bash
# Makes a self-contained Windows folder of a bemotion build (run in an MSYS2 UCRT64 shell):
#
#   tools/deploy-windows.sh build-qt dist/bemotion
#
# Copies bemotion.exe and its assets, the Qt plugins it needs (windeployqt) and
# every DLL it loads from the MSYS2 prefix (FFmpeg, ICU, HarfBuzz, the C++ runtime, ...).
set -euo pipefail

build="${1:?usage: deploy-windows.sh <build dir> <output dir>}"
out="${2:?usage: deploy-windows.sh <build dir> <output dir>}"
prefix="${MINGW_PREFIX:-/ucrt64}"

mkdir -p "$out"
cp "$build/bemotion.exe" "$out/"
rm -rf "$out/assets"
cp -r "$build/assets" "$out/assets"

# Qt libraries and plugins (platform, styles, image formats, ...).
"$prefix/bin/windeployqt6.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw \
    --dir "$(cygpath -w "$out")" "$(cygpath -w "$out/bemotion.exe")" >/dev/null

# Everything else the executable and the Qt plugins load from the prefix, recursively.
copy_deps() {
    # ldd prints MSYS paths (/ucrt64/bin/...): keep the prefix's DLLs, not Windows' own.
    { ldd "$1" || true; } | awk '{print $3}' | { grep -iE "/$(basename "$prefix")/bin/" || true; } | while read -r dll; do
        local name
        name="$(basename "$dll")"
        if [ ! -f "$out/$name" ]; then
            cp "$dll" "$out/"
            copy_deps "$dll"
        fi
    done
}
copy_deps "$out/bemotion.exe"
find "$out" -name '*.dll' -print0 | while IFS= read -r -d '' dll; do copy_deps "$dll"; done

echo "Deployed to $out ($(find "$out" -name '*.dll' | wc -l) DLLs)"
