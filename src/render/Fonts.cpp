#include "render/Fonts.h"

#include "app/Platform.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace bemotion {

namespace {

constexpr const char* kFiles[] = {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"};
ImFont* g_weights[static_cast<int>(FontWeight::Count)] = {};

// System faces, filled by the scan thread.
std::mutex g_mutex;
std::vector<FontFace> g_faces;
std::atomic<bool> g_ready{false};

// Faces loaded into the atlas (nullptr = failed) and those waiting to be.
std::map<std::string, ImFont*> g_loaded;
std::vector<FontFace> g_pending;
std::set<std::string> g_requested;

std::string key(const std::string& family, const std::string& style) { return family + "|" + style; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Inter is bundled: map a style name onto its weights.
FontWeight interWeight(const std::string& style) {
    const std::string s = lower(style);
    if (s.find("semibold") != std::string::npos || s.find("semi bold") != std::string::npos) return FontWeight::SemiBold;
    if (s.find("bold") != std::string::npos || s.find("black") != std::string::npos) return FontWeight::Bold;
    if (s.find("medium") != std::string::npos) return FontWeight::Medium;
    return FontWeight::Regular;
}

std::vector<std::filesystem::path> fontDirectories() {
    std::vector<std::filesystem::path> dirs;
#ifdef _WIN32
    if (const char* windir = std::getenv("WINDIR")) dirs.push_back(std::filesystem::path(windir) / "Fonts");
    if (const char* local = std::getenv("LOCALAPPDATA"))
        dirs.push_back(std::filesystem::path(local) / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
    dirs = {"/System/Library/Fonts", "/Library/Fonts"};
    if (const char* home = std::getenv("HOME")) dirs.push_back(std::filesystem::path(home) / "Library/Fonts");
#else
    dirs = {"/usr/share/fonts", "/usr/local/share/fonts"};
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(std::filesystem::path(home) / ".local/share/fonts");
        dirs.push_back(std::filesystem::path(home) / ".fonts");
    }
#endif
    return dirs;
}

// Scans every font file, reusing cached results for unchanged files.
void scanSystemFonts(const std::filesystem::path& cacheFile) {
    using nlohmann::json;
    json cache;
    if (std::ifstream in(cacheFile, std::ios::binary); in) try {
            cache = json::parse(in);
        } catch (...) {
        }
    std::map<std::string, json> cached;
    if (cache.is_object())
        for (const auto& f : cache.value("files", json::array())) cached[f.value("path", std::string())] = f;

    std::vector<FontFace> faces;
    json files = json::array();
    std::error_code ec;
    for (const auto& dir : fontDirectories()) {
        for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::end(it);
             it.increment(ec)) {
            const auto& path = it->path();
            const std::string ext = lower(path.extension().string());
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            const std::string p = platform::toUtf8(path);
            const auto size = static_cast<long long>(it->file_size(ec));
            const auto mtime = static_cast<long long>(std::filesystem::last_write_time(path, ec).time_since_epoch().count());
            std::vector<FontFace> fileFaces;
            if (auto c = cached.find(p); c != cached.end() && c->second.value("size", -1LL) == size &&
                                         c->second.value("mtime", -1LL) == mtime) {
                for (const auto& fj : c->second.value("faces", json::array()))
                    fileFaces.push_back({fj.value("family", std::string()), fj.value("style", std::string()), path,
                                         fj.value("index", 0), fj.value("weight", 400), fj.value("italic", false)});
            } else {
                fileFaces = scanFontFile(path);
            }
            json fj = json::array();
            for (const auto& f : fileFaces)
                fj.push_back({{"family", f.family}, {"style", f.style}, {"index", f.index}, {"weight", f.weight},
                              {"italic", f.italic}});
            files.push_back({{"path", p}, {"size", size}, {"mtime", mtime}, {"faces", fj}});
            faces.insert(faces.end(), fileFaces.begin(), fileFaces.end());
        }
        ec.clear();
    }
    if (std::ofstream out(cacheFile, std::ios::binary | std::ios::trunc); out) out << json{{"files", files}}.dump();
    {
        std::lock_guard lock(g_mutex);
        g_faces = std::move(faces);
    }
    g_ready = true;
}

const FontFace* findFace(const std::string& family, const std::string& style) {
    const FontFace* fallback = nullptr;
    for (const auto& f : g_faces) {
        if (f.family != family) continue;
        if (f.style == style) return &f;
        if (!fallback || (!f.italic && std::abs(f.weight - 400) < std::abs(fallback->weight - 400))) fallback = &f;
    }
    return fallback;
}

} // namespace

void Fonts::load(const std::filesystem::path& assetsDir) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    for (int i = 0; i < static_cast<int>(FontWeight::Count); ++i) {
        const auto file = assetsDir / "fonts" / kFiles[i];
        std::error_code ec;
        ImFont* font = nullptr;
        if (std::filesystem::exists(file, ec))
            font = io.Fonts->AddFontFromFileTTF(platform::toUtf8(file).c_str(), kUiSize, &cfg);
        g_weights[i] = font;
    }
    // Fall back to the built-in font if the assets are missing.
    ImFont* fallback = g_weights[0] ? g_weights[0] : io.Fonts->AddFontDefault();
    for (auto& f : g_weights)
        if (!f) f = fallback;
    io.FontDefault = g_weights[0];
}

void Fonts::startSystemScan(const std::filesystem::path& cacheFile) {
    std::thread([cacheFile] {
        try {
            scanSystemFonts(cacheFile);
        } catch (...) {
            g_ready = true; // a broken font or cache must never take the app down
        }
    }).detach();
}

bool Fonts::systemScanReady() { return g_ready; }

void Fonts::processPending() {
    if (g_pending.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    for (const FontFace& face : g_pending) {
        ImFontConfig cfg;
        cfg.FontNo = face.index;
        cfg.OversampleH = 2;
        std::error_code ec;
        ImFont* font = std::filesystem::exists(face.path, ec)
                           ? io.Fonts->AddFontFromFileTTF(platform::toUtf8(face.path).c_str(), kUiSize, &cfg)
                           : nullptr;
        g_loaded[key(face.family, face.style)] = font;
    }
    g_pending.clear();
}

ImFont* Fonts::ui() { return g_weights[static_cast<int>(FontWeight::Regular)]; }
ImFont* Fonts::uiMedium() { return g_weights[static_cast<int>(FontWeight::Medium)]; }
ImFont* Fonts::uiBold() { return g_weights[static_cast<int>(FontWeight::SemiBold)]; }

ImFont* Fonts::forWeight(FontWeight weight) {
    const int i = static_cast<int>(weight);
    return (i >= 0 && i < static_cast<int>(FontWeight::Count)) ? g_weights[i] : g_weights[0];
}

ImFont* Fonts::forText(const TextProps& text) {
    ImFont* fallback = forWeight(interWeight(text.fontStyle));
    if (text.fontFamily.empty() || text.fontFamily == kDefaultFamily || !g_ready) return fallback;
    const std::string k = key(text.fontFamily, text.fontStyle);
    if (auto it = g_loaded.find(k); it != g_loaded.end()) return it->second ? it->second : fallback;
    if (!g_requested.count(k)) { // load before the next frame
        g_requested.insert(k);
        std::lock_guard lock(g_mutex);
        if (const FontFace* face = findFace(text.fontFamily, text.fontStyle)) {
            FontFace f = *face;
            f.family = text.fontFamily;
            f.style = text.fontStyle; // remember under the requested name
            g_pending.push_back(std::move(f));
        } else {
            g_loaded[k] = nullptr; // not installed here
        }
    }
    return fallback;
}

std::vector<std::string> Fonts::families() {
    std::vector<std::string> out;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& f : g_faces) out.push_back(f.family);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    std::erase(out, std::string(kDefaultFamily));
    out.insert(out.begin(), kDefaultFamily);
    return out;
}

std::vector<std::string> Fonts::stylesOf(const std::string& family) {
    if (family == kDefaultFamily) return {"Regular", "Medium", "SemiBold", "Bold"};
    std::vector<FontFace> faces;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& f : g_faces)
            if (f.family == family) faces.push_back(f);
    }
    std::sort(faces.begin(), faces.end(), [](const FontFace& a, const FontFace& b) {
        return a.italic != b.italic ? !a.italic : a.weight < b.weight;
    });
    std::vector<std::string> out;
    for (const auto& f : faces)
        if (std::find(out.begin(), out.end(), f.style) == out.end()) out.push_back(f.style);
    return out;
}

bool Fonts::hasFamily(const std::string& family) {
    if (family == kDefaultFamily) return true;
    std::lock_guard lock(g_mutex);
    return std::any_of(g_faces.begin(), g_faces.end(), [&](const FontFace& f) { return f.family == family; });
}

bool Fonts::ensureLoaded(const Composition& comp) {
    bool ready = true;
    for (const Layer& l : comp.layers) {
        if (l.type != LayerType::Text || l.text.fontFamily.empty() || l.text.fontFamily == kDefaultFamily) continue;
        if (!g_ready) return false; // the face list is not known yet
        forText(l.text);            // queues it if needed
        ready &= g_loaded.count(key(l.text.fontFamily, l.text.fontStyle)) > 0;
    }
    return ready;
}

} // namespace bemotion
