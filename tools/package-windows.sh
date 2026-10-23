#!/usr/bin/env bash
# Builds what Windows users download (run in an MSYS2 UCRT64 shell, NSIS installed:
# pacman -S mingw-w64-ucrt-x86_64-nsis):
#
#   tools/package-windows.sh build dist
#
#   dist/bemotion/                        the program folder (tools/deploy-windows.sh)
#   dist/bemotion-<version>-setup.exe     installer: per user, no administrator rights
#   dist/bemotion-<version>-portable.zip  the folder zipped: unzip anywhere and run bemotion.exe
set -euo pipefail

build="${1:?usage: package-windows.sh <build dir> <output dir>}"
out="${2:?usage: package-windows.sh <build dir> <output dir>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -1)"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -rf "$out/bemotion"
"$root/tools/deploy-windows.sh" "$build" "$out/bemotion"
cp "$root/LICENSE" "$out/bemotion/LICENSE.txt"

setup="$out/bemotion-$version-setup.exe"
# NSIS needs a Windows temp folder it can map big files in (not MSYS2's /tmp).
mkdir -p "$out/.nsis-tmp"
TMP="$(cygpath -w "$out/.nsis-tmp")"
TEMP="$TMP"
export TMP TEMP
(cd "$root/tools/installer" &&
    makensis -V2 -DSOURCE="$(cygpath -w "$out/bemotion")" -DVERSION="$version" -DOUTFILE="$(cygpath -w "$setup")" bemotion.nsi)

zip="$out/bemotion-$version-portable.zip"
rm -f "$zip"
# Windows' own tar writes zip files.
/c/Windows/System32/tar.exe -a -c -f "$(cygpath -w "$zip")" -C "$(cygpath -w "$out")" bemotion

rm -rf "$out/.nsis-tmp"
echo "Installer: $setup ($(du -h "$setup" | cut -f1))"
echo "Portable:  $zip ($(du -h "$zip" | cut -f1))"
