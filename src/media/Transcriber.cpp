#include "media/Transcriber.h"

#include "core/Data.h"
#include "media/MediaIO.h"

#ifdef BEMOTION_WHISPER
#include <whisper.h>
#endif

#ifdef _WIN32
#include <windows.h>
#elif defined(BEMOTION_WHISPER)
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cmath>
#include <thread>

namespace bemotion::media {

namespace {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

#ifdef BEMOTION_WHISPER
// whisper.cpp's compute backends (the CPU variants, Vulkan or Metal for the GPU) can be
// libraries ggml loads at run time (MSYS2, Homebrew): looked for next to the executable, and
// then where ggml itself lives. Built in (BEMOTION_FETCH_WHISPER), there is nothing to load.
// Without any, whisper would abort: false instead.
bool computeBackendsReady() {
#ifdef _WIN32
    static const bool ready = [] {
        HMODULE ggml = GetModuleHandleW(L"ggml-0.dll");
        if (!ggml) ggml = GetModuleHandleW(L"ggml.dll");
        if (!ggml) return true; // not a split build: nothing to load
        using Count = size_t (*)();
        using LoadFrom = void (*)(const char*);
        const auto count = reinterpret_cast<Count>(reinterpret_cast<void*>(GetProcAddress(ggml, "ggml_backend_dev_count")));
        const auto load = reinterpret_cast<LoadFrom>(reinterpret_cast<void*>(GetProcAddress(ggml, "ggml_backend_load_all_from_path")));
        if (!count) return true;
        if (count() == 0 && load) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(ggml, path, MAX_PATH);
            load(utf8(std::filesystem::path(path).parent_path()).c_str());
        }
        return count() > 0;
    }();
    return ready;
#else
    static const bool ready = [] {
        using Count = size_t (*)();
        using LoadAll = void (*)();
        const auto count = reinterpret_cast<Count>(dlsym(RTLD_DEFAULT, "ggml_backend_dev_count"));
        const auto load = reinterpret_cast<LoadAll>(dlsym(RTLD_DEFAULT, "ggml_backend_load_all"));
        if (!count) return true; // linked in: its backends are there
        if (count() == 0 && load) load();
        return count() > 0;
    }();
    return ready;
#endif
}

// 48 kHz stereo -> 16 kHz mono (what whisper hears), through a small low-pass so nothing
// above 8 kHz folds back as noise.
std::vector<float> toWhisperRate(const std::vector<float>& stereo, size_t first, size_t last) {
    static const std::vector<float> taps = [] {
        constexpr int kHalf = 12;
        std::vector<float> t;
        float sum = 0.0f;
        for (int i = -kHalf; i <= kHalf; ++i) {
            const float x = static_cast<float>(i) / 3.0f; // cutoff at 1/3 of the band (8 kHz)
            const float sinc = i == 0 ? 1.0f : std::sin(3.14159265f * x) / (3.14159265f * x);
            const float window = 0.54f + 0.46f * std::cos(3.14159265f * static_cast<float>(i) / (kHalf + 1));
            t.push_back(sinc * window);
            sum += t.back();
        }
        for (float& v : t) v /= sum;
        return t;
    }();
    const int half = static_cast<int>(taps.size() / 2);
    std::vector<float> out;
    out.reserve((last - first) / 3 + 1);
    for (size_t i = first; i < last; i += 3) {
        float acc = 0.0f;
        for (int k = -half; k <= half; ++k) {
            const long j = static_cast<long>(i) + k;
            if (j < static_cast<long>(first) || j >= static_cast<long>(last)) continue;
            acc += taps[k + half] * 0.5f * (stereo[j * 2] + stereo[j * 2 + 1]);
        }
        out.push_back(acc);
    }
    return out;
}
#endif

} // namespace

bool speechToTextAvailable() {
#ifdef BEMOTION_WHISPER
    return true;
#else
    return false;
#endif
}

bool findSpeechModels(const std::vector<std::filesystem::path>& folders, TranscribeOptions& options) {
    for (const auto& folder : folders) {
        std::error_code ec;
        if (folder.empty() || !std::filesystem::is_directory(folder, ec)) continue;
        std::filesystem::path model, vad;
        uintmax_t best = 0;
        for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
            const std::string name = utf8(e.path().filename());
            if (!e.is_regular_file(ec) || name.rfind("ggml-", 0) != 0 || e.path().extension() != ".bin") continue;
            if (name.find("silero") != std::string::npos || name.find("vad") != std::string::npos) {
                vad = e.path();
            } else if (const uintmax_t size = e.file_size(ec); size > best) { // the biggest is the most accurate
                best = size;
                model = e.path();
            }
        }
        if (!model.empty()) {
            options.model = model;
            options.vad = vad;
            return true;
        }
    }
    return false;
}

bool transcribe(const std::filesystem::path& file, double from, double to, const TranscribeOptions& options,
                std::vector<Caption>& out, const std::function<bool(float)>& progress, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
#ifndef BEMOTION_WHISPER
    (void)file, (void)from, (void)to, (void)options, (void)out, (void)progress;
    return fail("This build of bemotion has no speech recognition (whisper.cpp).");
#else
    if (options.model.empty()) return fail("The speech model is missing.");
    if (!computeBackendsReady()) return fail("Speech recognition found no processor to run on (ggml backends missing).");
    std::vector<float> stereo;
    if (!decodeAudio(file, stereo, error)) return false;
    const size_t frames = stereo.size() / 2;
    const size_t first = std::min(frames, static_cast<size_t>(std::max(0.0, from) * kAudioRate));
    const size_t last = to > from ? std::min(frames, static_cast<size_t>(to * kAudioRate)) : frames;
    if (last <= first + kAudioRate / 10) return fail("There is no sound to transcribe there.");
    const std::vector<float> pcm = toWhisperRate(stereo, first, last);
    stereo.clear();
    stereo.shrink_to_fit();

    static const bool quiet = (whisper_log_set([](ggml_log_level, const char*, void*) {}, nullptr), true); // no console noise
    (void)quiet;
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = options.gpu;
    cp.flash_attn = false; // flash attention gives garbage on some GPUs (AMD with Vulkan): off, a little slower
    whisper_context* ctx = whisper_init_from_file_with_params(utf8(options.model).c_str(), cp);
    if (!ctx) return fail("The speech model could not be loaded.");

    struct Callbacks {
        const std::function<bool(float)>* progress;
        bool stopped = false;
    } cb{&progress};
    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    p.language = options.language.empty() ? "auto" : options.language.c_str();
    p.translate = false;
    p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
    p.suppress_blank = true;
    p.greedy.best_of = 5; // a few candidates at each uncertain step: more accurate, still fast
    p.progress_callback = [](whisper_context*, whisper_state*, int percent, void* user) {
        auto* c = static_cast<Callbacks*>(user);
        if (*c->progress && !(*c->progress)(std::clamp(percent, 0, 100) * 0.01f)) c->stopped = true;
    };
    p.progress_callback_user_data = &cb;
    p.abort_callback = [](void* user) { return static_cast<Callbacks*>(user)->stopped; };
    p.abort_callback_user_data = &cb;
    const std::string vad = utf8(options.vad);
    if (!options.vad.empty()) { // skips silence and cuts at pauses: faster, no invented words in silence
        p.vad = true;
        p.vad_model_path = vad.c_str();
    }
    const int result = whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size()));
    out.clear();
    if (result == 0 && !cb.stopped) {
        const double offset = static_cast<double>(first) / kAudioRate;
        for (int i = 0, n = whisper_full_n_segments(ctx); i < n; ++i) {
            std::string text = whisper_full_get_segment_text(ctx, i);
            while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.erase(text.begin());
            while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.pop_back();
            if (text.empty()) continue;
            // Times in 1/100 s from the start of what was heard.
            out.push_back({offset + whisper_full_get_segment_t0(ctx, i) * 0.01, offset + whisper_full_get_segment_t1(ctx, i) * 0.01, text});
        }
    }
    whisper_free(ctx);
    if (cb.stopped) return fail("Stopped.");
    if (result != 0) return fail("Speech recognition failed.");
    for (size_t i = 1; i < out.size(); ++i) out[i - 1].end = std::min(out[i - 1].end, out[i].start); // one at a time
    std::erase_if(out, [](const Caption& c) { return c.end <= c.start; });
    splitLongCaptions(out, 84); // two short lines at most
    if (progress) progress(1.0f);
    return true;
#endif
}

} // namespace bemotion::media
