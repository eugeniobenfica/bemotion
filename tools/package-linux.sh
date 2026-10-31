#!/usr/bin/env bash
# Makes a bemotion AppImage from a Linux build (linuxdeploy + its Qt plugin):
#
#   tools/package-linux.sh build dist
#
# dist/AppDir holds the unpacked application (it runs as is: dist/AppDir/AppRun),
# dist/bemotion-x86_64.AppImage the single-file version. Qt, FFmpeg, ICU and
# the other libraries go inside; the C library and graphics drivers stay the
# system's, as usual for AppImages.
set -euo pipefail

build="${1:?usage: package-linux.sh <build dir> <output dir>}"
out="${2:?usage: package-linux.sh <build dir> <output dir>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
appdir="$out/AppDir"
rm -rf "$appdir"

exe="$(find "$build" -maxdepth 3 -type f -name bemotion -perm -u+x | head -1)"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/scalable/apps"
cp "$exe" "$appdir/usr/bin/bemotion"
cp -r "$(dirname "$exe")/assets" "$appdir/usr/bin/assets" # found next to the executable
# Official plugins are not part of the package: the app downloads them from the release's tag
# when a project or the user asks (Settings › Plugins). BEMOTION_BUNDLE_PLUGINS="charts" keeps
# those ids inside ("all": every one).
bundle=" ${BEMOTION_BUNDLE_PLUGINS:-} "
for dir in "$appdir/usr/bin/assets/plugins"/*/; do
    id="$(basename "$dir")"
    [[ "$bundle" == *" all "* || "$bundle" == *" $id "* ]] || rm -rf "$dir"
done
# The speech models are not part of the package: bemotion offers to download them when first
# needed. BEMOTION_BUNDLE_MODELS=1 puts the fetched ones inside (tools/fetch-speech-model.sh).
models="${BEMOTION_MODELS:-$HOME/bemotion-deps/models}"
if [ "${BEMOTION_BUNDLE_MODELS:-}" = 1 ] && ls "$models"/ggml-*.bin >/dev/null 2>&1; then
    mkdir -p "$appdir/usr/bin/assets/models"
    cp "$models"/ggml-*.bin "$appdir/usr/bin/assets/models/"
fi
cp "$root/assets/branding/icon.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/bemotion.svg"
cat > "$appdir/usr/share/applications/bemotion.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=bemotion
Comment=Minimal 2D motion graphics animator
Exec=bemotion %f
Icon=bemotion
Categories=AudioVideo;Video;Graphics;
MimeType=application/x-bemotion;
DESKTOP

# The tools (pinned to their "continuous" builds, cached in the output dir).
tools="$out/.tools"
mkdir -p "$tools"
for t in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage; do
    [ -x "$tools/$t" ] && continue
    repo=linuxdeploy/${t%-x86_64.AppImage}
    curl -L --fail -o "$tools/$t" "https://github.com/$repo/releases/download/continuous/$t"
    chmod +x "$tools/$t"
done

export APPIMAGE_EXTRACT_AND_RUN=1 # no FUSE needed (CI)
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
# The "offscreen" platform too: tools/smoke-test.sh runs the package without a screen.
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
export OUTPUT="$out/bemotion-x86_64.AppImage"
cd "$out"
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --desktop-file "$appdir/usr/share/applications/bemotion.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/bemotion.svg" \
    --plugin qt --output appimage
echo "Packaged $OUTPUT"
