#pragma once

// Official plugins a release may leave out, to get when wanted: a catalog of each plugin and
// its files with their SHA-256 (assets/plugins/index.json in bemotion's repository, made by
// tools/plugin-index.py). The app fetches it from GitHub only when the user asks
// (app/Downloads), checks every file against it, then installs the plugin like any other.

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

struct CatalogFile {
    std::string path;   // relative to the plugin's folder, '/' separated
    std::string sha256; // lowercase hex
    int64_t size = 0;
};

struct CatalogPlugin {
    std::string id, name, version, author, description;
    int api = 1; // the plugin API it needs (PluginHost::kApiVersion)
    std::vector<std::string> permissions;
    std::vector<CatalogFile> files; // plugin.json among them
    int64_t size() const;
};

// Reads index.json. Plugins with a bad id, an unsafe path or no plugin.json are left out
// (`error` tells the first); false when the text is not a catalog at all.
bool parsePluginCatalog(const std::string& json, std::vector<CatalogPlugin>& out, std::string* error = nullptr);

// A path inside a plugin's folder: relative, '/' separated, no "..", no drive or root.
bool safeCatalogPath(const std::string& path);

// Versions compared part by part as numbers ("1.10.0" > "1.9"): <0, 0, >0.
int compareVersions(const std::string& a, const std::string& b);

} // namespace bemotion
