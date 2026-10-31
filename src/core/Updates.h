#pragma once

// New versions of bemotion, from its releases on GitHub: which release is newer than this one,
// which of its files is this system's package, and the SHA-256 it must have (the release's
// SHA256SUMS.txt). Pure data: app/Downloads fetches, only when the user turned updates on.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bemotion {

struct ReleaseAsset {
    std::string name, url;
    int64_t size = 0;
};

struct Release {
    std::string tag;     // "v0.2.0-beta.1"
    std::string version; // the tag without its "v"
    std::string name, notes, page; // title, release notes (Markdown), its web page
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;
    const ReleaseAsset* asset(const std::string& name) const;
};

enum class UpdatePlatform { Windows, Linux, Mac };

// Reads the GitHub API's list of releases (GET /repos/<owner>/<repo>/releases). Drafts and
// releases whose tag is not a version are left out; false when the text is not such a list.
bool parseReleases(const std::string& json, std::vector<Release>& out, std::string* error = nullptr);

// Release versions, semver's way: "0.2.0" > "0.2.0-beta.3" > "0.2.0-beta.2" > "0.1.9"; a
// leading "v" is ignored. <0, 0, >0.
int compareReleaseVersions(const std::string& a, const std::string& b);
bool isPrerelease(const std::string& version);

// The newest release above `current` (betas only with `betas`), or none.
std::optional<Release> newestRelease(const std::vector<Release>& releases, const std::string& current, bool betas);

// The release's package for a system: the installer on Windows (…-setup.exe), the AppImage on
// Linux, the disk image on macOS. Null when the release has none.
const ReleaseAsset* packageFor(const Release& release, UpdatePlatform platform);

// SHA256SUMS.txt ("<hex>  <name>" or "<hex> *<name>" per line): file name -> lowercase hex.
std::map<std::string, std::string> parseSha256Sums(const std::string& text);

} // namespace bemotion
