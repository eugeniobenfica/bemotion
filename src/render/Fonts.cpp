#include "render/Fonts.h"

#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"

#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#else
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>

namespace bemotion {

namespace {

constexpr const char* kInterFiles[] = {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"};

struct State {
    std::filesystem::path fontsDir;
    std::array<sk_sp<SkTypeface>, static_cast<int>(FontWeight::Count)> inter;
    std::vector<std::string> families; // cached list
    std::mutex mutex;                  // the export thread shapes text too
};

State& state() {
    static State s;
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

FontWeight interWeight(const std::string& style) {
    const std::string s = lower(style);
    if (s.find("semibold") != std::string::npos || s.find("semi bold") != std::string::npos) return FontWeight::SemiBold;
    if (s.find("bold") != std::string::npos || s.find("black") != std::string::npos || s.find("heavy") != std::string::npos)
        return FontWeight::Bold;
    if (s.find("medium") != std::string::npos) return FontWeight::Medium;
    return FontWeight::Regular;
}

// "SemiBold Italic" -> weight 600, italic.
SkFontStyle styleFromName(const std::string& name) {
    const std::string s = lower(name);
    int weight = SkFontStyle::kNormal_Weight;
    struct W {
        const char* key;
        int weight;
    };
    // Longest keys first: "extrabold" before "bold".
    static constexpr W kWeights[] = {{"extralight", 200}, {"ultralight", 200}, {"semibold", 600}, {"semi bold", 600},
                                     {"demibold", 600},   {"extrabold", 800},  {"ultrabold", 800}, {"extra bold", 800},
                                     {"thin", 100},       {"light", 300},      {"medium", 500},    {"bold", 700},
                                     {"black", 900},      {"heavy", 900}};
    for (const W& w : kWeights)
        if (s.find(w.key) != std::string::npos) {
            weight = w.weight;
            break;
        }
    const bool italic = s.find("italic") != std::string::npos || s.find("oblique") != std::string::npos;
    return SkFontStyle(weight, SkFontStyle::kNormal_Width, italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant);
}

} // namespace

sk_sp<SkFontMgr> Fonts::manager() {
    static const sk_sp<SkFontMgr> mgr = [] {
#if defined(_WIN32)
        return SkFontMgr_New_DirectWrite();
#elif defined(__APPLE__)
        return SkFontMgr_New_CoreText(nullptr);
#else
        return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#endif
    }();
    return mgr;
}

void Fonts::load(const std::filesystem::path& assetsDir) {
    State& s = state();
    s.fontsDir = assetsDir / "fonts";
    for (int i = 0; i < static_cast<int>(FontWeight::Count); ++i) {
        const std::u8string path = (s.fontsDir / kInterFiles[i]).u8string();
        auto data = SkData::MakeFromFileName(reinterpret_cast<const char*>(path.c_str()));
        s.inter[i] = data ? manager()->makeFromData(std::move(data)) : nullptr;
    }
}

std::filesystem::path Fonts::interFile(const std::string& style) {
    return state().fontsDir / kInterFiles[static_cast<int>(interWeight(style))];
}

sk_sp<SkTypeface> Fonts::forWeight(FontWeight weight) {
    sk_sp<SkTypeface> tf = state().inter[static_cast<int>(weight)];
    return tf ? tf : manager()->legacyMakeTypeface(nullptr, SkFontStyle());
}

sk_sp<SkTypeface> Fonts::typeface(const std::string& family, const std::string& style) {
    if (family.empty() || family == kDefaultFamily) return forWeight(interWeight(style));
    State& s = state();
    std::lock_guard lock(s.mutex);
    // An exact style name first (families like "Bahnschrift SemiLight Condensed" use their own names).
    sk_sp<SkFontStyleSet> set = manager()->matchFamily(family.c_str());
    if (set && set->count() > 0) {
        for (int i = 0; i < set->count(); ++i) {
            SkFontStyle fs;
            SkString name;
            set->getStyle(i, &fs, &name);
            if (lower(name.c_str()) == lower(style))
                if (auto tf = set->createTypeface(i)) return tf;
        }
        if (auto tf = set->matchStyle(styleFromName(style))) return tf;
    }
    return forWeight(interWeight(style)); // not installed here
}

std::vector<std::string> Fonts::families() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.families.empty()) {
        const sk_sp<SkFontMgr> mgr = manager();
        for (int i = 0; i < mgr->countFamilies(); ++i) {
            SkString name;
            mgr->getFamilyName(i, &name);
            if (name.size() && name.c_str()[0] != '@' && name.c_str()[0] != '.') s.families.emplace_back(name.c_str());
        }
        std::sort(s.families.begin(), s.families.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        s.families.erase(std::unique(s.families.begin(), s.families.end()), s.families.end());
        s.families.erase(std::remove(s.families.begin(), s.families.end(), kDefaultFamily), s.families.end());
        s.families.insert(s.families.begin(), kDefaultFamily);
    }
    return s.families;
}

std::vector<std::string> Fonts::stylesOf(const std::string& family) {
    if (family == kDefaultFamily) return {"Regular", "Medium", "SemiBold", "Bold"};
    std::vector<std::string> styles;
    sk_sp<SkFontStyleSet> set = manager()->matchFamily(family.c_str());
    if (!set) return styles;
    std::vector<std::pair<SkFontStyle, std::string>> all;
    for (int i = 0; i < set->count(); ++i) {
        SkFontStyle fs;
        SkString name;
        set->getStyle(i, &fs, &name);
        all.emplace_back(fs, name.size() ? name.c_str() : "Regular");
    }
    // Upright before italic, light to heavy: "Regular" lands near the top.
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
        if (a.first.slant() != b.first.slant()) return a.first.slant() < b.first.slant();
        const int da = std::abs(a.first.weight() - 400), db = std::abs(b.first.weight() - 400);
        return da != db ? da < db : a.first.weight() < b.first.weight();
    });
    for (auto& [fs, name] : all)
        if (std::find(styles.begin(), styles.end(), name) == styles.end()) styles.push_back(name);
    return styles;
}

bool Fonts::hasFamily(const std::string& family) {
    if (family == kDefaultFamily) return true;
    sk_sp<SkFontStyleSet> set = manager()->matchFamily(family.c_str());
    return set && set->count() > 0;
}

} // namespace bemotion
