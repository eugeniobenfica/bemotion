# Expressions and scripts

bemotion has two kinds of code, like After Effects:

- **Expressions** — a short formula on one property, worked out every frame.
  Right-click a property in *Properties* › *Add Expression* (or pick one of the
  *Expression Examples*). The property's name turns into `= Name` and shows
  the result. An expression that does not read is not applied (the error shows
  under it); one that fails while running (a layer that is gone) falls back to
  the keyframes. Keyframes stay: `value` is what they say.
- **Scripts** — Lua programs that edit the project, one undo step each:
  *Plugins* › *Scripts*. The bundled ones and your own (*Open Scripts Folder*)
  are listed; *Run Script File...* runs any `.lua`. They use the plugin `doc`
  API (`docs/PLUGINS.md`): `local doc = ...`, `doc.selection`, `doc.time`,
  `doc.apply { op = ... }` with the operations of `docs/OPERATIONS.md`, and
  `print()` shows in the status bar. From the command line:
  `bemotion script project.bemotion file.lua --select 3,7`.

## The expression language

It is small on purpose: it can only read the project (never files, the
network or the system), it is fast enough for every frame, and it gives the
same result in the editor, in exports and on the command line.

```text
time * 90                               turns 90° a second (rotation)
wiggle(2, 30)                           the keyframed value, shaken twice a second by up to 30
[960 + sin(time * 2) * 300, value.y]    goes left and right, keeps its height
layer("Ball").position + [0, -120]      stays above the ball
layer("Ball").position(time - 0.25)     follows it a quarter second later
loop()                                  plays its keyframes again and again
pingpong()                              ... back and forth
progress < 0.5 ? 0 : 100                a choice (progress: 0 at the clip's start, 1 at its end)
lerp(value, value * 2, smooth(progress)) grows smoothly over the clip
```

**Values** are numbers or vectors: `[x, y]` for positions, scales and sizes,
`[r, g, b, a]` (0 to 1) for colors (`rgb(232, 185, 49)` takes 0 to 255).
Math works per component, and a number meets a vector on every component
(`value * 2`, `value + [10, 0]`). The result fits the property: a number on
a position sets both x and y; `[r, g, b]` on a color is opaque.

**Names**

| | |
| --- | --- |
| `time`, `frame`, `fps` | the moment (seconds), its frame number, frames per second |
| `value` | what the property's keyframes (or static value) say now |
| `start`, `end`, `duration`, `progress` | the layer's clip: in and out (seconds), length, 0 → 1 across it |
| `index` | the layer's row, 1 at the top |
| `width`, `height`, `center` | the composition |
| `pi` | 3.14159… |

**Other layers**: `layer("Name")`, `layer(2)` (the second row) or `layer()`
(this one), then a property id from `docs/OPERATIONS.md`: `.position`,
`.scale`, `.rotation`, `.opacity`, `.position_z` (or `.z`), `.fill_color`,
`.font_size`, `.p.<param>` for plugin parameters… Add `(t)` to read it at
another time: `layer("Ball").position(time - 0.2)`. `.start` and `.end` are
the clip's times. A vector's parts: `.x .y .z .w` or `.r .g .b .a`, or `[0]`.

**Operators**: `+ - * / %` (modulo), `^` (power), `< > <= >= == !=`,
`&&` / `and`, `||` / `or`, `!`, and `condition ? a : b`.

**Functions**

| | |
| --- | --- |
| `sin cos tan asin acos atan atan2(y, x)` | angles in radians; `degrees(r)`, `radians(d)` |
| `abs floor ceil round fract sign sqrt pow(a, b) exp log` | per component |
| `min(a, b, …) max(a, b, …) clamp(v, lo, hi)` | |
| `lerp(a, b, t)` (or `mix`) | from a to b as t goes 0 → 1 |
| `map(x, a, b, lo, hi)` | x from a…b onto lo…hi (clamped) |
| `smooth(t)` (or `ease`) | 0 → 1 eased in and out |
| `length(v) distance(a, b)` | |
| `wiggle(times a second, amount)` | the value, shaken smoothly; different per layer, property and component, the same every time you play |
| `noise(x[, seed])` | smooth noise, −1 to 1 |
| `random()`, `random(max)`, `random(min, max[, seed])` | a fixed random number for this property (not a new one each frame) |
| `loop()`, `pingpong()` | the keyframes again after the last one |
| `vec(…)`, `rgb(r, g, b)`, `rgba(r, g, b, a)` | build vectors / colors |

Expressions are saved with the project (`"expression"` next to the
property's keyframes) and set by operations with
`{"op": "expression.set", "layer": …, "prop": …, "expression": "…"}`.
