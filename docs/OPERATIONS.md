# Operations reference (version 1)

Operations are how anything outside the interface changes a project: Lua
plugins and scripts, the command line (`bemotion apply`) and AI tools. Each one
is a JSON object with an `"op"` and its fields:

```json
{ "op": "layer.add", "type": "text", "text": "Hello", "position": [960, 540] }
```

A **batch** is a list, or `{"version": 1, "ops": [...]}`. Batches are
**atomic**: they run on a copy of the project, which replaces the original only
if every operation succeeded and the result follows every rule
(`src/core/Validate.h`). A failure names the operation and explains what is
wrong — often with a suggestion (`there is no layer named "Titel"; did you mean
"Title"?`). Later operations see what earlier ones did, so a batch can create a
layer and then animate it.

## Conventions

| | |
| --- | --- |
| **Layers** | by id (`12`, or `"#12"`) or by name (`"Title"`, when unique). `layer.add` returns the new id. |
| **Time** | seconds from the start of the composition (`1.5`). |
| **Positions** | composition pixels, origin at the top-left; `[x, y]`. Inside a compound, relative to it. |
| **Colors** | `"#RRGGBB"`, `"#RRGGBBAA"` or `[r, g, b(, a)]` from 0 to 1. |
| **Percent** | scale, opacity, trim… are percentages (`100` = full). Rotation in degrees, clockwise. |
| **Easing** | the segment *starting* at a keyframe: `linear`, `ease_in`, `ease_out`, `ease_in_out` (default), `back_in`, `back_out`, `elastic`, `bounce`, `hold`. |

### Property ids (`"prop"`)

| Layers | Properties |
| --- | --- |
| every layer | `anchor`, `position`, `scale`, `rotation`, `opacity`; 3D layers `position_z`, `rotation_x` (the top tips away), `rotation_y` (the right side turns away) |
| shapes | `size`, `roundness` (rectangle), `sides` (polygon, star), `inner_radius` (star), `fill_color`, `stroke_color`, `stroke_width`, `trim_start`, `trim_end` |
| text | `font_size`, `text_color`, `tracking`, `line_height`, `reveal` |
| camera | `position`, `position_z`, `rotation_x` (looks down), `rotation_y` (looks left), `rotation`, `camera_zoom` |
| media with sound | `volume` |
| plugin objects | `size`, `p.<parameter id>` (e.g. `p.progress`) |
| effects | `fx<index>.<param>` (e.g. `fx0.size`; `effect.add` lists the params) |

`query.layer` lists a layer's properties with their values and keyframes.

## Composition

**`composition.set`** — `name`, `width`, `height` (16–16384), `fps` (1–240),
`background`, `max_duration` (seconds; `0` = no limit). The duration follows
the content: it ends with the last clip, but never passes `max_duration`.

## Layers

**`layer.add`** → `{"layer": id, "name": name}`
- `type`: `text`, `rectangle`, `ellipse`, `polygon`, `star`, `line`, `camera`,
  `media` (with `"media": id` from `media.import`), `background` (fills the
  whole frame; goes to the bottom; `fill`: `solid`, `linear`, `radial` or
  `image` with `"media": id` and `fit`: `cover`, `contain`, `stretch`) or
  `plugin` (with `"object": "charts.bar"`). Background properties:
  `bg_color`, `bg_color2`, `bg_angle`, `bg_zoom`, `bg_offset`, `bg_darken`.
- `name`, `text` (text layers), `position` (default: the center), `start`
  (seconds, default 0), `duration` (default 5; media: their own length),
  `three_d` (a 3D layer) and `z` (its Z position; alone it makes the layer 3D;
  a camera's own Z),
  `parent` (a compound), `above` / `below` (a layer) or `"at": "bottom"`
  (default: on top).

**`layer.set`** → `{"in": s, "out": s}` — any of `name`, `visible`, `locked`,
`three_d` (not cameras or backgrounds),
`label` (0–7), `start` (moves the clip and its keyframes), `in`, `out` (trim,
kept between neighbors on its track), and for text `text`, `font`, `style`,
`align` (`left`, `center`, `right`); for shapes `shape` (kind), `fill`,
`stroke` (booleans); for backgrounds `fill`, `media`, `fit`.

**`layer.remove`** — `layer` (a compound takes its contents along).

**`layer.move`** — `layer`, `to`: `top`, `bottom`, `up`, `down` (stacking order).

**`layer.split`** → `{"layer": id of the second part}` — `layer`, `time`
(strictly inside the clip).

**`layer.duplicate`** → `{"layer": id}` — `layer` (not compounds yet).

**`group`** → `{"layer": compound id}` — `layers` (list, same parent), `name`,
`time` (whose transforms set the compound's center).

**`ungroup`** — `layer` (a compound), `time`: the layers keep their look.

## Values and keyframes

**`property.set`** — `layer`, `prop`, `value`, `time` (default 0). On an
animated property it sets (or adds) the keyframe at `time`; otherwise the
static value.

**`key.add`** — `layer`, `prop`, `time`, `value` (default: the current value
there), `easing`.

**`key.remove`** — `layer`, `prop`, `time` (of the key).

**`key.clear`** — `layer`, `prop`, `time` (the value kept, default 0), `value`.
Removes every keyframe.

**`key.easing`** — `layer`, `prop`, `easing`, and `time` (one key) or
`"all": true`.

## Effects

**`effect.add`** → `{"index": i, "params": [...]}` — `layer`, `effect`
(`gaussian_blur`, `directional_blur`, `brightness_contrast`, `hue_saturation`,
`tint`, `invert`, `fill`, `wave_warp`, `outline`, `glow`, `drop_shadow`,
`motion_blur`), `params` (`{"size": 30}`; ids from the result).

**`effect.remove`** — `layer`, `index`. **`effect.set`** — `layer`, `index`,
`enabled`, `samples` (motion blur, 2–64).

## Transitions and arranging

**`transition.add`** — `layer`, `transition` (Fade: `fade`, `fade_up`,
`fade_down`; Slide: `slide_left`, `slide_right`, `slide_up`, `slide_down`,
`whip_left`, `whip_right`, `drop`; Scale: `zoom`, `zoom_out`, `pop`,
`stretch`, `squash`, `elastic`; Rotate: `spin`, `swing`, `roll`; 3D: `turn`,
`tilt`, `fly`, which make the layer 3D), `edge` (`in`, `out`, `both`, or
`cut`: across the cut after this clip, with the next clip on its track, half
the duration on each side; default `in`), `duration` (seconds, default 0.5;
1 for `cut`). Keyframes at the clip's start and/or end; keys already there
are replaced. Cameras and sound-only clips take none.

**`arrange.distribute`** — `layers` (list, or `"all"`), `axis` (`x` or `y`),
`time` (default 0). Even spacing: three or more layers keep the outer two in
place; two spread over the composition.

**`arrange.stagger`** — `layers` (list, or `"all"`), `step` (seconds, default
0.1). Each layer starts `step` after the one above it.

## Data (charts and tables)

**`data.set`** — `layer` (a plugin object with data) and one of:
- `rows`: `[{"label": "Jan", "value": 42, "color": "#E8B931"}, ...]` (or
  `[["Jan", 42], ...]`) for charts;
- `table`: `{"header": ["Name", "Score"], "rows": [["Ana", "12"], ...]}` for
  tables;
- `csv`: CSV text (comma, semicolon or tab; `1.234,5` and `R$ 12` read as
  numbers). Charts take a label column and the first numeric column; tables
  take the first line as the header.

`query.layer` shows an object's `rows` / `table`.

**`param.set`** — `layer` (a plugin object), `param` (its id), `value`: `true`
/ `false` for toggles, one of the `choices` for choices, text, or a file path
for file parameters (e.g. the model of a 3D Model). Numbers and colors are
animatable: set them with `property.set` (`"prop": "p.<id>"`). `query.layer`
lists an object's `params`.

## Media

**`media.import`** → `{"media": id, "name", "kind", "width", "height",
"duration"}` — `path`. Then `layer.add` with `"type": "media", "media": id`.
(Plugins need the `media` permission.)

## Queries (change nothing)

**`query.composition`** — size, frame rate, duration, background, media items.
**`query.layers`** — every layer, top first: id, name, type, in, out, track
row, parent. **`query.layer`** — `layer`, `time`: everything about one layer,
with each property's value at `time` and its keyframes.
