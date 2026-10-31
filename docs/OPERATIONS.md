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
| shapes | `size` (not paths), `roundness` (rectangle), `sides` (polygon, star), `inner_radius` (star), `fill_color`, `stroke_color`, `stroke_width`, `trim_start`, `trim_end`, `stroke_dash` and `stroke_gap` (px; both above 0: a dashed line), `stroke_dash_offset`; with a gradient `fill_color2` and, linear, `fill_angle`; with a repeater `repeat_copies`, `repeat_offset` (`[x, y]` from one copy to the next), `repeat_rotation`, `repeat_scale`, `repeat_end_opacity` |
| masks (any layer with a picture) | `mask<index>.feather` (px), `mask<index>.opacity`, `mask<index>.expansion` (px, negative shrinks) |
| text, subtitle, counter | `font_size`, `text_color`, `tracking`, `line_height`, `wrap_width` (px, 0 = no wrapping), `reveal`, `outline_color`, `outline_width`, `box_color`, `box_padding`, `box_roundness`; with a gradient `text_color2` and, linear, `text_fill_angle`; counter also has `counter_from` and `counter_to` (it counts between them) and `counter_value` (keyframes on it replace the count) |
| camera | `position`, `position_z`, `rotation_x` (looks down), `rotation_y` (looks left), `rotation`, `camera_zoom` |
| media with a picture (video, image) | `crop_left`, `crop_top`, `crop_right`, `crop_bottom` (percent of the picture cut away from that side), `corner_radius` (px, rounds the picture's corners), `border_width` (px, a line around it), `border_color` |
| media with sound | `volume` |
| video and audio with Time Remap on | `time_remap` (the source second shown at each moment) |
| plugin objects | `size`, `p.<parameter id>` (e.g. `p.progress`); with a gradient `object_fill_color2` and, linear, `object_fill_angle` |
| effects | `fx<index>.<param>` (e.g. `fx0.size`; `effect.add` lists the params) |
| painted, pinned or warped layers | `paint_reveal` (Write On, %), `pin<index>` (`[x, y]`, a Puppet pin's place), `warp<index>` (`[x, y]` offset of a Mesh Warp point, 0–15) |

`query.layer` lists a layer's properties with their values and keyframes.

## Composition

**`composition.set`** — `name`, `width`, `height` (16–16384), `fps` (1–240),
`background`, `max_duration` (seconds; `0` = no limit). The duration follows
the content: it ends with the last clip, but never passes `max_duration`.

## Layers

**`layer.add`** → `{"layer": id, "name": name}`
- `type`: `text`, `counter` (a number that counts by itself from its clip's
  start, 0 to 100 in 2 s; `from`, `to`, `count_duration`, `count_easing`,
  `decimals`, `prefix`, `suffix` and `separator` as in `layer.set`), `rectangle`, `ellipse`, `polygon`, `star`, `line`, `camera`,
  `media` (with `"media": id` from `media.import`), `background` (fills the
  whole frame; goes to the bottom; `fill`: `solid`, `linear`, `radial` or
  `image` with `"media": id` and `fit`: `cover`, `contain`, `stretch`) or
  `plugin` (with `"object": "charts.bars"`), or `subtitle` (captions at the
  bottom of the frame, over the whole composition; `captions` or `srt` as in
  `layer.set`), or `path` (a Bézier shape, as the pen tool draws: `"path":
  {"closed": true, "vertices": [[x, y], [x, y, in_x, in_y, out_x, out_y],
  ...]}`, in the layer's own space, handles relative to their point; an open
  path is a stroked line). Background properties:
  `bg_color`, `bg_color2`, `bg_angle`, `bg_zoom`, `bg_offset`, `bg_darken`.
- `name`, `text` (text layers), `position` (default: the center), `start`
  (seconds, default 0), `duration` (default 5; media: their own length),
  `three_d` (a 3D layer) and `z` (its Z position; alone it makes the layer 3D;
  a camera's own Z),
  `parent` (a compound), `above` / `below` (a layer) or `"at": "bottom"`
  (default: on top), `track_of` (a layer: the clip joins its track, a row of
  clips one after another; it must not overlap them).

**`layer.set`** → `{"in": s, "out": s}` — any of `name`, `visible`, `locked`,
`track_of` (as in `layer.add`),
`three_d` (not cameras or backgrounds),
`label` (0–7), `start` (moves the clip and its keyframes), `in`, `out` (trim,
kept between neighbors on its track), and for text `text`, `font`, `style`,
`align` (`left`, `center`, `right`), `outline`, `box` (booleans); for
counters `from` and `to` (it counts between them), `count_duration` (seconds
from the clip's start, `0` = the whole clip), `count_easing` (`linear`,
`ease_out`, `ease_in_out`), `decimals` (0–6), `prefix` and `suffix` (`"R$ "`,
`"%"`), `separator` (thousands: `""`, `","`, `"."` with a decimal comma, or
`" "`); for any text the letter animation `letter_in` / `letter_out` (`none`,
`fade`, `rise`, `drop`, `pop`, `slide`, `spin`, `blur`: from the clip's start /
until its end), `letter_loop` (`none`, `wave`, `jitter`, `pulse`), `letter_by`
(`letter`, `word`, `line`), `letter_order` (`forward`, `backward`, `center`,
`random`), `letter_delay` and `letter_duration` (seconds), and `fit_lines`
(auto fit: with a `wrap_width`, the font shrinks so the text takes at most
that many lines; `0` = off, subtitles start with 2); for subtitles
`"fit_captions": true` (long captions split, each shown long enough to be
read, tiny gaps closed); for
subtitles `captions` (`[{"start": s, "end": s, "text": "..."}]`, composition
seconds; replaces them, sorted, overlaps cut) or `srt` (SubRip text); for shapes `shape` (kind), `fill`,
`stroke` (booleans), `gradient` (`solid`, `linear`, `radial`: from
`fill_color` to `fill_color2`; also on text, from `text_color` to `text_color2`, and on
plugin objects, from each filled part's color to `object_fill_color2`), `repeater` (boolean), `path` (as in
`layer.add`; the shape becomes a path; `"shape": "path"` alone turns its outline
into one); for backgrounds `fill`, `media`, `fit`; for media with a picture
`flip_h`, `flip_v` (booleans: mirrored left to right, top to bottom). Any layer: `muted` and
`solo` (booleans: while some layer beside it is solo, only those are seen and
heard), `blend` (`normal`, `multiply`, `screen`, `overlay`, `darken`,
`lighten`, `color_dodge`, `color_burn`, `hard_light`, `soft_light`,
`difference`, `exclusion`, `hue`, `saturation`, `color`, `luminosity`, `add`),
and a track matte (below).

**`layer.remove`** — `layer` (a compound takes its contents along).

**`layer.move`** — `layer`, `to`: `top`, `bottom`, `up`, `down` (stacking order).

**`layer.split`** → `{"layer": id of the second part}` — `layer`, `time`
(strictly inside the clip).

**`layer.duplicate`** → `{"layer": id}` — `layer` (not compounds yet).

**`group`** → `{"layer": compound id}` — `layers` (list, same parent), `name`,
`time` (whose transforms set the compound's center).

**`ungroup`** — `layer` (a compound), `time`: the layers keep their look.

**`svg.add`** → `{"layer": id, "name"}` — `svg`: the text of an SVG file;
`name` (default `SVG`), `time` (where its clips start, default 0). Its paths,
rectangles, circles, ellipses, lines, polylines and polygons become Path shape
layers (through their groups' transforms, with their fill — a gradient keeps its
first and last colors —, stroke and opacity), fitted in the middle of the
composition at 80% of its size, in a compound named `name` when there are
several (`layer` is the compound's id, else the one layer's). An outline inside
another (a letter's hole) cuts it as a Subtract mask. Text, images, filters and
patterns are left out. File › Import SVG does the same.

## Masks and track mattes

**`mask.add`** → `{"index": i}` — `layer`, and `shape` (`rectangle`, the
default, or `ellipse`) over the layer's bounds or over `box` (`[x0, y0, x1,
y1]` in the layer's own space), or a `path` (as in `layer.add`); `name`,
`mode` (`add`, `subtract`, `intersect`, `difference`: how it combines with the
masks before it), `inverted`, `feather`. Its animatable values are properties:
`mask0.feather`, `mask0.opacity`, `mask0.expansion`.

**`mask.set`** — `layer`, `index`, any of `name`, `mode`, `inverted`,
`enabled`, `path`. A `path` with a `time` (or on a mask already animated) is a
Mask Path keyframe there: in between, the outline moves point by point (eased)
when the two have as many points, else it holds. `"animated": false` keeps the
outline at `time` and drops the keyframes. `query.layer` lists each mask's
`path_keys`. **`mask.duplicate`** → `{"index": i}` — `layer`, `index`: a copy
right after it. **`mask.remove`** — `layer`, `index`.

A **track matte** (`layer.set`): `matte` (`alpha`, `alpha_inverted`, `luma`,
`luma_inverted`, `none`) and `matte_layer` (default: the layer right above,
in the same compound or both at the top). The layer shows only where the
matte is opaque (or bright; inverted: where it is not), and the matte itself is
not drawn. `query.layers` marks it `"is_matte": true`.

## Editing the timeline

**`layer.trim`** → `{"in": s, "out": s}` — `layer`, `edge` (`in`, `out`),
`time`; `"ripple": true` moves the later clips on its track by as much (no gap
opens or closes; a trimmed start keeps the clip where it began).

**`layer.roll`** → `{"cut": s, "next": id}` — `layer`, `time`: the cut between
this clip and the one right after it on its track moves there (both edges).

**`layer.slip`** → `{"delta": s}` — `layer`, `delta` (seconds): the clip stays
put and shows other moments of its content (footage, keyframes), within the
source.

**`layer.ripple_delete`** → `{"removed": n}` — `layers` (a list): removed, and
the later clips on each one's track close the gap.

**`track.close_gap`** — `layer` (any clip of the track), `time` (in the gap):
the clips after the gap move back to the clip before it.

**`layer.speed`** → `{"in": s, "out": s}` — `layer` (video or audio), `speed`
(percent, 1–10000), `reverse` (boolean), `ripple` (the later clips follow;
else the clip stops at the next one). The frame at the clip's start stays (when
reversing, the end's comes first); the clip gets longer or shorter. Sound plays
at the new speed (its pitch changes).

**`layer.time_remap`** — `layer`, `enabled`: on, the `time_remap` property
(keyframes at the clip's ends) says which source second is shown at each
moment; off, back to its speed. **`layer.separate_audio`** → `{"layer": id,
"name"}` — `layer`: a video with sound keeps its picture; its sound becomes a
clip of its own on a new track right below (same media, time, speed, volume
and audio effects). **`layer.link_audio`** → `{"in_sync": bool}` — `layer`: a
video without sound; `audio` (optional: else the sound-only clip of the same
media overlapping it most): that clip's sound goes back into the video (its
volume and audio effects) and the clip goes; the video's timing decides
(`in_sync`: whether they played the same moment). **`layer.freeze`** →
`{"source_time": s}` —
`layer`, `time`: the clip holds the frame it shows there (Time Remap with one
value).

## Markers

**`marker.add`** → `{"index": i, "time": s}` — `time` (to a frame), `name`,
`color` (a label color, 0–7); at a marker's time it renames that one.
**`marker.set`** — `index` or `time`, any of `name`, `color`, `to` (a new time).
**`marker.remove`** — `index` or `time`. `query.composition` lists `markers`.

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

**`expression.set`** — `layer`, `prop`, `expression` (`""` removes it): a
formula worked out every frame (`docs/EXPRESSIONS.md`), e.g. `"wiggle(2, 30)"`
or `"layer(\"Ball\").position + [0, -120]"`. One that does not read fails
(with the closest name when one is mistyped).

**`expression.bake`** → `{"keys": n}` — `layer`, `prop`, `from` / `to`
(seconds, default the clip), `step` (frames, default 1): the expression becomes
linear keyframes that many frames apart (the same values), and is removed —
After Effects' *Convert Expression to Keyframes*.

## Effects

**`effect.add`** → `{"index": i, "params": [...]}` — `layer`, `effect`
(`gaussian_blur`, `directional_blur`, `brightness_contrast`, `hue_saturation`,
`tint`, `invert`, `fill`, `wave_warp`, `outline`, `glow`, `drop_shadow`,
`motion_blur`, `lut`, `curves`, `chroma_key`), `params` (`{"size": 30}`; ids
from the result).

**`effect.remove`** — `layer`, `index`. **`effect.set`** — `layer`, `index`,
`enabled`, `samples` (motion blur, 2–64), `file` (`lut`: a `.cube` file, or
`"bundled:Teal & Orange.cube"` for a look that comes with bemotion — `Warm`,
`Cool`, `Bleach Bypass`, `Film Fade`; `""` clears it), `curves` (`curves`:
`{"rgb": [[0, 0], [0.5, 0.6], [1, 1]], "red": ..., "green": ..., "blue": ...}`,
points `[x, y]` from 0 to 1, at most 16 per curve, `null` resets one; the
channel's curve applies first, then RGB).

`lut` and `curves` blend with `opacity` (Amount, %). `chroma_key`: `color`
(the key, a studio green by default), `v1` tolerance, `v2` softness, `v3`
spill suppression (%), `size` choke (px: pulls the edge in).
`query.layer` lists each effect's `file` and `curves` in `effect_list`.

**Audio effects** (clips with sound: audio, and video with audio) — not on the
picture but on the sound, in playback and export:
`gain` (`value`: dB), `fade_in` / `fade_out` (`value`: seconds), `eq`
(`low` 100 Hz shelf, `mid` 1 kHz, `high` 8 kHz shelf: dB, −24 to 24),
`noise_reduction` (`reduction` dB 0–40, `sensitivity` %: how far above the
steady noise still counts as noise; with `sample` — `[from, to]` seconds of the
clip's source with only the noise, on `audio_effect.add` / `set`, `null` to
drop it — the noise is learned from that stretch instead of guessed), `ducking` (`amount`: how many dB quieter
while any other clip with sound — without ducking itself — is above
`threshold` dB, easing over `fade` seconds; put it on the music), `reverb`
(`mix`, `size`, `damping`: %), `echo` (`delay` seconds, `feedback` 0–90 %,
`mix` %), `compressor` (`threshold` dB, `ratio` 1–20, `makeup` dB),
`filter` (Low / High Cut: `low_cut` 20–2000 Hz, `high_cut` 1000–20000 Hz; at
20 and 20000 they are off), `auto_level` (`target` dB −40 to −6, `even_out`
%: how much quiet and loud passages are brought toward the target).

**`audio_effect.add`** → `{"index": i, "values": {...}}` — `layer`, `effect`,
`values` (`{"low": 3}`). **`audio_effect.set`** — `layer`, `index`,
`enabled`, `values`. **`audio_effect.remove`** — `layer`, `index`.
`query.layer` lists them in `audio_effects` (effect, enabled, values).

## Transitions and arranging

**`transition.add`** — `layer`, `transition` (Fade: `fade`, `fade_up`,
`fade_down`; Slide: `slide_left`, `slide_right`, `slide_up`, `slide_down`,
`whip_left`, `whip_right`, `drop`; Scale: `zoom`, `zoom_out`, `pop`,
`stretch`, `squash`, `elastic`; Rotate: `spin`, `swing`, `roll`; 3D: `turn`,
`tilt`, `fly`, which make the layer 3D), `edge` (`in`, `out`, `both`, or
`cut`: across the cut after this clip, with the next clip on its track: the
two clips overlap for the whole duration, half on each side of the cut, the
next one animating in above this one (a cross dissolve with `fade`, a push
with a slide); default `in`), `duration` (seconds, default 0.5; 1 for `cut`). Keyframes at the clip's start and/or end; keys already there
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

**`audio.level`** → `{"layers", "target"}` — automatic levels: every clip with
sound in `layer` (one) or `layers` (a list, or none: all) gets an Auto Level
effect (`target` dB, default -18; `even_out` 0–100, default 50), the one it has
updated, so they all sound as loud.
**`audio.remove_silence`** → `{"removed_seconds", "threshold"}` — `layer` (a
video or audio clip with sound): cuts out its quiet stretches, below
`threshold` dB (default -40) or, with `sample` (`[from, to]`, composition
seconds of only silence or noise), a little above that sample's loudest
moment, lasting at least `min` seconds (default 0.5), keeping `padding`
seconds of them on each side (default 0.1). `ripple` (default true) closes the
gaps: the later pieces and clips on its track move back. Not for clips playing
reversed or with Time Remap.

**Paint** (the Brush and Eraser tools): **`paint.stroke`** → `{"index": i}` —
`layer`, `points` (`[[x, y], ...]` in the layer's own space), `color`, `width`
(px, default 12), `softness` (px of soft edge, default 0), `erase` (true: cuts through the layer's picture and the
strokes before it). **`paint.clear`** — `layer`. The property `paint_reveal`
(0–100 %) draws the strokes in order up to that much of their length (Write On).

**Puppet and Warp**: **`puppet.pin.add`** → `{"index", "property"}` — `layer`,
`point` (`[x, y]` where the pin sits on the picture, in the layer's own
space; `starch`: true for a Starch pin, which holds nothing in place but
stiffens the picture around it). Its property `pin<index>` is where it is moved
to (keyframe it to animate): as in After Effects, a mesh covers what the
picture shows and bends to follow its pins as rigidly as it can; separate pieces
of the picture move on their own (a piece without pins stays put).
**`puppet.pin.remove`** — `layer`, `index`. `layer.set` with `"warp": true`
adds a Mesh Warp: a 4 × 4 grid over the layer's bounds whose points move by
the properties `warp0` … `warp15` (offsets in px, row by row from the top left).

**Media folders** keep the Media panel tidy; they are only names (nothing moves
on disk). **`folder.add`** → `{"folder": name}` — `name`.
**`folder.rename`** — `folder`, `name`. **`folder.remove`** — `folder` (its
media go back to the top). **`folder.move`** → `{"folder", "index"}` —
`folder`, `index` (its new place, 0 = first; past the end: last).
**`media.move`** — `media` (an id), `folder` (a
folder's name; `""` or none: the top). `query.composition` lists `folders` and
each media item's `folder`.

## Queries (change nothing)

**`query.composition`** — size, frame rate, duration, background, media items,
markers.
**`query.layers`** — every layer, top first: id, name, type, in, out, track
row, parent (and, when set: blend, matte, masks, speed, muted, solo). **`query.layer`** — `layer`, `time`: everything about one layer,
with each property's value at `time` and its keyframes (a path shape's `path`,
the masks in `mask_list`, the effects in `effect_list`, a clip's sound effects
in `audio_effects`); a property driven by an
expression also has `expression` and, when it fails there, `expression_error`.
**`query.expression`** — `layer`, `prop`, `expression` (default: the
property's own), `time` (default 0) or `times` (a list): works it out without
setting it → `{"ok": true, "value": ...}` (`"values": [...]` for `times`), or
`{"ok": false, "error": "...", "time": t}` — a wrong expression is an answer,
not a failure, so a script or an AI can try one before `expression.set`.
