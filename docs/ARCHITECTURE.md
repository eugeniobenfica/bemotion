# Architecture

bemotion is split into layers. Dependencies only point downwards:

```
app     application window, OS integration (Qt: dialogs, paths, trash), command line
 └─ ui      Qt Widgets: theme, widgets, home screen, editor window, dock panels, dialogs
     └─ export  encoders (FFmpeg libraries, PNG) and the export job
         └─ render  draws a Composition onto a Skia canvas (on screen or off screen)
             └─ core    document model — plain C++, no UI or graphics code
```

`core` builds as its own static library (`bemotion_core`) and is covered by the
tests in `tests/`.

## Document model (`src/core`)

- **`Animated<T>`** (`Animated.h`) — a property that is either a static value
  or a sorted list of keyframes. `valueAt(t)` interpolates using the easing of
  the segment's first key. `AnimatedBase` is the type-erased interface the UI
  uses to add, move and delete keys without knowing `T`.
- **`Layer`** (`Layer.h`) — name, timing (`inPoint`/`outPoint`), transform and
  either shape or text content. `Layer::properties()` lists every animatable
  property as a `PropertyRef` (id, group, label, UI hints).
  **To add a property:** declare an `Animated<T>` member and add one line to
  `properties()` — the inspector, timeline and file format pick it up
  automatically.
- **`Composition` / `Project`** (`Project.h`) — canvas size, frame rate,
  duration, background and the layer stack (index 0 is the top layer). The
  duration is not set by hand: `fitDuration` makes it end with the last clip
  (the editor calls it whenever an edit is committed). Layers
  carry a `track` id: a track is one timeline row and may hold several
  non-overlapping layers ("clips"); same-track layers are kept contiguous. Stack
  operations live here so they are testable: `moveTracks`, `shiftTracks`,
  `moveClipsToTrack` / `moveClipsToNewTracks` (dragging clips between rows),
  `timeShiftLimits` (no overlaps) and `splitLayer` (cut tool).
- **`Serialization`** — the `.bemotion` format is versioned JSON. Properties are
  stored by their stable `id`, so files stay readable when properties are
  added or reordered. Saves go through a temporary file to avoid corruption.
  Files from before the rename (format id `"mirage"`) still load.
- **`History`** — undo/redo by snapshots: after each completed edit the whole
  project is serialized. Editing code never deals with undo.
- **`Validate`** — every rule a project must follow, in one place (sizes,
  unique ids, tracks contiguous and without overlaps, compounds that exist,
  sorted keyframes, finite numbers…). `validate()` reports, `repair()` fixes;
  every loaded file is repaired, so the rest of the program only sees valid
  projects.
- **`Ops`** — operations: the one way to change a project from outside the
  interface (plugins, the command line, AI tools). JSON in, JSON out, atomic
  batches checked by `validate()`, errors that name the culprit and suggest a
  fix. Reference: [OPERATIONS.md](OPERATIONS.md). New capabilities for
  scripts are added here, once, for everyone.
- **`Describe`** — a compact text summary of a project (for terminals and AIs).
- **`Presets`** — "Quick Animations"; they only write regular keyframes.
- **`Geometry`** — shape outlines and trim paths.

## Rendering (`src/render`)

`SceneRenderer` draws a composition onto any `SkCanvas`. Each layer draws in
its own local space with the full layer → output matrix on the canvas, so
shapes, text and footage stay sharp at any zoom, rotation or scale. A layer
with effects is drawn into an offscreen layer (`saveLayer`, bounded to what
the layer covers) whose image-filter chain applies the effect stack in output
pixels: Skia blurs, drop shadows and color matrices, SkSL runtime effects for
the color operations and Wave Warp. Motion Blur averages several moments
inside the shutter with additive layers.

`Text` shapes with SkShaper (HarfBuzz + ICU: ligatures, complex scripts,
bidirectional text, per-character font fallback) and caches shaped lines;
`Fonts` wraps the platform font manager (DirectWrite, CoreText, fontconfig)
plus the bundled Inter.

`OffscreenRenderer` renders into a raster `SkSurface` and reads the pixels
back, so exports look exactly like the preview: it is the same code path.
Skia itself is built by `tools/skia/build-skia.sh` from a pinned revision,
without AVX2 / AVX-512 code so the binaries run on any x86-64 CPU.

## Export (`src/export`)

`ExportJob` snapshots the project, renders frame by frame and feeds an
`Encoder`. It is stepped from the UI thread a few frames at a time, which keeps
the progress dialog responsive and makes cancelling trivial. Encoders:

- `FfmpegEncoder` — FFmpeg's libraries, linked in: MP4 / H.264 (libx264, then
  OpenH264 or the hardware encoders available), MOV / ProRes 4444, WebM / VP9
  and GIF (palette built by an FFmpeg filter graph), with AAC / PCM / Opus audio.
- `PngSequenceEncoder` — numbered PNGs via stb_image_write, with alpha.

Audio is mixed once when the job starts (`Settings::mixAudio`, provided by the
media module) and muxed into the file; PNG sequences get a `.wav` beside them.

## Media (`src/media`)

Imported files live in `Composition::media` (`core/Media.h`); a media layer
points at one by id and stores where the source's frame 0 sits in composition
time (`MediaProps::start`, moved along by `shiftTime`), so cut, moved and
trimmed clips stay in sync with their source.

- `MediaIO` probes files and decodes images (stb_image); audio and video go
  through FFmpeg (`FfmpegMedia.cpp`): audio is resampled to stereo float, and
  `VideoReader` seeks and decodes video frames to RGBA.
- `MediaManager` is the runtime side: `SkImage`s for stills, frames and thumbnails,
  one video reader per (composition, layer) so the editor and an export never
  fight over seeking, and audio decoded in the background. It plugs into the
  renderer through `render::setMediaProvider`.
- `AudioEngine` plays the timeline through a miniaudio device (resynced to
  the playhead when it jumps) and mixes audio offline for exports.

## Editor (`src/ui`)

- **`EditorContext`** holds the open project, the multi-layer selection (the
  last selected layer is the *primary* one shown in Properties), the active
  tool, playhead and history. Panels mutate the project directly and call
  `ctx.touch()`; the change becomes one undo step when the user releases the
  mouse / stops typing (`commitIfIdle`).
- **`EditorWindow`** is the workspace: menus and shortcuts (`QAction`s bound
  from the keymap), and the panels in Qt Advanced Docking System docks: drag
  tabs to rearrange or float them, pin a panel to auto-hide it on the window
  edge, double-click a tab to maximize it; the layout is saved and *View →
  Reset Layout* restores the default. A 16 ms timer drives playback, media,
  audio and the undo commits, and calls each panel's `sync()`.
- **Panels** (`ui/panels`) are Qt widgets: `ObjectsPanel`, `MediaPanel` and
  `ToolsPanel` (left), `ViewportPanel` (center: the composition rendered by
  Skia plus overlays drawn with `ui::Draw` over `QPainter`), `PropertiesPanel`
  and `EffectsPanel` (right) and `TimelinePanel` (bottom: custom painted, with
  real editors in its property rows and the graph editor in
  `TimelineGraph.cpp`). Panels rebuild their widgets only when the structure
  changes (another layer, other properties) and otherwise just refresh values,
  using `EditorContext::revision()` to know when to repaint. Drag and drop
  between panels uses the MIME types of `ui/Mime.h`.
- **`PropertyRow`** is the shared editor of one animatable property (keyframe
  toggle, label, drag-to-scrub value, reset, context menu), used by Properties,
  Effects and the timeline.
- **`ToolsPanel`** lists the tools and actions of `ui/Tools.h`; the user drags
  them into the timeline's toolbar (saved as `Settings::toolbar`).
- **Dialogs** (`ui/Dialogs.h`): export (settings, progress, result), Settings
  and Help (both `PagedWindow`s), and the New Project dialog of the home screen.
- **`IntroScreen`** plays `assets/welcome/Welcome.bemotion` full-window at
  startup (skippable, can be turned off in Settings). The same file is the
  Welcome project users can open, so the intro is edited in bemotion itself.
- **`Theme`** defines the palette (dark neutrals + golden accent `#E8B931`)
  and applies it to Qt as a `QPalette` plus a style sheet. Use the named colors
  from `Theme.h` instead of literals.
- **`Widgets`** has the vector icon set and custom controls (accent button,
  object tile, keyframe toggle). The logo is `drawLogoMark`, built from the
  shared geometry in `render/LogoMark.h` (also used by the app icon).

## App (`src/app`)

`Application` creates the `QApplication` and the main window, which stacks the
home screen and the editor with the intro on top while it plays. With
`--export` it renders headlessly from the command line instead (no window).
`Platform` wraps OS specifics through Qt: known folders, native file dialogs,
the file manager, the trash and UTF-8 paths. `Automation` is the command line
for scripts and AIs (`describe`, `validate`, `apply`, `script`, `render`,
`new`; see [AUTOMATION.md](AUTOMATION.md)) and `bemotion mcp`, an MCP server
(`McpServer`) with the same abilities as tools for AI assistants; it also provides what operations need
from above the core (media probing, plugin objects). `PluginHost` runs Lua
plugins: objects that draw, and commands that edit through operations on a
copy of the project (one undo step, nothing changes on error).

### Developer aids

- `<app data>/bemotion.log` (`platform::log`) records startup, opened and saved
  projects, imports and crashes; it starts fresh at each launch.
- Paths, arguments and environment variables are UTF-8 everywhere
  (`platform::arguments`, `platform::env`): on Windows the C runtime would hand
  them over in the legacy code page.
- `tools/smoke-test.sh <dir>` exports the Welcome project to every format and
  opens the app offscreen; `tools/package-windows.sh` builds the installer
  (`tools/installer/bemotion.nsi`) and the portable zip.
- `BEMOTION_CAPTURE=shot.png` saves the window at frame `BEMOTION_CAPTURE_FRAME`
  (default 60, about 16 ms each) and quits.
- `BEMOTION_APPDATA=dir` keeps settings, layout and caches in `dir` instead of
  the user's app data folder (use it for scripted UI tests).
- `BEMOTION_SCRIPT=steps.txt` replays scripted mouse/keyboard input and can
  capture windows along the way (see `app/DevScript.h`), so interactions can be
  checked without anyone at the screen; `BEMOTION_SCRIPT_TRACE=log.txt` logs
  each command as it runs.
- `bemotion --write-icon <file.ico|png|svg>` / `--write-mark <file.svg>`
  regenerate the brand files in `assets/branding` from `render/LogoMark.h`.
- Configure with `-DBEMOTION_CONSOLE=ON` to keep a console window for logs and
  assertion messages.
