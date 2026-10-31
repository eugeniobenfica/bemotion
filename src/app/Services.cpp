#include "app/Services.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "core/AudioDsp.h"
#include "media/AudioEngine.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "render/OffscreenRenderer.h"
#include "render/SceneRenderer.h"

#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace bemotion {

std::vector<double> spreadTimes(const Composition& c, int count) {
    const int n = std::clamp(count, 1, 64);
    std::vector<double> times;
    for (int i = 0; i < n; ++i) times.push_back(n == 1 ? 0.0 : c.snapToFrame((c.duration - c.frameDuration()) * i / (n - 1)));
    return times;
}

bool renderFramesPng(const Composition& c, std::vector<double> times, int frameWidth, std::vector<uint8_t>& png, std::string& error) {
    if (times.empty()) times = {0.0};
    for (double& t : times) t = std::clamp(t, 0.0, c.duration);
    const float scale = static_cast<float>(std::clamp(frameWidth, 16, 8192)) / c.width;
    const int fw = std::max(1, static_cast<int>(std::lround(c.width * scale))), fh = std::max(1, static_cast<int>(std::lround(c.height * scale)));
    render::OffscreenRenderer renderer;
    if (!renderer.init(fw, fh, error)) return false;
    // One frame as is; several on a contact sheet, left to right and top to bottom, 4 px apart.
    const int n = static_cast<int>(times.size());
    const int cols = n == 1 ? 1 : std::min(n, n <= 4 ? 2 : 3), rows = (n + cols - 1) / cols;
    const int gap = n == 1 ? 0 : 4, sw = cols * fw + (cols + 1) * gap, sh = rows * fh + (rows + 1) * gap;
    std::vector<uint8_t> sheet(static_cast<size_t>(sw) * sh * 4, 0);
    for (size_t i = 0; i < sheet.size(); i += 4) sheet[i] = sheet[i + 1] = sheet[i + 2] = 40, sheet[i + 3] = 255;
    std::vector<uint8_t> frame;
    for (int k = 0; k < n; ++k) {
        renderer.render(c, times[k], scale, true, frame);
        const int x0 = gap + (k % cols) * (fw + gap), y0 = gap + (k / cols) * (fh + gap);
        for (int y = 0; y < fh; ++y)
            std::copy_n(frame.data() + static_cast<size_t>(y) * fw * 4, static_cast<size_t>(fw) * 4,
                        sheet.data() + (static_cast<size_t>(y0 + y) * sw + x0) * 4);
    }
    png.clear();
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* out = static_cast<std::vector<uint8_t>*>(ctx);
            out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &png, sw, sh, 4, sheet.data(), sw * 4);
    if (png.empty()) {
        error = "could not encode the image";
        return false;
    }
    return true;
}

bool exportProject(Project project, exporting::Settings settings, const std::function<void(int, int)>& progress, std::string& error) {
    if (settings.output.extension().empty()) {
        settings.output += exporting::formatExtension(settings.format);
    } else {
        for (int k = 0; k < static_cast<int>(exporting::Format::Count); ++k)
            if (settings.output.extension() == exporting::formatExtension(static_cast<exporting::Format>(k)))
                settings.format = static_cast<exporting::Format>(k);
    }
    project.comp.fitDuration();
    if (settings.end <= settings.start) settings.end = project.comp.duration;
    PluginHost::get().reconcile(project.comp);
    // Audio decodes in the background: wait for it.
    for (int tries = 0; !media::MediaManager::get().audioReady(project.comp) && tries < 60000; ++tries) {
        media::MediaManager::get().update(project.comp);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    settings.mixAudio = media::AudioEngine::mix;
    settings.decodeScale = [](const Composition* comp, double s) { media::MediaManager::get().setOutputScale(comp, s); };
    exporting::ExportJob job;
    if (!job.start(project, settings, error)) return false;
    while (job.running()) {
        job.step(0.25);
        if (progress) progress(job.framesDone(), job.framesTotal());
    }
    if (!job.succeeded()) {
        error = job.cancelled() ? "cancelled" : job.error();
        return false;
    }
    return true;
}

OpEnvironment automationEnvironment() {
    OpEnvironment env;
    env.probeMedia = [](const std::string& path, MediaItem& out, std::string& error) {
        const media::ProbeResult r = media::probe(platform::fromUtf8(path));
        if (!r.ok) {
            error = r.error;
            return false;
        }
        out = r.item;
        return true;
    };
    env.makePluginLayer = [](const std::string& objectType, Layer& out, std::string& error) {
        const PluginObjectType* type = PluginHost::get().findType(objectType);
        if (!type) {
            std::string known;
            for (const PluginObjectType* t : PluginHost::get().objectTypes()) known += (known.empty() ? "" : ", ") + t->id;
            error = "unknown object type \"" + objectType + "\" (installed: " + (known.empty() ? "none" : known) + ")";
            return false;
        }
        out = PluginHost::get().createLayer(*type, {0, 0}, kDefaultLayerLength);
        return true;
    };
    env.layerBounds = [](const Composition& comp, const Layer& layer, double t, Vec2& mn, Vec2& mx) {
        return render::localBounds(layer, t, mn, mx, &comp);
    };
    env.mediaLoudness = [](const MediaItem& item, std::vector<float>& loudness, double& blockSeconds, std::string& error) {
        blockSeconds = static_cast<double>(media::kLoudnessBlock) / media::kAudioRate;
        // The editor's decoded sound when it has it (same item, same file); else read it now.
        if (const media::Loudness known = media::MediaManager::get().loudness(item.id);
            known && media::MediaManager::get().audioPathOf(item.id) == item.path) {
            loudness = *known;
            return true;
        }
        std::vector<float> stereo;
        if (!media::decodeAudio(platform::fromUtf8(item.path), stereo, &error)) return false;
        loudness = dsp::loudness(stereo, media::kLoudnessBlock);
        return true;
    };
    return env;
}

} // namespace bemotion
