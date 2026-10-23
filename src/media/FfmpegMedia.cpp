// Video and audio files through FFmpeg's libraries (libavformat / libavcodec /
// libswscale / libswresample): probing, whole-track audio decoding and
// random-access video frames, on every platform.

#include "media/MediaIO.h"

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

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

struct Decoder {
    AVCodecContext* ctx = nullptr;
    ~Decoder() { avcodec_free_context(&ctx); }
    bool open(const AVStream* stream) {
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) return false;
        ctx = avcodec_alloc_context3(codec);
        if (!ctx || avcodec_parameters_to_context(ctx, stream->codecpar) < 0) return false;
        ctx->thread_count = 0; // as many threads as useful
        return avcodec_open2(ctx, codec, nullptr) >= 0;
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
        if (!dec_.open(stream_)) {
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

    bool frameAt(double seconds, std::vector<uint8_t>& rgba) override {
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
            if (pkt.p->stream_index == index_) avcodec_send_packet(dec_.ctx, pkt.p);
            av_packet_unref(pkt.p);
        }
        if (!have_) return false;
        rgba = rgba_;
        return true;
    }

private:
    void store(const AVFrame* f, double t) {
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

std::unique_ptr<VideoReader> VideoReader::open(const std::filesystem::path& file, std::string* error) {
    auto reader = std::make_unique<FfVideoReader>();
    if (!reader->init(file, error)) return nullptr;
    return reader;
}

} // namespace bemotion::media
