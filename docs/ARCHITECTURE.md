# Architecture and conventions

This is the reference for anyone changing bemotion — people and AI agents
alike. It says how the program is organized, which rules the code follows and
how to add things the way the rest of the code does, so that changes made by
different contributors (or several agents at once) fit together without
conflicts. When code and this document disagree, fix one of them in the same
change.

Contents: [1. Principles](#1-principles) ·
[2. Layers and dependencies](#2-layers-and-dependencies) ·
[3. The document model](#3-the-document-model-srccore) ·
[4. Changing a project](#4-changing-a-project-the-three-paths) ·
[5. Rendering](#5-rendering-srcrender) · [6. Media and export](#6-media-and-export) ·
[7. The editor](#7-the-editor-srcui) · [8. The application](#8-the-application-srcapp) ·
[9. Plugins](#9-plugins) · [10. Automation and AI](#10-automation-and-ai) ·
[11. Code conventions](#11-code-conventions) · [12. Recipes](#12-recipes-adding-things) ·
[13. Testing and verification](#13-testing-and-verification) ·
[14. Build, packaging, CI](#14-build-packaging-ci) ·
[15. Working in parallel](#15-working-in-parallel-humans-and-agents)

---

## 1. Principles

bemotion is a video editor (in the spirit of Premiere) with 2D animation at its
heart, free, calm rather than a cockpit, human and robot friendly, fast, and a
small core extended by plugins — see "The idea" in the [README](../README.md).
It is an idea more than a technology: the philosophy, the file format, the
operations and this document define it, so any part can be replaced or
rewritten without changing what bemotion is. The engineering principles that
follow from that:

1. **The core is the truth.** The document model (`src/core`) is plain C++20
   with no UI, graphics or OS code. Everything else is a view of it or a way
   to change it.
2. **One rule book.** What makes a project valid is written once, in
   `core/Validate`. Every way of changing a project ends in a valid project.
3. **One door from outside.** Plugins, the command line and AI tools change
   projects only through operations (`core/Ops`): validated, atomic, versioned.
   A new ability for scripts is added there, once, for all of them.
4. **What you see is what you export.** The viewport, thumbnails and export use
   the same renderer (`render/SceneRenderer`), on the CPU, deterministically:
   the same project and time give the same pixels.
5. **Never lose the user's work.** Old files keep opening, saves are atomic,
   damaged files are repaired instead of refused, undo covers everything.
6. **Industry conventions.** When in doubt, do what After Effects / Premiere
   users expect (names, shortcuts, units, behaviors).
7. **Simple and durable over clever.** Snapshot undo instead of inverse
   commands, plain structs, few dependencies with long lives (Qt, Skia,
   FFmpeg, Lua).
8. **Replaceable parts.** Libraries stay behind small seams (`Platform`,
   `ui::Draw`, the renderer, the encoders, `OpEnvironment`) and contracts are
   written down (file format, operations, plugin API), so a part can be
   rewritten — in another library or language — without touching the rest.
9. **A small core, the rest as plugins.** The download stays essential; new
   kinds of objects, commands and workflows go into plugins when they can.
10. **Calm interface.** Show what is needed now; everything else one click away
   and rearrangeable. No feature earns permanent space by default.

## 2. Layers and dependencies

```
src/app      application, command line, MCP server, plugins, settings, OS (Qt)
 └─ src/ui       Qt Widgets editor: window, panels, dialogs, theme, widgets
     ├─ src/export   export job and encoders (FFmpeg libraries, PNG)
     ├─ src/media    media decoding (FFmpeg, stb), audio playback (miniaudio)
     └─ src/render   draws compositions with Skia (screen and offscreen), text, fonts
         └─ src/core     document model, operations, validation, file format
```

Dependencies only point **down**. In particular:

| Directory | May include | Must not include |
| --- | --- | --- |
| `core` | the C++ standard library, nlohmann/json | Qt, Skia, FFmpeg, Lua, anything above |
| `render` | `core`, Skia | Qt, `ui`, `app` |
| `media`, `export` | `core`, `render`, FFmpeg, stb, miniaudio | `ui`, `app` (`Platform` path helpers are the one exception) |
| `ui` | everything below, Qt | `app` internals other than `Platform`, `Settings`, `PluginHost`, the libraries |
| `app` | everything | — |

`core` builds as its own library (`bemotion_core`) with its own tests; a core
change must never need Qt to compile. Things the core cannot do itself (probe
a media file, build a plugin object) come in as callbacks (`OpEnvironment`).

## 3. The document model (`src/core`)

### Project, composition, layers

- **`Project`** (`Project.h`) — a name and one `Composition`.
- **`Composition`** — size (16–16384 px), frame rate (1–240), background, the
  layer stack, media items, guides. **Index 0 of `layers` is the top-most
  layer.** The duration is derived: `fitDuration()` makes it end with the last
  clip (at least `kMinCompDuration`, 1 s, at most the optional `maxDuration`
  length limit). Never set it as a user value.
- **`Layer`** (`Layer.h`) — id, name, type (`Shape`, `Text`, `Camera`,
  `Plugin`, `Compound`, `Media`), timing (`inPoint` ≤ t < `outPoint`),
  `track`, `parent`, transform, the content of its type, effects. One struct
  holds the props of every type; only the ones of `type` matter.
- **Tracks** — a track is one timeline row. Several layers ("clips") may share
  a track if they do not overlap in time. **Layers of one track are always
  contiguous in `layers`.** Use the composition's track functions
  (`moveTracks`, `shiftTracks`, `moveClipsToTrack`, `moveClipsToNewTracks`,
  `timeShiftLimits`, `splitLayer`, `setInPoint` / `setOutPoint`) instead of
  editing `layers` by hand.
- **Compounds** — a compound groups layers (`Layer::parent`): children draw
  through its transform and their own transform is in its space.
  `createCompound` / `releaseCompound` keep the look when grouping and
  ungrouping; `removeLayerTree` removes a compound with its contents.
- **Backgrounds** — `LayerType::Background` fills the whole frame (color,
  gradient or picture / video): its matrix is the identity, the camera does
  not move it, the viewport does not pick it with a click (only the timeline
  and Properties select it), and new ones go to the bottom of the stack.
- **Media** — `Composition::media` lists imported files (`core/Media.h`); a
  media layer refers to one by id and stores where the source's first frame
  sits in composition time (`MediaProps::start`, moved by `shiftTime`).
- **Cameras** — the top-most active camera frames the scene (`cameraAt`).
  **3D layers** (`Layer::threeD`, top level only) have a Z position
  (`transform.depth`, `position_z`) and X / Y rotation (`rotation_x`,
  `rotation_y`, around their position); the camera has them too (it tilts and
  pans). `viewFor` gives each layer's plane as the camera sees it: a
  `Projective` (3x3) transform whose w is the distance in near-plane units
  (w < 1: too close or behind). `focalLength` is a 50 mm lens on the width;
  facing the camera at its Z it equals the 2D camera, so 2D layers (which
  follow only the camera's pan, roll and zoom) and old projects do not change.
  The renderer draws through the perspective matrix and clips each layer to
  the half-plane in front of the camera; `hitTest` and the viewport's handles
  invert it. A layer's position moves in its unturned plane (`viewFor` with
  `tilted` = false), as does its motion path. `drawOrder` is the stacking
  order with each run of neighboring 3D layers sorted by camera distance,
  farthest first. The free view shows 3D layers from the front, turned but
  without perspective.

### Identity and units

| | |
| --- | --- |
| Layer ids | `uint64_t`, unique, never reused (`Composition::nextLayerId`). The id is the identity; names are for people and may repeat. |
| Track / media ids | same rules (`nextTrackId`, `nextMediaId`). |
| Property ids | stable strings (`"position"`, `"fill_color"`, `"fx0.size"`, `"p.progress"`), used in files and operations. Never rename one; add a new id and migrate. |
| Keyframe uids | `uint32_t` per key, stable while the key exists (selection, drags). |
| Time | `double` seconds from the composition start. Equal within `kTimeEpsilon` (1e-4 s). Frames come from `snapToFrame` / `frameAt`. |
| Space | composition pixels, origin top-left, y down. Rotation in degrees clockwise. Scale, opacity, trims, volume in percent (100 = full). |
| Colors | `Color` floats 0–1, straight (not premultiplied) alpha. |
| Strings | UTF-8 everywhere, including paths (`platform::toUtf8` / `fromUtf8`). |

### Animation

- **`Animated<T>`** (`Animated.h`) — a static `base` value or a sorted list of
  `Keyframe<T>`; `valueAt(t)` interpolates with the easing of the segment's
  first key. `AnimatedBase` is the type-erased interface used by generic code.
- **Easing** (`Easing.h`) — named curves plus `Custom` cubic-beziers; stored by
  id (`easingId`).
- **`Layer::properties()`** lists every animatable property of a layer as a
  `PropertyRef` (id, group, label, kind, UI hints). It is what makes the
  inspector, timeline, file format, operations and validation generic.
  `PropertyRef` holds pointers into the layer: **use it immediately, never
  store it** across edits. Store the layer id + property id instead
  (`findProperty`).
- `propertyValue` / `setPropertyValue` / `defaultPropertyValue` read and write
  any property as 1, 2 or 4 floats.

### Effects

`Effect` (`Effects.h`) has a type and fixed animatable **slots** (`color`,
`opacity`, `size`, `offset`, `shutter`, `v1`–`v3`); each type uses some of
them (`effectParams`). Slots make effects generic for files, UI and
operations. Effect ids (`effectId`) are stable strings.

### Rules, files, history

- **`Validate`** — `validate(project)` lists violations, `repair(project)`
  fixes them all (and lists what it fixed). The rules: sizes and frame rate in
  range; unique non-zero ids with counters above them; names not empty; every
  layer on a track; tracks contiguous and without overlaps; in < out by at
  least a frame, in ≥ 0; parents exist, are compounds, no cycles; media layers
  point at existing items; known effect types; keyframes finite, sorted,
  without duplicates. **Adding a rule means adding its check and its repair
  here**, plus a test.
- **`Serialization`** — `.bemotion` is UTF-8 JSON with `"format": "bemotion"`
  and `"version"` (`Project::kFormatVersion`, now 1). Properties are stored by
  id; missing fields take defaults; unknown fields are ignored. Saving writes a
  temporary file and renames it. **Loading repairs** (so nothing above ever
  sees an invalid project). Never break old files: add fields with defaults;
  bump the version only for incompatible changes, keeping a migration.
- **`History`** — undo by snapshots: after each completed edit the serialized
  project is pushed (limit 200). Editing code does not do undo bookkeeping; it
  changes the project and lets the editor commit (§7).
- **`Describe`** — a compact text summary (for terminals and AIs).
- **`Presets`** — Quick Animations: they write ordinary keyframes. The
  *Animate* panel (`ui/panels/AnimationsPanel`) lists them; dragged onto a
  clip (`kAnimationMime`, the preset as JSON) one plays from the drop point.
- **`Transitions`** — a clip's way in and out (fade, slides, zoom, pop, spin),
  written as keyframes at its edges with the preset engine.
- **`Arrange`** — distribute layers evenly, stagger start times.
- **`Clipboard`** — copy / paste of layers as text (compounds with their
  contents, media items along), pasted at a time with fresh ids.
- **`Data`** — CSV in and out, people's numbers (`1.234,5`, `R$ 12`), and the
  two data shapes of plugin objects: chart rows and `DataTable`s.
- **`Geometry`**, **`Snapping`** — shape outlines and trim paths; 1-D snapping.

## 4. Changing a project: the three paths

| Who | How | Undo | Validation |
| --- | --- | --- | --- |
| The editor's own tools (drag, inspector) | mutate the project directly, then `EditorContext::touch()` | one step when the gesture ends (`commitIfIdle`) | by construction (they use the core's track / compound functions); files are repaired on load |
| Plugins (Lua commands), the command line, AI tools (MCP) | **operations** (`applyOps`) | a command or batch = one step | every batch, atomically |
| Opening a file | `loadProject` | — | `repair()` |

Rules:

- **New abilities for scripts or AIs are operations.** Add them to
  `core/Ops.cpp`, document them in `docs/OPERATIONS.md` (the same commit), test
  them. Do not add parallel ways (a Lua-only setter, an MCP-only edit).
- Operations are **atomic**: a batch runs on a copy and replaces the original
  only if every operation succeeded and `validate()` is empty.
- Error messages are written for the person or model who has to fix the call:
  say which operation and what is wrong, and suggest the right name
  (`suggest()`).
- The operations JSON is a public contract (`kOpsVersion`). Adding operations
  or optional fields is compatible; renaming or changing meaning is not —
  that needs a new version.
- Editor tools may keep mutating directly for interactive gestures (they need
  60 fps feedback); when an editor action is also useful to scripts, move its
  logic into the core and call it from both (the gradual direction).

## 5. Rendering (`src/render`)

- `drawComposition(canvas, comp, time, view)` draws everything; `drawLayer`
  one layer. Each layer draws in its local space with the full layer → output
  matrix on the `SkCanvas` (sharp at any zoom). A pure function of (project,
  time, view): **no randomness without a seed, no clocks, no global state**
  that changes the picture.
- **CPU raster only** (Skia "Ganesh" GPU is compiled but unused). Mind CPU
  costs: effect layers are bounded to what the layer covers (`saveLayer`
  bounds); avoid per-pixel loops in SkSL and Skia's morphology filters
  (`Dilate` / `Erode` are hundreds of ms per frame) — build effects from blurs,
  color matrices, blends and transforms instead (see Outline and Directional
  Blur).
- Effects become an `SkImageFilter` chain applied in output pixels; Motion Blur
  averages moments inside the shutter.
- **Text** (`Text.cpp`): SkShaper (HarfBuzz + ICU) with font fallback, shaped
  lines cached. **Fonts** (`Fonts.cpp`): the platform font manager plus the
  bundled Inter (`assets/fonts`).
- **Pixel formats.** `OffscreenRenderer::render` returns RGBA, straight alpha,
  top-down. When Skia draws into a `QImage` (`Format_ARGB32_Premultiplied`),
  always use `qtImageInfo()` (`ui/SkiaQt.h`): BGRA premultiplied, named
  explicitly — Skia's "N32" means RGBA on macOS.
- Plugin objects draw through `PluginCanvas` (simple primitives for Lua).
  `Mesh3D` draws triangle meshes for them (3D models): orbit camera, one
  light, back-to-front triangles in a single `drawVertices` — the 3D look
  inside the 2D scene, not a 3D engine (no depth buffer, textures, shadows).
- Media pictures come from `setMediaProvider` (installed by `MediaManager`).

## 6. Media and export

- `media/MediaIO` probes files and decodes stills (stb), audio and video
  (FFmpeg, `FfmpegMedia.cpp`). `MediaManager` caches `SkImage`s, keeps one
  video reader per (composition, layer) and decodes audio in the background.
  `AudioEngine` plays the timeline (miniaudio) and mixes audio for exports.
  `Recorder` keeps mono capture samples and a 100 Hz peak envelope in
  `RecordingBuffer`. The editor shows a growing, noneditable recording clip
  with this waveform while capturing, following capture time beyond the
  composition end. This preview stays outside the document: stopping the
  Recorder plugin creates the saved media layer once, as one undoable edit.
- `export/ExportJob` snapshots the project and feeds an `Encoder` frame by
  frame from the calling thread (`step()`), so the UI stays responsive and
  cancelling is trivial. `FfmpegEncoder` (MP4 / H.264, MOV / ProRes, WebM /
  VP9, GIF) and `PngSequenceEncoder`. `app/Automation::exportProject` is the
  one helper the command line, MCP and `--export` share.
- FFmpeg versions differ between platforms (Ubuntu 24.04 ships 6.1, MSYS2
  8.x): guard newer APIs with `LIBAVCODEC_VERSION_INT` checks.

## 7. The editor (`src/ui`)

- **`EditorContext`** — the open project, file path, selection (layer ids; the
  last one is the *primary* layer shown in Properties), selected keyframes,
  tool, playhead, playback, the compound being edited (`scope`) and history.
  - After changing the project, call **`touch()`**. The window's tick calls
    `commitIfIdle()`: while the user is still dragging or typing nothing is
    committed; when they stop, the change becomes one undo step.
  - `revision()` increases on every change, undo, redo and open; views use it
    to know when to repaint.
- **`EditorWindow`** — menus and shortcuts (`QAction`s from the keymap), the
  panels in Qt Advanced Docking System docks (layout saved in
  `layout.ads`; a saved layout missing a newer panel is replaced by the
  default), and a **16 ms timer** (`tick()`) that drives playback, media,
  audio, undo commits and every panel's `sync()`. A slower
  timer keeps crash-recovery copies and autosaves (`app/Recovery`).
  - Panel headers have only tabs and a **minimize** button (no close, undock,
    tabs-menu or pin buttons): a minimized panel hides and leaves a handle on
    the window edge that brings it back at its size. View > Panels shows or
    hides panels; double-clicking a tab maximizes it.
  - Floating helpers that are not panels (the Tools popover, the toolbar
    when it floats over the viewport) are child widgets of the window, not
    separate windows, so drag and drop keeps working. The toolbar moves by its
    grip (a drag with its own MIME type); its tools move by themselves.
    Dragging the toolbar grip reveals drop targets at the window's top,
    bottom, left and right edges, with an 8 px margin below the main header
    and inside the other edges. The toolbar floats above all panels; the grip's
    context menu offers the same positions. Normalized coordinates preserve
    the position when resizing. The optional `toolbar_in_window` setting
    defaults to false so older free viewport positions keep their meaning.
    Edge targets are accent outlines without labels, with an extra target over
    the timeline toolbar to restore it. Side positions use a vertical toolbar;
    its grip appears on hover and clears its hover/cursor state after dragging.
  - Panels minimize from a handle that appears, on hover, where the restore
    handle will be. No focus highlight on panels.
  - Never `show()` / `setVisible(true)` a widget before it has a parent: for
    a moment it is a window of its own (it flashes on screen).
- **Panels** (`ui/panels`) follow one pattern:
  - `sync()` is called every tick. It computes a cheap **structure key** (what
    the widgets depend on: selected layer, its properties, …). If the key
    changed, `rebuild()` recreates the widgets; otherwise it only refreshes
    values (`PropertyRow::refresh`, refresher lambdas).
  - Widgets never hold pointers into the document: they keep ids and look
    things up when used.
  - Custom-painted panels (viewport, timeline) compute their layout from the
    document in paint / mouse handlers and repaint on `revision()` / time
    changes.
- **`PropertyRow`** — the editor of one property (keyframe toggle, label,
  drag-to-scrub value, reset, context menu), used by Properties, Effects and the
  timeline rows.
- **Drag and drop** between panels uses the MIME types of `ui/Mime.h`.
- **`Theme`** — palettes (`Palette`) applied as a `QPalette` + style sheet.
  Colors come from the named tokens (`theme::kText`, `kTextDim`, `kAccent`,
  `kBgPanel`, `kDanger`, …), never literals; the default accent is gold
  `#E8B931` on near-black. Docking widgets get their extra rules from
  `theme::dockStyleSheet()`. **Themes change at run time**: custom-painted
  widgets read the tokens when they paint; a label colored by a token uses
  `ui::setTone(label, Tone::…)` (recolored on `appearanceChanged`), never a
  style sheet with a baked color; screens that bake colors otherwise rebuild
  on `appearanceChanged` (the home screen does).
- **`Style`** (`ui/Style.h`) — Fusion plus our check boxes and radio buttons
  (outline and check mark; an accent-filled box reads as a color swatch).
- **`Draw`** (`ui/Draw.h`) — the small drawing vocabulary over `QPainter` for
  custom-painted parts (`Rgba` packed 0xAABBGGRR, `Pt`). **Icons** are vector
  code in `Widgets.cpp` (`Icon` enum, `drawIcon`, `makeIcon`) — no icon files.
- **`Keymap`** — every shortcut is an `Action` with a default chord (After
  Effects conventions), rebindable in Settings and saved by id.
- **`Tools`** — the tools and actions users can put in the timeline toolbar
  (`toolCatalog()`), saved in `Settings::toolbar`.
- **`AppSignals`** — app-wide notifications (shortcuts, appearance, toolbar,
  plugins changed) between windows that do not own each other.
- Dialogs live in `ui/Dialogs.h` (export, Settings, Help); Settings and Help are
  `PagedWindow`s. Settings shows plugins as responsive cards with install,
  enable and folder controls. Installed plugins can be moved to Trash;
  bundled plugins can be disabled. The host checks that removal stays inside
  the user plugin folder.
- All interface text is **English**, short, sentence case, with After Effects /
  Premiere terms.

## 8. The application (`src/app`)

- **`Application`** — starts Qt, loads settings, fonts, plugins and media
  support, shows the home screen / editor, plays the intro; or runs a
  command-line command without a window (`describe`, `apply`, `mcp`,
  `--export`, …).
- **`Platform`** — OS specifics through Qt: app data folder (`%APPDATA%\bemotion`,
  overridable with `BEMOTION_APPDATA`), Documents, file and folder dialogs, file
  manager, trash, and UTF-8 helpers: **`platform::arguments()` and
  `platform::env()` instead of `argv` / `getenv`** (the Windows C runtime
  hands those over in the legacy code page), `fromUtf8` / `toUtf8` for paths.
  `platform::log()` writes `bemotion.log` (startup, projects, imports, saves,
  plugin and MCP calls, crashes).
- **`Settings`** — user preferences in `settings.json` (theme, sizes,
  shortcuts, toolbar, disabled plugins, autosave, the default length limit, the
  projects folder the user picked on the home screen…)
  and the theme library. New settings get defaults and are read tolerantly.
  The Settings window shows the essentials; *Advanced Options* (one switch for
  every page) shows the rest.
- **`Recovery`** — while a project has unsaved changes the editor keeps a copy
  in `<app data>/recovery`; saving or closing normally removes it, and copies
  left by a crash are offered back at the next start.
- **`Automation`** — the command line for scripts and AIs and the helpers it
  shares with MCP (`automationEnvironment`, `renderFramesPng`, `exportProject`).
- **`McpServer`** — `bemotion mcp`, the Model Context Protocol server (§10).
- **`PluginHost`** — Lua plugins (§9).
  The application's frame timer processes pending reloads even on the home
  screen; reloads wait until capture stops, then notify the plugin views.
- **`DevScript`** — scripted mouse / keyboard input for UI checks (§13).

## 9. Plugins

- A plugin is a folder with `plugin.json` and Lua 5.4 code; bundled ones in
  `assets/plugins`, user ones in `<app data>/plugins`. Documentation:
  [PLUGINS.md](PLUGINS.md).
- **Sandbox**: only `base`, `table`, `string`, `math`, `utf8`; no files, OS,
  network, `load` or `require`. Instruction budgets: 4 M per draw, 20 M while
  loading, 200 M per command; a plugin that exceeds them is stopped, not the
  app.
- **Objects** (`register_object`) declare parameters (animatable numbers and
  colors, toggles, choices, text) and draw with `PluginCanvas` primitives. Layers
  keep their parameter definitions, so projects still open without the plugin.
- **Commands** (`register_command`, API 2) edit through operations with a
  `doc` object; they run on a copy (one undo step; nothing changes on error).
- **Data** (API 3): objects declare `rows` (charts) or a `table`; an optional
  `import_csv(cells, o)` maps a CSV file the host read to rows / table /
  parameters (the plugin never opens files). Operations: `data.set`.
- What every user needs belongs in the app, not in a plugin (arranging layers
  and transitions are built in); plugins add kinds of objects and workflows.
- **Files and meshes** (API 4): a `file` parameter stores a path the user
  picked (saved relative to the project too); with the `files` permission the
  host reads it and hands the bytes to the plugin. `bemotion.cached` keeps
  expensive results (a parsed model), `bemotion.mesh` / `g.mesh` build and
  draw triangle meshes natively (`render/Mesh3D`). The 3D Models plugin is
  built on this.
- **Buttons and the microphone** (API 5): a command can carry a timeline
  button (`toggle` ones give `doc.pressed`), ask for playback (`doc.play`),
  and record through `media/Recorder` (miniaudio capture to WAV) with the
  `microphone` permission. The Recorder plugin is built on this.
- **Permissions** in `plugin.json`: `edit` (commands), `media`
  (`media.import`), `files` (read the files chosen in `file` parameters),
  `microphone` (record). New
  capabilities that touch the user's files or the outside world must be new
  permissions.
- The API is versioned (`bemotion.api_version`); keep old plugins working.

## 10. Automation and AI

- **Command line** (`bemotion new | describe | validate | apply | script |
  render | mcp`, `--export`) — see [AUTOMATION.md](AUTOMATION.md). Results are
  JSON or plain text on stdout; exit code 0 on success.
- **MCP server** (`bemotion mcp`) — JSON-RPC 2.0, one message per line on stdin
  / stdout; **nothing else may be written to stdout** in that mode (logs go to
  `bemotion.log`). Tools: describe, create, query, apply operations, run Lua,
  render frames (returns a PNG), validate, export, operations reference (built
  from `docs/OPERATIONS.md` at configure time). Tool failures are results with
  `isError`, not protocol errors.
- AI-facing text (tool descriptions, errors, `describe`) is part of the
  interface: keep it short, exact and actionable.

## 11. Code conventions

- **C++20**, `.clang-format` (LLVM base, 4 spaces, 120 columns). Types
  `PascalCase`, functions and variables `camelCase`, members end with `_`
  (private), constants `kName`, enums `enum class`. One main class per file
  pair, named after it.
- **Includes**: own header first; then project headers (`"core/…"`,
  `"render/…"`, …); then third-party (`"include/core/…"` for Skia, `<Q…>`,
  `<nlohmann/json.hpp>`); then the standard library — each group sorted.
- **Comments** in English, explaining *why* and the contract, not restating the
  code. Each header starts with a short comment saying what the module is for.
  Keep comment density like the surrounding code.
- **Errors**: functions that can fail return `bool` (or a result struct) with an
  error string for people; no exceptions across module boundaries (operations
  use them internally and convert). Never crash on bad input — files, scripts
  and AIs send bad input on purpose and by accident.
- **No global mutable state** except the documented singletons (`settings()`,
  `PluginHost::get()`, `MediaManager::get()`, `AudioEngine::get()`, theme
  tokens). The editor is single-threaded; background work (audio decoding)
  hands results back through the owning manager.
- **Paths and text**: `std::filesystem::path` built with `platform::fromUtf8`;
  never pass `path.string()` to APIs on Windows. Strings are UTF-8.
- **Pixels**: see §5 (explicit color types when wrapping images).
- **Dependencies**: adding a library is a design decision (size, license,
  platforms) — propose it first. Prefer what Qt, Skia, FFmpeg or the standard
  library already provide.

## 12. Recipes: adding things

**An animatable property** — add an `Animated<T>` member to the right props
struct (`Layer.h`) with its default value, and list it in
`Layer::properties()` with a new stable id (`defaultPropertyValue`, used by the
reset buttons and repair, derives defaults from the `make*Layer` factories). The
inspector, timeline, file format, operations, validation and `describe` pick it
up. Add a test (round trip). Mention it in `docs/OPERATIONS.md` (property ids).

**An effect** — add the `EffectType`, its row in `kEffects` (id, label,
category, description) and `effectParams` (which slots, labels, ranges) in
`Effects.cpp`, and its case in `effectChain` (`SceneRenderer.cpp`), built from
cheap filters (§5). Check speed with `bemotion --export` on a test project.
Update the effect list in `docs/OPERATIONS.md`.

**An operation** — a method in `Applier` (`core/Ops.cpp`) registered in the
operation table; validate every field with the `field` / `number` / `text`
helpers so errors are precise; return created ids. Tests in
`tests/core_tests.cpp`, reference in `docs/OPERATIONS.md`. MCP and Lua get it
automatically.

**A validation rule** — check and repair in `core/Validate.cpp` (both), a test
that breaks and repairs it; make sure existing projects still pass
(`bemotion validate` on the test projects).

**A layer type** — a `LayerType` value (at the end), its props struct and
factory, its properties and `defaultPropertyValue`, its matrix and bounds, its
drawing in `SceneRenderer`, serialization, the type range in `Validate`, its
`layer.add` type and `layerTypeName`, hit testing and handles in the viewport,
an icon, an Objects panel tile and drop kind, Properties rows for its
non-animatable settings. This touches many files: plan it as one change
(the Background type is a complete example).

**A shortcut** — an `Action` with its default chord in `Keymap.cpp`, wired in
`EditorWindow` (`action(...)`); it appears in Settings and Help automatically.

**A toolbar tool** — an entry in `toolCatalog()` (`Tools.cpp`) with
`toolEnabled` / `toolActive` / `runTool` behavior.

**A panel** — a `QWidget` with `sync()` (structure key + refresh), created and
docked in `EditorWindow::buildDocks`, listed in *View › Panels*. Saved layouts
without it fall back to the default layout automatically.

**A setting** — a field with a default in `Settings`, read and written in
`Settings.cpp`, a control in `SettingsWindow.cpp`, and a reset in its page.

**A plugin capability** — an operation if it edits; a `doc` / `bemotion`
function if it is plugin-only; a permission if it reaches outside the
project; document it in `docs/PLUGINS.md` and bump the API version when it
changes behavior.

## 13. Testing and verification

- **Core tests** (`tests/core_tests.cpp`, `ctest`): every change to `src/core`
  comes with tests. Keep them small and deterministic; no heavy fuzzing
  infrastructure.
- **Smoke test** (`tools/smoke-test.sh <build dir>`): exports the Welcome
  project to every format (checked with ffprobe), drives the automation
  commands and the MCP server (`tools/mcp-test.py`), and opens the app
  offscreen. CI runs it on the build and on the packages.
- **Visual checks of the interface**: run the app with
  `BEMOTION_APPDATA=<scratch folder>` (**never** touch the user's real
  settings), a `BEMOTION_SCRIPT=steps.txt` (mouse, keys, `capture`, `window
  TITLE` for other windows; see `app/DevScript.h`) and look at the captures.
  `BEMOTION_SCRIPT_TRACE` logs the steps; `QT_QPA_PLATFORM=offscreen` works
  without a screen. On Windows, launch these runs from PowerShell or Python:
  MSYS2's shell sometimes starts programs without their environment variables.
  Scripted input cannot drive a real drag (`QDrag`), a native file dialog or
  the real cursor: test those paths another way and report what was not seen.
- **Rendering checks**: `bemotion render <project> --count 6 --out sheet.png`.
- Before committing: build, core tests, and the smoke test (or at least the
  part your change touches).

## 14. Build, packaging, CI

- CMake + Ninja. Windows builds with **MSYS2 UCRT64** (GCC); Linux with apt
  packages; macOS with Homebrew. Skia is built once by
  `tools/skia/build-skia.sh` (pinned revision, no AVX2 / AVX-512 code paths:
  the binaries run on any x86-64 CPU). Third-party code fetched by CMake is
  pinned to exact versions.
- `tools/build-release.sh` (or `build-release.bat`): build, test, package,
  check → `dist/bemotion-<version>-setup.exe` (NSIS, per-user, no admin) and
  `-portable.zip`. `tools/package-linux.sh` (AppImage) and
  `tools/package-macos.sh` (`.app` / `.dmg`) do the same for the other systems.
- CI (`.github/workflows/build.yml`) builds, tests, smoke-tests and packages on
  Windows, Linux and macOS, and uploads the packages as artifacts.
- The version is `project(VERSION …)` in the top `CMakeLists.txt`.

## 15. Working in parallel (humans and agents)

- **Read before writing.** Look at how neighboring code does the same thing and
  match it (naming, comments, helpers). Reuse helpers instead of adding a
  second way.
- **Stay in your lane.** Change the files your task needs; do not reformat,
  rename or "clean up" unrelated code in the same change — it creates
  conflicts for everyone else working at the same time.
- **Contracts change deliberately.** The file format, the operations JSON, the
  plugin API, property ids, effect ids, settings keys and command-line
  options are used by files and scripts in the wild. Extend compatibly; if a
  break is unavoidable, version it and migrate.
- **One change, one commit**, with docs and tests in the same commit. Commit
  messages: a short imperative subject (≤ 72 characters), a blank line, then
  what changed and why. Work written with an AI assistant ends with a
  `Co-Authored-By:` trailer.
- **Keep the tree clean.** Build output goes in `build*/`, packages in `dist/`
  (both ignored); scratch files and test captures stay outside the repository.
- **Ask before** adding dependencies, changing contracts, or deleting user
  data. Never delete or overwrite files outside the repository you did not
  create.
- **Leave it verified**: build, tests and smoke test pass, and anything visual
  was looked at.
