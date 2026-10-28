#include "render/Gpu.h"

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"

namespace bemotion::render {

namespace {
Gpu* g_gpu = nullptr;
}

void setGpu(Gpu* gpu) { g_gpu = gpu; }
Gpu* gpu() { return g_gpu; }

sk_sp<SkSurface> makeGpuSurface(const SkImageInfo& info) {
    if (!g_gpu || !g_gpu->makeCurrent() || !g_gpu->context()) return nullptr;
    return SkSurfaces::RenderTarget(g_gpu->context(), skgpu::Budgeted::kYes, info);
}

} // namespace bemotion::render
