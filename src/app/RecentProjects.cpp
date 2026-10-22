#include "app/RecentProjects.h"

#include "app/Platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>

namespace bemotion {

namespace {
std::filesystem::path storePath() { return platform::appDataDir() / "recent.json"; }

int64_t nowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

void RecentProjects::load() {
    items_.clear();
    std::ifstream f(storePath(), std::ios::binary);
    if (!f) return;
    try {
        const auto j = nlohmann::json::parse(f);
        for (const auto& e : j.value("projects", nlohmann::json::array())) {
            RecentProject r;
            r.path = platform::fromUtf8(e.value("path", std::string()));
            r.name = e.value("name", std::string());
            r.lastOpened = e.value("last_opened", int64_t{0});
            std::error_code ec;
            if (!r.path.empty() && std::filesystem::exists(r.path, ec)) items_.push_back(std::move(r));
        }
    } catch (...) {
        items_.clear();
    }
}

void RecentProjects::save() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& r : items_)
        list.push_back({{"path", platform::toUtf8(r.path)}, {"name", r.name}, {"last_opened", r.lastOpened}});
    std::ofstream f(storePath(), std::ios::binary | std::ios::trunc);
    if (f) f << nlohmann::json{{"projects", list}}.dump(2);
}

void RecentProjects::touch(const std::filesystem::path& path, const std::string& name) {
    remove(path);
    items_.insert(items_.begin(), RecentProject{path, name, nowSeconds()});
    if (items_.size() > kMaxItems) items_.resize(kMaxItems);
    save();
}

void RecentProjects::remove(const std::filesystem::path& path) {
    std::erase_if(items_, [&](const RecentProject& r) { return r.path == path; });
    save();
}

} // namespace bemotion
