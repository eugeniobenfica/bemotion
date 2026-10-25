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

std::filesystem::path projectsFolder() {
    if (settings().projectsFolder.empty()) return {};
    const std::filesystem::path p = platform::fromUtf8(settings().projectsFolder);
    std::error_code ec;
    return std::filesystem::is_directory(p, ec) ? p : std::filesystem::path();
}

std::filesystem::path projectsFolderOrDocuments() {
    const std::filesystem::path p = projectsFolder();
    return p.empty() ? platform::documentsDir() : p;
}

void loadSettings() {
    Settings& s = settings();
    std::error_code fe;
    s.firstRun = !std::filesystem::exists(settingsFile(), fe);
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
        s.autosaveMinutes = std::clamp(j.value("autosave_minutes", s.autosaveMinutes), 0, 120);
        s.showIntro = j.value("show_intro", s.showIntro);
        s.showRulers = j.value("show_rulers", s.showRulers);
        s.previewQuality = j.value("preview_quality", s.previewQuality);
        if (s.previewQuality != 2 && s.previewQuality != 4) s.previewQuality = 1;
        s.welcomeAdded = j.value("welcome_added", s.welcomeAdded);
        s.welcomeRemoved = j.value("welcome_removed", s.welcomeRemoved);
        s.projectsFolder = j.value("projects_folder", s.projectsFolder);
        s.speechLanguage = j.value("speech_language", s.speechLanguage);
        s.toolbarInViewport = j.value("toolbar_in_viewport", s.toolbarInViewport);
        s.toolbarInWindow = j.value("toolbar_in_window", s.toolbarInWindow);
        s.toolbarX = std::clamp(j.value("toolbar_x", s.toolbarX), 0.0f, 1.0f);
        s.toolbarY = std::clamp(j.value("toolbar_y", s.toolbarY), 0.0f, 1.0f);
        if (j.contains("toolbar")) {
            s.toolbar.clear();
            for (const std::string& id : j.value("toolbar", std::vector<std::string>{}))
                if (ui::toolIndex(id) >= 0) s.toolbar.push_back(id); // tools of newer versions are skipped
        }
        s.recentColors = j.value("recent_colors", std::vector<std::string>{});
        if (s.recentColors.size() > 16) s.recentColors.resize(16);
        s.showMeters = j.value("show_meters", s.showMeters);
        s.experience = j.value("experience", s.experience);
        if (s.experience != "friendly" && s.experience != "expert") s.experience = "balanced";
        s.essentialsOnly = j.value("essentials_only", s.essentialsOnly);
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
              {"autosave_minutes", s.autosaveMinutes}, {"show_intro", s.showIntro}, {"show_rulers", s.showRulers}, {"preview_quality", s.previewQuality}, {"welcome_added", s.welcomeAdded}, {"welcome_removed", s.welcomeRemoved}, {"projects_folder", s.projectsFolder}, {"speech_language", s.speechLanguage}, {"toolbar", s.toolbar},
              {"toolbar_in_viewport", s.toolbarInViewport}, {"toolbar_in_window", s.toolbarInWindow}, {"toolbar_x", s.toolbarX}, {"toolbar_y", s.toolbarY},
              {"recent_colors", s.recentColors}, {"show_meters", s.showMeters},
              {"experience", s.experience}, {"essentials_only", s.essentialsOnly},
              {"shortcuts", json::parse(ui::saveKeymap())}};
    if (s.customPalette) j["palette"] = json::parse(theme::paletteToJson(s.palette));
    writeFile(settingsFile(), j.dump(2), nullptr);
}

void applyExperience(const std::string& experience) {
    Settings& s = settings();
    s.experience = experience == "friendly" || experience == "expert" ? experience : "balanced";
    if (s.experience == "friendly") {
        s.toolbar = {"select", "razor", "delete"};
        s.showMeters = false;
        s.showRulers = false;
        s.advanced = false;
    } else if (s.experience == "expert") {
        s.toolbar = {"select", "razor", "crop", "split", "compound", "duplicate", "easy_ease", "forward", "backward", "delete"};
        s.showMeters = true;
        s.showRulers = true;
        s.advanced = true;
    } else {
        s.toolbar = ui::defaultToolbar();
        s.showMeters = true;
        s.showRulers = false;
        s.advanced = false;
    }
}

bool friendlyMode() { return settings().experience == "friendly"; }
bool expertMode() { return settings().experience == "expert"; }

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
