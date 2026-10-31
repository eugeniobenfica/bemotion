#!/usr/bin/env bash
# Downloads the Skia that tools/skia/build-skia.sh would build, already built by CI
# (.github/workflows/skia.yml, the release skia-<revision>), into third_party/skia.
# Minutes instead of an hour. Each archive is checked against the release's SHA256SUMS.txt.
#
#   tools/skia/fetch-skia.sh [install-dir]   (default: third_party/skia)
#
# Skia's libraries link against the system's harfbuzz, ICU and freetype. When those moved to
# a new major version since the archive was built (MSYS2 and Homebrew update often), linking
# bemotion fails with undefined ICU or harfbuzz symbols: then build it (build-skia.sh).

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PREFIX="${1:-$ROOT/third_party/skia}"
REV="$(sed -n 's/^COMMIT=\([0-9a-f]*\).*/\1/p' "$HERE/build-skia.sh" | cut -c1-12)"
REPO="${BEMOTION_SKIA_REPO:-eugeniobenfica/bemotion}"

case "$(uname -s)" in
    MINGW*|MSYS*) OS=mingw ;;
    Darwin) OS=mac ;;
    *) OS=linux ;;
esac
NAME="skia-$OS-$REV.tar.xz"
URL="https://github.com/$REPO/releases/download/skia-$REV"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "Fetching $NAME"
curl -fL --retry 3 -o "$TMP/$NAME" "$URL/$NAME" ||
    { echo "No prebuilt Skia for $OS at $REV: build it with tools/skia/build-skia.sh"; exit 1; }
curl -fsSL --retry 3 -o "$TMP/SHA256SUMS.txt" "$URL/SHA256SUMS.txt"
(cd "$TMP" && grep " $NAME\$" SHA256SUMS.txt | sha256sum -c -) || { echo "$NAME did not arrive intact"; exit 1; }

rm -rf "$PREFIX"
mkdir -p "$PREFIX"
tar -C "$PREFIX" --strip-components=1 -xJf "$TMP/$NAME"
echo "Skia $REV installed in $PREFIX"
