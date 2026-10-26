#include "core/Language.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace bemotion {

bool parseLanguage(const std::string& text, LanguageFile& out, std::string* error) {
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        if (j.value("format", std::string()) != "bemotion-language") throw std::runtime_error("This is not a bemotion language file.");
        LanguageFile file;
        file.code = j.value("code", std::string());
        file.name = j.value("name", file.code);
        if (file.code.empty()) throw std::runtime_error("The language file has no \"code\".");
        const nlohmann::json strings = j.value("strings", nlohmann::json::object()); // kept: items() refers to it
        for (const auto& [english, translation] : strings.items())
            if (translation.is_string() && !translation.get<std::string>().empty()) file.strings[english] = translation.get<std::string>();
        out = std::move(file);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::vector<std::string> placeholdersOf(const std::string& text) {
    std::vector<std::string> found;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 1 < text.size()) {
            size_t j = i + 1;
            if (text[j] == 'n') {
                found.push_back("%n");
                continue;
            }
            while (j < text.size() && j < i + 3 && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
            if (j > i + 1) found.push_back(text.substr(i, j - i));
        } else if (text[i] == '{') {
            const size_t close = text.find('}', i);
            if (close != std::string::npos && close > i + 1 &&
                std::all_of(text.begin() + static_cast<std::ptrdiff_t>(i + 1), text.begin() + static_cast<std::ptrdiff_t>(close),
                            [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }))
                found.push_back(text.substr(i, close - i + 1));
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

int matchLanguage(const std::vector<std::string>& codes, const std::string& locale) {
    auto norm = [](std::string s) {
        for (char& c : s) c = c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string want = norm(locale);
    const std::string lang = want.substr(0, want.find('-'));
    for (size_t i = 0; i < codes.size(); ++i)
        if (norm(codes[i]) == want) return static_cast<int>(i);
    for (size_t i = 0; i < codes.size(); ++i) {
        const std::string c = norm(codes[i]);
        if (c.substr(0, c.find('-')) == lang) return static_cast<int>(i);
    }
    return -1;
}

} // namespace bemotion
