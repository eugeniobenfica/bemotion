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

    AudioData audio(uint64_t mediaId) const; // null while decoding (or without audio)
    // The audio as a layer's EQ and Noise Reduction make it (shared by layers with the same
    // settings; update() prepares it in the background). The plain audio until it is ready.
    AudioData audioFor(uint64_t mediaId, const std::vector<AudioEffect>& effects) const;
    Loudness loudness(uint64_t mediaId) const; // of the plain audio (null while decoding)
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
