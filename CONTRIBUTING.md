<img src="assets/branding/icon.svg" width="64" alt="bemotion icon">

# Contributing to bemotion

Thanks for your interest! bemotion aims to stay **simple, fast and easy to
maintain**, so a few guidelines:

1. **Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first.** It explains the
   layers, the rules, how to add properties, effects, operations and panels,
   and how to work alongside others (including AI agents) without conflicts.
2. **Style:** C++20, 4-space indentation, 120 columns. A `.clang-format` file
   is provided — run `clang-format -i` on the files you touch.
   Types are `PascalCase`, functions and variables `camelCase`, private
   members end with `_`, constants start with `k`.
3. **Tests:** add or update tests in `tests/` for changes to `src/core`, and
   make sure `ctest --test-dir build` passes.
4. **File format:** never break existing `.bemotion` files. Add new properties
   with sensible defaults; bump `Project::kFormatVersion` only for
   incompatible changes and keep loading older versions.
5. **Commits / PRs:** small and focused, with a clear description. Screenshots
   are welcome for UI changes (the pull request template has a checklist).
6. **Issues:** use the *Bug report* or *Feature request* forms; a small
   `.bemotion` project that shows a problem helps a lot.

## Building

See [Getting started](README.md#getting-started) in the README: Qt 6, FFmpeg and a
Skia built once with `tools/skia/build-skia.sh` (on Windows, from an MSYS2
UCRT64 shell).
