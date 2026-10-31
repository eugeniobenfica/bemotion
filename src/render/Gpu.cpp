#include "render/Gpu.h"

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"

namespace bemotion::render {

namespace {
Gpu* g_gpu = nullptr;
thread_local Gpu* t_gpu = nullptr;
} // namespace

void setGpu(Gpu* gpu) { g_gpu = gpu; }
void setThreadGpu(Gpu* gpu) { t_gpu = gpu; }
Gpu* gpu() { return t_gpu ? t_gpu : g_gpu; }

sk_sp<SkSurface> makeGpuSurface(const SkImageInfo& info) {
    Gpu* g = gpu();
    if (!g || !g->makeCurrent() || !g->context()) return nullptr;
    return SkSurfaces::RenderTarget(g->context(), skgpu::Budgeted::kYes, info);
}

} // namespace bemotion::render
