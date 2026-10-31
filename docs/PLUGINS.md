# Writing plugins

A plugin is a folder with a `plugin.json` manifest and a Lua 5.4 script that
registers new object types. Bundled plugins live in `assets/plugins`; user
plugins in `<app data>/bemotion/plugins` (*Plugins → Install Plugin…* copies a
folder there). The bundled [Charts plugin](../assets/plugins/charts/main.lua)
is a complete example.

**Official plugins** are the folders in `assets/plugins`. A release can leave
some out (`BEMOTION_LEAVE_OUT_PLUGINS="models3d recorder"` for the package
scripts); users get them from *Settings › Plugins › Show Official Plugins*,
which reads `assets/plugins/index.json` from the repository's main branch on
GitHub (only when clicked), downloads the plugin's files, checks each one's
SHA-256 against the catalog, then installs it like *Install Plugin…* (a
downloaded one replaces a bundled one of the same id, so this also updates).
Plugins needing a newer `api` than the app are not offered. After changing an
official plugin, run `tools/plugin-index.py` (a test fails while the catalog
is out of date). `BEMOTION_PLUGIN_CATALOG` points the app at another catalog:
a mirror, or a local folder (`file:///.../plugins/`) for testing offline.
*Essentials only* (Settings › Interface) loads no plugin at all.

Settings > Plugins displays cards for installed and bundled plugins, with
installation and enable controls. Remove moves an installed plugin folder to
Trash; bundled plugins stay installed and can be disabled.

```json
{ "id": "myplugin", "name": "My Plugin", "version": "1.0.0", "author": "me",
  "description": "What it adds.", "main": "main.lua", "api": 3 }
```

`api` is the version the plugin was written for: 1 objects, 2 commands and
operations, 3 tables and CSV import, 4 files and 3D meshes, 5 command buttons
and the microphone, 6 include, 7 row labels, 8 settings, 9 `load_mesh`.
`bemotion.api_version` tells the running version.

Scripts run in a sandbox (base, table, string, math and utf8 only — no files,
OS or network) with an instruction budget per draw call and at most 512 MB of
memory per plugin (past it, an allocation fails with "not enough memory").

## Registering an object

```lua
bemotion.register_object {
  id = "badge", name = "Badge", icon = "bars",   -- icons: bars, line_chart, pie, text, rectangle...
  size = { 600, 300 },                           -- default frame size
  rows = { { "A", 10, "#E8B931" } },             -- optional chart data (label, value, color)
  -- table = { header = { "Name", "Score" }, rows = { { "Ana", "12" } } },  -- or a table of text cells
  -- import_csv = function(cells, o) ... end,   -- optional: how a CSV file becomes this object's data
  params = {
    { id = "progress", label = "Progress", type = "number", default = 100, min = 0, max = 100,
      format = "%.0f%%", group = "Animation", intro = { from = 0, duration = 1.2 } },
    { id = "fill", type = "color", default = "#4F8EF7" },
    { id = "rounded", type = "toggle", default = true },
    { id = "style", type = "choice", choices = { "Flat", "Outline" }, default = "Flat" },
    { id = "caption", type = "text", default = "Hello" },
    { id = "logo", type = "file", accept = { "glb", "obj" } },  -- API 4, needs the "files" permission
  },
  draw = function(o, g) ... end,
}
```

`number` and `color` parameters are animatable (keyframes, timeline, reset);
`intro` adds keyframes when the object is created.

## draw(o, g)

`o` holds every parameter by id (colors as `{r, g, b, a}`, choices as the
chosen string), plus `width`, `height`, `time` (seconds since the clip
start), `duration`, `rows` (objects with chart data) and `table` (`{header =
{...}, rows = {{...}, ...}}`, cells as text, for objects with a table).
Coordinates are local: the origin is the object's center and the frame spans
±width/2, ±height/2.

`g` draws: `rect(x, y, w, h, color[, radius])`, `rect_outline(x, y, w, h,
color, width[, radius])`, `circle(x, y, r, color)`, `line(x1, y1, x2, y2,
width, color)`, `polyline({x1, y1, ...}, width, color[, closed])`,
`polygon({x1, y1, ...}, color)`, `clip(x, y, w, h)` / `unclip()` (API 6: later
drawing only inside that box), `arc(cx, cy, inner, outer, fromDeg, toDeg,
color)`, `text(s, x, y, size, color[, "left"|"center"|"right"[, bold]])`
(y = line center) and `measure(s, size[, bold])`.

`bemotion.include("maps/world.lua")` (API 6) runs a Lua file of the plugin's
own folder once and returns what it returns: data and code split into files.

Helpers: `bemotion.ease(name, t)` (`"ease_out"`, `"back_out"`, `"bounce"`…),
`bemotion.color(hex)`, `bemotion.mix(a, b, t)`, `bemotion.with_alpha(c, k)`,
`bemotion.format(value, decimals)`, `bemotion.number(text)` (a number written
by people — `"1.234,5"`, `"R$ 12"`, `"45%"` — or `nil`) and
`bemotion.parse_csv(text)` (rows of cells; comma, semicolon or tab). `print`
goes to the Plugin Manager log. Colors can be `"#RRGGBB[AA]"` strings or tables.

`bemotion.load_mesh(file, { up = "Auto" | "Y Up" | "Z Up", max_triangles = n })`
(API 9) reads a model file natively — OBJ, STL, glTF / GLB, a `.gltf`'s `.bin`
files from beside it — into a mesh for `g.mesh`, simplified to at most
`max_triangles` (0: all), many times faster than parsing it in Lua; `nil` and
why when it cannot. The 3D Models plugin is built on it (its *Detail*).

## Settings (API 8)

A plugin can declare settings of its own in `plugin.json`; Settings › Plugins
shows a *Settings…* button on its card that opens them (applied at once, kept
in the app's settings — not in projects — with a Reset to the defaults):

```json
"settings": [
  { "id": "grid_width", "label": "Grid line width", "kind": "number", "default": 1.5,
    "min": 0.5, "max": 6, "step": 0.05, "format": "%.1f px", "description": "..." },
  { "id": "play", "label": "Play while recording", "kind": "toggle", "default": true },
  { "id": "corners", "label": "Corners", "kind": "choice", "choices": ["Rounded", "Square"], "default": "Rounded" },
  { "id": "name", "label": "Clip name", "kind": "text", "default": "Recording" }
]
```

`bemotion.setting(id)` returns the user's value (or the default): a number, a
boolean, the chosen label or the text; `nil` for an id not declared. Changing
one redraws the plugin's objects. The bundled Charts (grid, corners), Recorder
(play while recording, clip name) and 3D Models (turn while revealing) plugins
use them.

## Files and 3D meshes (API 4)

A `file` parameter lets the user pick a file in Properties (`accept` lists the
extensions). The project stores its path (relative to the project too, so a
moved folder still works). With the **`files` permission** in `plugin.json`,
`o.<id>` is `{ path, name, ext, data, version }` — `data` is the file's bytes,
read by bemotion; without it, or with no file chosen, it is `nil`. Plugins
still never open files themselves.

Parsing every frame would be slow: `bemotion.cached(key, fn)` runs `fn` once
per key (with the larger command budget) and returns the same value
afterwards. Key it by `file.path .. file.version`, so a changed file is read
again. Helpers for reading: `string.unpack` (binary), `bemotion.json(text)`,
`bemotion.base64(text)`.

`bemotion.mesh{...}` builds a triangle mesh once (keep it with `cached`), and
`g.mesh(mesh, view)` draws it projected, lit and depth sorted, fast (native):

```lua
local mesh = bemotion.cached(file.path .. file.version, function()
  return bemotion.mesh {
    parts = { -- one per material / node; or the fields below directly
      { positions = { x, y, z, ... },     -- or packed float32 bytes
        normals = { ... },                -- optional (computed: smooth)
        indices = { 1, 2, 3, ... },       -- optional; or packed bytes + index_size = 1 | 2 | 4 (0-based)
        color = "#E8B931",                -- or colors = { r, g, b, a, ... } per vertex (color_size = 3 for rgb)
        matrix = { 16 numbers },          -- optional, column-major (glTF)
      },
    },
  }
end)
g.mesh(mesh, {
  width = o.width, height = o.height, -- the frame it fits in
  rotation = { tilt, spin, roll },    -- degrees
  zoom = 1, fov = 30,                 -- fov 0: orthographic
  light = { x, y, z }, ambient = 0.35, shine = 0.25,
  tint = "#FFFFFF", model_colors = true, flat = false, cull = false,
})
```

`mesh.triangles` and `mesh.vertices` count them. It is a 2D drawing of a
3D model, not a 3D engine: triangles are painted back to front (surfaces that
cut through each other may sort wrongly), with one light and no textures or
shadows. The bundled **3D Models** plugin (glTF / GLB, OBJ, STL) is a complete
example.

## Data: rows, tables and CSV (API 3)

An object declares its data with `rows` (charts: label, value, color) or
`table` (any grid of text). The Properties panel edits it, pastes it from a
spreadsheet and imports CSV files; operations set it with `data.set`.

*Import CSV…* reads the file (the plugin never touches files) and hands its
cells to the object's `import_csv(cells, o)`, which returns what to change:

```lua
import_csv = function(cells, o)
  local rows = {}
  for i = 2, #cells do -- skip the header
    rows[#rows + 1] = { cells[i][1], bemotion.number(cells[i][2]) or 0 }
  end
  return { rows = rows, params = { title = cells[1][2] } } -- also `table = {header, rows}`
end,
```

Without `import_csv`, charts take the first text column as labels and the
first numeric column as values, and tables take the first line as the header.

`row_labels(o)` (API 7, optional) tells which labels a row can take — a map's
regions. The Properties panel then completes labels as they are typed, adds a
row by typing part of a name (*Add: type a name…*, or *Add All*), and points
out the rows naming none of them, which the object returns as a second list
(1-based row numbers):

```lua
row_labels = function(o)
  return { "Acre", "Alagoas", ... }, { 3 } -- the names; row 3 is none of them
end,
```
The bundled Charts plugin does both, plus a **Table** object.

## Commands: editing the project (API 2)

Besides objects, a plugin can add **commands** to the *Plugins* menu: Lua that
changes the project through [operations](OPERATIONS.md), the same ones the
command line and AI tools use. Declare the permission in `plugin.json`:

```json
{ "id": "tidy", "name": "Tidy", "main": "main.lua", "api": 3, "permissions": ["edit"] }
```

```lua
bemotion.register_command {
  id = "pop_in", name = "Pop In Selected",
  description = "Each selected layer springs in, one after another",
  run = function(doc)
    doc.apply { op = "arrange.stagger", layers = doc.selection, step = 0.15 }
    for _, id in ipairs(doc.selection) do
      doc.apply { op = "transition.add", layer = id, transition = "pop", edge = "in", duration = 0.6 }
    end
  end,
}
```

`doc` is the project while the command runs:

- `doc.apply(op)` or `doc.apply({op, op, ...})` runs operations right away and
  returns their results (`doc.apply{op = "layer.add", ...}.layer` is the new
  id). A failing operation raises an error with the reason.
- `doc.layers()`, `doc.layer(id_or_name[, time])`, `doc.composition()` read it
  (the `query.*` operations).
- `doc.expression(layer, prop[, expression[, time]])` works an
  [expression](EXPRESSIONS.md) out without setting it (`query.expression`; by
  default the property's own, at `doc.time`): the value (a number or a list),
  or `nil` and what is wrong — `local v, err = doc.expression(id, "rotation",
  "time * 90")`.
- `doc.time` is the playhead, `doc.selection` the selected layer ids.

A command is **one undo step**, and it runs on a copy: if it raises an error
(or an operation fails), the project stays exactly as it was and the message
goes to the plugin's log. Permissions: `edit` for commands, `media` to use
`media.import`, `files` to read the files chosen in `file` parameters,
`microphone` to record.

### Buttons, playback and the microphone (API 5)

A command can have a button in the timeline's toolbar (next to its controls on
the right): `button = { place = "timeline", icon = "mic", toggle = true }`. A
toggle button stays pressed; `doc.pressed` tells the command which way it went.
`doc.play(true | false)` starts or stops playback when the command ends.

With the **`microphone` permission**, `bemotion.microphone.start()` records
the default microphone, `stop()` saves it (WAV, in the project's
`Recordings` folder) and returns the file, `recording()`, `level()` (0–1) and
`cancel()`. The bundled [Recorder](../assets/plugins/recorder/main.lua) is a
voice-over button in a dozen lines. While capturing, the editor shows a
growing recording clip and waveform in the timeline, following the captured
audio even beyond the composition end. This temporary preview is replaced by
the normal media layer when the command stops and imports the WAV, with no
duplicate clip or extra undo steps:


```lua
local started_at = 0
bemotion.register_command {
  id = "record", name = "Record Audio",
  button = { place = "timeline", icon = "mic", toggle = true },
  run = function(doc)
    if doc.pressed then
      bemotion.microphone.start(); started_at = doc.time; doc.play(true); return
    end
    doc.play(false)
    if not bemotion.microphone.recording() then return end
    local media = doc.apply { op = "media.import", path = bemotion.microphone.stop() }
    doc.apply { op = "layer.add", type = "media", media = media.media, start = started_at }
  end,
}
```

The same `doc` API runs standalone scripts from the command line — the script
receives `doc` as its argument (`local doc = ...`):

```bash
bemotion script project.bemotion build.lua
```
