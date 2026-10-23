# Notes for coding agents

bemotion is a C++20 2D motion graphics animator (Qt 6 Widgets, Skia, FFmpeg,
Lua plugins). **Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before
changing code** — it is the source of truth for structure, conventions and
recipes. The essentials:

- Layers: `core` (no Qt / Skia) ← `render` ← `media` / `export` ← `ui` ← `app`.
  Dependencies only point down.
- Outside changes to a project go through **operations** (`src/core/Ops.cpp`,
  reference `docs/OPERATIONS.md`); every rule lives in `src/core/Validate.cpp`.
  New script / AI abilities are new operations, documented in the same commit.
- Contracts — the `.bemotion` format, operations JSON, plugin API, property and
  effect ids, settings keys, command-line options — change only compatibly.
- Match the surrounding code (`.clang-format`, naming, comment style); change
  only what the task needs; no unrelated reformatting.
- UTF-8 everywhere: `platform::fromUtf8` / `toUtf8`, `platform::arguments`,
  `platform::env` (never raw `argv` / `getenv` / `path.string()` on Windows).
- Verify: build, `ctest`, `tools/smoke-test.sh <build dir>`; check the
  interface with `BEMOTION_APPDATA=<scratch>` + `BEMOTION_SCRIPT` captures —
  never the user's real settings.

Build (Windows, MSYS2 UCRT64 shell): `cmake -S . -B build -G Ninja
-DCMAKE_BUILD_TYPE=Release -DSKIA_ROOT=<skia>` then `cmake --build build`;
releases with `tools/build-release.sh`. Details in the README.
