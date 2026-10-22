// Media files: images (stb_image), audio (miniaudio; Media Foundation for
// audio in video containers on Windows) and probing. Video frames are read by
// the platform reader (VideoReaderMF.cpp on Windows).

#include "media/MediaIO.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#include <miniaudio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace bemotion::media {

// Implemented per platform (VideoReaderMF.cpp; a stub elsewhere).
bool platformProbeVideo(const std::filesystem::path& file, MediaItem& item, std::string* error);
bool platformDecodeAudio(const std::filesystem::path& file, std::vector<float>& stereo, std::string* error);

namespace {

const std::vector<std::string> kImages = {"png", "jpg", "jpeg", "bmp", "tga", "gif", "psd", "hdr"};
const std::vector<std::string> kAudio = {"wav", "mp3", "flac"};
const std::vector<std::string> kPlatformAudio = {"m4a", "aac", "wma"};
const std::vector<std::string> kVideo = {"mp4", "mov", "m4v", "wmv", "avi", "mkv"};

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

bool miniaudioDecode(const std::filesystem::path& file, std::vector<float>& stereo, double* duration, std::string* error) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, kAudioRate);
    ma_decoder decoder;
#ifdef _WIN32
    const ma_result r = ma_decoder_init_file_w(file.wstring().c_str(), &config, &decoder);
#else
    const ma_result r = ma_decoder_init_file(file.c_str(), &config, &decoder);
#endif
    if (r != MA_SUCCESS) {
        if (error) *error = "Could not read this audio file.";
        return false;
    }
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    if (duration) *duration = static_cast<double>(frames) / kAudioRate;
    if (!duration) { // decoding wanted, not just the length
        stereo.clear();
        float chunk[4096 * 2];
        for (;;) {
            ma_uint64 read = 0;
            if (ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read) != MA_SUCCESS && read == 0) break;
            stereo.insert(stereo.end(), chunk, chunk + read * 2);
            if (read < 4096) break;
        }
    }
    ma_decoder_uninit(&decoder);
    return true;
}

} // namespace

const std::vector<std::string>& supportedExtensions() {
    static const std::vector<std::string> all = [] {
        std::vector<std::string> v = kImages;
        v.insert(v.end(), kAudio.begin(), kAudio.end());
#ifdef _WIN32
        v.insert(v.end(), kPlatformAudio.begin(), kPlatformAudio.end());
        v.insert(v.end(), kVideo.begin(), kVideo.end());
#endif
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
    if (contains(kAudio, ext)) {
        std::vector<float> none;
        double duration = 0;
        if (!miniaudioDecode(file, none, &duration, &r.error)) return r;
        r.item.kind = MediaKind::Audio;
        r.item.duration = duration;
        r.item.hasAudio = true;
        r.ok = true;
        return r;
    }
    if (contains(kVideo, ext) || contains(kPlatformAudio, ext)) {
        if (!platformProbeVideo(file, r.item, &r.error)) return r;
        r.ok = true;
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
    const std::string ext = extensionOf(file);
    if (contains(kAudio, ext)) return miniaudioDecode(file, stereo, nullptr, error);
    return platformDecodeAudio(file, stereo, error);
}

} // namespace bemotion::media
