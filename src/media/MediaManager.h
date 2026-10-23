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
    bool audioPending(uint64_t mediaId) const;
    bool audioReady(const Composition& comp) const; // no audio of the composition still decoding
    bool fileMissing(const MediaItem& item) const;
    std::string error(uint64_t mediaId) const;

    // Imports files into the composition; returns the new (or already imported) item ids.
    std::vector<uint64_t> import(Composition& comp, const std::vector<std::filesystem::path>& files,
                                 std::vector<std::string>* errors = nullptr);

private:
    MediaManager() = default;
    struct Impl;
    Impl& impl() const;
};

} // namespace bemotion::media
