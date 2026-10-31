#include "core/Updates.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace bemotion {

using json = nlohmann::json;

namespace {

struct Version {
    long major = 0, minor = 0, patch = 0;
    std::vector<std::string> pre; // "beta.2" -> {"beta", "2"}; empty for a release
};

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

std::optional<Version> parseVersion(std::string s) {
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    const size_t build = s.find('+'); // build metadata does not count
    if (build != std::string::npos) s.erase(build);
    Version v;
    const size_t dash = s.find('-');
    std::string core = s.substr(0, dash);
    if (dash != std::string::npos) {
        std::stringstream pre(s.substr(dash + 1));
        std::string part;
        while (std::getline(pre, part, '.')) {
            if (part.empty()) return std::nullopt;
            v.pre.push_back(part);
        }
        if (v.pre.empty()) return std::nullopt;
    }
    std::stringstream parts(core);
    std::string part;
    long* fields[] = {&v.major, &v.minor, &v.patch};
    int n = 0;
    while (std::getline(parts, part, '.')) {
        if (n == 3 || !allDigits(part) || part.size() > 9) return std::nullopt;
        *fields[n++] = std::stol(part);
    }
    if (n == 0) return std::nullopt;
    return v;
}

int comparePre(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return a.empty() == b.empty() ? 0 : a.empty() ? 1 : -1; // a release beats its betas
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const bool na = allDigits(a[i]) && a[i].size() < 10, nb = allDigits(b[i]) && b[i].size() < 10;
        if (na && nb) {
            const long x = std::stol(a[i]), y = std::stol(b[i]);
            if (x != y) return x < y ? -1 : 1;
        } else if (na != nb) {
            return na ? -1 : 1; // numbers come before words
        } else if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && std::equal(end.rbegin(), end.rend(), s.rbegin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

const ReleaseAsset* Release::asset(const std::string& assetName) const {
    for (const ReleaseAsset& a : assets)
        if (a.name == assetName) return &a;
    return nullptr;
}

int compareReleaseVersions(const std::string& a, const std::string& b) {
    const auto va = parseVersion(a), vb = parseVersion(b);
    if (!va || !vb) return va ? 1 : vb ? -1 : 0; // something that is not a version is the oldest
    if (va->major != vb->major) return va->major < vb->major ? -1 : 1;
    if (va->minor != vb->minor) return va->minor < vb->minor ? -1 : 1;
    if (va->patch != vb->patch) return va->patch < vb->patch ? -1 : 1;
    return comparePre(va->pre, vb->pre);
}

bool isPrerelease(const std::string& version) {
    const auto v = parseVersion(version);
    return v && !v->pre.empty();
}

bool parseReleases(const std::string& text, std::vector<Release>& out, std::string* error) {
    out.clear();
    const json j = json::parse(text, nullptr, false);
    if (!j.is_array()) {
        if (error) *error = j.is_object() && j.contains("message") && j["message"].is_string()
                                ? "GitHub: " + j["message"].get<std::string>() // rate limit, not found...
                                : "not a list of releases";
        return false;
    }
    auto str = [](const json& o, const char* key) {
        return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string();
    };
    for (const json& r : j) {
        if (!r.is_object() || r.value("draft", false)) continue;
        Release rel;
        rel.tag = str(r, "tag_name");
        if (!parseVersion(rel.tag)) continue;
        rel.version = rel.tag[0] == 'v' || rel.tag[0] == 'V' ? rel.tag.substr(1) : rel.tag;
        rel.name = str(r, "name");
        rel.notes = str(r, "body");
        rel.page = str(r, "html_url");
        rel.prerelease = r.value("prerelease", false) || isPrerelease(rel.version);
        if (r.contains("assets") && r["assets"].is_array())
            for (const json& a : r["assets"]) {
                if (!a.is_object()) continue;
                ReleaseAsset asset{str(a, "name"), str(a, "browser_download_url"), 0};
                if (a.contains("size") && a["size"].is_number_integer()) asset.size = a["size"].get<int64_t>();
                if (!asset.name.empty() && !asset.url.empty()) rel.assets.push_back(std::move(asset));
            }
        out.push_back(std::move(rel));
    }
    return true;
}

std::optional<Release> newestRelease(const std::vector<Release>& releases, const std::string& current, bool betas) {
    const Release* best = nullptr;
    for (const Release& r : releases) {
        if (r.prerelease && !betas) continue;
        if (compareReleaseVersions(r.version, current) <= 0) continue;
        if (!best || compareReleaseVersions(r.version, best->version) > 0) best = &r;
    }
    if (!best) return std::nullopt;
    return *best;
}

const ReleaseAsset* packageFor(const Release& release, UpdatePlatform platform) {
    const char* end = platform == UpdatePlatform::Windows ? "-setup.exe" : platform == UpdatePlatform::Linux ? ".AppImage" : ".dmg";
    for (const ReleaseAsset& a : release.assets)
        if (endsWith(a.name, end)) return &a;
    return nullptr;
}

std::map<std::string, std::string> parseSha256Sums(const std::string& text) {
    std::map<std::string, std::string> sums;
    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 66) continue;
        std::string hex = line.substr(0, 64);
        if (!std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); }) || line[64] != ' ') continue;
        size_t start = 65;
        if (start < line.size() && (line[start] == ' ' || line[start] == '*')) ++start; // text or binary mode
        if (start >= line.size()) continue;
        std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        sums[line.substr(start)] = hex;
    }
    return sums;
}

} // namespace bemotion
