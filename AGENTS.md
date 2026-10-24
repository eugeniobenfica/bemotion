# Notes for coding agents

bemotion is a free video editor with a special focus on 2D animation (C++20,
Qt 6 Widgets, Skia, FFmpeg, Lua plugins). Its philosophy is in the README ("The
idea"): calm rather than a cockpit, human and robot friendly, fast, a small core
plus plugins, and an idea more than a technology. **Read
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before changing code** — it is the
source of truth for structure, conventions and recipes (§12 lists what to touch
for a new property, effect, operation, layer type, panel, setting, plugin
capability). This file is the short, practical version.

## Where things are

| Area | Files |
| --- | --- |
| Document model, rules, operations | `src/core` (`Project`, `Layer`, `Validate`, `Ops`, `Serialization`, `Clipboard`, `Data`, `Arrange`, `Transitions`, `Presets`) — no Qt, no Skia |
| Drawing | `src/render` (`SceneRenderer`, `Text`, `PluginCanvas`, `Mesh3D`) |
| Media, audio, recording, export | `src/media`, `src/export` |
| Interface | `src/ui` (`EditorWindow`, `EditorContext`, `Theme`, `Style`, `Widgets`, `panels/*`) |
| App, platform, plugins, automation | `src/app` (`Application`, `Platform`, `Settings`, `PluginHost`, `Automation`, `McpServer`, `Recovery`, `DevScript`) |
| Bundled plugins | `assets/plugins` (charts, models3d, recorder) |
| Contracts in prose | `docs/OPERATIONS.md`, `docs/PLUGINS.md`, `docs/AUTOMATION.md` |

## Rules

- Layers: `core` ← `render` ← `media` / `export` ← `ui` ← `app`. Dependencies
  only point down.
- Outside changes to a project go through **operations** (`src/core/Ops.cpp`,
  reference `docs/OPERATIONS.md`); every rule lives in `src/core/Validate.cpp`
  (a check *and* a repair). New script / AI abilities are new operations,
  documented and tested in the same commit. Logic the editor and scripts share
  goes into the core (as `Clipboard`, `Arrange`, `Transitions` did).
- Contracts — the `.bemotion` format, operations JSON, plugin API (now version
  5), property and effect ids, settings keys, command-line options — change
  only compatibly: new optional fields with defaults, never renames.
- What every user needs belongs in the app; plugins add kinds of objects and
  workflows. Plugins never touch files, devices or the network themselves:
  the host does it for them behind a permission (`edit`, `media`, `files`,
  `microphone`).
- Match the surrounding code (`.clang-format`, naming, comment density); change
  only what the task needs; no unrelated reformatting.
- UTF-8 everywhere: `platform::fromUtf8` / `toUtf8`, `platform::arguments`,
  `platform::env` (never raw `argv` / `getenv` / `path.string()` on Windows).
- Colors come from theme tokens (`theme::kText`, `kAccent`…), read at paint
  time. Labels use `ui::setTone(label, ui::Tone::…)`, never a style sheet with
  a baked color — themes change while the app runs. Interface text is English,
  short, sentence case, After Effects / Premiere terms.

## Build, run, test (Windows)

```bash
# MSYS2 UCRT64 shell, first time
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKIA_ROOT=<skia>
# every time after: only what changed is rebuilt
cmake --build build && ctest --test-dir build && tools/smoke-test.sh build smoke
```

- The dev executable `build/bemotion.exe` needs `C:\msys64\ucrt64\bin` on
  `PATH` (Qt / FFmpeg DLLs); `run.bat` sets it, rebuilds what changed and opens
  it (`--build-only`, `--no-build`), and starts the build folder over when the
  sources moved. Releases: `tools/build-release.sh` (installer and portable
  zip in `dist/`).
- If linking fails with "Permission denied", someone has `bemotion.exe` open —
  maybe the user. Never kill it: rename the running file
  (`build/bemotion-old-running.exe`) and build again.
- **Look at results.** Core logic: `bemotion new / apply / validate / render
  --at 1 --out x.png` (headless, see `docs/AUTOMATION.md`). Interface: run with
  `BEMOTION_APPDATA=<scratch folder>` and `BEMOTION_SCRIPT=steps.txt`
  (`app/DevScript.h`: `move`, `down`/`up`, `key`, `wheel`, `type`, `drop`,
  `capture`, `window TITLE` to drive another window such as Settings, `quit`),
  then read the captures (other open windows and menus are captured too).
  Start these runs from PowerShell or Python: MSYS2's bash sometimes drops the
  environment. Scripted input moves no real cursor and cannot drive a real
  drag-and-drop (`QDrag`) or a native file dialog — test those paths another
  way (a menu, an operation) and say what was not tested.
- Before committing: build, core tests, smoke test, and a look at anything
  visual. After pushing: `gh run list`, then check that Windows, Linux and
  macOS passed.

## Pitfalls already met (do not repeat them)

- `show()` / `setVisible(true)` on a widget before it has a parent makes it a
  window for an instant (windows flash). Add it to its layout first.
- `QString::arg` fills the *lowest* `%n` present: dropping one placeholder
  shifts every value after it. Prefer named placeholders and `replace`.
- A `QAction` shortcut repeats while the key is held: only stepping / zooming
  actions keep auto-repeat (see `EditorWindow::action`).
- Lua C API: check the stack order when building tables (`lua_setfield(L, -2,
  …)` on the wrong value aborts the app at startup).
- libc++ (macOS) lacks some C++20 pieces GCC has (`std::clock_cast`); CI
  catches it — keep the code portable.
- FFmpeg differs between platforms (6.1 on Ubuntu, 8 on MSYS2): guard newer
  APIs with version checks.
- A saved dock layout (`layout.ads`) from before a new panel is replaced by the
  default; keep that check when adding panels.
- Skia drawing into a `QImage`: use `qtImageInfo()` (explicit BGRA), or colors
  swap on macOS.
- Files in the OneDrive folder can be locked for a moment while syncing; a
  failed write is worth one retry.

## Safety

- Never touch the user's real settings, recents or projects (`%APPDATA%\bemotion`,
  and the projects folder they chose: the `projects_folder` setting) in tests:
  always a scratch `BEMOTION_APPDATA`. Never delete user files. bemotion never
  creates folders on its own: the user picks where projects live.
- Never record from the microphone, open the camera, or reach the network in
  tests.
- Ask before adding dependencies, breaking a contract, or anything
  irreversible. Commits: short imperative subject, body with what and why, a
  `Co-Authored-By:` trailer for AI-assisted work; docs and tests in the same
  commit.
