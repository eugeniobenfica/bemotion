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
# Official plugins are not part of the package: the app downloads them from the release's tag
# when a project or the user asks (Settings › Plugins). BEMOTION_BUNDLE_PLUGINS="charts" keeps
# those ids inside ("all": every one).
bundle=" ${BEMOTION_BUNDLE_PLUGINS:-} "
for dir in "$out/assets/plugins"/*/; do
    id="$(basename "$dir")"
    [[ "$bundle" == *" all "* || "$bundle" == *" $id "* ]] || rm -rf "$dir"
done

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
# The speech-to-text model (about 550 MB) is not part of the package: bemotion offers to download
# it the first time someone transcribes. BEMOTION_BUNDLE_MODELS=1 puts the fetched one inside
# (tools/fetch-speech-model.sh), for offline installs.
models="${BEMOTION_MODELS:-$HOME/bemotion-deps/models}"
if [ "${BEMOTION_BUNDLE_MODELS:-}" = 1 ]; then
    ls "$models"/ggml-*.bin >/dev/null 2>&1 || { echo "error: no speech model in $models (run tools/fetch-speech-model.sh)"; exit 1; }
    mkdir -p "$out/assets/models"
    cp "$models"/ggml-*.bin "$out/assets/models/"
fi
find "$out" -name '*.dll' -print0 | while IFS= read -r -d '' dll; do copy_deps "$dll"; done

echo "Deployed to $out ($(find "$out" -name '*.dll' | wc -l) DLLs)"
