// MP4 (H.264), MOV (ProRes), WebM (VP9) and GIF through FFmpeg's libraries,
// in-process: frames are converted with libswscale (GIF goes through a
// palettegen / paletteuse filter graph), audio is resampled with
// libswresample, and libavformat writes the file.
//
// H.264 uses the best encoder the linked FFmpeg has: libx264 when present
// (GPL builds), otherwise OpenH264 or the system / hardware encoders
// (Media Foundation, VideoToolbox, NVENC, Quick Sync, AMF), so LGPL builds
// export MP4 too.

#include "export/Export.h"

#include "app/Platform.h"

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

namespace bemotion::exporting {

namespace {

// FFmpeg prints codec details to stderr by default: only errors are worth seeing.
const bool kQuietLogs = (av_log_set_level(AV_LOG_ERROR), true);


std::string avError(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return buf;
}

std::vector<const char*> videoEncoders(Format f) {
    switch (f) {
    case Format::Mp4:
        return {"libx264", "libopenh264", "h264_mf", "h264_videotoolbox", "h264_nvenc", "h264_qsv", "h264_amf", "mpeg4"};
    case Format::MovProRes: return {"prores_ks", "prores"};
    case Format::WebM: return {"libvpx-vp9", "libvpx"};
    case Format::Gif: return {"gif"};
    default: return {};
    }
}

std::vector<const char*> audioEncoders(Format f) {
    switch (f) {
    case Format::Mp4: return {"aac", "aac_mf"};
    case Format::MovProRes: return {"pcm_s16le"};
    case Format::WebM: return {"libopus", "opus", "libvorbis"};
    default: return {};
    }
}

// The pixel format to encode in: the first of `wanted` the encoder supports, else its first one.
AVPixelFormat pickPixelFormat(const AVCodecContext* ctx, const AVCodec* codec, std::initializer_list<AVPixelFormat> wanted) {
    const void* list = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &list, &count) < 0 || !list || count == 0)
        return *wanted.begin();
    const auto* fmts = static_cast<const AVPixelFormat*>(list);
    for (AVPixelFormat w : wanted)
        for (int i = 0; i < count; ++i)
            if (fmts[i] == w) return w;
    return fmts[0];
}

AVSampleFormat pickSampleFormat(const AVCodecContext* ctx, const AVCodec* codec) {
    const void* list = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &list, &count) < 0 || !list || count == 0)
        return AV_SAMPLE_FMT_FLTP;
    const auto* fmts = static_cast<const AVSampleFormat*>(list);
    for (AVSampleFormat w : {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16})
        for (int i = 0; i < count; ++i)
            if (fmts[i] == w) return w;
    return fmts[0];
}

class FfmpegEncoder final : public Encoder {
public:
    ~FfmpegEncoder() override {
        std::string ignored;
        if (oc_) finish(ignored);
        cleanup();
    }

    bool open(const EncoderParams& p, std::string& error) override {
        params_ = p;
        const std::string path = platform::toUtf8(p.output);
        int r = avformat_alloc_output_context2(&oc_, nullptr, p.format == Format::Gif ? "gif" : nullptr, path.c_str());
        if (r < 0 || !oc_) {
            error = "Could not create the file (" + avError(r) + ").";
            return false;
        }
        if (!openVideo(error)) return false;
        if (p.audio && !p.audio->empty() && !openAudio(error)) return false;
        if (!(oc_->oformat->flags & AVFMT_NOFILE)) {
            r = avio_open(&oc_->pb, path.c_str(), AVIO_FLAG_WRITE);
            if (r < 0) {
                error = "Could not write " + path + " (" + avError(r) + ").";
                return false;
            }
        }
        AVDictionary* opts = nullptr;
        if (p.format == Format::Mp4 || p.format == Format::MovProRes) av_dict_set(&opts, "movflags", "+faststart", 0);
        if (p.format == Format::Gif) av_dict_set(&opts, "loop", "0", 0);
        r = avformat_write_header(oc_, &opts);
        av_dict_free(&opts);
        if (r < 0) {
            error = "Could not start the file (" + avError(r) + ").";
            return false;
        }
        headerWritten_ = true;
        return true;
    }

    bool write(const uint8_t* rgba, std::string& error) override {
        AVFrame* frame = videoFrame_;
        if (av_frame_make_writable(frame) < 0) {
            error = "Out of memory.";
            return false;
        }
        if (graph_) { // GIF: RGBA goes into the palette filter graph as is
            AVFrame* in = av_frame_alloc();
            in->format = AV_PIX_FMT_RGBA;
            in->width = params_.width;
            in->height = params_.height;
            av_frame_get_buffer(in, 0);
            for (int y = 0; y < params_.height; ++y)
                std::memcpy(in->data[0] + y * in->linesize[0], rgba + static_cast<size_t>(y) * params_.width * 4,
                            static_cast<size_t>(params_.width) * 4);
            in->pts = frameIndex_;
            const int r = av_buffersrc_add_frame(src_, in);
            av_frame_free(&in);
            if (r < 0 || !pullFiltered(error)) return fail(error, "Could not reduce the frame to a palette.");
        } else {
            const uint8_t* src[4] = {rgba, nullptr, nullptr, nullptr};
            const int stride[4] = {params_.width * 4, 0, 0, 0};
            sws_scale(sws_, src, stride, 0, params_.height, frame->data, frame->linesize);
            frame->pts = frameIndex_;
            if (!encode(video_, videoStream_, frame, error)) return false;
        }
        ++frameIndex_;
        // The audio belonging up to the end of this frame.
        if (audio_) {
            const double t = static_cast<double>(frameIndex_) / params_.fps;
            if (!writeAudio(static_cast<size_t>(t * kExportAudioRate), error)) return false;
        }
        return true;
    }

    bool finish(std::string& error) override {
        if (!oc_) return true;
        bool ok = true;
        if (headerWritten_) {
            if (graph_) {
                (void)av_buffersrc_add_frame(src_, nullptr); // end of stream: the palette is built now
                ok = pullFiltered(error);
            }
            if (ok && audio_) ok = writeAudio(SIZE_MAX, error) && encode(audio_, audioStream_, nullptr, error);
            if (ok) ok = encode(video_, videoStream_, nullptr, error);
            const int r = av_write_trailer(oc_);
            if (ok && r < 0) {
                error = "Could not finish the file (" + avError(r) + ").";
                ok = false;
            }
        }
        cleanup();
        return ok;
    }

private:
    bool fail(std::string& error, const char* message) {
        if (error.empty()) error = message;
        return false;
    }

    bool openVideo(std::string& error) {
        const EncoderParams& p = params_;
        const AVRational rate = av_d2q(p.fps, 100000);
        std::string tried;
        for (const char* name : videoEncoders(p.format)) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name);
            if (!codec) continue;
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            ctx->width = p.width;
            ctx->height = p.height;
            ctx->time_base = av_inv_q(rate);
            ctx->framerate = rate;
            ctx->gop_size = std::max(1, static_cast<int>(p.fps * 2));
            if (oc_->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            configure(ctx, codec, name);
            const int r = avcodec_open2(ctx, codec, nullptr);
            if (r < 0) { // e.g. a hardware encoder without the hardware: try the next one
                tried += std::string(tried.empty() ? "" : ", ") + name;
                avcodec_free_context(&ctx);
                continue;
            }
            video_ = ctx;
            break;
        }
        if (!video_) {
            error = "No encoder for this format is available" + (tried.empty() ? std::string(".") : " (tried " + tried + ").");
            return false;
        }
        videoStream_ = avformat_new_stream(oc_, nullptr);
        avcodec_parameters_from_context(videoStream_->codecpar, video_);
        videoStream_->time_base = video_->time_base;
        videoStream_->avg_frame_rate = video_->framerate;

        videoFrame_ = av_frame_alloc();
        videoFrame_->format = video_->pix_fmt;
        videoFrame_->width = p.width;
        videoFrame_->height = p.height;
        av_frame_get_buffer(videoFrame_, 0);
        if (p.format == Format::Gif) return openPaletteGraph(error);
        sws_ = sws_getContext(p.width, p.height, AV_PIX_FMT_RGBA, p.width, p.height, video_->pix_fmt,
                              SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr, nullptr, nullptr);
        if (!sws_) {
            error = "Could not convert frames for this encoder.";
            return false;
        }
        // BT.709 for HD video.
        const int* coefs = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws_, coefs, 1, coefs, 0, 0, 1 << 16, 1 << 16);
        video_->colorspace = AVCOL_SPC_BT709;
        return true;
    }

    void configure(AVCodecContext* ctx, const AVCodec* codec, const std::string& name) {
        const EncoderParams& p = params_;
        const int q = static_cast<int>(p.quality); // 0 low, 1 medium, 2 high
        const double bpp = q == 0 ? 0.06 : q == 1 ? 0.12 : 0.22; // bits per pixel per frame, for bitrate encoders
        const int64_t bitrate = static_cast<int64_t>(std::clamp(bpp * p.width * p.height * p.fps, 500e3, 120e6));
        if (name == "libx264") {
            ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            av_opt_set(ctx->priv_data, "preset", "medium", 0);
            av_opt_set(ctx->priv_data, "crf", q == 0 ? "26" : q == 1 ? "20" : "15", 0);
        } else if (name.rfind("h264", 0) == 0 || name == "libopenh264" || name == "mpeg4") {
            ctx->pix_fmt = pickPixelFormat(ctx, codec, {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12});
            ctx->bit_rate = bitrate;
            if (name == "h264_mf") av_opt_set(ctx->priv_data, "rate_control", "quality", 0);
        } else if (name.rfind("prores", 0) == 0) {
            ctx->pix_fmt = p.alpha ? AV_PIX_FMT_YUVA444P10LE : AV_PIX_FMT_YUV422P10LE;
            av_opt_set(ctx->priv_data, "profile", p.alpha ? "4444" : (q == 0 ? "lt" : q == 1 ? "standard" : "hq"), 0);
        } else if (name.rfind("libvpx", 0) == 0) {
            ctx->pix_fmt = p.alpha ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
            ctx->bit_rate = 0;
            av_opt_set(ctx->priv_data, "crf", q == 0 ? "40" : q == 1 ? "32" : "24", 0);
            av_opt_set(ctx->priv_data, "row-mt", "1", 0);
            av_opt_set(ctx->priv_data, "deadline", "good", 0);
            av_opt_set(ctx->priv_data, "cpu-used", "2", 0);
        } else if (name == "gif") {
            ctx->pix_fmt = AV_PIX_FMT_PAL8;
        }
    }

    // GIF: [rgba] split [a][b]; [a] palettegen [p]; [b][p] paletteuse -> pal8.
    bool openPaletteGraph(std::string& error) {
        graph_ = avfilter_graph_alloc();
        char args[256];
        std::snprintf(args, sizeof args, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1", params_.width,
                      params_.height, AV_PIX_FMT_RGBA, video_->time_base.num, video_->time_base.den);
        if (avfilter_graph_create_filter(&src_, avfilter_get_by_name("buffer"), "in", args, nullptr, graph_) < 0 ||
            !(sink_ = avfilter_graph_alloc_filter(graph_, avfilter_get_by_name("buffersink"), "out")) ||
            avfilter_init_str(sink_, nullptr) < 0) {
            error = "Could not set up the GIF palette.";
            return false;
        }
        const std::string spec = std::string("split[a][b];[a]palettegen=stats_mode=diff") +
                                 (params_.alpha ? ":reserve_transparent=1" : "") +
                                 "[p];[b][p]paletteuse=dither=sierra2_4a" + (params_.alpha ? ":alpha_threshold=128" : "");
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = src_;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_;
        const int r = avfilter_graph_parse_ptr(graph_, spec.c_str(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        if (r < 0 || avfilter_graph_config(graph_, nullptr) < 0) {
            error = "Could not set up the GIF palette.";
            return false;
        }
        return true;
    }

    bool pullFiltered(std::string& error) {
        AVFrame* out = av_frame_alloc();
        for (;;) {
            const int r = av_buffersink_get_frame(sink_, out);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) {
                av_frame_free(&out);
                error = "The GIF palette failed (" + avError(r) + ").";
                return false;
            }
            const bool ok = encode(video_, videoStream_, out, error);
            av_frame_unref(out);
            if (!ok) {
                av_frame_free(&out);
                return false;
            }
        }
        av_frame_free(&out);
        return true;
    }

    bool openAudio(std::string& error) {
        for (const char* name : audioEncoders(params_.format)) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name);
            if (!codec) continue;
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            ctx->sample_rate = kExportAudioRate;
            ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
            ctx->sample_fmt = pickSampleFormat(ctx, codec);
            ctx->time_base = {1, kExportAudioRate};
            ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL; // FFmpeg's own Opus encoder
            if (std::strcmp(name, "pcm_s16le") != 0) ctx->bit_rate = std::strstr(name, "opus") ? 160000 : 192000;
            if (oc_->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(ctx, codec, nullptr) < 0) {
                avcodec_free_context(&ctx);
                continue;
            }
            audio_ = ctx;
            break;
        }
        if (!audio_) {
            error = "No audio encoder for this format is available.";
            return false;
        }
        audioStream_ = avformat_new_stream(oc_, nullptr);
        avcodec_parameters_from_context(audioStream_->codecpar, audio_);
        audioStream_->time_base = audio_->time_base;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&swr_, &audio_->ch_layout, audio_->sample_fmt, kExportAudioRate, &stereo, AV_SAMPLE_FMT_FLT,
                                kExportAudioRate, 0, nullptr) < 0 ||
            swr_init(swr_) < 0) {
            error = "Could not convert the audio for this encoder.";
            return false;
        }
        audioFrameSize_ = audio_->frame_size > 0 ? audio_->frame_size : 1024; // PCM takes any size
        return true;
    }

    // Encodes the mixed audio up to sample `until` in encoder-sized frames.
    bool writeAudio(size_t until, std::string& error) {
        const std::vector<float>& s = *params_.audio;
        const size_t total = s.size() / 2;
        const bool flushAll = until == SIZE_MAX;
        until = std::min(until, total);
        while (audioDone_ < until) {
            const size_t n = std::min<size_t>(audioFrameSize_, until - audioDone_);
            if (n < static_cast<size_t>(audioFrameSize_) && !flushAll && !(audio_->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
                break; // wait for a full frame (the last one may be short)
            AVFrame* f = av_frame_alloc();
            f->nb_samples = static_cast<int>(n);
            f->format = audio_->sample_fmt;
            f->sample_rate = kExportAudioRate;
            av_channel_layout_copy(&f->ch_layout, &audio_->ch_layout);
            av_frame_get_buffer(f, 0);
            const uint8_t* in = reinterpret_cast<const uint8_t*>(s.data() + audioDone_ * 2);
            swr_convert(swr_, f->data, f->nb_samples, &in, f->nb_samples);
            f->pts = static_cast<int64_t>(audioDone_);
            const bool ok = encode(audio_, audioStream_, f, error);
            av_frame_free(&f);
            if (!ok) return false;
            audioDone_ += n;
        }
        return true;
    }

    // Sends a frame (nullptr: flush) and writes every packet that comes out.
    bool encode(AVCodecContext* ctx, AVStream* stream, AVFrame* frame, std::string& error) {
        int r = avcodec_send_frame(ctx, frame);
        if (r < 0 && r != AVERROR_EOF) {
            error = "Encoding failed (" + avError(r) + ").";
            return false;
        }
        AVPacket* pkt = av_packet_alloc();
        for (;;) {
            r = avcodec_receive_packet(ctx, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) {
                av_packet_free(&pkt);
                error = "Encoding failed (" + avError(r) + ").";
                return false;
            }
            av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            r = av_interleaved_write_frame(oc_, pkt);
            if (r < 0) {
                av_packet_free(&pkt);
                error = "Writing the file failed (" + avError(r) + ").";
                return false;
            }
        }
        av_packet_free(&pkt);
        return true;
    }

    void cleanup() {
        avfilter_graph_free(&graph_);
        sws_freeContext(sws_);
        sws_ = nullptr;
        swr_free(&swr_);
        av_frame_free(&videoFrame_);
        avcodec_free_context(&video_);
        avcodec_free_context(&audio_);
        if (oc_) {
            if (oc_->pb && !(oc_->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc_->pb);
            avformat_free_context(oc_);
            oc_ = nullptr;
        }
    }

    EncoderParams params_{};
    AVFormatContext* oc_ = nullptr;
    AVCodecContext* video_ = nullptr;
    AVCodecContext* audio_ = nullptr;
    AVStream* videoStream_ = nullptr;
    AVStream* audioStream_ = nullptr;
    AVFrame* videoFrame_ = nullptr;
    SwsContext* sws_ = nullptr;
    SwrContext* swr_ = nullptr;
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* src_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    int64_t frameIndex_ = 0;
    size_t audioDone_ = 0;
    int audioFrameSize_ = 1024;
    bool headerWritten_ = false;
};

} // namespace

bool ffmpegCanEncode(Format f) {
    for (const char* name : videoEncoders(f))
        if (avcodec_find_encoder_by_name(name)) return true;
    return false;
}

std::unique_ptr<Encoder> makeFfmpegEncoder() { return std::make_unique<FfmpegEncoder>(); }

} // namespace bemotion::exporting
