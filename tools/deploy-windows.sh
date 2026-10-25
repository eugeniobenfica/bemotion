#!/usr/bin/env bash
# Makes a self-contained Windows folder of a bemotion build (run in an MSYS2 UCRT64 shell):
#
#   tools/deploy-windows.sh build dist/bemotion
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
cp -r "$(dirname "$build/bemotion.exe")/assets" "$out/assets"
# Official plugins a release leaves out (they stay downloadable from Settings › Plugins):
#   BEMOTION_LEAVE_OUT_PLUGINS="models3d recorder" tools/package-....sh ...
for id in ${BEMOTION_LEAVE_OUT_PLUGINS:-}; do rm -rf "$out/assets/plugins/$id"; done

# Qt libraries and plugins (platform, styles, image formats, TLS for the downloads the user asks
# for: official plugins, the speech model). No network-information or touch plugins.
"$prefix/bin/windeployqt6.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw \
    --skip-plugin-types networkinformation,generic \
    --dir "$(cygpath -w "$out")" "$(cygpath -w "$out/bemotion.exe")" >/dev/null
# The "offscreen" platform too (about 1 MB): tools/smoke-test.sh runs the package without a screen.
cp "$prefix/share/qt6/plugins/platforms/qoffscreen.dll" "$out/platforms/"

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
# whisper.cpp's compute backends (CPU variants, Vulkan...) are loaded at run time, from beside the
# executable: ldd does not see them.
for dll in "$prefix"/bin/ggml-*.dll; do cp "$dll" "$out/"; done
# The speech-to-text model ships inside: nobody has to download anything (tools/fetch-speech-model.sh).
models="${BEMOTION_MODELS:-$HOME/bemotion-deps/models}"
if ls "$models"/ggml-*.bin >/dev/null 2>&1; then
    mkdir -p "$out/assets/models"
    cp "$models"/ggml-*.bin "$out/assets/models/"
else
    echo "warning: no speech model in $models (run tools/fetch-speech-model.sh): transcription will be missing"
fi
find "$out" -name '*.dll' -print0 | while IFS= read -r -d '' dll; do copy_deps "$dll"; done

echo "Deployed to $out ($(find "$out" -name '*.dll' | wc -l) DLLs)"
