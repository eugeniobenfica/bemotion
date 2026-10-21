# Mirage

**A minimal, open source 2D motion graphics animator.**

Mirage gives you a clean, focused space to animate text and graphics: drop an
object on the canvas, set keyframes, pick an easing and press play. Everything
is customizable (color, transform, stroke, typography, timing) while the most
common moves are one click away through *Quick Animations*.

> 🇧🇷 **Resumo:** Mirage é um animador 2D minimalista e de código aberto para
> motion graphics. Escrito em C++20 com Dear ImGui e OpenGL, foco em
> simplicidade, liberdade de customização e uma interface limpa no modo escuro.

## Features (v0.1)

- **Home screen** with recent projects (live thumbnails), search and a
  *New Project* dialog with format presets (Full HD, 4K, Vertical/Reels, Square…)
- **Objects panel**: Text, Rectangle, Ellipse, Polygon, Star and Line
- **Viewport**: zoom/pan, click to select, drag to move, corner handles to
  scale (Shift = uniform), rotation handle (Shift = 15° snaps)
- **Properties panel**: every property is animatable with a keyframe toggle —
  transform, size, roundness, fill, stroke, trim paths, font weight, tracking,
  line height and text *reveal* (typewriter)
- **Quick Animations**: Fade In/Out, Pop In, Slide Up, Slide In, Spin,
  Typewriter, Draw On — they create normal keyframes you can tweak
- **Timeline**: playback with loop, frame-accurate scrubbing, layer bars
  (move / trim in & out), drag to reorder layers, keyframe dragging,
  multi-select (Shift), easing menu (right click), zoom (Ctrl+Wheel)
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
./build/src/Mirage        # Windows: build\src\Mirage.exe
```

Run the unit tests with `ctest --test-dir build`.

On Linux, install the X11/OpenGL development packages first, e.g. on Ubuntu:
`sudo apt install libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev`.

An example project lives in [`examples/Welcome.mirage`](examples/Welcome.mirage):
open it from *Open Project…* or pass it on the command line.

## Keyboard shortcuts

| Keys | Action |
| --- | --- |
| Space | Play / pause |
| ← / → (Shift: ×10) | Previous / next frame |
| Home / End | Go to start / end |
| J / K | Previous / next keyframe |
| T | Add text |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+D | Duplicate layer |
| Delete | Delete selected keyframes, or the layer |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| Ctrl+0 | Fit composition in the viewport |

## Project layout

```
src/core     document model (layers, keyframes, easing, file format, undo) — no UI code
src/render   draws compositions (shared by viewport and thumbnails)
src/ui       theme, widgets, home screen, editor and its panels
src/app      window, main loop, OS integration
tests        unit tests for the core
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the pieces fit
together and [CONTRIBUTING.md](CONTRIBUTING.md) to get involved.

## License

Mirage is released under the [MIT License](LICENSE). The bundled Inter font is
licensed under the [SIL Open Font License](assets/fonts/Inter-OFL.txt).
