# Architecture

Mirage is split into four layers. Dependencies only point downwards:

```
app     window, main loop, OS integration (GLFW, file dialogs, paths)
 └─ ui      theme, widgets, home screen, editor + panels (Dear ImGui)
     └─ render  draws a Composition into an ImDrawList
         └─ core    document model — plain C++, no UI or GPU code
```

`core` builds as its own static library (`mirage_core`) and is covered by the
tests in `tests/`. Keeping it free of UI code means future features such as a
video exporter or a command line renderer can reuse it as is.

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
  duration, background and the layer stack (index 0 is the top layer).
- **`Serialization`** — the `.mirage` format is versioned JSON. Properties are
  stored by their stable `id`, so files stay readable when properties are
  added or reordered. Saves go through a temporary file to avoid corruption.
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

## Editor (`src/ui`)

- **`EditorContext`** holds the open project, selection, playhead and history.
  Panels mutate the project directly and call `ctx.touch()`; the change
  becomes one undo step when the user releases the mouse / stops typing
  (`commitIfIdle`).
- **Panels** (`ui/panels`) are independent classes with a `draw(ctx)` method:
  `ObjectsPanel` (left), `ViewportPanel` (center), `PropertiesPanel` (right)
  and `TimelinePanel` (bottom). They live in an ImGui dockspace, so users can
  rearrange them; *View → Reset Layout* restores the default.
- **`Theme`** defines the palette (dark neutrals + golden accent `#E8B931`).
  Use the named colors from `Theme.h` instead of literals.
- **`Widgets`** has the vector icon set and custom controls (accent button,
  object tile, keyframe toggle).

## App (`src/app`)

`Application` owns the GLFW window and ImGui context and switches between the
home screen and the editor. Frames are paced in software at the monitor
refresh rate (driver vsync can stall on some GPUs). `Platform` wraps OS
specifics: known folders, native file dialogs and UTF-8 paths.

Developer aid: setting `MIRAGE_CAPTURE=shot.bmp` saves frame
`MIRAGE_CAPTURE_FRAME` (default 60) to a BMP and quits — handy for checking UI
changes without a display. Configure with `-DMIRAGE_CONSOLE=ON` to keep a
console window for logs and assertion messages.
