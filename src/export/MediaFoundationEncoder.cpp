// MP4 (H.264) through Windows Media Foundation: no external software needed.
// Frames are converted to NV12 (BT.709, limited range) before encoding.

#ifdef _WIN32

#include "export/Export.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace bemotion::exporting {

namespace {

template <typename T>
void releaseCom(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::string hresultMessage(const char* what, HRESULT hr) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s (error 0x%08lX).", what, static_cast<unsigned long>(hr));
    return buf;
}

uint8_t clampByte(float v) { return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f)); }

// Top-down RGBA -> NV12: full-resolution Y plane followed by interleaved U/V at half resolution.
void rgbaToNv12(const uint8_t* rgba, int w, int h, uint8_t* nv12) {
    uint8_t* yPlane = nv12;
    uint8_t* uvPlane = nv12 + static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
            yPlane[static_cast<size_t>(y) * w + x] = clampByte(16.0f + 0.1826f * p[0] + 0.6142f * p[1] + 0.0620f * p[2]);
        }
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2) {
            float r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t* p = rgba + (static_cast<size_t>(y + dy) * w + (x + dx)) * 4;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            r *= 0.25f;
            g *= 0.25f;
            b *= 0.25f;
            uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * w + x;
            uv[0] = clampByte(128.0f - 0.1006f * r - 0.3386f * g + 0.4392f * b);
            uv[1] = clampByte(128.0f + 0.4392f * r - 0.3989f * g - 0.0403f * b);
        }
}

class MediaFoundationEncoder final : public Encoder {
public:
    ~MediaFoundationEncoder() override {
        std::string ignored;
        finish(ignored);
    }

    bool open(const EncoderParams& params, std::string& error) override {
        width_ = params.width;
        height_ = params.height;
        // Rational frame rate (handles 23.976, 29.97, ...).
        rateDen_ = 1000;
        rateNum_ = static_cast<UINT32>(std::lround(params.fps * rateDen_));

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInit_ = SUCCEEDED(hr); // S_FALSE also needs a matching CoUninitialize
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            error = hresultMessage("Media Foundation is not available on this system", hr);
            return false;
        }
        mfInit_ = true;

        IMFAttributes* attrs = nullptr;
        MFCreateAttributes(&attrs, 1);
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        DeleteFileW(params.output.wstring().c_str()); // the sink writer does not overwrite
        hr = MFCreateSinkWriterFromURL(params.output.wstring().c_str(), nullptr, attrs, &writer_);
        releaseCom(attrs);
        if (FAILED(hr)) {
            error = hresultMessage("Could not create the video file", hr);
            return false;
        }

        // Bits per pixel per frame, scaled with quality.
        const double bpp = params.quality == Quality::Low ? 0.06 : params.quality == Quality::Medium ? 0.12 : 0.22;
        const UINT32 bitrate =
            static_cast<UINT32>(std::clamp(bpp * width_ * height_ * params.fps, 500'000.0, 120'000'000.0));

        IMFMediaType* out = nullptr;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        out->SetUINT32(MF_MT_MPEG2_PROFILE, 100); // eAVEncH264VProfile_High
        describeFrames(out);
        hr = writer_->AddStream(out, &stream_);
        releaseCom(out);
        if (FAILED(hr)) {
            error = hresultMessage("The H.264 encoder rejected these settings (try a smaller resolution)", hr);
            return false;
        }

        IMFMediaType* in = nullptr;
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        describeFrames(in);
        hr = writer_->SetInputMediaType(stream_, in, nullptr);
        releaseCom(in);
        if (FAILED(hr)) {
            error = hresultMessage("The H.264 encoder rejected the input format", hr);
            return false;
        }
        if (params.audio && !addAudioStream(*params.audio, error)) return false;
        hr = writer_->BeginWriting();
        if (FAILED(hr)) {
            error = hresultMessage("Could not start writing the video", hr);
            return false;
        }
        nv12_.resize(static_cast<size_t>(width_) * height_ * 3 / 2);
        return true;
    }

    bool write(const uint8_t* rgba, std::string& error) override {
        rgbaToNv12(rgba, width_, height_, nv12_.data());

        IMFMediaBuffer* buffer = nullptr;
        HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(nv12_.size()), &buffer);
        if (SUCCEEDED(hr)) {
            BYTE* data = nullptr;
            hr = buffer->Lock(&data, nullptr, nullptr);
            if (SUCCEEDED(hr)) {
                std::memcpy(data, nv12_.data(), nv12_.size());
                buffer->Unlock();
                buffer->SetCurrentLength(static_cast<DWORD>(nv12_.size()));
            }
        }
        IMFSample* sample = nullptr;
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
        if (SUCCEEDED(hr)) {
            // Timestamps in 100 ns units, computed from the frame index to avoid drift.
            const LONGLONG start = frame_ * 10'000'000LL * rateDen_ / rateNum_;
            const LONGLONG end = (frame_ + 1) * 10'000'000LL * rateDen_ / rateNum_;
            sample->SetSampleTime(start);
            sample->SetSampleDuration(end - start);
            hr = writer_->WriteSample(stream_, sample);
        }
        releaseCom(sample);
        releaseCom(buffer);
        if (FAILED(hr)) {
            error = hresultMessage("Encoding a frame failed", hr);
            return false;
        }
        ++frame_;
        // The audio that belongs up to the end of this frame.
        const size_t until = static_cast<size_t>(frame_ * kExportAudioRate * static_cast<LONGLONG>(rateDen_) / rateNum_);
        if (!writeAudio(until, error)) return false;
        return true;
    }

    bool finish(std::string& error) override {
        bool ok = true;
        if (writer_) {
            if (frame_ > 0 && !writeAudio(SIZE_MAX, error)) ok = false;
            const HRESULT hr = frame_ > 0 ? writer_->Finalize() : S_OK;
            if (FAILED(hr)) {
                error = hresultMessage("Could not finish the video file", hr);
                ok = false;
            }
            releaseCom(writer_);
        }
        if (mfInit_) MFShutdown();
        if (comInit_) CoUninitialize();
        mfInit_ = comInit_ = false;
        return ok;
    }

private:
    // AAC (48 kHz stereo) next to the video; fed 16-bit PCM.
    bool addAudioStream(const std::vector<float>& stereo, std::string& error) {
        IMFMediaType* out = nullptr;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kExportAudioRate);
        out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbps
        HRESULT hr = writer_->AddStream(out, &audioStream_);
        releaseCom(out);
        if (FAILED(hr)) {
            error = hresultMessage("The AAC encoder is not available", hr);
            return false;
        }
        IMFMediaType* in = nullptr;
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kExportAudioRate);
        in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kExportAudioRate * 4);
        hr = writer_->SetInputMediaType(audioStream_, in, nullptr);
        releaseCom(in);
        if (FAILED(hr)) {
            error = hresultMessage("The AAC encoder rejected the audio format", hr);
            return false;
        }
        audio_ = &stereo;
        audioDone_ = 0;
        return true;
    }

    // Writes audio frames (stereo pairs) up to `until`.
    bool writeAudio(size_t until, std::string& error) {
        if (!audio_) return true;
        const size_t total = audio_->size() / 2;
        until = std::min(until, total);
        while (audioDone_ < until) {
            const size_t n = std::min<size_t>(until - audioDone_, kExportAudioRate / 10);
            IMFMediaBuffer* buffer = nullptr;
            HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(n * 4), &buffer);
            if (SUCCEEDED(hr)) {
                BYTE* data = nullptr;
                hr = buffer->Lock(&data, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    auto* pcm = reinterpret_cast<int16_t*>(data);
                    for (size_t i = 0; i < n * 2; ++i)
                        pcm[i] = static_cast<int16_t>(std::lround(std::clamp((*audio_)[audioDone_ * 2 + i], -1.0f, 1.0f) * 32767.0f));
                    buffer->Unlock();
                    buffer->SetCurrentLength(static_cast<DWORD>(n * 4));
                }
            }
            IMFSample* sample = nullptr;
            if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
            if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
            if (SUCCEEDED(hr)) {
                sample->SetSampleTime(static_cast<LONGLONG>(audioDone_) * 10'000'000LL / kExportAudioRate);
                sample->SetSampleDuration(static_cast<LONGLONG>(n) * 10'000'000LL / kExportAudioRate);
                hr = writer_->WriteSample(audioStream_, sample);
            }
            releaseCom(sample);
            releaseCom(buffer);
            if (FAILED(hr)) {
                error = hresultMessage("Encoding the audio failed", hr);
                return false;
            }
            audioDone_ += n;
        }
        return true;
    }

    void describeFrames(IMFMediaType* t) const {
        t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(t, MF_MT_FRAME_SIZE, width_, height_);
        MFSetAttributeRatio(t, MF_MT_FRAME_RATE, rateNum_, rateDen_);
        MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        t->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
        t->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
        t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    }

    IMFSinkWriter* writer_ = nullptr;
    DWORD stream_ = 0;
    DWORD audioStream_ = 0;
    const std::vector<float>* audio_ = nullptr;
    size_t audioDone_ = 0;
    int width_ = 0;
    int height_ = 0;
    UINT32 rateNum_ = 30;
    UINT32 rateDen_ = 1;
    LONGLONG frame_ = 0;
    bool comInit_ = false;
    bool mfInit_ = false;
    std::vector<uint8_t> nv12_;
};

} // namespace

std::unique_ptr<Encoder> makeMediaFoundationEncoder() { return std::make_unique<MediaFoundationEncoder>(); }

} // namespace bemotion::exporting

#endif // _WIN32
