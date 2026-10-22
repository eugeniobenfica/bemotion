#pragma once

// Runtime side of imported media: GPU textures for images and video frames,
// one video reader per layer (so cut clips of one video show different
// frames), decoded audio, thumbnails and errors. Plugs into the renderer.

#include "core/Project.h"

#include <imgui.h>

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
    void release(); // frees GPU textures (GL context still alive)

    // Once per frame: starts decoding audio of new items, polls finished jobs.
    void update(const Composition& comp);

    // Texture to draw for a media layer at composition time `t` (0 when not available).
    ImTextureID frameFor(const Composition& comp, const Layer& layer, double t);
    ImTextureID thumbnail(const MediaItem& item); // first frame / the image; 0 while unavailable

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
