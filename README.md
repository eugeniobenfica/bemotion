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
- **Objects panel**: Text, Rectangle, Ellipse, Polygon, Star, Line, Camera and
  plugin objects (charts) — click to add at the center, or **drag onto the
  canvas** to drop at an exact spot
- **Viewport**: wheel zoom around the cursor, pan with middle/right drag,
  Space+drag or Alt+drag; click or **box-select** layers, drag to move, corner
  handles to scale (Shift = uniform), rotation handle (Shift = 15° snaps)
- **Snapping** (S, or the magnet button): canvas center and edges, other
  layers' edges and centers, **rulers and custom guides** (drag them out of the
  rulers); in the timeline clips snap to each other, keyframes and the playhead
- **Motion paths**: position keyframes follow a curve drawn in the viewport —
  drag the keys and their bezier handles (Alt breaks a handle into a corner)
- **Graph Editor** (the curve button in the timeline toolbar), like After
  Effects': value curves of the selected layer (X / Y separately), drag
  keyframes and easing handles; Easy Ease (F9), Ease In (Shift+F9), Ease Out
  (Ctrl+Shift+F9), Linear and Hold
- **Properties panel**: every property is animatable, with a keyframe toggle
  and a **reset** button; collapsible sections; any installed **font family**
- **Effects** tab next to Properties (like After Effects' *Effect Controls* +
  *Effects & Presets*): Gaussian and Directional Blur, Brightness & Contrast,
  Hue/Saturation, Tint, Invert, Wave Warp, Fill, Stroke, Drop Shadow, Glow and
  Motion Blur — browse them by category icons, apply from the tab or from the
  timeline's right-click *Effect* menu (also *Layer → Effect*); GPU rendered,
  stackable, every parameter keyframable (also in the timeline under
  *Effects*), with presets you can **import / save**
- **Quick Animations**: Fade, Pop, Slide, Drop, Spin, Pulse, Typewriter,
  Draw On… stack without breaking; **import** more or save your own
- **Timeline**: tracks holding several clips, trim in/out (clamped to
  neighbors), inline value editing on keyframe rows, keyframe dragging,
  easing menu with a **custom cubic-bezier curve editor**, zoom (Ctrl+Wheel)
- **Camera**: animate position, rotation and zoom; toggle the viewport between
  the camera view and a free view showing the camera frame
- **Cut tool** (C) and split at playhead (Ctrl+K)
- **Video export** (Ctrl+M, or the button in the top-right corner): MP4 / H.264
  (built in on Windows), PNG sequence with transparency, and — with
  [FFmpeg](https://ffmpeg.org) installed — MOV ProRes 4444, WebM VP9 and GIF
- **Panels** can be minimized to their tab (the arrow at the right of the tab bar)
- **Settings** (Ctrl+, or the gear) with *Theme*, *Interface* (timeline row
  heights, keyframe size, text size), *Editor* and *Plugins* pages: Dark, Light and Midnight themes, any
  accent color, and an *advanced mode* to edit every UI color, rounding and the
  snap distance; **import, save and export themes** (JSON, see
  [examples/themes](examples/themes))
- **Plugins**: sandboxed Lua scripts that add new kinds of objects. The bundled
  *Charts* plugin draws animated **column / bar, line / area and donut charts**
  from an editable data table (paste from a spreadsheet). See
  [docs/PLUGINS.md](docs/PLUGINS.md) to write your own
- **Undo / redo** for everything, unsaved-changes protection
- Dockable, rearrangeable panels

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
| S | Toggle snapping (hold Ctrl while dragging to skip it) |
| Ctrl+, | Settings |
| F9 / Shift+F9 / Ctrl+Shift+F9 | Easy Ease / Ease In / Ease Out on the selected keyframes |
| Wheel · Middle/right drag | Zoom · pan the viewport |
| Ctrl+0 / Ctrl+1 / Ctrl+= / Ctrl+- | Fit / 100% / zoom in / zoom out |

## Project layout

```
src/core     document model (layers, keyframes, easing, file format, undo) — no UI code
src/render   draws compositions (viewport, thumbnails, off-screen export)
src/export   video / image-sequence encoders and the export job
src/ui       theme, widgets, home screen, editor, panels and dialogs
src/app      window, main loop, OS integration, settings, preset/theme libraries, plugin host
assets       fonts and bundled plugins (assets/plugins/charts)
tests        unit tests for the core
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the pieces fit
together and [CONTRIBUTING.md](CONTRIBUTING.md) to get involved.

## License

bemotion is released under the [MIT License](LICENSE). The bundled Inter font is
licensed under the [SIL Open Font License](assets/fonts/Inter-OFL.txt).
