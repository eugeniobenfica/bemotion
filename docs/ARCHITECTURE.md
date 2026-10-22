# Architecture

bemotion is split into layers. Dependencies only point downwards:

```
app     window, main loop, OS integration (GLFW, file dialogs, paths), command line
 └─ ui      theme, widgets, home screen, editor, panels, dialogs (Dear ImGui)
     └─ export  encoders (Media Foundation, FFmpeg, PNG) and the export job
         └─ render  draws a Composition into an ImDrawList (on screen or off screen)
             └─ core    document model — plain C++, no UI or GPU code
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
- **`Presets`** — "Quick Animations"; they only write regular keyframes.
- **`Geometry`** — shape outlines and trim paths.

## Rendering (`src/render`)

`SceneRenderer` draws through Dear ImGui's `ImDrawList`, the same path used by
the UI. Each primitive is generated in layer space, rasterized at roughly
screen density, and its vertices are then transformed by the layer → screen
matrix. That gives rotation and non-uniform scale for shapes *and* text with a
single code path. Text uses ImGui 1.92's dynamic font atlas; raster sizes are
quantized to keep the atlas small while zooming.

`OffscreenRenderer` renders the same draw lists into an OpenGL framebuffer and
reads the pixels back, so exports look exactly like the preview.

## Export (`src/export`)

`ExportJob` snapshots the project, renders frame by frame and feeds an
`Encoder`. It is stepped from the UI thread a few frames at a time (the GL
context lives there), which keeps the progress dialog responsive and makes
cancelling trivial. Encoders:

- `MediaFoundationEncoder` — MP4/H.264 on Windows with no extra software
  (frames are converted to NV12, BT.709).
- `FfmpegEncoder` — pipes raw RGBA into an `ffmpeg` process for MP4 on other
  systems and for ProRes 4444, WebM VP9 and GIF.
- `PngSequenceEncoder` — numbered PNGs via stb_image_write, with alpha.

Audio is mixed once when the job starts (`Settings::mixAudio`, provided by the
media module) and handed to the encoder: an AAC stream in Media Foundation
MP4s, a temporary WAV input for FFmpeg, a `.wav` beside PNG sequences.

## Media (`src/media`)

Imported files live in `Composition::media` (`core/Media.h`); a media layer
points at one by id and stores where the source's frame 0 sits in composition
time (`MediaProps::start`, moved along by `shiftTime`), so cut, moved and
trimmed clips stay in sync with their source.

- `MediaIO` probes files and decodes images (stb_image) and audio (miniaudio;
  Media Foundation for audio in video containers). `VideoReader` reads video
  frames (Media Foundation on Windows, a stub elsewhere).
- `MediaManager` is the runtime side: GL textures for images and thumbnails,
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
- **Panels** (`ui/panels`) are independent classes with a `draw(ctx)` method:
  `ObjectsPanel` (left), `ViewportPanel` (center), `PropertiesPanel` (right)
  and `TimelinePanel` (bottom). They live in an ImGui dockspace, so users can
  rearrange them; *View → Reset Layout* restores the default. Objects are
  dragged onto the viewport with an ImGui drag-and-drop payload
  (`kObjectPayload`).
- **`ToolsPanel`** lists the tools and actions of `ui/Tools.h`; the user drags
  them into the timeline's toolbar (saved as `Settings::toolbar`).
- Minimized panels are not drawn at all (`ui::panelHidden`): their neighbors
  take the space and `drawHiddenPanelHandles` leaves a restore handle on the
  window edge they came from.
- **`ExportDialog`** — settings, progress and result of an export.
- **`IntroScreen`** plays `assets/welcome/Welcome.bemotion` full-window at
  startup (skippable, can be turned off in Settings). The same file is the
  Welcome project users can open, so the intro is edited in bemotion itself.
- **`Theme`** defines the palette (dark neutrals + golden accent `#E8B931`).
  Use the named colors from `Theme.h` instead of literals.
- **`Widgets`** has the vector icon set and custom controls (accent button,
  object tile, keyframe toggle). The logo is `drawLogoMark`, built from the
  shared geometry in `render/LogoMark.h` (also used by the app icon).

## App (`src/app`)

`Application` owns the GLFW window and ImGui context and switches between the
home screen and the editor. Frames are paced in software at the monitor
refresh rate (driver vsync can stall on some GPUs). With `--export` it renders
headlessly from the command line instead. `Platform` wraps OS specifics: known
folders, native file dialogs and UTF-8 paths.

### Developer aids

- `BEMOTION_CAPTURE=shot.bmp` saves frame `BEMOTION_CAPTURE_FRAME` (default 60)
  to a BMP and quits.
- `BEMOTION_APPDATA=dir` keeps settings, layout and caches in `dir` instead of
  the user's app data folder (use it for scripted UI tests).
- `BEMOTION_SCRIPT=steps.txt` replays scripted mouse/keyboard input and can
  capture frames along the way (see `app/DevScript.h`), so interactions can be
  checked without anyone at the screen.
- `bemotion --write-icon <file.ico|png|svg>` / `--write-mark <file.svg>`
  regenerate the brand files in `assets/branding` from `render/LogoMark.h`.
- Configure with `-DBEMOTION_CONSOLE=ON` to keep a console window for logs and
  assertion messages.
