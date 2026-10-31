#pragma once

// Draws the viewport's frames on a thread of its own, so the interface never waits for a heavy
// frame (blurs, many videos, 3D): the viewport asks for a frame, keeps showing the last one, and
// shows the new one when it is ready. Only the latest request is drawn (a newer one replaces a
// waiting one). The thread draws on a graphics card context of its own (ui/GpuContext), or on the
// processor when the editor does; where the card cannot be shared between threads, available()
// is false and the viewport draws as before, on the interface's thread.
//
// What the thread reads is its own copy of the composition, kept while the document does not
// change (the same address: media readers are kept by it, MediaManager::setPreviewCopy). Plugin
// objects (PluginHost) and media (MediaManager) are locked while it uses them.

#include "core/Project.h"
#include "render/SceneRenderer.h"
#include "ui/GpuContext.h"

#include <QImage>
#include <QRect>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace bemotion::ui {

class ViewportRenderer {
  public:
    struct Job {
        std::shared_ptr<const Composition> comp; // the document, copied once per revision
        uint64_t revision = 0;
        uint64_t uncropped = 0; // a layer shown without its crop (the Crop tool)
        double time = 0.0;
        QRect region; // raster pixels of the view to draw
        qreal rasterScale = 1.0;
        render::View view; // already offset to the region
        bool throughCamera = true;
        bool orbit = false;
        Lens lens;
        uint64_t serial = 0; // the viewport's own number, to know which request this answers
    };
    struct Result {
        Job job;
        QImage image; // the region, premultiplied, at rasterScale
        double ms = 0.0;
        bool stale = false; // a video frame still on its way was shown
    };

    // `ready` runs on the render thread when a frame is done (post it to the interface's).
    explicit ViewportRenderer(std::function<void()> ready);
    ~ViewportRenderer(); // waits for the frame being drawn
    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    bool available() const { return available_; }
    Engine engine() const { return engine_; } // the drawing engine it was made for
    void submit(Job job);
    std::optional<Result> take(); // the last frame done, once

  private:
    void run(std::shared_ptr<std::promise<bool>> started);
    Result draw(const Job& job);

    std::function<void()> ready_;
    std::unique_ptr<ThreadGpu> gpu_; // made here, created and released on the thread
    Engine engine_ = Engine::Processor;
    bool available_ = false;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Job> waiting_;
    std::optional<Result> done_;
    bool stop_ = false;

    // The thread's own (only it touches them).
    Composition snapshot_;
    uint64_t snapshotRevision_ = 0, snapshotUncropped_ = 0;
    bool hasSnapshot_ = false;
    sk_sp<SkSurface> surface_;

    std::thread thread_; // last: everything above exists before it runs
};

} // namespace bemotion::ui
