// Media files: still images through stb_image, video and audio through
// FFmpeg's libraries (FfmpegMedia.cpp), the same on every platform.

#include "media/MediaIO.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace bemotion::media {

// FfmpegMedia.cpp
bool ffmpegProbe(const std::filesystem::path& file, MediaItem& item, std::string* error);
bool ffmpegDecodeAudio(const std::filesystem::path& file, std::vector<float>& stereo, std::string* error);

namespace {

const std::vector<std::string> kImages = {"png", "jpg", "jpeg", "bmp", "tga", "gif", "psd", "hdr"};
const std::vector<std::string> kAudio = {"wav", "mp3", "flac", "m4a", "aac", "ogg", "opus", "wma", "aif", "aiff"};
const std::vector<std::string> kVideo = {"mp4", "mov", "m4v", "mkv", "webm", "avi", "wmv", "mpg", "mpeg", "mts", "m2ts"};

std::string extensionOf(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool contains(const std::vector<std::string>& list, const std::string& ext) {
    return std::find(list.begin(), list.end(), ext) != list.end();
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// stb_image opens files by narrow name: go through a FILE* opened with the wide name instead.
std::FILE* openFile(const std::filesystem::path& file) {
#ifdef _WIN32
    return _wfopen(file.wstring().c_str(), L"rb");
#else
    return std::fopen(file.c_str(), "rb");
#endif
}

} // namespace

const std::vector<std::string>& supportedExtensions() {
    static const std::vector<std::string> all = [] {
        std::vector<std::string> v = kImages;
        v.insert(v.end(), kAudio.begin(), kAudio.end());
        v.insert(v.end(), kVideo.begin(), kVideo.end());
        return v;
    }();
    return all;
}

ProbeResult probe(const std::filesystem::path& file) {
    ProbeResult r;
    r.item.path = utf8(file);
    r.item.name = utf8(file.filename());
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        r.error = "The file does not exist.";
        return r;
    }
    const std::string ext = extensionOf(file);
    if (contains(kImages, ext)) {
        std::FILE* f = openFile(file);
        int w = 0, h = 0, n = 0;
        const bool ok = f && stbi_info_from_file(f, &w, &h, &n);
        if (f) std::fclose(f);
        if (!ok) {
            r.error = "This image could not be read.";
            return r;
        }
        r.item.kind = MediaKind::Image;
        r.item.width = w;
        r.item.height = h;
        r.item.hasVideo = true;
        r.ok = true;
        return r;
    }
    if (contains(kAudio, ext) || contains(kVideo, ext)) {
        r.ok = ffmpegProbe(file, r.item, &r.error);
        return r;
    }
    r.error = "This file type is not supported (" + (ext.empty() ? std::string("no extension") : "." + ext) + ").";
    return r;
}

bool loadImage(const std::filesystem::path& file, int& width, int& height, std::vector<uint8_t>& rgba, std::string* error) {
    std::FILE* f = openFile(file);
    if (!f) {
        if (error) *error = "Could not open the image.";
        return false;
    }
    int n = 0;
    stbi_uc* pixels = stbi_load_from_file(f, &width, &height, &n, 4);
    std::fclose(f);
    if (!pixels) {
        if (error) *error = stbi_failure_reason() ? stbi_failure_reason() : "Could not read the image.";
        return false;
    }
    rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

bool decodeAudio(const std::filesystem::path& file, std::vector<float>& stereo, std::string* error) {
    return ffmpegDecodeAudio(file, stereo, error);
}

} // namespace bemotion::media
