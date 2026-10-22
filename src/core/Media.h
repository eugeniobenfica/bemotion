#pragma once

// Imported footage (images, videos, audio), like After Effects' Project panel
// items: the composition keeps a list of media items, and media layers refer
// to them. Decoding lives in src/media; this is only the document model.

#include <cstdint>
#include <string>

namespace bemotion {

enum class MediaKind { Image, Video, Audio };

const char* mediaKindLabel(MediaKind kind);

struct MediaItem {
    uint64_t id = 0;
    std::string name;     // file name, shown in the Media panel
    std::string path;     // absolute path (UTF-8) when imported
    std::string relative; // path relative to the project file, tried first when loading
    MediaKind kind = MediaKind::Image;
    int width = 0, height = 0; // pixels (images and videos)
    double duration = 0.0;     // seconds (videos and audio)
    double fps = 0.0;          // videos
    bool hasVideo = false;
    bool hasAudio = false;
};

} // namespace bemotion
