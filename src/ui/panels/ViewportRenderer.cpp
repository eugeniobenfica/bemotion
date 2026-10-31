#include "ui/panels/ViewportRenderer.h"

#include "media/MediaManager.h"
#include "render/Gpu.h"
#include "ui/SkiaQt.h"

#include "include/core/SkCanvas.h"

#include <QElapsedTimer>

#include <future>

namespace bemotion::ui {

ViewportRenderer::ViewportRenderer(std::function<void()> ready) : ready_(std::move(ready)) {
    engine_ = currentEngine();
    if (engine_ != Engine::Processor) {
        gpu_ = prepareThreadGpu();
        if (!gpu_) return; // the card cannot be shared: the viewport draws as before
    }
    auto started = std::make_shared<std::promise<bool>>();
    std::future<bool> result = started->get_future();
    thread_ = std::thread([this, started] { run(started); });
    available_ = result.get();
}

ViewportRenderer::~ViewportRenderer() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    gpu_.reset(); // (an OpenGL surface goes on the interface's thread)
}

void ViewportRenderer::submit(Job job) {
    {
        std::lock_guard lock(mutex_);
        waiting_ = std::move(job);
    }
    wake_.notify_all();
}

std::optional<ViewportRenderer::Result> ViewportRenderer::take() {
    std::lock_guard lock(mutex_);
    return std::exchange(done_, std::nullopt);
}

void ViewportRenderer::run(std::shared_ptr<std::promise<bool>> started) {
    const bool onGpu = gpu_ && gpu_->create();
    if (gpu_ && !onGpu) {
        gpu_->release();
        started->set_value(false); // drawing on the processor here would be slower than the card there
        return;
    }
    if (onGpu) render::setThreadGpu(gpu_.get());
    media::MediaManager::get().setPreviewCopy(&snapshot_);
    started->set_value(true);
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || waiting_; });
            if (stop_) break;
            job = std::move(*waiting_);
            waiting_.reset();
        }
        Result result = draw(job);
        {
            std::lock_guard lock(mutex_);
            done_ = std::move(result);
        }
        if (ready_) ready_();
    }
    media::MediaManager::get().setPreviewCopy(nullptr);
    surface_.reset();
    if (onGpu) {
        gpu_->release();
        render::setThreadGpu(nullptr);
    }
}

ViewportRenderer::Result ViewportRenderer::draw(const Job& job) {
    Result result;
    result.job = job;
    // The document, copied in place (the same address) only when it changed.
    if (!hasSnapshot_ || job.revision != snapshotRevision_ || job.uncropped != snapshotUncropped_) {
        snapshot_ = *job.comp;
        if (job.uncropped)
            if (Layer* l = snapshot_.findLayer(job.uncropped))
                l->media.cropLeft = l->media.cropTop = l->media.cropRight = l->media.cropBottom = AnimFloat{0.0f};
        snapshotRevision_ = job.revision;
        snapshotUncropped_ = job.uncropped;
        hasSnapshot_ = true;
    }
    QImage image(job.region.size(), QImage::Format_ARGB32_Premultiplied);
    image.setOffset(job.region.topLeft());
    image.setDevicePixelRatio(job.rasterScale);
    const SkImageInfo info = qtImageInfo(job.region.width(), job.region.height());
    // On this thread's graphics card when it has one (read back into the image), else straight
    // into the image.
    render::Gpu* gpu = render::gpu();
    if (!gpu)
        surface_.reset();
    else if (!surface_ || surface_->width() != info.width() || surface_->height() != info.height())
        surface_ = render::makeGpuSurface(info);
    const bool onGpu = surface_ && gpu && gpu->makeCurrent();
    if (onGpu)
        surface_->getCanvas()->clear(SK_ColorTRANSPARENT);
    else
        image.fill(Qt::transparent);
    sk_sp<SkSurface> surface =
        onGpu ? surface_ : SkSurfaces::WrapPixels(info, image.bits(), static_cast<size_t>(image.bytesPerLine()));
    if (!surface) return result;
    media::MediaManager& media = media::MediaManager::get();
    media.takeStale(); // from here on: did this frame use a stand-in?
    QElapsedTimer timer;
    timer.start();
    render::drawComposition(surface->getCanvas(), snapshot_, job.time, job.view, true, job.throughCamera,
                            job.orbit ? &job.lens : nullptr);
    if (onGpu) surface->readPixels(info, image.bits(), static_cast<size_t>(image.bytesPerLine()), 0, 0);
    result.ms = timer.nsecsElapsed() / 1e6;
    result.stale = media.takeStale();
    result.image = std::move(image);
    return result;
}

} // namespace bemotion::ui
