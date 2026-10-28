// Media files: still images through stb_image (or FFmpeg's image decoders for the formats stb
// does not know: WebP, TIFF, AVIF, HEIC, JPEG XL, EXR...), video and audio through FFmpeg's
// libraries (FfmpegMedia.cpp), the same on every platform. An animated GIF or WebP is a video.

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
bool ffmpegDecodeImage(const std::filesystem::path& file, int& width, int& height, std::vector<uint8_t>& rgba, std::string* error,
                       int* frames);

namespace {

const std::vector<std::string> kImages = {"png", "jpg", "jpeg", "jfif", "bmp", "tga", "gif", "psd", "hdr"};
// Pictures FFmpeg reads (when its build has the decoder: a missing one says so on import).
const std::vector<std::string> kFfmpegImages = {"webp", "tif", "tiff", "avif", "heic", "heif", "jxl", "exr", "qoi", "ico", "cur", "dds",
                                                "ppm", "pgm", "pbm", "pam", "pfm", "pcx", "sgi", "rgb", "dpx", "cin", "jp2", "j2k", "jpx",
                                                "xbm", "xpm", "xwd", "ras", "sun", "pic", "pict", "jpe", "apng"};
const std::vector<std::string> kAudio = {"wav", "mp3", "flac", "m4a", "m4b", "aac", "ogg", "oga", "opus", "wma", "aif", "aiff", "aifc",
                                         "caf", "wv", "ape", "amr", "ac3", "eac3", "dts", "mka", "mp2", "au", "snd", "w64", "tta", "spx", "voc"};
const std::vector<std::string> kVideo = {"mp4", "mov", "m4v", "mkv", "webm", "avi", "wmv", "mpg", "mpeg", "mts", "m2ts", "ts", "3gp", "3g2",
                                         "flv", "f4v", "ogv", "mxf", "vob", "dv", "asf", "m2v", "mpv", "y4m", "nut", "rm", "rmvb", "hevc",
                                         "h264", "h265", "264", "265", "ivf", "divx"};
// Formats that may be a still or an animation: more than one picture makes them a video.
const std::vector<std::string> kMaybeAnimated = {"gif", "webp", "apng"};

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
        v.insert(v.end(), kFfmpegImages.begin(), kFfmpegImages.end());
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
    if (contains(kMaybeAnimated, ext)) { // an animation plays as a video
        int w = 0, h = 0, frames = 0;
        std::vector<uint8_t> first;
        if (ffmpegDecodeImage(file, w, h, first, nullptr, &frames) && frames > 1 && ffmpegProbe(file, r.item, nullptr) && r.item.hasVideo &&
            r.item.duration > 0.0) {
            r.ok = true;
            return r;
        }
        r.item = MediaItem{};
        r.item.path = utf8(file);
        r.item.name = utf8(file.filename());
    }
    if (contains(kImages, ext) || contains(kFfmpegImages, ext)) {
        std::FILE* f = contains(kImages, ext) ? openFile(file) : nullptr;
        int w = 0, h = 0, n = 0;
        bool ok = f && stbi_info_from_file(f, &w, &h, &n);
        if (f) std::fclose(f);
        if (!ok) { // FFmpeg's decoders: the formats stb does not read, and stb's misses
            std::vector<uint8_t> rgba;
            ok = ffmpegDecodeImage(file, w, h, rgba, &r.error, nullptr);
        }
        if (!ok) {
            if (r.error.empty()) r.error = "This image could not be read.";
            return r;
        }
        r.error.clear();
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
    stbi_uc* pixels = contains(kImages, extensionOf(file)) ? stbi_load_from_file(f, &width, &height, &n, 4) : nullptr;
    std::fclose(f);
    if (!pixels) { // not one stb reads: FFmpeg's decoders
        const char* why = stbi_failure_reason();
        if (ffmpegDecodeImage(file, width, height, rgba, nullptr, nullptr)) return true;
        if (error) *error = why ? why : "Could not read the image.";
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
