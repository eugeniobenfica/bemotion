#pragma once

// The editor's graphics card (render/Gpu.h): an offscreen OpenGL context Qt makes, and Skia's
// context on it. Installed at start when Settings › Performance allows it and OpenGL works;
// otherwise everything keeps drawing on the processor.

namespace bemotion::ui {

// Makes it (once) and hands it to the renderer; false: no usable OpenGL here. `on` false: the
// renderer goes back to the processor (the context stays, for when it is turned on again).
bool useGpu(bool on);
// Releases Skia's resources while the context still exists (before the application ends).
void releaseGpu();
// What draws the pictures, for Settings ("NVIDIA GeForce ..."; empty: the processor).
const char* gpuName();

} // namespace bemotion::ui
