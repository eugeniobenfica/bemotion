#pragma once

// The editor's graphics card (render/Gpu.h), through the drawing engine Settings › Performance
// picks: OpenGL (an offscreen context Qt makes) or Vulkan (an instance and device of our own,
// when Skia was built with it), each with Skia's context on it. An engine that does not work here
// falls back to the other, then to the processor.

namespace bemotion::ui {

// Makes it (once) and hands it to the renderer; false: no usable OpenGL here. `on` false: the
// renderer goes back to the processor (the context stays, for when it is turned on again).
bool useGpu(bool on);
enum class Engine { Processor, OpenGL, Vulkan };
// Draws with `engine` from now on; returns the one in use (another when it does not work here).
Engine useEngine(Engine engine);
Engine currentEngine();
bool vulkanAvailable(); // this build's Skia has Vulkan
// Releases Skia's resources while the context still exists (before the application ends).
void releaseGpu();
// What draws the pictures, for Settings ("NVIDIA GeForce ..."; empty: the processor).
const char* gpuName();
// How much of the graphics card's memory Skia keeps for pictures, glyphs and effects between
// frames (Settings › Performance): more reuses more, fewer uploads while scrubbing.
void setGpuCacheMegabytes(int megabytes);

} // namespace bemotion::ui
