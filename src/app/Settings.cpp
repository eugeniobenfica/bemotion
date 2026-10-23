#include "app/Settings.h"

#include "app/Platform.h"
#include "ui/Keymap.h"

#include <nlohmann/json.hpp>

#include <QApplication>
#include <QFont>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

using json = nlohmann::json;

std::filesystem::path settingsFile() { return platform::appDataDir() / "settings.json"; }

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFile(const std::filesystem::path& file, const std::string& text, std::string* error) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out << text) return true;
    if (error) *error = "Could not write " + platform::toUtf8(file);
    return false;
}

std::string safeFileName(std::string name) {
    for (char& c : name)
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
    return name.empty() ? "theme" : name;
}

std::vector<LibraryTheme>& storage() {
    static std::vector<LibraryTheme> themes;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        ThemeLibrary::reload();
    }
    return themes;
}

} // namespace

// --- settings -------------------------------------------------------------------

Settings& settings() {
    static Settings s;
    return s;
}

void loadSettings() {
    Settings& s = settings();
    try {
        const json j = json::parse(readFile(settingsFile()));
        if (!j.is_object()) return;
        s.theme = j.value("theme", s.theme);
        s.customAccent = j.value("custom_accent", false);
        s.accent = j.value("accent", s.accent);
        s.advanced = j.value("advanced", false);
        s.snapDistance = std::clamp(j.value("snap_distance", s.snapDistance), 2.0f, 30.0f);
        s.disabledPlugins = j.value("disabled_plugins", std::vector<std::string>{});
        s.timelineLayerRow = std::clamp(j.value("timeline_layer_row", s.timelineLayerRow), 22.0f, 80.0f);
        s.timelinePropertyRow = std::clamp(j.value("timeline_property_row", s.timelinePropertyRow), 20.0f, 60.0f);
        s.keyframeSize = std::clamp(j.value("keyframe_size", s.keyframeSize), 3.0f, 10.0f);
        s.textScale = std::clamp(j.value("text_scale", s.textScale), 0.8f, 1.5f);
        s.uiFont = j.value("ui_font", s.uiFont);
        s.arrowFrames = std::clamp(j.value("arrow_frames", s.arrowFrames), 1, 1000);
        s.bigArrowFrames = std::clamp(j.value("big_arrow_frames", s.bigArrowFrames), 1, 1000);
        s.maxDuration = std::clamp(j.value("max_duration", s.maxDuration), 0.0, 24.0 * 3600.0);
        s.autosaveMinutes = std::clamp(j.value("autosave_minutes", s.autosaveMinutes), 0, 120);
        s.showIntro = j.value("show_intro", s.showIntro);
        s.welcomeAdded = j.value("welcome_added", s.welcomeAdded);
        if (j.contains("toolbar")) {
            s.toolbar.clear();
            for (const std::string& id : j.value("toolbar", std::vector<std::string>{}))
                if (ui::toolIndex(id) >= 0) s.toolbar.push_back(id); // tools of newer versions are skipped
        }
        ui::loadKeymap(j.contains("shortcuts") ? j["shortcuts"].dump() : "{}");
        if (j.contains("palette")) s.customPalette = theme::paletteFromJson(j["palette"].dump(), s.palette);
    } catch (const std::exception&) {
        // Missing or unreadable settings: keep the defaults.
    }
}

void saveSettings() {
    const Settings& s = settings();
    json j = {{"theme", s.theme},        {"custom_accent", s.customAccent}, {"accent", s.accent},
              {"advanced", s.advanced}, {"snap_distance", s.snapDistance}, {"disabled_plugins", s.disabledPlugins},
              {"timeline_layer_row", s.timelineLayerRow}, {"timeline_property_row", s.timelinePropertyRow},
              {"keyframe_size", s.keyframeSize}, {"text_scale", s.textScale}, {"ui_font", s.uiFont},
              {"arrow_frames", s.arrowFrames}, {"big_arrow_frames", s.bigArrowFrames},
              {"max_duration", s.maxDuration}, {"autosave_minutes", s.autosaveMinutes}, {"show_intro", s.showIntro}, {"welcome_added", s.welcomeAdded}, {"toolbar", s.toolbar},
              {"shortcuts", json::parse(ui::saveKeymap())}};
    if (s.customPalette) j["palette"] = json::parse(theme::paletteToJson(s.palette));
    writeFile(settingsFile(), j.dump(2), nullptr);
}

theme::Palette effectivePalette() {
    const Settings& s = settings();
    theme::Palette p = theme::darkPalette();
    if (s.customPalette) p = s.palette;
    else if (const LibraryTheme* t = ThemeLibrary::find(s.theme)) p = t->palette;
    if (s.customAccent) p.accent = s.accent;
    return p;
}

void applySettings() {
    theme::apply(effectivePalette());
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        QFont font(QString::fromStdString(settings().uiFont));
        font.setPointSizeF(9.5 * std::clamp(settings().textScale, 0.8f, 1.5f));
        font.setStyleStrategy(QFont::PreferAntialias);
        app->setFont(font);
    }
}

// --- themes ---------------------------------------------------------------------

const std::vector<LibraryTheme>& ThemeLibrary::all() { return storage(); }

std::filesystem::path ThemeLibrary::userDir() {
    const auto dir = platform::appDataDir() / "themes";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void ThemeLibrary::reload() {
    auto& themes = storage();
    themes.clear();
    for (auto p : {theme::darkPalette(), theme::lightPalette(), theme::midnightPalette()}) themes.push_back({p, {}});
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(userDir(), ec)) {
        if (entry.path().extension() != ".json") continue;
        theme::Palette p;
        if (!theme::paletteFromJson(readFile(entry.path()), p)) continue;
        // A user theme with a built-in's name replaces it.
        std::erase_if(themes, [&](const LibraryTheme& t) { return t.palette.name == p.name; });
        themes.push_back({std::move(p), entry.path()});
    }
}

const LibraryTheme* ThemeLibrary::find(const std::string& name) {
    for (const auto& t : all())
        if (t.palette.name == name) return &t;
    return nullptr;
}

bool ThemeLibrary::importFile(const std::filesystem::path& file, std::string* importedName, std::string* error) {
    theme::Palette p;
    if (!theme::paletteFromJson(readFile(file), p, error)) return false;
    if (importedName) *importedName = p.name;
    return save(p, error);
}

bool ThemeLibrary::save(const theme::Palette& palette, std::string* error) {
    if (!writeFile(userDir() / platform::fromUtf8(safeFileName(palette.name) + ".json"), theme::paletteToJson(palette), error))
        return false;
    reload();
    return true;
}

bool ThemeLibrary::exportTo(const theme::Palette& palette, const std::filesystem::path& file, std::string* error) {
    return writeFile(file, theme::paletteToJson(palette), error);
}

} // namespace bemotion
