#pragma once

#include <cstdint>
#include <vector>

namespace mirage {

// Procedurally rasterized app icon (golden "M" on a dark rounded square),
// RGBA8, size x size pixels. Used for the window/taskbar icon.
std::vector<uint8_t> renderAppIcon(int size);

} // namespace mirage
