#!/usr/bin/env bash
# Checks that a build actually runs on this machine (used by CI on every platform):
#
#   tools/smoke-test.sh <build dir> [output dir]
#
# - renders the Welcome project from the command line to every format and checks
#   the files with ffprobe (frame count, size, codec)
# - opens the app without a screen (Qt "offscreen" platform), on the home screen
#   and in the editor, and saves what the window shows
# - drives the editor through scripted scenarios and checks what it showed (tools/ui-test.py)
# The output dir (default smoke/) keeps the files for a look afterwards.
set -euo pipefail

build="${1:?usage: smoke-test.sh <build dir> [output dir]}"
out="${2:-smoke}"
rm -rf "$out"
mkdir -p "$out/appdata"
out="$(cd "$out" && pwd)"
root="$(cd "$(dirname "$0")/.." && pwd)"

exe="$(find "$build" -maxdepth 3 -type f \( -name bemotion -o -name bemotion.exe \) | head -1)"
[ -n "$exe" ] || { echo "no bemotion executable in $build"; exit 1; }
echo "testing $exe"

native() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
project="$out/welcome.bemotion"
cp "$root/assets/welcome/Welcome.bemotion" "$project"
export BEMOTION_APPDATA="$(native "$out/appdata")" # never the user's settings
export QT_QPA_PLATFORM=offscreen
export BEMOTION_STRICT=1 # an editor edit that breaks a rule stops the app (ui/EditorContext.cpp)

# timeout(1) is not on macOS: perl's alarm does the same.
limited() { if command -v timeout >/dev/null; then timeout "$@"; else perl -e 'alarm shift; exec @ARGV' "$@"; fi; }

failed=0
check() { # file, expected codec, expected width
    local info
    info="$(ffprobe -v error -select_streams v:0 -count_frames \
        -show_entries stream=codec_name,width,nb_read_frames -of csv=p=0 "$1" 2>/dev/null || true)"
    echo "  $(basename "$1"): $info"
    case "$info" in
        "$2,$3,"*) [ "${info##*,}" -gt 0 ] 2>/dev/null || { echo "  FAILED: no frames"; failed=1; } ;;
        *) echo "  FAILED: expected $2 at width $3"; failed=1 ;;
    esac
}

echo "== command-line export"
for f in mp4 mov webm gif; do
    "$exe" "$(native "$project")" --export "$(native "$out/welcome.$f")" --scale 0.5 >/dev/null
done
"$exe" "$(native "$project")" --export "$(native "$out/frames/f.png")" --format png --scale 0.25 >/dev/null
check "$out/welcome.mp4" h264 960
check "$out/welcome.mov" prores 960
check "$out/welcome.webm" vp9 960
check "$out/welcome.gif" gif 960
frames=$(find "$out/frames" -name 'f_*.png' | wc -l)
echo "  png sequence: $frames frames"
[ "$frames" -gt 100 ] || { echo "  FAILED: png sequence"; failed=1; }

echo "== automation (new, apply, script, describe, validate)"
auto="$out/auto.bemotion"
"$exe" new "$(native "$auto")" >/dev/null
"$exe" apply "$(native "$auto")" --ops '[{"op":"layer.add","type":"text","name":"T","text":"Hi"},{"op":"key.add","layer":"T","prop":"opacity","time":0,"value":0},{"op":"key.add","layer":"T","prop":"opacity","time":1,"value":100}]' >/dev/null ||
    { echo "  FAILED: apply"; failed=1; }
printf 'local doc = ...\ndoc.apply { op = "layer.add", type = "star", name = "S" }\n' > "$out/s.lua"
"$exe" script "$(native "$auto")" "$(native "$out/s.lua")" >/dev/null || { echo "  FAILED: script"; failed=1; }
if "$exe" apply "$(native "$auto")" --ops '{"op":"property.set","layer":"Nope","prop":"opacity","value":1}' >/dev/null; then
    echo "  FAILED: a bad operation was accepted"; failed=1
fi
summary="$("$exe" describe "$(native "$auto")")"
case "$summary" in *'"T" text "Hi"'*'opacity 2 keys'*) ;; *) echo "  FAILED: describe"; failed=1 ;; esac
case "$summary" in *'"S" star'*) ;; *) echo "  FAILED: script result"; failed=1 ;; esac
"$exe" validate "$(native "$auto")" >/dev/null || { echo "  FAILED: validate"; failed=1; }
echo "  ok unless reported above"

echo "== MCP server (bemotion mcp)"
py="$(command -v python3 || command -v python || true)"
if [ -n "$py" ]; then
    "$py" "$root/tools/mcp-test.py" "$(native "$exe")" "$(native "$out/mcp")" | sed 's/^/  /' || failed=1
    [ "${PIPESTATUS[0]}" = 0 ] || failed=1
else
    echo "  skipped (no python)"
fi

echo "== interface (offscreen)"
gui() { # name, capture frame, args...
    local name="$1" frame="$2"
    shift 2
    BEMOTION_CAPTURE="$(native "$out/$name.png")" BEMOTION_CAPTURE_FRAME="$frame" \
        limited 120 "$exe" "$@" >/dev/null 2>&1 || true
    if [ -s "$out/$name.png" ]; then echo "  $name: ok"; else echo "  $name: FAILED (no capture)"; failed=1; fi
}
gui home 400                         # the intro plays first
gui editor 60 "$(native "$project")"

echo "== interface scenarios (tools/ui-test.py)"
if [ -n "$py" ]; then
    "$py" "$root/tools/ui-test.py" "$(native "$exe")" "$(native "$out/ui")" || failed=1
else
    echo "  skipped (no python)"
fi

[ "$failed" = 0 ] && echo "smoke test passed" || echo "smoke test FAILED"
exit "$failed"
