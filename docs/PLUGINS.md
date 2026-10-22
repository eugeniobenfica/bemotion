# Writing plugins

A plugin is a folder with a `plugin.json` manifest and a Lua 5.4 script that
registers new object types. Bundled plugins live in `assets/plugins`; user
plugins in `<app data>/bemotion/plugins` (*Plugins → Install Plugin…* copies a
folder there). The bundled [Charts plugin](../assets/plugins/charts/main.lua)
is a complete example.

```json
{ "id": "myplugin", "name": "My Plugin", "version": "1.0.0", "author": "me",
  "description": "What it adds.", "main": "main.lua", "api": 1 }
```

Scripts run in a sandbox (base, table, string, math and utf8 only — no files,
OS or network) with an instruction budget per draw call.

## Registering an object

```lua
bemotion.register_object {
  id = "badge", name = "Badge", icon = "bars",   -- icons: bars, line_chart, pie, text, rectangle...
  size = { 600, 300 },                           -- default frame size
  rows = { { "A", 10, "#E8B931" } },             -- optional data table (label, value, color)
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
start), `duration` and `rows`. Coordinates are local: the origin is the
object's center and the frame spans ±width/2, ±height/2.

`g` draws: `rect(x, y, w, h, color[, radius])`, `rect_outline(x, y, w, h,
color, width[, radius])`, `circle(x, y, r, color)`, `line(x1, y1, x2, y2,
width, color)`, `polyline({x1, y1, ...}, width, color[, closed])`,
`polygon({x1, y1, ...}, color)`, `arc(cx, cy, inner, outer, fromDeg, toDeg,
color)`, `text(s, x, y, size, color[, "left"|"center"|"right"[, bold]])`
(y = line center) and `measure(s, size[, bold])`.

Helpers: `bemotion.ease(name, t)` (`"ease_out"`, `"back_out"`, `"bounce"`…),
`bemotion.color(hex)`, `bemotion.mix(a, b, t)`, `bemotion.with_alpha(c, k)`,
`bemotion.format(value, decimals)`. `print` goes to the Plugin Manager log.
Colors can be `"#RRGGBB[AA]"` strings or tables.
