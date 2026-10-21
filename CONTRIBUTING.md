# Contributing to Mirage

Thanks for your interest! Mirage aims to stay **simple, fast and easy to
maintain**, so a few guidelines:

1. **Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first.** Keep
   `src/core` free of UI code and put each panel's logic in its own class.
2. **Style:** C++20, 4-space indentation, 120 columns. A `.clang-format` file
   is provided — run `clang-format -i` on the files you touch.
   Types are `PascalCase`, functions and variables `camelCase`, private
   members end with `_`, constants start with `k`.
3. **Tests:** add or update tests in `tests/` for changes to `src/core`, and
   make sure `ctest --test-dir build` passes.
4. **File format:** never break existing `.mirage` files. Add new properties
   with sensible defaults; bump `Project::kFormatVersion` only for
   incompatible changes and keep loading older versions.
5. **Commits / PRs:** small and focused, with a clear description. Screenshots
   are welcome for UI changes.

## Building

See the [README](README.md#building).
