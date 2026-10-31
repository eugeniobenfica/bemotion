#pragma once

// The graphics card, when the app has one to draw on (ui/GpuContext makes it): Skia draws the
// composition there (effects, blurs and video frames many times faster than on the processor),
// and the pixels are read back. None (no OpenGL, the command line, switched off in Settings ›
// Performance): everything draws on the processor, as before. Same pictures either way, up to
// the last bit of antialiasing.
//
// Only the thread that made it (the interface's) may use it; others draw on the processor, or on
// a graphics card context of their own (setThreadGpu: the viewport's render thread).

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

class GrDirectContext;

namespace bemotion::render {

class Gpu {
public:
    virtual ~Gpu() = default;
    // Its context made current on this thread; false: not here (another thread, a lost device).
    virtual bool makeCurrent() = 0;
    virtual GrDirectContext* context() = 0;
};

void setGpu(Gpu* gpu); // null: the processor
// This thread's own, in place of the interface's (null: back to it).
void setThreadGpu(Gpu* gpu);
Gpu* gpu(); // this thread's own, else the interface's

// A surface of `info` to draw a frame on: on the graphics card when there is one usable from
// this thread (made current), else null (the caller draws in memory).
sk_sp<SkSurface> makeGpuSurface(const SkImageInfo& info);

} // namespace bemotion::render
