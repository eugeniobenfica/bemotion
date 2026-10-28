#include "ui/GpuContext.h"

#include "app/Platform.h"
#include "render/Gpu.h"

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>

#include <memory>
#include <string>

namespace bemotion::ui {

namespace {

class QtGpu final : public render::Gpu {
public:
    bool create() {
        context_ = std::make_unique<QOpenGLContext>();
        if (!context_->create()) return false;
        surface_ = std::make_unique<QOffscreenSurface>();
        surface_->setFormat(context_->format());
        surface_->create();
        if (!surface_->isValid() || !context_->makeCurrent(surface_.get())) return false;
        GrContextOptions options;
        options.fAvoidStencilBuffers = true; // an offscreen surface without one
        skia_ = GrDirectContexts::MakeGL(GrGLMakeNativeInterface(), options);
        if (!skia_) return false;
        if (const GLubyte* renderer = context_->functions()->glGetString(GL_RENDERER)) name_ = reinterpret_cast<const char*>(renderer);
        return true;
    }
    bool makeCurrent() override {
        if (!skia_ || QThread::currentThread() != context_->thread()) return false;
        if (QOpenGLContext::currentContext() == context_.get()) return true;
        return context_->makeCurrent(surface_.get());
    }
    GrDirectContext* context() override { return skia_.get(); }
    void release() {
        if (!skia_) return;
        if (context_->makeCurrent(surface_.get())) skia_->releaseResourcesAndAbandonContext();
        skia_.reset();
        context_->doneCurrent();
    }
    const std::string& name() const { return name_; }

private:
    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<QOffscreenSurface> surface_;
    sk_sp<GrDirectContext> skia_;
    std::string name_;
};

QtGpu* g_gpu = nullptr;
bool g_failed = false;

} // namespace

bool useGpu(bool on) {
    if (!on) {
        render::setGpu(nullptr);
        return true;
    }
    if (!g_gpu && !g_failed) {
        auto* gpu = new QtGpu;
        if (gpu->create()) {
            g_gpu = gpu;
            platform::log("drawing on the graphics card: " + gpu->name());
        } else {
            gpu->release();
            delete gpu;
            g_failed = true;
            platform::log("no usable OpenGL: drawing on the processor");
        }
    }
    render::setGpu(g_gpu);
    return g_gpu != nullptr;
}

void releaseGpu() {
    render::setGpu(nullptr);
    if (g_gpu) g_gpu->release();
}

const char* gpuName() { return g_gpu && render::gpu() ? g_gpu->name().c_str() : ""; }

} // namespace bemotion::ui
