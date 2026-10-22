# bemotion

**A minimal, open source 2D motion graphics animator.**

bemotion gives you a clean, focused space to animate text and graphics: drop an
object on the canvas, set keyframes, pick an easing and press play. Everything
is customizable (color, transform, stroke, typography, timing) while the most
common moves are one click away through *Quick Animations*.

> 🇧🇷 **Resumo:** bemotion é um animador 2D minimalista e de código aberto para
> motion graphics. Escrito em C++20 com Dear ImGui e OpenGL, foco em
> simplicidade, liberdade de customização e uma interface limpa no modo escuro.

## Features

- **Home screen** with recent projects (live thumbnails), search and a
  *New Project* dialog with format presets (Full HD, 4K, Vertical/Reels, Square…)
- **Objects panel**: Text, Rectangle, Ellipse, Polygon, Star and Line — click
  to add at the center, or **drag onto the canvas** to drop at an exact spot
- **Viewport**: wheel zoom around the cursor, pan with middle/right drag,
  Space+drag or Alt+drag, zoom presets (Fit, 25–400%); click or **box-select**
  layers (Shift/Ctrl to add), drag to move every selected layer, corner
  handles to scale (Shift = uniform), rotation handle (Shift = 15° snaps)
- **Properties panel**: every property is animatable with a keyframe toggle —
  transform, size, roundness, fill, stroke, trim paths, font weight, tracking,
  line height and text *reveal* (typewriter)
- **Quick Animations**: Fade In/Out, Pop In, Slide Up, Slide In, Spin,
  Typewriter, Draw On — applied to all selected layers as normal keyframes
- **Timeline**: playback with loop, frame-accurate scrubbing, layer bars
  (move in time / trim in & out), **drag layers up and down to change the
  stacking order**, multi-select (Shift/Ctrl), keyframe dragging, easing menu
  (right click), zoom (Ctrl+Wheel)
- **Cut tool**: the razor (C) splits a layer where you click; Ctrl+K splits the
  selected layers at the playhead
- **Video export** (Ctrl+M): MP4 / H.264 (built in on Windows), PNG sequence
  with transparency, and — with [FFmpeg](https://ffmpeg.org) installed — MOV
  ProRes 4444 (alpha), WebM VP9 and animated GIF; any size from ¼× to 2×
- **Easing**: Linear, Ease In/Out/In-Out, Back, Elastic, Bounce, Hold
- **Undo / redo** for everything, unsaved-changes protection
- Dockable, rearrangeable panels; dark theme with a golden accent

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022)
and an internet connection on first configure (dependencies are fetched
automatically and pinned to exact versions).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/bemotion        # Windows: build\src\bemotion.exe
```

Run the unit tests with `ctest --test-dir build`.

On Linux, install the X11/OpenGL development packages first, e.g. on Ubuntu:
`sudo apt install libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev`.

An example project lives in [`examples/Welcome.bemotion`](examples/Welcome.bemotion):
open it from *Open Project…* or pass it on the command line.

### Rendering from the command line

```bash
bemotion examples/Welcome.bemotion --export welcome.mp4
bemotion project.bemotion --export frames/shot.png --transparent --scale 0.5
```

The format follows the file extension (`.mp4`, `.png`, `.mov`, `.webm`, `.gif`).

## Keyboard shortcuts

| Keys | Action |
| --- | --- |
| Space | Play / pause (hold + drag: pan the viewport) |
| ← / → (Shift: ×10) | Previous / next frame |
| Home / End | Go to start / end |
| J / K | Previous / next keyframe |
| V / C | Selection tool / Cut tool |
| Ctrl+K | Split selected layers at the playhead |
| T | Add text |
| Ctrl+A | Select all layers |
| Ctrl+] / Ctrl+[ (Shift: to front / back) | Bring forward / send backward |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+D | Duplicate layers |
| Delete | Delete selected keyframes, or the layers |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| Ctrl+M | Export video |
| Wheel · Middle/right drag | Zoom · pan the viewport |
| Ctrl+0 / Ctrl+1 / Ctrl+= / Ctrl+- | Fit / 100% / zoom in / zoom out |

## Project layout

```
src/core     document model (layers, keyframes, easing, file format, undo) — no UI code
src/render   draws compositions (viewport, thumbnails, off-screen export)
src/export   video / image-sequence encoders and the export job
src/ui       theme, widgets, home screen, editor, panels and dialogs
src/app      window, main loop, OS integration, command line
tests        unit tests for the core
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the pieces fit
together and [CONTRIBUTING.md](CONTRIBUTING.md) to get involved.

## License

bemotion is released under the [MIT License](LICENSE). The bundled Inter font is
licensed under the [SIL Open Font License](assets/fonts/Inter-OFL.txt).
