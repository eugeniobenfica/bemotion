#!/usr/bin/env bash
# Makes bemotion.app and bemotion.dmg from a macOS build (Qt from Homebrew):
#
#   tools/package-macos.sh build dist
#
# The bundle holds the executable, the assets (Contents/Resources/assets), the
# icon, and — through macdeployqt — Qt and every Homebrew library it loads
# (FFmpeg, ICU, HarfBuzz, ...), so it runs on Macs without Homebrew.
set -euo pipefail

build="${1:?usage: package-macos.sh <build dir> <output dir>}"
out="${2:?usage: package-macos.sh <build dir> <output dir>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -1)"
suffix="$(sed -n 's/^set(BEMOTION_VERSION_SUFFIX "\(.*\)")$/\1/p' "$root/CMakeLists.txt" | head -1)"
fullversion="$version${suffix:+-$suffix}" # 0.1.0-beta.2, as the release tag
qtbin="$(brew --prefix qt)/bin"

exe="$(find "$build" -maxdepth 3 -type f -name bemotion -perm -u+x | head -1)"
app="$out/bemotion.app"
rm -rf "$app" "$out/bemotion.dmg"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$exe" "$app/Contents/MacOS/bemotion"
cp -R "$(dirname "$exe")/assets" "$app/Contents/Resources/assets"
# Official plugins are not part of the package: the app downloads them from the release's tag
# when a project or the user asks (Settings › Plugins). BEMOTION_BUNDLE_PLUGINS="charts" keeps
# those ids inside ("all": every one).
bundle=" ${BEMOTION_BUNDLE_PLUGINS:-} "
for dir in "$app/Contents/Resources/assets/plugins"/*/; do
    id="$(basename "$dir")"
    [[ "$bundle" == *" all "* || "$bundle" == *" $id "* ]] || rm -rf "$dir"
done
# The speech models are not part of the package: bemotion offers to download them when first
# needed. BEMOTION_BUNDLE_MODELS=1 puts the fetched ones inside (tools/fetch-speech-model.sh).
models="${BEMOTION_MODELS:-$HOME/bemotion-deps/models}"
if [ "${BEMOTION_BUNDLE_MODELS:-}" = 1 ] && ls "$models"/ggml-*.bin >/dev/null 2>&1; then
    mkdir -p "$app/Contents/Resources/assets/models"
    cp "$models"/ggml-*.bin "$app/Contents/Resources/assets/models/"
fi

# Icon: every size iconutil wants, from the 1024 px brand PNG.
iconset="$out/bemotion.iconset"
rm -rf "$iconset"
mkdir -p "$iconset"
for s in 16 32 128 256 512; do
    sips -z $s $s "$root/assets/branding/icon-1024.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$root/assets/branding/icon-1024.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/bemotion.icns"
rm -rf "$iconset"

cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>bemotion</string>
    <key>CFBundleDisplayName</key><string>bemotion</string>
    <key>CFBundleIdentifier</key><string>io.github.eugeniobenfica.bemotion</string>
    <key>CFBundleExecutable</key><string>bemotion</string>
    <key>CFBundleIconFile</key><string>bemotion</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$fullversion</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>LSMinimumSystemVersion</key><string>12.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key><string>bemotion Project</string>
            <key>CFBundleTypeExtensions</key><array><string>bemotion</string></array>
            <key>CFBundleTypeRole</key><string>Editor</string>
        </dict>
    </array>
</dict>
</plist>
PLIST

# Qt frameworks and plugins plus the Homebrew libraries, with their install names fixed.
"$qtbin/macdeployqt" "$app" -always-overwrite
# The "offscreen" platform too: tools/smoke-test.sh runs the bundle without a screen.
cp "$(brew --prefix qt)/share/qt/plugins/platforms/libqoffscreen.dylib" "$app/Contents/PlugIns/platforms/"

# macdeployqt misses libraries that Homebrew links through @rpath (libsharpyuv
# under libwebp, libbrotlicommon under libbrotlidec, ...). Copy every library
# still missing into Contents/Frameworks and point all references at the copies,
# repeating until nothing new is pulled in.
frameworks="$app/Contents/Frameworks"
brew_prefix="$(brew --prefix)"
find_brew_lib() {
    local d
    for d in "$brew_prefix/lib" "$brew_prefix"/opt/*/lib; do
        if [ -f "$d/$1" ]; then
            echo "$d/$1"
            return
        fi
    done
}
for pass in 1 2 3 4 5 6 7 8; do
    copied=0
    while IFS= read -r f; do
        file -b "$f" | grep -q Mach-O || continue
        chmod u+w "$f"
        id="$(otool -D "$f" | tail -n +2)"
        for dep in $(otool -L "$f" | tail -n +2 | awk '{print $1}'); do
            [ "$dep" = "$id" ] && continue
            case "$dep" in
                /usr/lib/* | /System/*) continue ;;
                @rpath/Qt*.framework/*)
                    # A Qt plugin for a module we do not ship (QtVirtualKeyboard, ...): drop it.
                    qtfw="${dep#@rpath/}"
                    if [ ! -d "$frameworks/${qtfw%%/*}" ] && [[ "$f" == "$app/Contents/PlugIns/"* ]]; then
                        echo "removing $f (needs ${qtfw%%/*})"
                        rm -f "$f"
                        continue 2
                    fi
                    continue ;;
                *.framework/*) continue ;;
            esac
            name="${dep##*/}"
            target="@executable_path/../Frameworks/$name"
            if [ ! -f "$frameworks/$name" ]; then
                src=""
                [[ "$dep" == /* ]] && [ -f "$dep" ] && src="$dep"
                [ -z "$src" ] && src="$(find_brew_lib "$name")"
                if [ -z "$src" ]; then
                    echo "error: cannot find $name (needed by $f)" >&2
                    exit 1
                fi
                cp -L "$src" "$frameworks/$name"
                chmod u+w "$frameworks/$name"
                install_name_tool -id "$target" "$frameworks/$name" 2>/dev/null
                copied=1
            fi
            if [ "$dep" != "$target" ]; then
                install_name_tool -change "$dep" "$target" "$f" 2>/dev/null
            fi
        done
    done < <(find "$app/Contents/MacOS" "$frameworks" "$app/Contents/PlugIns" -type f)
    if [ "$copied" = 0 ]; then
        break
    fi
done
# Signed with a Developer ID when BEMOTION_MACOS_IDENTITY names one in the keychain ("Developer
# ID Application: ..."), with the hardened runtime, then notarized when BEMOTION_NOTARY_PROFILE
# names a notarytool keychain profile: Gatekeeper opens it without asking. Otherwise an ad-hoc
# signature, required on Apple silicon (Gatekeeper still asks on first open).
if [ -n "${BEMOTION_MACOS_IDENTITY:-}" ]; then
    codesign --force --deep --options runtime --timestamp --sign "$BEMOTION_MACOS_IDENTITY" "$app"
    codesign --verify --deep --strict "$app"
else
    codesign --force --deep --sign - "$app"
fi

hdiutil create -volname bemotion -srcfolder "$app" -ov -format UDZO "$out/bemotion.dmg" >/dev/null
if [ -n "${BEMOTION_MACOS_IDENTITY:-}" ]; then
    codesign --force --timestamp --sign "$BEMOTION_MACOS_IDENTITY" "$out/bemotion.dmg"
    if [ -n "${BEMOTION_NOTARY_PROFILE:-}" ]; then
        xcrun notarytool submit "$out/bemotion.dmg" --keychain-profile "$BEMOTION_NOTARY_PROFILE" --wait
        xcrun stapler staple "$out/bemotion.dmg"
    fi
fi
echo "Packaged $app and $out/bemotion.dmg"
