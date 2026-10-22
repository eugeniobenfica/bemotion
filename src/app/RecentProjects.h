#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bemotion {

struct RecentProject {
    std::filesystem::path path;
    std::string name;
    int64_t lastOpened = 0; // unix seconds
};

// Most-recently-used project list, persisted in the user's app data folder.
class RecentProjects {
public:
    void load();
    void save() const;

    void touch(const std::filesystem::path& path, const std::string& name);
    void remove(const std::filesystem::path& path);

    const std::vector<RecentProject>& items() const { return items_; }

private:
    static constexpr size_t kMaxItems = 40;
    std::vector<RecentProject> items_; // newest first
};

} // namespace bemotion
