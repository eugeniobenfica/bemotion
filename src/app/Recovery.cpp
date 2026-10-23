#include "app/Recovery.h"

#include "app/Platform.h"
#include "core/Serialization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace bemotion::recovery {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

fs::path folder() {
    const fs::path dir = platform::appDataDir() / "recovery";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// One file per project: named after a hash of its path (or of its name, never saved).
fs::path copyFor(const fs::path& original, const std::string& name) {
    const std::string key = original.empty() ? "name:" + name : platform::toUtf8(original);
    uint64_t h = 1469598103934665603ull; // FNV-1a: stable across runs (std::hash is not required to be)
    for (unsigned char c : key) h = (h ^ c) * 1099511628211ull;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return folder() / (std::string(buf) + kProjectExtension);
}

fs::path infoFor(const fs::path& copy) {
    fs::path info = copy;
    info += ".json";
    return info;
}

} // namespace

bool keep(const Project& project, const fs::path& original, std::string* error) {
    const fs::path copy = copyFor(original, project.name);
    if (!saveProject(project, copy, error)) return false;
    using namespace std::chrono;
    const json info = {{"original", platform::toUtf8(original)}, {"name", project.name},
                       {"saved_at", duration_cast<seconds>(system_clock::now().time_since_epoch()).count()}};
    std::ofstream f(infoFor(copy), std::ios::binary | std::ios::trunc);
    f << info.dump(2);
    return static_cast<bool>(f);
}

void discard(const fs::path& original, const std::string& name) {
    const fs::path copy = copyFor(original, name);
    std::error_code ec;
    fs::remove(copy, ec);
    fs::remove(infoFor(copy), ec);
}

void discard(const Entry& entry) {
    std::error_code ec;
    fs::remove(entry.copy, ec);
    fs::remove(infoFor(entry.copy), ec);
}

std::vector<Entry> pending() {
    std::vector<Entry> out;
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(folder(), ec)) {
        if (file.path().extension() != kProjectExtension) continue;
        Entry e;
        e.copy = file.path();
        try {
            std::ifstream f(infoFor(e.copy), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            const json info = json::parse(ss.str());
            e.original = platform::fromUtf8(info.value("original", std::string()));
            e.name = info.value("name", std::string());
            e.savedAt = info.value("saved_at", int64_t{0});
        } catch (...) {
            e.name = platform::toUtf8(e.copy.stem());
        }
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.savedAt > b.savedAt; });
    return out;
}

} // namespace bemotion::recovery
