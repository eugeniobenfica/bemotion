# bemotion user guide

Everything bemotion does, area by area. The [README](../README.md) has the
short version; *Help* (F1) in the app has the shortcuts as they are set now
and the guides.

- [Create](#create) · [Animate](#animate) · [Edit](#edit) · [Deliver](#deliver)
  · [Make it yours](#make-it-yours) · [Extend](#extend) · [More details](#more-details)
- [Keyboard shortcuts](#keyboard-shortcuts) · [Rendering from the command line](#rendering-from-the-command-line)

## Create

- **Objects**: text, counter, subtitles, rectangle, ellipse, polygon, star,
  line, path, camera, background and plugin objects — click to add (or the *Add*
  menu), or drag onto the canvas or the timeline; right-click one on the
  canvas for its menu
- **Pen tool** (G): click to place points, drag for curves, click the first
  point to close — a Bézier shape; drag points and handles (Alt: a corner),
  click the outline to add a point, Ctrl+click to remove one. Any shape turns
  into one (*Convert to Bézier Path*). Shapes take **gradients** (linear,
  radial), **dashed** strokes and a **Repeater** (copies stepping in position,
  rotation and scale, fading out); text and plugin objects take gradients too
- **Brush and Eraser**: paint on any layer (or a new one), soft or hard;
  *Write On* draws the strokes over time
- **Masks**: a rectangle or ellipse over a layer, or drawn with the Pen on a
  selected layer; feather, opacity, expansion (all animatable), Add /
  Subtract / Intersect / Difference, inverted. Each mask is a row of its own
  on the timeline, under its layer: pick it (only its points show, and
  Properties shows its options alone), hide it, rename it, change its mode,
  delete it
- **Track mattes and blending modes**, as in After Effects: a layer shows
  only through another one's alpha or brightness (inverted too), and mixes
  with what is under it (Multiply, Screen, Overlay, Add, Color… 17 modes,
  grouped as in After Effects; the one under the mouse shows on the picture
  before you pick it)
- **Counter**: a number that counts up by itself from its clip's start
  (Start → End, for how long, eased; digits that do not shake), with decimals,
  thousands separators (1,234.5 or 1.234,5), a prefix and a suffix (R$, %)
- **Text** with an outline, a box behind it and automatic line wrapping
- **Subtitles**: captions one after another at the bottom of the frame, on a
  box; type them, import / export `.srt`, edit them in a list, see them as
  blocks on the timeline
- **Speech to text**: right-click a clip with speech, *Transcribe to
  Subtitles*: the words land as captions at their time. Accurate (Whisper
  large-v3-turbo) and fast on the GPU (Vulkan on Windows, Metal on macOS),
  offline. The model is not in the download: bemotion offers it (about
  550 MB) the first time you transcribe, once, and *Settings › Plugins ›
  Other downloads* lists it
  ([building on Linux and macOS](../README.md#speech-to-text-on-linux-and-macos))
- **Media**: import images, video and audio (Ctrl+I or drop files on the
  window) into the *Media* panel; media layers animate like everything else;
  the **Crop** tool (Shift+C) drags a video's or image's edges (four
  animatable sides in *Properties › Crop*), rounded corners, a border and
  flips; clips with sound get an animatable *Volume*
- **Background**: a color, a gradient or a picture / video that always fills
  the frame (cover, contain, zoom, darken), under everything
- **Compounds** (Ctrl+G): group layers into one object with its own timeline
- **Edit text in place**: double-click a text on the canvas
- **Copy and paste** objects (Ctrl+C / Ctrl+X / Ctrl+V) in the timeline and
  the viewport, between projects too; files copied in the file manager paste
  as media

## Animate

- Every property keyframable, with a reset button and live values
- **Graph Editor**, motion paths with bezier handles, easing presets and a
  custom cubic-bezier curve editor; Easy Ease with F9
- **Letter Animation** for any text: letters, words or lines come in one
  after another (Fade, Rise, Drop, Pop, Slide, Spin, Blur), leave before the
  end, or keep moving (Wave, Jitter, Pulse), in order, backwards, from the
  center or at random; one click from *Animate › Letters*
- **Auto fit**: long text shrinks to fit its lines (subtitles: two), and
  *Auto Fit* gives captions time to be read
- **Expressions** typed under any property (its *ƒ* button, next to the
  keyframe diamond, or Alt+click the diamond, as in After Effects): several
  lines, completion, and the **ƒ library** (*ƒ Insert*): examples for that
  property, this clip's values, animation, math and every other layer's
  properties, each with what it does, searchable; close it and it keeps
  working; *Convert to Keyframes* bakes it
- **Animate** panel: transitions and Quick Animations are one thing there
  (keyframes made for you): one list, one search, the same tiles and, on the
  timeline, the same bands; on top, everything the clip has in time order
  (its in, its animations, its out), each with its length and a way to take
  it off
- **Quick Animations**: Fade, Pop, Slide, Drop, Zoom, Rise, Shake, Heartbeat,
  Float, Bounce, Wobble, Blink, Spin, Typewriter, Draw On… click one for the
  selection or drag it onto a clip; stackable, importable, savable; each is a
  band on its clip in the timeline: pick it on its own, drag it along the
  clip, drag its end to make it longer or shorter, Delete (or the toolbar's
  trash) takes it off with its keys
- **Transitions** by category (Fade, Slide, Scale, Rotate, 3D): click one for
  the selected clips, or drag it onto a clip's start or end, or onto the cut
  between two clips (they overlap: a real cross dissolve, a push); drag its
  edge for the duration, drag it to the clip's other end to move it — plain
  keyframes you can still edit. A transition picked on the timeline is the
  selection on its own (Delete or the trash removes it, not its clip)
- **Puppet pins and Mesh Warp**, as in After Effects: pin a picture and bend
  it (it follows as rigidly as it can; Starch pins stiffen a part), or move
  the points of a 4 × 4 grid — every pin and point keyframable
- **Track Point**: follow a detail of a video frame after frame, forward or
  back; the layer you pick (or a new marker layer) gets Position keyframes
  and moves with it
- Add keyframes right in the Graph Editor (right-click, double-click or *+*)
- **Effects**: blur, color, distort, glow, drop shadow, motion blur…
  stackable, keyframable, with presets
- **Color**: the **Color** panel (beside Transitions, like Premiere's
  Lumetri) grades the selected layer in one place: brightness, contrast,
  saturation, hue and lightness on sliders whose track shows what they do, a
  **look** picked from cards that preview it (any `.cube` LUT, or five that
  come with bemotion: Teal & Orange, Warm, Cool, Bleach Bypass, Film Fade) and
  **curves** (RGB and each channel); plus **Chroma Key** for green and blue
  screens (tolerance, softness, spill suppression, choke)
- **Scopes** (View › Panels › Scopes): waveform, RGB parade, vectorscope and
  histogram of the frame at the playhead
- **Audio effects** on any clip with sound: gain, fades, a three-band **EQ**,
  **Noise Reduction** (hum, hiss, fans; or learned from a stretch of only
  noise), **Ducking** (the music gets quieter by itself while a voice plays),
  Reverb, Echo, Compressor, Low / High Cut and **Auto Level** (every clip
  as loud as the others); **Remove Silence** cuts a clip's pauses out and
  closes the gaps

## Edit

- **Timeline** with multi-clip tracks, trimming, cut tool (C), split at
  playhead (Ctrl+K), snapping to clips, keys, markers and the playhead; dragging the
  playhead past an edge scrolls the timeline along; a **scrollbar** under the
  tracks moves along long projects (drag its ends to zoom, as in Premiere)
- **Editing like Premiere**: Ripple Edit (B), Rolling Edit (N) and Slip (Y)
  tools, Ripple Delete (Shift+Del), Close Gap, ripple trims to the playhead
  (Q / W), **J / K / L** to play backwards, stop and forward (again: faster)
- **Speed**: a clip at any speed (Ctrl+R, *Speed / Duration*), reversed,
  **Time Remap** (keyframe which moment of the footage shows: ramps, slow
  motion) and **Freeze Frame**; the sound follows
- **Markers** (M; Shift+M / Ctrl+Shift+M to step between them): named and
  colored on the ruler, dragged, snapped to
- **Solo and mute** per track, next to its eye and lock
- **Your own toolbar**: the toolbar's *+* opens every tool; drag them into
  the toolbar, reorder them, drag them out to remove; drag its grip and it
  follows the mouse, showing where it can go: along any window edge, over the
  viewport, or back into the timeline
- **No fixed length**: the composition ends where its last clip ends and grows
  as you add objects — or set a length limit (15 s ads, 60 s shorts)
- **Arrange**: distribute layers evenly, stagger their start times
- **Autosave and recovery**: projects save themselves after every change, and
  changes a crash would lose (a project not saved yet) are offered back the
  next time
- **Viewport** with box selection, rulers, guides and smart snapping
- **3D controls like After Effects**: a *3D view* (the cube button) shows the
  layers and the camera in space, with what the camera sees; drag to turn
  around, right-drag to pan, wheel to get closer. Red / green / blue arrows
  move a 3D layer (or the camera) along X / Y / Z, and *Orbit*, *Pan* and
  *Dolly* move the camera while looking through it — all keyframable
- **Camera** with position, rotation and zoom — and depth: turn on **3D
  Layer** and give layers a Z position and X / Y rotation, and the camera
  (which moves, tilts and pans in 3D too) sees them in perspective: parallax,
  floors and walls, flying through the scene, like After Effects' 3D layers

## Deliver

- **Export** to MP4 / H.264, MOV ProRes 4444, WebM VP9, GIF and PNG sequences
  (the frame being written shows as it goes; Cancel stops at once and removes
  the unfinished file)
  with transparency — [FFmpeg](https://ffmpeg.org) is built in
- Audio is mixed in: AAC in MP4, PCM in MOV, Opus in WebM, a `.wav` beside PNGs
- **Export Frame** (Shift+E, the viewport's camera button, or *File › Copy
  Frame*): the frame at the playhead as a full-size PNG
- **Export Lottie** (*File › Export Lottie*, or `bemotion lottie`): shapes,
  paths, plain text, pictures, backgrounds, masks and compounds as a Lottie
  JSON for the web and apps, keyframes with their easing (expressions are
  baked); what Lottie has no place for (video, effects, cameras) is listed
- Render, export and edit from the command line or an AI assistant

## Make it yours

- **Friendly, Balanced or Expert**, chosen on the first start (and in
  *Settings › Interface*): from the essentials (a short toolbar, no
  expressions or curves) to everything at hand (every option, rulers, audio
  meters, a fuller toolbar); **Essentials only** turns every plugin off
- **Show** what you want, one by one (*Settings › Interface › Show*): the ƒ
  expression buttons, the graph editor button, the Export Frame button, the
  timeline scrollbar, the audio meters — whatever the experience
- Dark, Light and Midnight themes, any accent color; *Advanced Options* show
  every color (surfaces, fields, buttons, focus, selection, menus, status,
  playhead, keyframes...), corner rounding, outline width and control height;
  save, import and export themes ([example](../examples/themes))
- One look for each kind of control: buttons stand out (raised, outlined),
  fields sink in (darker, outlined, a chevron on drop-downs), switches for
  what applies at once, segmented choices
- Rebind every shortcut, pick the interface font and text size, timeline
  densities, frames per arrow press
- Dockable panels: drag tabs anywhere; each header has **minimize** (the
  panel becomes a named tab on the window edge) and **maximize** (or
  double-click a tab); **full screen** for the whole app with F11 (*View ›
  Full Screen*); *Maximize Viewport* (Ctrl+F11, Esc returns)
- **Full Screen Preview** (Ctrl+Shift+F, or the viewport's full screen
  button): only the picture, playing, on the whole screen, with step, play,
  seek, loop and exit buttons; Esc returns
- **bemotion's own color picker**: saturation / brightness square, hue and
  opacity bars, eyedropper (anywhere on the screen), hex and RGB fields, the
  color before, the last colors used
- **Audio meters**: left and right levels beside the timeline, with peak hold
  and a clip light (View › Audio Meters, or Settings › Interface)
- **In your language**: English and Portuguese (Brazil) built in
  (Settings › Interface › Language, switched at once, no restart); a
  language is one JSON file anyone can translate and drop into bemotion's
  `languages` folder
- **Tool Settings** panel: the tool in use, its shortcut, what it does and its
  options (snapping, brush, eraser, puppet, warp, track), kept between sessions
- **Smooth with big footage**: the picture drawn on the GPU, videos decoded on threads of their own, read
  ahead while playing and **at the size they are shown** (a 4K clip in a
  small viewport is read small; exports always at full size), **proxies**
  (Media panel › right-click › *Create Proxy*, or made on import), frames
  kept in memory as they are shown (they play again at once), scrubbing that
  never waits for a video, filmstrips that fill in at once (keyframes first,
  then the exact frames), and optional **hardware decoding** — all in
  *Settings › Performance*
- Each object says what it has: effects and transitions at the top of
  Properties; the Animate panel shows the clip's ins, outs and animations,
  their durations and a remove button; clips in the timeline name their
  speed, matte, blending mode and masks

## Extend

- **Expressions**, like After Effects', in a small safe language: `wiggle(2,
  30)`, `time * 90`, `loop()`, `layer("Ball").position(time - 0.2)`… on any
  property ([docs/EXPRESSIONS.md](EXPRESSIONS.md)); scripts and AI
  assistants set them, try them first and bake them through the same
  operations
- **Scripts** in Lua (*Plugins › Scripts*): bundled ones (Stagger, Wiggle,
  Follow the Leader, Grid of Copies, Random Colors) and your own, one undo
  step each
- **Plugins** are sandboxed Lua scripts that add new kinds of objects
- The official **Charts** plugin draws animated column / bar, line / area and
  donut charts, **tables** and **maps** (world by country; Brazil, USA,
  Canada, Australia, India, China, South Africa by state), from data you
  paste from a spreadsheet or import from a **CSV** file
- The official **3D Models** plugin brings glTF / GLB, OBJ and STL models into
  the 2D scene: turn, zoom, light and color them, all keyframable
- The official **Recorder** plugin records voice-overs from the microphone,
  straight into the timeline
- **Built-in features** that not everyone needs (speech to text, point
  tracking, Puppet and Warp, SVG import, Lottie export, scopes) can be turned
  off like plugins in *Settings › Plugins*: they leave the menus and tools
- **Official plugins are optional**: the download carries none of them.
  Open a project that uses one and bemotion offers to get it; or pick them
  in *Settings › Plugins › Show Official Plugins*. They come from bemotion's
  repository on GitHub, at the tag of your version or else the main
  branch (only when you click),
  and each file is checked (SHA-256) before it is installed
- Plugin **commands** edit the project through the same operations as
  scripts and AI tools — see [docs/PLUGINS.md](PLUGINS.md)
- **Robot-friendly**: scripts and AI assistants create and edit projects from
  the command line — `describe`, `apply` [operations](OPERATIONS.md),
  Lua `script`s, `render` contact sheets to look at, `validate` — every change
  checked, atomic and undoable — and `bemotion mcp` plugs it straight into
  Claude and other MCP assistants ([docs/AUTOMATION.md](AUTOMATION.md))

## More details

- **Home screen** with every project in the projects folder you choose (found
  by itself, live thumbnails; nothing is created on its own) plus the ones
  opened from elsewhere, search, the Welcome
  project, a *⋯* menu on each card (show in folder, delete to the Recycle Bin
  / Trash) and a *New Project* dialog with format presets (Full HD, 4K,
  Vertical/Reels, Square…) and an optional length limit
- **Media panel** (like After Effects' *Project* panel): images (PNG, JPEG,
  BMP, TGA, GIF, PSD, HDR), audio (WAV, MP3, FLAC, M4A/AAC, OGG/Opus, WMA, AIFF)
  and video (MP4, MOV, M4V, MKV, WebM, AVI, WMV, MPEG, MTS) on every platform,
  through FFmpeg. **Folders** keep it tidy (they live in the project only;
  nothing moves on disk): *New Folder*, then drag media onto one or
  right-click › *Move to Folder*; drag it out of the folder (anywhere below
  or outside it) to take it out.
  **SVG drawings** (*File › Import SVG*, or dropped like media) become shape
  layers, one per outline, in a compound: paths, shapes, groups' transforms,
  fills, gradients, strokes; a letter's hole cuts it as a mask.
  Audio plays with the timeline; video clips show their frames along them and
  their sound on an *Audio* row of its own under them (mute it there); sound
  clips show their waveform; video and audio clips trim only within their
  source, missing files can be relinked, *Separate Audio* (right-click a
  clip, or the Layer menu) gives a video's sound a clip of its own and *Link
  Audio* puts it back. Delete removes the media picked in the Media panel
  (inside a folder too)
- Dialogs and windows wear bemotion's own header (the title, a close button;
  drag it to move, an edge to resize): no system title bar
- The mouse wears the tool you picked (cut, pen, crop, ripple, roll, slip)
  where it works
- **Viewport**: wheel zoom around the cursor, pan with middle/right drag,
  Space+drag or Alt+drag; corner handles scale (Shift = uniform), the rotation
  handle rotates (Shift = 15° snaps)
- **Snapping** (S): canvas center and edges, other layers, rulers and custom
  guides; hold Ctrl while dragging to skip it
- **Properties panel**: collapsible sections, any installed font family, a
  *Source* section for media layers (Fit to Composition, Show in Folder)
- **Effects tab** (like *Effect Controls* + *Effects & Presets*): Gaussian and
  Directional Blur, Brightness & Contrast, Hue/Saturation, Tint, Invert, Wave
  Warp, Fill, Stroke, Drop Shadow, Glow and Motion Blur
- **Compounds**: move, scale, rotate, fade and take effects as a whole;
  double-click to open their timeline (a breadcrumb leads back); Ctrl+Shift+G
  releases the layers keeping their look
- **New objects** start at the playhead and last 5 seconds (like Premiere's
  default still duration); the playhead may go past the end to add more
- **Settings** (Ctrl+,): Theme, Interface, Editor, Performance, Shortcuts
  (searchable) and Plugins pages, each option a titled, described row, each page with a reset;
  the intro animation can be turned off in *Interface*
- **Help** (F1), in the same style as Settings: every shortcut as key caps
  (searchable), *Learn* (the guides), About, Credits and License
- **Undo / redo** for everything, unsaved-changes protection

## Keyboard shortcuts

Every shortcut can be changed in *Settings › Shortcuts*; *Help* (F1) shows the
current ones.

| Keys | Action |
| --- | --- |
| <kbd>Space</kbd> | Play / pause (hold + drag: pan the viewport) |
| <kbd>←</kbd> / <kbd>→</kbd> (Shift: ×10) | Previous / next frame |
| <kbd>Home</kbd> / <kbd>End</kbd> | Go to start / end |
| <kbd>J</kbd> / <kbd>K</kbd> / <kbd>L</kbd> | Play backwards / stop / play forward (again: faster) |
| <kbd>Shift</kbd>+<kbd>J</kbd> / <kbd>Shift</kbd>+<kbd>K</kbd> | Previous / next keyframe |
| <kbd>M</kbd> (<kbd>Shift</kbd>+<kbd>M</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>M</kbd>) | Add a marker (next / previous marker) |
| <kbd>V</kbd> / <kbd>C</kbd> | Selection tool / Cut tool |
| <kbd>G</kbd> | Pen tool (Bézier shapes; a mask on a selected layer that is not a shape) |
| <kbd>B</kbd> / <kbd>N</kbd> / <kbd>Y</kbd> | Ripple Edit / Rolling Edit / Slip tools |
| <kbd>Q</kbd> / <kbd>W</kbd> | Ripple trim the clip's start / end to the playhead |
| <kbd>Shift</kbd>+<kbd>Delete</kbd> | Ripple Delete: delete and close the gap |
| <kbd>Ctrl</kbd>+<kbd>R</kbd> | Speed / Duration of a video or audio clip |
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Split selected layers at the playhead |
| <kbd>T</kbd> | Add text (double-click a text to edit it) |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | Select all layers |
| <kbd>Ctrl</kbd>+<kbd>]</kbd> / <kbd>Ctrl</kbd>+<kbd>[</kbd> (Shift: to front / back) | Bring forward / send backward |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Undo / redo |
| <kbd>Ctrl</kbd>+<kbd>D</kbd> | Duplicate layers |
| <kbd>Ctrl</kbd>+<kbd>G</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd> | Create / release a compound |
| <kbd>Delete</kbd> | Delete selected keyframes, or the layers |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> | Save / save as |
| <kbd>Ctrl</kbd>+<kbd>I</kbd> | Import media |
| <kbd>Ctrl</kbd>+<kbd>M</kbd> | Export video |
| <kbd>S</kbd> | Toggle snapping (hold Ctrl while dragging to skip it) |
| <kbd>Ctrl</kbd>+<kbd>,</kbd> | Settings |
| <kbd>F9</kbd> / <kbd>Shift</kbd>+<kbd>F9</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F9</kbd> | Easy Ease / Ease In / Ease Out on the selected keyframes |
| Wheel · Middle/right drag | Zoom · pan the viewport |
| <kbd>Ctrl</kbd>+<kbd>0</kbd> / <kbd>Ctrl</kbd>+<kbd>1</kbd> / <kbd>Ctrl</kbd>+<kbd>=</kbd> / <kbd>Ctrl</kbd>+<kbd>-</kbd> | Fit / 100% / zoom in / zoom out |

## Rendering from the command line

```bash
bemotion assets/welcome/Welcome.bemotion --export welcome.mp4
bemotion project.bemotion --export frames/shot.png --transparent --scale 0.5
```

The format follows the file extension (`.mp4`, `.png`, `.mov`, `.webm`, `.gif`).
