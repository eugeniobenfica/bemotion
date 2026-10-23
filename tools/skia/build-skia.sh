#!/usr/bin/env bash
# Builds Skia (static libraries + the modules bemotion uses) into third_party/skia.
#
# Skia has no stable release packages, and prebuilt ones are not portable
# enough: some are compiled with AVX-512 code in shared functions and crash on
# CPUs without it. So bemotion pins one Skia revision and builds it here:
#
#   Windows: run from an MSYS2 UCRT64 shell (pacman packages listed in README)
#   Linux / macOS: clang or gcc, python3, ninja, gn; system harfbuzz, icu,
#                  freetype, fontconfig, libpng, libjpeg, libwebp, expat, zlib
#
#   tools/skia/build-skia.sh [install-dir]   (default: third_party/skia)
#
# The result: <dir>/include (Skia's include/, modules/ and the src/ headers the
# modules need), <dir>/lib/*.a, and <dir>/skia.cmake with the link line.

set -euo pipefail

COMMIT=da51f0d60ea2b14e845a823dc11b405dbeef42d8 # milestone 143 (same as MSYS2's package)
SHA256=63eee9235414e0171dec6b47d54a5b55057c4b38fce4514bdfc8003b5713a761
ZLIB_COMMIT=646b7f569718921d7d4b5b8e22572ff6c76f2596 # chromium zlib "google" helpers Skia's build expects

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PREFIX="${1:-$ROOT/third_party/skia}"
WORK="${SKIA_WORK_DIR:-$ROOT/third_party/skia-build}"
if command -v cygpath >/dev/null; then # MSYS2: "C:/x" would read as a remote host to tar
    PREFIX="$(cygpath -u "$PREFIX")"
    WORK="$(cygpath -u "$WORK")"
fi
mkdir -p "$WORK" "$PREFIX"

case "$(uname -s)" in
    MINGW*|MSYS*) OS=mingw ;;
    Darwin) OS=mac ;;
    *) OS=linux ;;
esac

# --- source ---------------------------------------------------------------------
SRC="$WORK/skia"
if [ ! -f "$SRC/.bemotion-ready" ]; then
    rm -rf "$SRC"
    if ! echo "$SHA256  $WORK/skia.tar.gz" | sha256sum -c - >/dev/null 2>&1; then
        curl -L --fail -o "$WORK/skia.tar.gz" "https://github.com/google/skia/archive/$COMMIT.tar.gz"
        echo "$SHA256  $WORK/skia.tar.gz" | sha256sum -c -
    fi
    tar -xzf "$WORK/skia.tar.gz" -C "$WORK"
    mv "$WORK/skia-$COMMIT" "$SRC"

    cd "$SRC"
    if [ "$OS" = mingw ]; then
        # MSYS2's patches (vendored from msys2/MINGW-packages, mingw-w64-skia): mingw toolchain,
        # system packages through pkg-config, DirectWrite fixes.
        for p in 0001-add-pkgconfig-support 0002-add-mingw-toolchain-skia 0003-fix-dwrite-function-mingw \
                 0004-misc-mingw-fixes 0005-external-packages-update 0008-dwrite; do
            patch -Np1 -i "$HERE/patches/$p.patch"
        done
        patch -R -Np1 -i "$HERE/patches/0007-Reland-Make-SkPath-immutable-on-GN-build.patch"
        # The mingw toolchain patch compiles everything with -mavx2 -mavx512f: the library would
        # only run on CPUs with AVX-512. Plain x86-64 instead.
        sed -i.bak '/"-mavx2",/d; /"-mavx512f",/d; /"-mf16c",/d; /"-mfma",/d' gn/skia/BUILD.gn
        git clone --quiet https://chromium.googlesource.com/chromium/src/third_party/zlib.git "$WORK/zlib"
        git -C "$WORK/zlib" checkout --quiet "$ZLIB_COMMIT"
        mkdir -p third_party/externals/zlib
        cp -r "$WORK/zlib/google" third_party/externals/zlib/
    fi
    # No AVX2 / AVX-512 code paths: they are compiled with -march=haswell / skylake-avx512 and
    # inline functions from those files can be picked by the linker for the whole library,
    # which then crashes on CPUs without those instructions.
    sed -i.bak '/^opts("hsw")/,/^}/ s/enabled = is_x86/enabled = false/' BUILD.gn
    sed -i.bak '/^opts("skx")/,/^}/ s/enabled = is_x86/enabled = false/' BUILD.gn
    sed -i.bak '/defines += \[ "SK_ENABLE_AVX512_OPTS" \]/d' BUILD.gn
    # ... and nothing dispatches to them.
    sed -i.bak '/SkCpu::Supports(SkCpu::HSW)/d; /SkCpu::Supports(SkCpu::SKX)/d' \
        src/core/SkOpts.cpp src/core/SkBlitRow_opts.cpp src/core/SkSwizzler_opts.cpp
    touch .bemotion-ready
fi
cd "$SRC"

GN="$(command -v gn || true)"
if [ -z "$GN" ]; then
    python3 bin/fetch-gn
    GN=bin/gn
fi

CC_BIN="${CC:-$( [ "$OS" = mingw ] && echo gcc || echo clang )}"
CXX_BIN="${CXX:-$( [ "$OS" = mingw ] && echo g++ || echo clang++ )}"

ARGS="
  target_cpu=\"x64\"
  cc=\"$CC_BIN\"
  cxx=\"$CXX_BIN\"
  is_debug=false
  is_official_build=true
  is_component_build=false
  skia_use_dng_sdk=false
  skia_use_wuffs=false
  skia_use_xps=false
  skia_use_vulkan=false
  skia_use_icu=true
  skia_use_harfbuzz=true
  skia_use_system_icu=true
  skia_use_system_harfbuzz=true
  skia_enable_svg=true
  skia_enable_skottie=true
  skia_enable_skshaper=true
  skia_enable_skparagraph=true
  skia_enable_pdf=false
  skia_enable_tools=false
  skia_enable_fontmgr_empty=false
"
if [ "$(uname -m)" = arm64 ] || [ "$(uname -m)" = aarch64 ]; then ARGS="${ARGS/target_cpu=\"x64\"/target_cpu=\"arm64\"}"; fi

OUT=out/bemotion
"$GN" gen "$OUT" --args="$ARGS"
ninja -C "$OUT" skia modules/skshaper modules/skunicode modules/skparagraph modules/svg modules/skottie

# --- install ----------------------------------------------------------------------
rm -rf "$PREFIX/include" "$PREFIX/lib"
mkdir -p "$PREFIX/include" "$PREFIX/lib"
# Headers keep Skia's layout ("include/core/SkCanvas.h", "modules/...", and the src/ headers the modules include).
find include modules src -name '*.h' | tar -cf - -T - | tar -xf - -C "$PREFIX/include"
cp "$OUT"/*.a "$PREFIX/lib/"
cp LICENSE "$PREFIX/LICENSE"
echo "$COMMIT" > "$PREFIX/REVISION"
# The defines the libraries were built with (headers must agree).
"$GN" desc "$OUT" //:skia defines --format=json > "$PREFIX/defines.json" 2>/dev/null || true
echo "Skia installed in $PREFIX"
