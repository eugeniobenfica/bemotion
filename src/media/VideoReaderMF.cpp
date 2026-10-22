// Video (and audio in video containers) through Windows Media Foundation.
// Other systems get a stub: video import is not available there yet.

#include "media/MediaIO.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bemotion::media {

namespace {

template <typename T>
void releaseCom(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Media Foundation stays started for the whole session (Startup / Shutdown are ref counted).
bool ensureMediaFoundation() {
    static const bool ok = [] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        return SUCCEEDED(MFStartup(MF_VERSION));
    }();
    return ok;
}

IMFSourceReader* openReader(const std::filesystem::path& file, bool videoProcessing) {
    if (!ensureMediaFoundation()) return nullptr;
    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    if (videoProcessing) attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); // decode to RGB32
    IMFSourceReader* reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(file.wstring().c_str(), attrs, &reader))) reader = nullptr;
    releaseCom(attrs);
    return reader;
}

double durationOf(IMFSourceReader* reader) {
    PROPVARIANT var;
    PropVariantInit(&var);
    double seconds = 0.0;
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)))
        seconds = static_cast<double>(var.uhVal.QuadPart) / 1e7;
    PropVariantClear(&var);
    return seconds;
}

bool hasStream(IMFSourceReader* reader, DWORD stream) {
    IMFMediaType* type = nullptr;
    const bool ok = SUCCEEDED(reader->GetNativeMediaType(stream, 0, &type));
    releaseCom(type);
    return ok;
}

class MFVideoReader final : public VideoReader {
public:
    ~MFVideoReader() override { releaseCom(reader_); }

    bool init(const std::filesystem::path& file, std::string* error) {
        reader_ = openReader(file, true);
        if (!reader_) {
            if (error) *error = "This video could not be opened (unsupported format or codec).";
            return false;
        }
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
        IMFMediaType* type = nullptr;
        MFCreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        const HRESULT hr = reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
        releaseCom(type);
        if (FAILED(hr)) {
            if (error) *error = "This video cannot be decoded on this system.";
            return false;
        }
        IMFMediaType* current = nullptr;
        reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current);
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
        width_ = static_cast<int>(w);
        height_ = static_cast<int>(h);
        stride_ = static_cast<LONG>(MFGetAttributeUINT32(current, MF_MT_DEFAULT_STRIDE, w * 4));
        UINT32 num = 0, den = 0;
        if (SUCCEEDED(MFGetAttributeRatio(current, MF_MT_FRAME_RATE, &num, &den)) && num && den)
            frameDuration_ = static_cast<LONGLONG>(1e7 * den / num);
        releaseCom(current);
        if (width_ <= 0 || height_ <= 0) return false;
        // Media Foundation ignores MP4 edit lists: with B-frames the first frame is stamped a few frames
        // late. The first frame is the source's time 0.
        for (int guard = 0; guard < 64 && !haveFrame_; ++guard) {
            DWORD stream = 0, flags = 0;
            LONGLONG time = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags,
                                           &time, &sample)) ||
                (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                releaseCom(sample);
                break;
            }
            if (!sample) continue;
            origin_ = std::max<LONGLONG>(0, time);
            store(sample, time - origin_);
            releaseCom(sample);
        }
        return true;
    }

    int width() const override { return width_; }
    int height() const override { return height_; }

    bool frameAt(double seconds, std::vector<uint8_t>& rgba) override {
        const LONGLONG target = static_cast<LONGLONG>(std::max(0.0, seconds) * 1e7);
        // Already showing it?
        if (haveFrame_ && target >= frameTime_ && target < frameTime_ + frameDuration_) {
            rgba = frame_;
            return true;
        }
        // Backwards or far ahead: seek (lands on a keyframe before the target, then read forward).
        if (!haveFrame_ || target < frameTime_ || target > frameTime_ + 10'000'000LL) {
            PROPVARIANT pos;
            InitPropVariantFromInt64(target + origin_, &pos);
            reader_->SetCurrentPosition(GUID_NULL, pos);
            PropVariantClear(&pos);
            haveFrame_ = false;
        }
        for (int guard = 0; guard < 2000; ++guard) {
            DWORD stream = 0, flags = 0;
            LONGLONG time = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags,
                                           &time, &sample)))
                return finish(rgba);
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                releaseCom(sample);
                return finish(rgba); // past the end: keep the last frame
            }
            if (!sample) continue;
            time -= origin_;
            LONGLONG duration = 0;
            if (SUCCEEDED(sample->GetSampleDuration(&duration)) && duration > 0) frameDuration_ = duration;
            store(sample, time);
            releaseCom(sample);
            if (time + frameDuration_ > target) break; // this frame covers the target
        }
        return finish(rgba);
    }

private:
    bool finish(std::vector<uint8_t>& rgba) {
        if (!haveFrame_) return false;
        rgba = frame_;
        return true;
    }

    // RGB32 (BGRX) sample -> top-down RGBA frame.
    void store(IMFSample* sample, LONGLONG time) {
        IMFMediaBuffer* buffer = nullptr;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return;
        BYTE* data = nullptr;
        DWORD length = 0;
        if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
            frame_.resize(static_cast<size_t>(width_) * height_ * 4);
            const LONG stride = stride_ != 0 ? stride_ : width_ * 4;
            const LONG absStride = std::abs(stride);
            if (static_cast<size_t>(absStride) * height_ <= length)
                for (int y = 0; y < height_; ++y) {
                    // Negative stride: rows are stored bottom-up.
                    const BYTE* row = stride > 0 ? data + static_cast<size_t>(y) * stride
                                                 : data + static_cast<size_t>(height_ - 1 - y) * absStride;
                    uint8_t* out = frame_.data() + static_cast<size_t>(y) * width_ * 4;
                    for (int x = 0; x < width_; ++x) {
                        out[x * 4 + 0] = row[x * 4 + 2];
                        out[x * 4 + 1] = row[x * 4 + 1];
                        out[x * 4 + 2] = row[x * 4 + 0];
                        out[x * 4 + 3] = 255;
                    }
                }
            buffer->Unlock();
            frameTime_ = time;
            haveFrame_ = true;
        }
        releaseCom(buffer);
    }

    IMFSourceReader* reader_ = nullptr;
    int width_ = 0, height_ = 0;
    LONG stride_ = 0;
    LONGLONG frameDuration_ = 333'333; // 30 fps until known
    LONGLONG frameTime_ = 0;
    LONGLONG origin_ = 0; // timestamp of the first frame
    bool haveFrame_ = false;
    std::vector<uint8_t> frame_;
};

} // namespace

std::unique_ptr<VideoReader> VideoReader::open(const std::filesystem::path& file, std::string* error) {
    auto reader = std::make_unique<MFVideoReader>();
    if (!reader->init(file, error)) return nullptr;
    return reader;
}

bool platformProbeVideo(const std::filesystem::path& file, MediaItem& item, std::string* error) {
    IMFSourceReader* reader = openReader(file, false);
    if (!reader) {
        if (error) *error = "This file could not be opened (unsupported format or codec).";
        return false;
    }
    item.duration = durationOf(reader);
    item.hasVideo = hasStream(reader, static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM));
    item.hasAudio = hasStream(reader, static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM));
    if (item.hasVideo) {
        IMFMediaType* type = nullptr;
        if (SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type))) {
            UINT32 w = 0, h = 0, num = 0, den = 0;
            MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
            if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) && den) item.fps = double(num) / den;
            item.width = static_cast<int>(w);
            item.height = static_cast<int>(h);
            releaseCom(type);
        }
    }
    releaseCom(reader);
    if (!item.hasVideo && !item.hasAudio) {
        if (error) *error = "No video or audio found in this file.";
        return false;
    }
    item.kind = item.hasVideo ? MediaKind::Video : MediaKind::Audio;
    return true;
}

bool platformDecodeAudio(const std::filesystem::path& file, std::vector<float>& stereo, std::string* error) {
    IMFSourceReader* reader = openReader(file, false);
    if (!reader) {
        if (error) *error = "This file could not be opened.";
        return false;
    }
    const DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(audio, TRUE);
    IMFMediaType* type = nullptr;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    HRESULT hr = reader->SetCurrentMediaType(audio, nullptr, type);
    releaseCom(type);
    IMFMediaType* current = nullptr;
    if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(audio, &current);
    if (FAILED(hr)) {
        releaseCom(reader);
        if (error) *error = "The audio of this file cannot be decoded.";
        return false;
    }
    const UINT32 channels = std::max<UINT32>(1, MFGetAttributeUINT32(current, MF_MT_AUDIO_NUM_CHANNELS, 2));
    const UINT32 rate = std::max<UINT32>(1, MFGetAttributeUINT32(current, MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate));
    releaseCom(current);

    // Native rate and channels first, then stereo at kAudioRate.
    std::vector<float> native;
    for (;;) {
        DWORD stream = 0, flags = 0;
        LONGLONG time = 0;
        IMFSample* sample = nullptr;
        if (FAILED(reader->ReadSample(audio, 0, &stream, &flags, &time, &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            releaseCom(sample);
            break;
        }
        if (!sample) continue;
        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                const float* f = reinterpret_cast<const float*>(data);
                native.insert(native.end(), f, f + length / sizeof(float));
                buffer->Unlock();
            }
            releaseCom(buffer);
        }
        releaseCom(sample);
    }
    releaseCom(reader);

    const size_t frames = native.size() / channels;
    const double ratio = static_cast<double>(rate) / kAudioRate;
    const size_t outFrames = static_cast<size_t>(frames / ratio);
    stereo.assign(outFrames * 2, 0.0f);
    for (size_t i = 0; i < outFrames; ++i) { // linear resampling
        const double src = i * ratio;
        const size_t a = std::min(frames - 1, static_cast<size_t>(src));
        const size_t b = std::min(frames - 1, a + 1);
        const float k = static_cast<float>(src - a);
        for (int c = 0; c < 2; ++c) {
            const UINT32 ch = std::min<UINT32>(static_cast<UINT32>(c), channels - 1); // mono -> both sides
            stereo[i * 2 + c] = native[a * channels + ch] * (1 - k) + native[b * channels + ch] * k;
        }
    }
    return true;
}

} // namespace bemotion::media

#else // !_WIN32

namespace bemotion::media {

std::unique_ptr<VideoReader> VideoReader::open(const std::filesystem::path&, std::string* error) {
    if (error) *error = "Video import is only available on Windows for now.";
    return nullptr;
}

bool platformProbeVideo(const std::filesystem::path&, MediaItem&, std::string* error) {
    if (error) *error = "Video import is only available on Windows for now.";
    return false;
}

bool platformDecodeAudio(const std::filesystem::path&, std::vector<float>&, std::string* error) {
    if (error) *error = "This audio format is only supported on Windows.";
    return false;
}

} // namespace bemotion::media

#endif
