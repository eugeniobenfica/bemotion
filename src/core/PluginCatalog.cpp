#include "core/PluginCatalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace bemotion {

using json = nlohmann::json;

int64_t CatalogPlugin::size() const {
    int64_t total = 0;
    for (const CatalogFile& f : files) total += f.size;
    return total;
}

bool safeCatalogPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
    std::stringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/'))
        if (part.empty() || part == "." || part == "..") return false;
    return path.back() != '/';
}

int compareVersions(const std::string& a, const std::string& b) {
    std::stringstream sa(a), sb(b);
    std::string pa, pb;
    while (true) {
        const bool ha = static_cast<bool>(std::getline(sa, pa, '.')), hb = static_cast<bool>(std::getline(sb, pb, '.'));
        if (!ha && !hb) return 0;
        const long va = ha ? std::strtol(pa.c_str(), nullptr, 10) : 0, vb = hb ? std::strtol(pb.c_str(), nullptr, 10) : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
}

bool parsePluginCatalog(const std::string& text, std::vector<CatalogPlugin>& out, std::string* error) {
    out.clear();
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        if (error) *error = std::string("not a plugin catalog: ") + e.what();
        return false;
    }
    if (!j.is_object() || !j.contains("plugins") || !j["plugins"].is_array()) {
        if (error) *error = "not a plugin catalog";
        return false;
    }
    auto reject = [&](const std::string& why) {
        if (error && error->empty()) *error = why;
    };
    for (const json& p : j["plugins"]) {
        if (!p.is_object()) continue;
        CatalogPlugin c;
        c.id = p.value("id", std::string());
        if (c.id.empty() || c.id.find_first_of("/\\:. ") != std::string::npos) {
            reject("a plugin has a bad id");
            continue;
        }
        c.name = p.value("name", c.id);
        c.version = p.value("version", std::string("1.0"));
        c.author = p.value("author", std::string());
        c.description = p.value("description", std::string());
        c.api = p.value("api", 1);
        for (const json& perm : p.value("permissions", json::array()))
            if (perm.is_string()) c.permissions.push_back(perm.get<std::string>());
        bool ok = true, manifest = false;
        for (const json& f : p.value("files", json::array())) {
            CatalogFile file{f.value("path", std::string()), f.value("sha256", std::string()), f.value("size", int64_t{0})};
            const bool hex = file.sha256.size() == 64 && std::all_of(file.sha256.begin(), file.sha256.end(), [](char ch) {
                return std::isdigit(static_cast<unsigned char>(ch)) || (ch >= 'a' && ch <= 'f');
            });
            if (!safeCatalogPath(file.path) || !hex || file.size < 0) ok = false;
            manifest = manifest || file.path == "plugin.json";
            c.files.push_back(std::move(file));
        }
        if (!ok || !manifest) {
            reject("plugin \"" + c.id + "\" has an unsafe or incomplete file list");
            continue;
        }
        out.push_back(std::move(c));
    }
    return true;
}

} // namespace bemotion
