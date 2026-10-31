#pragma once

// UTF-8 paths for every layer (app/Platform.h forwards to these). On Windows a
// path's string() is in the legacy code page, which mangles names like
// "C:\Users\João": go through these instead.

#include <exception>
#include <filesystem>
#include <string>

namespace bemotion::utf8 {

inline std::string fromPath(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

// Invalid UTF-8 falls back to the system code page.
inline std::filesystem::path toPath(const std::string& utf8) {
    try {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    } catch (const std::exception&) {
        return std::filesystem::path(utf8);
    }
}

} // namespace bemotion::utf8
