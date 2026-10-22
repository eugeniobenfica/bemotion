#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace bemotion {

// Procedurally rasterized app icon (the golden mark on a dark rounded square),
// RGBA8, size x size pixels. Used for the window/taskbar icon.
std::vector<uint8_t> renderAppIcon(int size);

// Writes the icon as a multi-size .ico (16-256 px), a 1024 px .png or an
// .svg; with `markOnly`, just the golden mark on a transparent background
// (`bemotion --write-icon <file>` / `--write-mark <file>`, used to refresh assets/branding).
bool writeAppIcon(const std::filesystem::path& file, bool markOnly = false);

} // namespace bemotion
