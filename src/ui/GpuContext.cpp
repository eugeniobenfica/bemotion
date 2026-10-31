#include "ui/GpuContext.h"

#include "app/Platform.h"
#include "render/Gpu.h"

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#ifdef SK_VULKAN
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#endif

#include <QLibrary>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace bemotion::ui {

namespace {

// A graphics card the renderer draws on, made once per engine and kept for when it is picked again.
class EngineGpu : public render::Gpu {
public:
    virtual bool create() = 0;
    virtual void release() = 0;
    const std::string& name() const { return name_; }

protected:
    std::string name_;
};

class QtGpu final : public EngineGpu {
public:
    bool create() override {
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
    void release() override {
        if (!skia_) return;
        if (context_->makeCurrent(surface_.get())) skia_->releaseResourcesAndAbandonContext();
        skia_.reset();
        context_->doneCurrent();
    }

private:
    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<QOffscreenSurface> surface_;
    sk_sp<GrDirectContext> skia_;
};

#ifdef SK_VULKAN
// Vulkan: an instance and a device of our own, no window (Skia draws offscreen and the pixels are
// read back, as with OpenGL). The loader is opened at run time: without Vulkan the app still starts
// and this engine reports that it is not there.
class VulkanGpu final : public EngineGpu {
public:
    bool create() override {
        setThread();
#ifdef _WIN32
        loader_.setFileName("vulkan-1");
#elif defined(__APPLE__)
        loader_.setFileName("libvulkan.1");
#else
        loader_.setFileName("libvulkan.so.1");
#endif
        getInstanceProc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(loader_.resolve("vkGetInstanceProcAddr"));
        if (!getInstanceProc_) return false;
        auto global = [this](const char* name) { return getInstanceProc_(VK_NULL_HANDLE, name); };
        auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(global("vkCreateInstance"));
        if (!createInstance) return false;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "bemotion";
        app.apiVersion = VK_API_VERSION_1_1; // Skia's minimum
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        if (createInstance(&info, nullptr, &instance_) != VK_SUCCESS) return false;
        auto inst = [this](const char* name) { return getInstanceProc_(instance_, name); };
        auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(inst("vkEnumeratePhysicalDevices"));
        auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(inst("vkGetPhysicalDeviceProperties"));
        auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(inst("vkGetPhysicalDeviceQueueFamilyProperties"));
        auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(inst("vkCreateDevice"));
        getDeviceProc_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(inst("vkGetDeviceProcAddr"));
        if (!enumerate || !properties || !families || !createDevice || !getDeviceProc_) return false;
        uint32_t count = 0;
        enumerate(instance_, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        if (count == 0 || enumerate(instance_, &count, devices.data()) != VK_SUCCESS) return false;
        // The discrete card first, then an integrated one; each needs a queue that draws.
        int best = -1, bestRank = -1;
        uint32_t bestQueue = 0;
        for (uint32_t i = 0; i < count; ++i) {
            VkPhysicalDeviceProperties p;
            properties(devices[i], &p);
            if (p.apiVersion < VK_API_VERSION_1_1) continue;
            uint32_t n = 0;
            families(devices[i], &n, nullptr);
            std::vector<VkQueueFamilyProperties> qs(n);
            families(devices[i], &n, qs.data());
            int queue = -1;
            for (uint32_t q = 0; q < n && queue < 0; ++q)
                if (qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) queue = static_cast<int>(q);
            if (queue < 0) continue;
            const int rank = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (rank > bestRank) best = static_cast<int>(i), bestRank = rank, bestQueue = static_cast<uint32_t>(queue), name_ = p.deviceName;
        }
        if (best < 0) return false;
        physical_ = devices[static_cast<size_t>(best)];
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = bestQueue;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        if (createDevice(physical_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) return false;
        auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(getDeviceProc_(device_, "vkGetDeviceQueue"));
        if (!getQueue) return false;
        VkQueue queue = VK_NULL_HANDLE;
        getQueue(device_, bestQueue, 0, &queue);

        skgpu::VulkanGetProc proc = [this](const char* name, VkInstance instance, VkDevice device) -> PFN_vkVoidFunction {
            if (device != VK_NULL_HANDLE) return getDeviceProc_(device, name);
            return getInstanceProc_(instance, name);
        };
        extensions_.init(proc, instance_, physical_, 0, nullptr, 0, nullptr);
        skgpu::VulkanBackendContext backend;
        backend.fInstance = instance_;
        backend.fPhysicalDevice = physical_;
        backend.fDevice = device_;
        backend.fQueue = queue;
        backend.fGraphicsQueueIndex = bestQueue;
        backend.fMaxAPIVersion = VK_API_VERSION_1_1;
        backend.fVkExtensions = &extensions_;
        backend.fGetProc = proc;
        skia_ = GrDirectContexts::MakeVulkan(backend);
        if (skia_) name_ += " (Vulkan)";
        return static_cast<bool>(skia_);
    }
    bool makeCurrent() override { return skia_ && QThread::currentThread() == thread_; } // nothing to bind
    void setThread() { thread_ = QThread::currentThread(); } // the one that creates it uses it
    GrDirectContext* context() override { return skia_.get(); }
    void release() override {
        // Skia lets go of what it made on the card. The device and instance stay: surfaces kept by
        // the views (and Skia's memory allocator with them) go only after this, as the windows
        // close; destroying the device first crashes there. Ending the process frees them.
        if (skia_) skia_->releaseResourcesAndAbandonContext();
        skia_.reset();
    }

private:
    QLibrary loader_;
    QThread* thread_ = QThread::currentThread();
    PFN_vkGetInstanceProcAddr getInstanceProc_ = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProc_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    skgpu::VulkanExtensions extensions_;
    sk_sp<GrDirectContext> skia_;
};
#endif

// OpenGL for another thread: its offscreen surface is made on the interface's thread (Qt asks
// for that), its context on the thread that draws, current there and nowhere else.
class QtThreadGpu final : public ThreadGpu {
  public:
    QtThreadGpu() {
        surface_ = std::make_unique<QOffscreenSurface>();
        surface_->setFormat(QSurfaceFormat::defaultFormat());
        surface_->create();
    }
    bool create() override {
        if (!surface_->isValid()) return false;
        context_ = std::make_unique<QOpenGLContext>();
        context_->setFormat(surface_->format());
        if (!context_->create() || !context_->makeCurrent(surface_.get())) return false;
        GrContextOptions options;
        options.fAvoidStencilBuffers = true;
        skia_ = GrDirectContexts::MakeGL(GrGLMakeNativeInterface(), options);
        return static_cast<bool>(skia_);
    }
    bool makeCurrent() override {
        if (!skia_ || QThread::currentThread() != context_->thread()) return false;
        if (QOpenGLContext::currentContext() == context_.get()) return true;
        return context_->makeCurrent(surface_.get());
    }
    GrDirectContext* context() override { return skia_.get(); }
    void release() override {
        if (skia_ && context_->makeCurrent(surface_.get())) skia_->releaseResourcesAndAbandonContext();
        skia_.reset();
        if (context_) context_->doneCurrent();
        context_.reset(); // on its own thread
    }

  private:
    std::unique_ptr<QOffscreenSurface> surface_; // goes on the interface's thread (the destructor)
    std::unique_ptr<QOpenGLContext> context_;
    sk_sp<GrDirectContext> skia_;
};

#ifdef SK_VULKAN
class VulkanThreadGpu final : public ThreadGpu {
  public:
    bool create() override { return vulkan_.create(); }
    bool makeCurrent() override { return vulkan_.makeCurrent(); }
    GrDirectContext* context() override { return vulkan_.context(); }
    void release() override { vulkan_.release(); }

  private:
    VulkanGpu vulkan_;
};
#endif

EngineGpu* g_gpus[3] = {};   // by Engine; Processor stays null
bool g_failed[3] = {};
Engine g_engine = Engine::Processor;
int g_cacheMegabytes = 512;

EngineGpu* current() { return g_engine == Engine::Processor ? nullptr : g_gpus[static_cast<int>(g_engine)]; }

void applyCacheLimit() {
    if (EngineGpu* gpu = current(); gpu && gpu->makeCurrent() && gpu->context())
        gpu->context()->setResourceCacheLimit(static_cast<size_t>(std::max(64, g_cacheMegabytes)) << 20);
}

} // namespace

bool vulkanAvailable() {
#ifdef SK_VULKAN
    return true;
#else
    return false;
#endif
}

Engine useEngine(Engine engine) {
    const int i = static_cast<int>(engine);
    if (engine != Engine::Processor && !g_gpus[i] && !g_failed[i]) {
        EngineGpu* gpu = nullptr;
        if (engine == Engine::OpenGL) gpu = new QtGpu;
#ifdef SK_VULKAN
        else gpu = new VulkanGpu;
#endif
        if (gpu && gpu->create()) {
            g_gpus[i] = gpu;
            platform::log("drawing on the graphics card: " + gpu->name());
        } else {
            if (gpu) gpu->release();
            delete gpu;
            g_failed[i] = true;
            platform::log(engine == Engine::Vulkan ? "no usable Vulkan" : "no usable OpenGL");
        }
    }
    // What was asked, else the other graphics engine, else the processor.
    if (engine == Engine::Processor || !g_gpus[i]) {
        const Engine other = engine == Engine::Vulkan ? Engine::OpenGL : engine == Engine::OpenGL && vulkanAvailable() ? Engine::Vulkan : Engine::Processor;
        if (engine != Engine::Processor && other != Engine::Processor && other != engine && !g_failed[static_cast<int>(other)]) return useEngine(other);
        if (engine != Engine::Processor && !g_gpus[i]) engine = Engine::Processor;
    }
    g_engine = engine;
    render::setGpu(current());
    applyCacheLimit();
    return engine;
}

std::unique_ptr<ThreadGpu> prepareThreadGpu() {
    if (g_engine == Engine::OpenGL && QOpenGLContext::supportsThreadedOpenGL()) return std::make_unique<QtThreadGpu>();
#ifdef SK_VULKAN
    if (g_engine == Engine::Vulkan) return std::make_unique<VulkanThreadGpu>();
#endif
    return nullptr;
}

bool useGpu(bool on) { return useEngine(on ? Engine::OpenGL : Engine::Processor) != Engine::Processor; }

Engine currentEngine() { return g_engine; }

void releaseGpu() {
    render::setGpu(nullptr);
    for (EngineGpu* gpu : g_gpus)
        if (gpu) gpu->release();
}

const char* gpuName() { return current() && render::gpu() ? current()->name().c_str() : ""; }

void setGpuCacheMegabytes(int megabytes) {
    g_cacheMegabytes = megabytes;
    applyCacheLimit();
}

} // namespace bemotion::ui
