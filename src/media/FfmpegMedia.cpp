// Video and audio files through FFmpeg's libraries (libavformat / libavcodec /
// libswscale / libswresample): probing, whole-track audio decoding and
// random-access video frames, on every platform.

#include "media/MediaIO.h"

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace bemotion::media {

namespace {

// FFmpeg prints codec details to stderr by default: only errors are worth seeing.
const bool kQuietLogs = (av_log_set_level(AV_LOG_ERROR), true);


std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string avError(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return buf;
}

// Owning wrappers (FFmpeg's free functions take pointer-to-pointer).
struct Input {
    AVFormatContext* fmt = nullptr;
    ~Input() { avformat_close_input(&fmt); }
    bool open(const std::filesystem::path& file, std::string* error) {
        const int r = avformat_open_input(&fmt, utf8(file).c_str(), nullptr, nullptr);
        if (r < 0 || avformat_find_stream_info(fmt, nullptr) < 0) {
            if (error) *error = "This file could not be read (" + avError(r) + ").";
            return false;
        }
        return true;
    }
};

// --- hardware decoding ---------------------------------------------------------------

std::atomic_bool g_hardware{false};

constexpr AVHWDeviceType kHardwareType =
#if defined(_WIN32)
    AV_HWDEVICE_TYPE_D3D11VA;
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
    AV_HWDEVICE_TYPE_VAAPI;
#endif

// One device for every reader (opening one is slow); null when there is none.
AVBufferRef* hardwareDevice() {
    static std::mutex mutex;
    static AVBufferRef* device = nullptr;
    static bool tried = false;
    std::lock_guard lock(mutex);
    if (!tried) {
        tried = true;
        if (av_hwdevice_ctx_create(&device, kHardwareType, nullptr, nullptr, 0) < 0) device = nullptr;
    }
    return device;
}

// The decoder's offer of formats: the hardware one we asked for (in `opaque`), else software.
AVPixelFormat pickFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
    const auto wanted = static_cast<AVPixelFormat>(reinterpret_cast<intptr_t>(ctx->opaque));
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f)
        if (*f == wanted) return *f;
    return avcodec_default_get_format(ctx, formats);
}

struct Decoder {
    AVCodecContext* ctx = nullptr;
    AVPixelFormat hwFormat = AV_PIX_FMT_NONE; // frames in graphics memory have this format
    ~Decoder() { avcodec_free_context(&ctx); }
    // `hardware`: try the graphics card first (videos only).
    bool open(const AVStream* stream, bool hardware = false) {
        avcodec_free_context(&ctx);
        hwFormat = AV_PIX_FMT_NONE;
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) return false;
        ctx = avcodec_alloc_context3(codec);
        if (!ctx || avcodec_parameters_to_context(ctx, stream->codecpar) < 0) return false;
        ctx->thread_count = 0; // as many threads as useful
        if (hardware) {
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
                if (!config) break;
                if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == kHardwareType) {
                    hwFormat = config->pix_fmt;
                    break;
                }
            }
            AVBufferRef* device = hwFormat != AV_PIX_FMT_NONE ? hardwareDevice() : nullptr;
            if (device) {
                ctx->hw_device_ctx = av_buffer_ref(device);
                ctx->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(hwFormat));
                ctx->get_format = pickFormat;
            } else {
                hwFormat = AV_PIX_FMT_NONE;
            }
        }
        if (avcodec_open2(ctx, codec, nullptr) >= 0) return true;
        if (hwFormat == AV_PIX_FMT_NONE) return false;
        return open(stream, false); // the card would not take it: software
    }
};

struct Frame {
    AVFrame* f = av_frame_alloc();
    ~Frame() { av_frame_free(&f); }
};
struct Packet {
    AVPacket* p = av_packet_alloc();
    ~Packet() { av_packet_free(&p); }
};

double streamDuration(const AVFormatContext* fmt, const AVStream* s) {
    if (s && s->duration > 0) return s->duration * av_q2d(s->time_base);
    return fmt->duration > 0 ? static_cast<double>(fmt->duration) / AV_TIME_BASE : 0.0;
}

// Still pictures stored as "video" (cover art in audio files) are not footage.
bool isRealVideo(const AVStream* s) { return s && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC); }

class FfVideoReader final : public VideoReader {
public:
    ~FfVideoReader() override { sws_freeContext(sws_); }

    bool init(const std::filesystem::path& file, std::string* error) {
        if (!in_.open(file, error)) return false;
        index_ = av_find_best_stream(in_.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index_ < 0 || !isRealVideo(in_.fmt->streams[index_])) {
            if (error) *error = "This file has no video.";
            return false;
        }
        stream_ = in_.fmt->streams[index_];
        // The card for HD and bigger (smaller videos decode fast enough, and copying frames back costs).
        const bool hd = static_cast<int64_t>(stream_->codecpar->width) * stream_->codecpar->height >= 1280 * 720;
        if (!dec_.open(stream_, g_hardware && hd)) {
            if (error) *error = "This video's codec is not supported.";
            return false;
        }
        width_ = dec_.ctx->width;
        height_ = dec_.ctx->height;
        const AVRational rate = av_guess_frame_rate(in_.fmt, stream_, nullptr);
        frameDuration_ = rate.num > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30.0;
        // Time 0 of the clip is its first frame (edit lists and offsets are handled by libavformat).
        origin_ = stream_->start_time != AV_NOPTS_VALUE ? stream_->start_time * av_q2d(stream_->time_base) : 0.0;
        return width_ > 0 && height_ > 0;
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hardware() const override { return dec_.hwFormat != AV_PIX_FMT_NONE; }
    double frameTime() const override { return have_ ? shown_ : -1.0; }
    double frameDuration() const { return frameDuration_; }

    bool frameAt(double seconds, std::vector<uint8_t>& rgba) override {
        if (decode(seconds, rgba)) return true;
        if (!hardware() || !failedHardware_) return false;
        // The card failed on this video: read it in software from here on.
        if (!dec_.open(stream_, false)) return false;
        have_ = false;
        return decode(seconds, rgba);
    }

private:
    bool decode(double seconds, std::vector<uint8_t>& rgba) {
        seconds = std::max(0.0, seconds);
        // Already decoded?
        if (have_ && seconds >= shown_ - 1e-6 && seconds < shown_ + frameDuration_ - 1e-6) {
            rgba = rgba_;
            return true;
        }
        // Backwards, or far ahead: seek to the keyframe before, then decode forward.
        if (!have_ || seconds < shown_ || seconds > shown_ + 1.0) {
            const int64_t ts = static_cast<int64_t>((seconds + origin_) / av_q2d(stream_->time_base));
            av_seek_frame(in_.fmt, index_, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(dec_.ctx);
            eof_ = false;
        }
        Frame frame;
        Packet pkt;
        for (int guard = 0; guard < 4000; ++guard) {
            int r = avcodec_receive_frame(dec_.ctx, frame.f);
            if (r == 0) {
                const int64_t pts = frame.f->best_effort_timestamp;
                const double t = (pts == AV_NOPTS_VALUE ? shown_ + frameDuration_ : pts * av_q2d(stream_->time_base)) - origin_;
                const bool covers = t + frameDuration_ > seconds + 1e-6;
                if (covers || !have_) store(frame.f, t);
                av_frame_unref(frame.f);
                if (covers) break;
                continue;
            }
            if (r == AVERROR_EOF) break; // past the end: keep the last frame
            if (eof_) {
                avcodec_send_packet(dec_.ctx, nullptr); // drain
                continue;
            }
            r = av_read_frame(in_.fmt, pkt.p);
            if (r < 0) {
                eof_ = true;
                avcodec_send_packet(dec_.ctx, nullptr);
                continue;
            }
            if (pkt.p->stream_index == index_ && avcodec_send_packet(dec_.ctx, pkt.p) < 0 && hardware()) failedHardware_ = true;
            av_packet_unref(pkt.p);
        }
        if (!have_) {
            if (hardware()) failedHardware_ = true;
            return false;
        }
        rgba = rgba_;
        return true;
    }

    void store(const AVFrame* frame, double t) {
        const AVFrame* f = frame;
        Frame copy;
        if (frame->format == dec_.hwFormat && dec_.hwFormat != AV_PIX_FMT_NONE) { // in graphics memory: bring it back
            if (av_hwframe_transfer_data(copy.f, frame, 0) < 0) {
                failedHardware_ = true;
                return;
            }
            f = copy.f;
        }
        sws_ = sws_getCachedContext(sws_, f->width, f->height, static_cast<AVPixelFormat>(f->format), width_, height_,
                                    AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) return;
        rgba_.resize(static_cast<size_t>(width_) * height_ * 4);
        uint8_t* dst[4] = {rgba_.data(), nullptr, nullptr, nullptr};
        int stride[4] = {width_ * 4, 0, 0, 0};
        sws_scale(sws_, f->data, f->linesize, 0, f->height, dst, stride);
        shown_ = t;
        have_ = true;
    }

    Input in_;
    Decoder dec_;
    AVStream* stream_ = nullptr;
    SwsContext* sws_ = nullptr;
    int index_ = -1;
    int width_ = 0, height_ = 0;
    double frameDuration_ = 1.0 / 30.0;
    double origin_ = 0.0;
    double shown_ = 0.0;
    bool have_ = false;
    bool eof_ = false;
    bool failedHardware_ = false;
    std::vector<uint8_t> rgba_;
};

} // namespace

bool ffmpegProbe(const std::filesystem::path& file, MediaItem& item, std::string* error) {
    Input in;
    if (!in.open(file, error)) return false;
    const int v = av_find_best_stream(in.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int a = av_find_best_stream(in.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    const AVStream* vs = v >= 0 && isRealVideo(in.fmt->streams[v]) ? in.fmt->streams[v] : nullptr;
    const AVStream* as = a >= 0 ? in.fmt->streams[a] : nullptr;
    item.hasVideo = vs != nullptr;
    item.hasAudio = as != nullptr && avcodec_find_decoder(as->codecpar->codec_id) != nullptr;
    if (!item.hasVideo && !item.hasAudio) {
        if (error) *error = "No video or audio this app can play was found in the file.";
        return false;
    }
    item.kind = item.hasVideo ? MediaKind::Video : MediaKind::Audio;
    item.duration = streamDuration(in.fmt, vs ? vs : as);
    if (vs) {
        if (!avcodec_find_decoder(vs->codecpar->codec_id)) {
            if (error) *error = std::string("Its video codec (") + avcodec_get_name(vs->codecpar->codec_id) + ") is not supported.";
            return false;
        }
        item.width = vs->codecpar->width;
        item.height = vs->codecpar->height;
        const AVRational rate = av_guess_frame_rate(in.fmt, const_cast<AVStream*>(vs), nullptr);
        item.fps = rate.num > 0 ? av_q2d(rate) : 0.0;
    }
    return true;
}

bool ffmpegDecodeAudio(const std::filesystem::path& file, std::vector<float>& stereo, std::string* error) {
    stereo.clear();
    Input in;
    if (!in.open(file, error)) return false;
    const int index = av_find_best_stream(in.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (index < 0) {
        if (error) *error = "This file has no audio.";
        return false;
    }
    AVStream* stream = in.fmt->streams[index];
    Decoder dec;
    if (!dec.open(stream)) {
        if (error) *error = "This audio codec is not supported.";
        return false;
    }
    // Everything becomes interleaved stereo float at kAudioRate.
    SwrContext* swr = nullptr;
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate, &dec.ctx->ch_layout, dec.ctx->sample_fmt,
                            dec.ctx->sample_rate, 0, nullptr) < 0 ||
        swr_init(swr) < 0) {
        swr_free(&swr);
        if (error) *error = "This audio could not be converted.";
        return false;
    }
    if (stream->duration > 0)
        stereo.reserve(static_cast<size_t>(stream->duration * av_q2d(stream->time_base) * kAudioRate * 2 + 4096));
    // Leading silence when the track starts after the container's time 0 (keeps sync with the video).
    if (stream->start_time != AV_NOPTS_VALUE && stream->start_time > 0) {
        const double lead = stream->start_time * av_q2d(stream->time_base);
        if (lead < 10.0) stereo.assign(static_cast<size_t>(lead * kAudioRate) * 2, 0.0f);
    }

    Frame frame;
    Packet pkt;
    std::vector<float> chunk;
    auto convert = [&](const AVFrame* f) {
        const int maxOut = swr_get_out_samples(swr, f ? f->nb_samples : 0);
        if (maxOut <= 0) return;
        chunk.resize(static_cast<size_t>(maxOut) * 2);
        uint8_t* out = reinterpret_cast<uint8_t*>(chunk.data());
        const int n = swr_convert(swr, &out, maxOut, f ? const_cast<const uint8_t**>(f->extended_data) : nullptr,
                                  f ? f->nb_samples : 0);
        if (n > 0) stereo.insert(stereo.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n) * 2);
    };
    bool eof = false;
    for (;;) {
        const int r = avcodec_receive_frame(dec.ctx, frame.f);
        if (r == 0) {
            convert(frame.f);
            av_frame_unref(frame.f);
            continue;
        }
        if (r == AVERROR_EOF) break;
        if (eof) {
            avcodec_send_packet(dec.ctx, nullptr);
            continue;
        }
        if (av_read_frame(in.fmt, pkt.p) < 0) {
            eof = true;
            avcodec_send_packet(dec.ctx, nullptr);
            continue;
        }
        if (pkt.p->stream_index == index) avcodec_send_packet(dec.ctx, pkt.p);
        av_packet_unref(pkt.p);
    }
    convert(nullptr); // what the resampler still holds
    swr_free(&swr);
    if (stereo.empty()) {
        if (error) *error = "No audio could be decoded from this file.";
        return false;
    }
    return true;
}

void setHardwareDecoding(bool on) { g_hardware = on; }
bool hardwareDecoding() { return g_hardware; }

std::string hardwareDecoderName() {
    if (!g_hardware || !hardwareDevice()) return {};
    const char* name = av_hwdevice_get_type_name(kHardwareType);
    return name ? name : "";
}

std::unique_ptr<VideoReader> VideoReader::open(const std::filesystem::path& file, std::string* error) {
    auto reader = std::make_unique<FfVideoReader>();
    if (!reader->init(file, error)) return nullptr;
    return reader;
}

// --- proxies -------------------------------------------------------------------------

namespace {

std::mutex g_proxyMutex;
std::filesystem::path g_proxyFolder;

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// Owning encoder / output wrappers for makeProxy.
struct Output {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* enc = nullptr;
    SwsContext* sws = nullptr;
    ~Output() {
        sws_freeContext(sws);
        avcodec_free_context(&enc);
        if (fmt && fmt->pb) avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }
};

} // namespace

void setProxyFolder(const std::filesystem::path& folder) {
    std::lock_guard lock(g_proxyMutex);
    g_proxyFolder = folder;
}

std::filesystem::path proxyFolder() {
    std::lock_guard lock(g_proxyMutex);
    return g_proxyFolder;
}

std::filesystem::path proxyPath(const MediaItem& item) {
    const std::filesystem::path folder = proxyFolder();
    if (folder.empty() || item.path.empty()) return {};
    std::error_code ec;
    const std::filesystem::path source = pathOf(item.path);
    const auto size = std::filesystem::file_size(source, ec);
    const auto stamp = std::filesystem::last_write_time(source, ec);
    const std::string key = item.path + "|" + std::to_string(size) + "|" + std::to_string(static_cast<long long>(stamp.time_since_epoch().count())) /* libc++: a 128-bit count */;
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.mkv", static_cast<unsigned long long>(fnv1a(key)));
    return folder / name;
}

bool proxyWanted(const MediaItem& item) {
    return item.kind == MediaKind::Video && item.hasVideo && static_cast<int64_t>(item.width) * item.height > 1280 * 720;
}

bool makeProxy(const MediaItem& item, std::atomic<float>* progress, const std::atomic_bool* cancel, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const std::filesystem::path target = proxyPath(item);
    if (target.empty()) return fail("There is no folder for proxies.");
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec); // the app's own data folder
    std::filesystem::path part = target;
    part += ".part";

    Input in;
    if (!in.open(pathOf(item.path), error)) return false;
    const int index = av_find_best_stream(in.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0 || !isRealVideo(in.fmt->streams[index])) return fail("This file has no video.");
    AVStream* stream = in.fmt->streams[index];
    Decoder dec;
    if (!dec.open(stream, g_hardware)) return fail("This video's codec is not supported.");
    const double origin = stream->start_time != AV_NOPTS_VALUE ? stream->start_time * av_q2d(stream->time_base) : 0.0;
    const double duration = std::max(0.01, streamDuration(in.fmt, stream));
    // At most 540 lines high (a quarter of 4K, half of HD), even sizes.
    const int srcW = dec.ctx->width, srcH = dec.ctx->height;
    if (srcW <= 0 || srcH <= 0) return fail("This video has no size.");
    const int h = std::min(srcH, 540) & ~1;
    const int w = std::max(2, static_cast<int>(std::lround(static_cast<double>(srcW) * h / srcH)) & ~1);

    Output out;
    const std::string partName = utf8(part);
    if (avformat_alloc_output_context2(&out.fmt, nullptr, "matroska", partName.c_str()) < 0 || !out.fmt) return fail("The proxy file could not be started.");
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) return fail("This FFmpeg has no Motion JPEG encoder.");
    AVStream* os = avformat_new_stream(out.fmt, nullptr);
    out.enc = avcodec_alloc_context3(codec);
    if (!os || !out.enc) return fail("The proxy file could not be started.");
    out.enc->width = w;
    out.enc->height = h;
    out.enc->pix_fmt = AV_PIX_FMT_YUVJ420P;
    out.enc->color_range = AVCOL_RANGE_JPEG;
    out.enc->time_base = AVRational{1, 1000};
    out.enc->flags |= AV_CODEC_FLAG_QSCALE;
    out.enc->global_quality = FF_QP2LAMBDA * 4; // good enough to edit with, a fraction of the size
    if (out.fmt->oformat->flags & AVFMT_GLOBALHEADER) out.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(out.enc, codec, nullptr) < 0 || avcodec_parameters_from_context(os->codecpar, out.enc) < 0)
        return fail("The proxy encoder could not start.");
    os->time_base = out.enc->time_base;
    if (avio_open(&out.fmt->pb, partName.c_str(), AVIO_FLAG_WRITE) < 0) return fail("The proxy file could not be written.");
    if (avformat_write_header(out.fmt, nullptr) < 0) return fail("The proxy file could not be written.");

    Frame frame, soft, scaled;
    scaled.f->format = AV_PIX_FMT_YUVJ420P;
    scaled.f->width = w;
    scaled.f->height = h;
    if (av_frame_get_buffer(scaled.f, 0) < 0) return fail("Out of memory.");
    Packet pkt, encoded;
    int64_t lastPts = -1;
    bool ok = true;
    auto drain = [&](bool flush) {
        if (flush) avcodec_send_frame(out.enc, nullptr);
        while (avcodec_receive_packet(out.enc, encoded.p) == 0) {
            av_packet_rescale_ts(encoded.p, out.enc->time_base, os->time_base);
            encoded.p->stream_index = os->index;
            if (av_interleaved_write_frame(out.fmt, encoded.p) < 0) ok = false;
            av_packet_unref(encoded.p);
        }
    };
    auto encode = [&](const AVFrame* decoded) {
        const AVFrame* f = decoded;
        if (decoded->format == dec.hwFormat && dec.hwFormat != AV_PIX_FMT_NONE) {
            av_frame_unref(soft.f);
            if (av_hwframe_transfer_data(soft.f, decoded, 0) < 0) return;
            f = soft.f;
        }
        out.sws = sws_getCachedContext(out.sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), w, h, AV_PIX_FMT_YUVJ420P,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!out.sws || av_frame_make_writable(scaled.f) < 0) return;
        sws_scale(out.sws, f->data, f->linesize, 0, f->height, scaled.f->data, scaled.f->linesize);
        const int64_t ts = decoded->best_effort_timestamp;
        const double seconds = ts == AV_NOPTS_VALUE ? (lastPts + 1) / 1000.0 : ts * av_q2d(stream->time_base) - origin;
        // The proxy starts at 0 where the original's first frame is: the same times show the same frames.
        int64_t pts = static_cast<int64_t>(std::llround(seconds * 1000.0));
        if (pts <= lastPts) pts = lastPts + 1;
        lastPts = pts;
        scaled.f->pts = pts;
        scaled.f->quality = out.enc->global_quality;
        if (avcodec_send_frame(out.enc, scaled.f) >= 0) drain(false);
        if (progress) progress->store(static_cast<float>(std::clamp(seconds / duration, 0.0, 1.0)));
    };
    bool eof = false;
    for (;;) {
        if (cancel && cancel->load()) {
            ok = false;
            if (error) *error = "Cancelled.";
            break;
        }
        const int r = avcodec_receive_frame(dec.ctx, frame.f);
        if (r == 0) {
            encode(frame.f);
            av_frame_unref(frame.f);
            continue;
        }
        if (r == AVERROR_EOF) break;
        if (eof) {
            avcodec_send_packet(dec.ctx, nullptr);
            continue;
        }
        if (av_read_frame(in.fmt, pkt.p) < 0) {
            eof = true;
            avcodec_send_packet(dec.ctx, nullptr);
            continue;
        }
        if (pkt.p->stream_index == index) avcodec_send_packet(dec.ctx, pkt.p);
        av_packet_unref(pkt.p);
    }
    if (ok) {
        drain(true);
        if (av_write_trailer(out.fmt) < 0) ok = false;
    }
    avio_closep(&out.fmt->pb);
    if (!ok || lastPts < 0) {
        std::filesystem::remove(part, ec);
        return error && !error->empty() ? false : fail("The proxy could not be made.");
    }
    std::filesystem::rename(part, target, ec);
    if (ec) {
        std::filesystem::remove(part, ec);
        return fail("The proxy file could not be saved.");
    }
    if (progress) progress->store(1.0f);
    return true;
}

} // namespace bemotion::media
