#pragma once

// Runtime side of imported media: decoded pictures (images, video frames and
// thumbnails as Skia images), one video reader per (composition, layer) so
// cut clips of one video show different frames and an export never fights the
// editor over seeking, decoded audio, and errors. Plugs into the renderer.

#include "core/Project.h"

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bemotion::media {

using AudioData = std::shared_ptr<const std::vector<float>>; // stereo, kAudioRate
using Loudness = std::shared_ptr<const std::vector<float>>;  // RMS every kLoudnessBlock frames
// Small pictures along a video, for the timeline: frame i shows the source at (i + 0.5) * step.
// Made a little at a time: an empty frame is not there yet (show the nearest one).
struct Filmstrip {
    double step = 1.0;
    int width = 0, height = 0;
    std::vector<std::vector<uint8_t>> frames; // RGBA, width * height * 4 each
    bool complete = false;
    uint64_t version = 0; // grows as frames arrive
};
constexpr size_t kLoudnessBlock = 480;                         // 10 ms at kAudioRate

class MediaManager {
public:
    static MediaManager& get();

    void install(); // registers the renderer's frame provider
    void release(); // frees decoded pictures and readers

    // Once per frame: starts decoding audio of new items, polls finished jobs, drops idle readers.
    void update(const Composition& comp);

    // Picture of a media layer at composition time `t` (null when not available).
    sk_sp<SkImage> frameFor(const Composition& comp, const Layer& layer, double t);
    sk_sp<SkImage> thumbnail(const MediaItem& item); // the image / an early video frame; null while unavailable
    // A video's filmstrip, made in the background the first time it is asked for (from its proxy
    // when there is one): a few pictures along all of it first, then the others, then the exact
    // frames; null until the first ones are there (arrivals() changes as more arrive).
    std::shared_ptr<const Filmstrip> filmstrip(const MediaItem& item);

    AudioData audio(uint64_t mediaId) const; // null while decoding (or without audio)
    // The audio as a layer's EQ and Noise Reduction make it (shared by layers with the same
    // settings; update() prepares it in the background). The plain audio until it is ready.
    AudioData audioFor(uint64_t mediaId, const std::vector<AudioEffect>& effects) const;
    Loudness loudness(uint64_t mediaId) const; // of the plain audio (null while decoding)
    std::string audioPathOf(uint64_t mediaId) const; // the file that audio was decoded from ("" if none)
    bool audioPending(uint64_t mediaId) const;
    bool audioReady(const Composition& comp) const; // no audio of the composition still decoding or processing
    bool fileMissing(const MediaItem& item) const;
    std::string error(uint64_t mediaId) const;

    // Proxies (see MediaIO.h): the editor's composition (`preview`) shows a video's proxy when
    // there is one and they are on; exports and other compositions always show the original.
    void setPreview(const Composition* preview);
    void setUseProxies(bool on);
    bool useProxies() const;
    bool hasProxy(const MediaItem& item) const;
    bool showsProxy(const Composition& comp, const MediaItem& item) const; // this frame comes from the proxy
    void makeProxies(const std::vector<MediaItem>& items); // one after the other, in the background
    float proxyProgress(uint64_t mediaId) const;            // 0..1 while being made (or waiting), -1 otherwise
    std::string proxyError(uint64_t mediaId) const;
    void deleteProxy(const MediaItem& item);
    // Every proxy file's size; deleting them all (none being made).
    uint64_t proxyBytes() const;
    void deleteAllProxies();

    // Decoded video frames are kept (scrubbing back, a loop playing again, cut clips of one
    // video) up to this much memory, the least recently shown going first.
    void setFrameCacheMegabytes(int megabytes);
    // Bumped whenever a picture can change without the project changing (a proxy made or
    // switched on, a file found again): rendered frames cached elsewhere are stale then.
    uint64_t generation() const;
    // Scrubbing the editor's composition: a far video frame is decoded in the background and the
    // last one is shown meanwhile. arrivals() changes when such a frame is ready (draw again);
    // takeStale() says whether a picture since the last call was such a stand-in (a render that
    // used one is not kept), and clears it.
    uint64_t arrivals() const;
    bool takeStale();
    // How many screen pixels a composition pixel takes in a view of the editor's composition
    // (0: the view is gone): its videos are decoded no bigger than shown (setScaledPreview).
    void setPreviewScale(const void* view, double pixelsPerUnit);
    void setScaledPreview(bool on); // the Performance setting "Decode video at the size shown"
    // How many output pixels a composition pixel takes when `comp` (another than the editor's: an
    // export's copy) is rendered (0: forget it): its videos are decoded at the size they come
    // out, rounded up (never smaller), not at their full size to be shrunk while drawing.
    void setOutputScale(const Composition* comp, double pixelsPerUnit);
    // Every video opened again (hardware decoding turned on or off).
    void reopenVideos();

    // Imports files into the composition; returns the new (or already imported) item ids.
    std::vector<uint64_t> import(Composition& comp, const std::vector<std::filesystem::path>& files,
                                 std::vector<std::string>* errors = nullptr);

private:
    MediaManager() = default;
    struct Impl;
    Impl& impl() const;
};

} // namespace bemotion::media
