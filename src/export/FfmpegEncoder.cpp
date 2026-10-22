// Encodes by piping raw RGBA frames into an external ffmpeg process.

#include "export/Export.h"

#include "app/Platform.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bemotion::exporting {

namespace {

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string codecArguments(const EncoderParams& p) {
    const int q = static_cast<int>(p.quality); // 0 low, 1 medium, 2 high
    std::ostringstream a;
    switch (p.format) {
    case Format::Mp4:
        a << "-c:v libx264 -preset medium -crf " << (q == 0 ? 26 : q == 1 ? 20 : 15)
          << " -pix_fmt yuv420p -movflags +faststart";
        break;
    case Format::MovProRes:
        if (p.alpha) a << "-c:v prores_ks -profile:v 4 -pix_fmt yuva444p10le";
        else a << "-c:v prores_ks -profile:v " << (q == 0 ? 1 : q == 1 ? 2 : 3) << " -pix_fmt yuv422p10le";
        break;
    case Format::WebM:
        a << "-c:v libvpx-vp9 -b:v 0 -row-mt 1 -crf " << (q == 0 ? 40 : q == 1 ? 32 : 24)
          << (p.alpha ? " -pix_fmt yuva420p" : " -pix_fmt yuv420p");
        break;
    case Format::Gif:
        a << "-filter_complex \"[0:v]split[a][b];[a]palettegen=stats_mode=diff"
          << (p.alpha ? ":reserve_transparent=1" : "") << "[p];[b][p]paletteuse=dither=sierra2_4a\" -loop 0";
        break;
    default: break;
    }
    return a.str();
}

std::string readLog(const std::filesystem::path& log) {
    std::ifstream f(log, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.size() > 400) s = "..." + s.substr(s.size() - 400);
    return s;
}

class FfmpegEncoder final : public Encoder {
public:
    ~FfmpegEncoder() override {
        std::string ignored;
        finish(ignored);
    }

    bool open(const EncoderParams& params, std::string& error) override {
        const auto ffmpeg = findFfmpeg();
        if (!ffmpeg) {
            error = "FFmpeg was not found. Install it (https://ffmpeg.org) and make sure it is on the PATH.";
            return false;
        }
        frameBytes_ = static_cast<size_t>(params.width) * params.height * 4;
        std::error_code ec;
        log_ = std::filesystem::temp_directory_path(ec) / "bemotion-ffmpeg.log";

        std::ostringstream cmd;
        cmd << quote(platform::toUtf8(*ffmpeg)) << " -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgba -s "
            << params.width << "x" << params.height << " -framerate " << params.fps << " -i - ";
        if (params.audio) { // the mixed audio goes in as a WAV file
            wav_ = std::filesystem::temp_directory_path(ec) / "bemotion-export-audio.wav";
            if (!writeWav(wav_, *params.audio, kExportAudioRate, error)) return false;
            const char* codec = params.format == Format::MovProRes ? "pcm_s16le"
                                : params.format == Format::WebM    ? "libopus -b:a 160k"
                                                                   : "aac -b:a 192k";
            cmd << "-i " << quote(platform::toUtf8(wav_)) << " -map 0:v -map 1:a -c:a " << codec << " ";
        }
        cmd << codecArguments(params) << " " << quote(platform::toUtf8(params.output));
        return launch(cmd.str(), error);
    }

    bool write(const uint8_t* rgba, std::string& error) override {
        if (!writeAll(rgba, frameBytes_)) {
            std::string ignored;
            finish(ignored);
            error = "FFmpeg stopped unexpectedly: " + readLog(log_);
            return false;
        }
        return true;
    }

    bool finish(std::string& error) override {
        if (!running_) return true;
        running_ = false;
        const int code = wait();
        if (!wav_.empty()) {
            std::error_code ec;
            std::filesystem::remove(wav_, ec);
            wav_.clear();
        }
        if (code != 0) {
            error = "FFmpeg failed (code " + std::to_string(code) + "): " + readLog(log_);
            return false;
        }
        return true;
    }

private:
#ifdef _WIN32
    bool launch(const std::string& command, std::string& error) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE readEnd = nullptr;
        if (!CreatePipe(&readEnd, &stdin_, &sa, 1 << 20)) {
            error = "Could not create a pipe to FFmpeg.";
            return false;
        }
        SetHandleInformation(stdin_, HANDLE_FLAG_INHERIT, 0);
        HANDLE logHandle = CreateFileW(log_.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = readEnd;
        si.hStdOutput = logHandle;
        si.hStdError = logHandle;
        std::wstring cmd = platform::fromUtf8(command).wstring();
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                       &si, &pi);
        CloseHandle(readEnd);
        if (logHandle != INVALID_HANDLE_VALUE) CloseHandle(logHandle);
        if (!ok) {
            CloseHandle(stdin_);
            stdin_ = nullptr;
            error = "Could not start FFmpeg.";
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        running_ = true;
        return true;
    }

    bool writeAll(const uint8_t* data, size_t size) {
        while (size > 0) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1 << 24));
            if (!WriteFile(stdin_, data, chunk, &written, nullptr) || written == 0) return false;
            data += written;
            size -= written;
        }
        return true;
    }

    int wait() {
        if (stdin_) CloseHandle(stdin_);
        stdin_ = nullptr;
        WaitForSingleObject(process_, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process_, &code);
        CloseHandle(process_);
        process_ = nullptr;
        return static_cast<int>(code);
    }

    HANDLE stdin_ = nullptr;
    HANDLE process_ = nullptr;
#else
    bool launch(const std::string& command, std::string& error) {
        std::signal(SIGPIPE, SIG_IGN); // a crashed ffmpeg must not take the app down
        pipe_ = popen((command + " 2> " + quote(platform::toUtf8(log_))).c_str(), "w");
        if (!pipe_) {
            error = "Could not start FFmpeg.";
            return false;
        }
        running_ = true;
        return true;
    }

    bool writeAll(const uint8_t* data, size_t size) { return std::fwrite(data, 1, size, pipe_) == size; }

    int wait() {
        const int status = pclose(pipe_);
        pipe_ = nullptr;
        return status;
    }

    FILE* pipe_ = nullptr;
#endif

    size_t frameBytes_ = 0;
    std::filesystem::path log_;
    std::filesystem::path wav_; // temporary audio input
    bool running_ = false;
};

} // namespace

std::unique_ptr<Encoder> makeFfmpegEncoder() { return std::make_unique<FfmpegEncoder>(); }

} // namespace bemotion::exporting
