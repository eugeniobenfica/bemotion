# Automation and AI editing

bemotion projects can be created and edited without the interface — by
scripts, build pipelines or AI assistants (Claude Code and others). Everything
goes through the same [operations](OPERATIONS.md) the plugins use, so the
rules are the same as in the editor: every change is validated, and a batch
that fails changes nothing.

## The command line

The `bemotion` executable takes these commands (results on stdout; exit code 0
on success):

```bash
bemotion new promo.bemotion --width 1080 --height 1920 --fps 30
bemotion describe promo.bemotion                 # what is in it, compactly
bemotion apply promo.bemotion ops.json           # run operations (or - for stdin, or --ops '<json>')
bemotion script promo.bemotion build.lua         # Lua with the plugin doc API (bigger jobs)
bemotion render promo.bemotion --count 6 --out sheet.png   # look at it: a contact sheet
bemotion render promo.bemotion --at 0,1.5 --each --out frame.png
bemotion lottie promo.bemotion --out promo.json  # a Lottie animation (shapes, text, pictures; lists what it left out)
bemotion validate promo.bemotion                 # checks the rules (--fix repairs and saves)
bemotion transcribe voice.wav --language en      # speech -> SubRip captions (--out file.srt, --from / --to s, --cpu)
bemotion promo.bemotion --export promo.mp4       # the video
```

`apply` and `script` accept `--dry-run` (check without saving), `--out` (save
elsewhere) and `--describe` (include the new summary in the result).

## Connecting an AI assistant (MCP)

`bemotion mcp` is an [MCP](https://modelcontextprotocol.io) server: assistants
that speak the Model Context Protocol (Claude Desktop, Claude Code, and many
others) get bemotion's tools directly — no terminal needed. Its tools:
`describe_project`, `create_project`, `query_layer`, `apply_operations`,
`run_lua`, `render_frames` (returns the image, so the assistant sees its work),
`validate_project`, `export_video`, `test_expression` (an
[expression](EXPRESSIONS.md)'s values without setting it),
`get_operations_reference` and `get_expressions_reference`.

With the installer, the program is
`%LOCALAPPDATA%\Programs\bemotion\bemotion.exe` (or wherever you unzipped the
portable version).

**Claude Desktop** — *Settings › Developer › Edit Config* opens
`claude_desktop_config.json`; add:

```json
{
  "mcpServers": {
    "bemotion": {
      "command": "C:\\Users\\YOU\\AppData\\Local\\Programs\\bemotion\\bemotion.exe",
      "args": ["mcp"]
    }
  }
}
```

then restart Claude Desktop. **Claude Code**:

```bash
claude mcp add bemotion -- "C:\Users\YOU\AppData\Local\Programs\bemotion\bemotion.exe" mcp
```

Other assistants take the same two things: the command (`bemotion.exe`) and
its argument (`mcp`). Then ask, for example: *"Create
C:\Videos\promo.bemotion, 1080x1920, with my logo bouncing in and the title
'Launch day' fading up, then export it to MP4"*. The assistant works on the
files you name, with the same checks as everywhere else; `bemotion.log` (in
`%APPDATA%\bemotion`) lists the tools it called.

## A loop for AI assistants

1. **Read**: `bemotion describe <project>` — a few lines per layer: id, name,
   type, timing, position, what is animated. For one layer's details:
   `bemotion apply <project> --ops '{"op":"query.layer","layer":"Title"}' --dry-run`.
   An expression before setting it: `--ops '{"op":"query.expression",
   "layer":"Title","prop":"rotation","expression":"wiggle(2, 5)","times":[0,1]}'`.
2. **Edit**: write operations (JSON) and `apply` them. Refer to layers by the
   ids or names from the description. Read the error if it fails: it says which
   operation and why, usually with the right name.
3. **Look**: `bemotion render <project> --count 6 --out sheet.png` and inspect
   the image. Rendering is deterministic: the same project always gives the
   same pixels.
4. Repeat; then `--export` the video.

Example batch — a title that fades and rises in:

```json
{ "version": 1, "ops": [
  { "op": "layer.add", "type": "text", "name": "Title", "text": "Launch day", "position": [960, 540] },
  { "op": "property.set", "layer": "Title", "prop": "font_size", "value": 140 },
  { "op": "key.add", "layer": "Title", "prop": "opacity", "time": 0, "value": 0 },
  { "op": "key.add", "layer": "Title", "prop": "opacity", "time": 0.6, "value": 100, "easing": "ease_out" },
  { "op": "key.add", "layer": "Title", "prop": "position", "time": 0, "value": [960, 600] },
  { "op": "key.add", "layer": "Title", "prop": "position", "time": 0.6, "value": [960, 540], "easing": "ease_out" }
]}
```

For loops and layouts, a Lua script is shorter (see `doc` in
[PLUGINS.md](PLUGINS.md)):

```lua
local doc = ...
for i = 1, 5 do
  local r = doc.apply { op = "layer.add", type = "ellipse", position = { 300 * i, 540 } }
  doc.apply { op = "key.add", layer = r.layer, prop = "scale", time = i * 0.1, value = 0, easing = "back_out" }
  doc.apply { op = "key.add", layer = r.layer, prop = "scale", time = i * 0.1 + 0.4, value = 100 }
end
```

## Robustness

- Every file is checked when it opens; whatever breaks the rules (a damaged or
  hand-edited file) is repaired, and `bemotion validate` reports it.
- Batches and scripts are atomic and validated: an AI can never leave a project
  half-edited or inconsistent.
- Projects are JSON (`.bemotion`); editing them by hand works too, but
  operations are safer: they keep ids, tracks and compounds consistent.
