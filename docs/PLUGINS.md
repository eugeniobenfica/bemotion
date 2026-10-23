# Writing plugins

A plugin is a folder with a `plugin.json` manifest and a Lua 5.4 script that
registers new object types. Bundled plugins live in `assets/plugins`; user
plugins in `<app data>/bemotion/plugins` (*Plugins → Install Plugin…* copies a
folder there). The bundled [Charts plugin](../assets/plugins/charts/main.lua)
is a complete example.

```json
{ "id": "myplugin", "name": "My Plugin", "version": "1.0.0", "author": "me",
  "description": "What it adds.", "main": "main.lua", "api": 3 }
```

`api` is the version the plugin was written for: 1 objects, 2 commands and
operations, 3 tables and CSV import. `bemotion.api_version` tells the running
version.

Scripts run in a sandbox (base, table, string, math and utf8 only — no files,
OS or network) with an instruction budget per draw call.

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
`polygon({x1, y1, ...}, color)`, `arc(cx, cy, inner, outer, fromDeg, toDeg,
color)`, `text(s, x, y, size, color[, "left"|"center"|"right"[, bold]])`
(y = line center) and `measure(s, size[, bold])`.

Helpers: `bemotion.ease(name, t)` (`"ease_out"`, `"back_out"`, `"bounce"`…),
`bemotion.color(hex)`, `bemotion.mix(a, b, t)`, `bemotion.with_alpha(c, k)`,
`bemotion.format(value, decimals)`, `bemotion.number(text)` (a number written
by people — `"1.234,5"`, `"R$ 12"`, `"45%"` — or `nil`) and
`bemotion.parse_csv(text)` (rows of cells; comma, semicolon or tab). `print`
goes to the Plugin Manager log. Colors can be `"#RRGGBB[AA]"` strings or tables.

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
- `doc.time` is the playhead, `doc.selection` the selected layer ids.

A command is **one undo step**, and it runs on a copy: if it raises an error
(or an operation fails), the project stays exactly as it was and the message
goes to the plugin's log. Permissions: `edit` for commands, `media` to use
`media.import`.

The same `doc` API runs standalone scripts from the command line — the script
receives `doc` as its argument (`local doc = ...`):

```bash
bemotion script project.bemotion build.lua
```
