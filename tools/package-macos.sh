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
qtbin="$(brew --prefix qt)/bin"

exe="$(find "$build" -maxdepth 3 -type f -name bemotion -perm -u+x | head -1)"
app="$out/bemotion.app"
rm -rf "$app" "$out/bemotion.dmg"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$exe" "$app/Contents/MacOS/bemotion"
cp -R "$(dirname "$exe")/assets" "$app/Contents/Resources/assets"

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
    <key>CFBundleShortVersionString</key><string>$version</string>
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
# Ad-hoc signature: required on Apple silicon (not notarized: Gatekeeper still asks on first open).
codesign --force --deep --sign - "$app"

hdiutil create -volname bemotion -srcfolder "$app" -ov -format UDZO "$out/bemotion.dmg" >/dev/null
echo "Packaged $app and $out/bemotion.dmg"
