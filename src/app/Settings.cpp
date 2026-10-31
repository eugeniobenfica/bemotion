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
        s.disabledModules = j.value("disabled_modules", std::vector<std::string>{});
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
        // Before these were settings, Friendly hid the ƒ and graph buttons: it still does.
        s.showExpressionButtons = j.value("show_expression_buttons", s.experience != "friendly");
        s.showGraphButton = j.value("show_graph_button", s.experience != "friendly");
        s.showSnapshotButton = j.value("show_snapshot_button", s.showSnapshotButton);
        s.showTimelineScrollbar = j.value("show_timeline_scrollbar", s.showTimelineScrollbar);
        s.essentialsOnly = j.value("essentials_only", s.essentialsOnly);
        s.checkUpdates = j.value("check_updates", s.checkUpdates);
        s.updateChannel = j.value("update_channel", s.updateChannel);
        if (s.updateChannel != "stable" && s.updateChannel != "beta") s.updateChannel.clear();
        s.lastUpdateCheck = j.value("last_update_check", s.lastUpdateCheck);
        s.skippedVersion = j.value("skipped_version", s.skippedVersion);
        s.hardwareDecoding = j.value("hardware_decoding", s.hardwareDecoding);
        s.useProxies = j.value("use_proxies", s.useProxies);
        s.cacheMegabytes = std::clamp(j.value("cache_megabytes", s.cacheMegabytes), 128, 32768);
        s.scaledVideoPreview = j.value("scaled_video_preview", s.scaledVideoPreview);
    s.adaptivePlayback = j.value("adaptive_playback", s.adaptivePlayback);
    s.gpuRendering = j.value("gpu_rendering", s.gpuRendering);
    s.renderEngine = j.value("render_engine", s.renderEngine) == "vulkan" ? "vulkan" : "opengl";
        s.autoProxies = j.value("auto_proxies", s.autoProxies);
        s.showFilmstrips = j.value("show_filmstrips", s.showFilmstrips);
        s.animationFolders = j.value("animation_folders", s.animationFolders);
        s.animationFolderOf = j.value("animation_folder_of", s.animationFolderOf);
        s.closedAnimationFolders = j.value("closed_animation_folders", s.closedAnimationFolders);
    s.closedEffectCategories = j.value("closed_effect_categories", s.closedEffectCategories);
        s.closedObjectCategories = j.value("closed_object_categories", s.closedObjectCategories);
        s.processorThreads = std::clamp(j.value("processor_threads", s.processorThreads), 0, 64);
        if (const auto tj = j.find("tool_settings"); tj != j.end() && tj->is_object()) {
            ToolSettings& t = s.tools;
            if (const auto c = tj->find("brush_color"); c != tj->end() && c->is_array() && c->size() == 4)
                for (int i = 0; i < 4; ++i) t.brushColor[i] = std::clamp((*c)[static_cast<size_t>(i)].is_number() ? (*c)[static_cast<size_t>(i)].get<float>() : 1.0f, 0.0f, 1.0f);
            auto num = [&](const char* key, float& v, float lo, float hi) { v = std::clamp(tj->value(key, v), lo, hi); };
            num("brush_size", t.brushSize, 1, 500);
            num("brush_opacity", t.brushOpacity, 1, 100);
            num("brush_softness", t.brushSoftness, 0, 200);
            num("eraser_size", t.eraserSize, 1, 500);
            num("eraser_softness", t.eraserSoftness, 0, 200);
            t.pinAutoKey = tj->value("pin_auto_key", t.pinAutoKey);
            t.pinStarch = tj->value("pin_starch", t.pinStarch);
            t.showMesh = tj->value("show_mesh", t.showMesh);
            num("track_feature", t.trackFeature, 8, 120);
            num("track_search", t.trackSearch, 8, 200);
            num("track_match", t.trackMatch, 10, 99);
            t.trackBackward = tj->value("track_backward", t.trackBackward);
            num("silence_threshold", t.silenceThreshold, -80, -6);
            num("silence_min", t.silenceMin, 0.1f, 10);
            num("silence_padding", t.silencePadding, 0, 2);
            t.silenceRipple = tj->value("silence_ripple", t.silenceRipple);
            num("level_target", t.levelTarget, -40, -6);
            num("level_even_out", t.levelEvenOut, 0, 100);
        }
        if (const auto ps = j.find("plugin_settings"); ps != j.end() && ps->is_object())
            for (const auto& [plugin, values] : ps->items())
                if (values.is_object())
                    for (const auto& [id, value] : values.items()) s.pluginSettings[plugin][id] = value.dump();
        s.gpuCacheMegabytes = std::clamp(j.value("gpu_cache_megabytes", s.gpuCacheMegabytes), 64, 8192);
        s.language = j.value("language", s.language);
        ui::loadKeymap(j.contains("shortcuts") ? j["shortcuts"].dump() : "{}");
        if (j.contains("palette")) s.customPalette = theme::paletteFromJson(j["palette"].dump(), s.palette);
    } catch (const std::exception&) {
        // Missing or unreadable settings: keep the defaults.
    }
}

void saveSettings() {
    const Settings& s = settings();
    json j = {{"theme", s.theme},        {"custom_accent", s.customAccent}, {"accent", s.accent},
              {"advanced", s.advanced}, {"snap_distance", s.snapDistance}, {"disabled_plugins", s.disabledPlugins}, {"disabled_modules", s.disabledModules},
              {"timeline_layer_row", s.timelineLayerRow}, {"timeline_property_row", s.timelinePropertyRow},
              {"keyframe_size", s.keyframeSize}, {"text_scale", s.textScale}, {"ui_font", s.uiFont},
              {"arrow_frames", s.arrowFrames}, {"big_arrow_frames", s.bigArrowFrames},
              {"autosave_minutes", s.autosaveMinutes}, {"show_intro", s.showIntro}, {"show_rulers", s.showRulers}, {"preview_quality", s.previewQuality}, {"welcome_added", s.welcomeAdded}, {"welcome_removed", s.welcomeRemoved}, {"projects_folder", s.projectsFolder}, {"speech_language", s.speechLanguage}, {"toolbar", s.toolbar},
              {"toolbar_in_viewport", s.toolbarInViewport}, {"toolbar_in_window", s.toolbarInWindow}, {"toolbar_x", s.toolbarX}, {"toolbar_y", s.toolbarY},
              {"recent_colors", s.recentColors}, {"show_meters", s.showMeters},
              {"show_expression_buttons", s.showExpressionButtons}, {"show_graph_button", s.showGraphButton},
              {"show_snapshot_button", s.showSnapshotButton}, {"show_timeline_scrollbar", s.showTimelineScrollbar},
              {"experience", s.experience}, {"essentials_only", s.essentialsOnly},
              {"check_updates", s.checkUpdates}, {"update_channel", s.updateChannel},
              {"last_update_check", s.lastUpdateCheck}, {"skipped_version", s.skippedVersion},
              {"hardware_decoding", s.hardwareDecoding}, {"use_proxies", s.useProxies}, {"cache_megabytes", s.cacheMegabytes},
              {"scaled_video_preview", s.scaledVideoPreview}, {"adaptive_playback", s.adaptivePlayback}, {"gpu_rendering", s.gpuRendering},
              {"render_engine", s.renderEngine},
              {"auto_proxies", s.autoProxies}, {"show_filmstrips", s.showFilmstrips},
              {"animation_folders", s.animationFolders}, {"animation_folder_of", s.animationFolderOf},
              {"closed_animation_folders", s.closedAnimationFolders}, {"closed_effect_categories", s.closedEffectCategories},
              {"closed_object_categories", s.closedObjectCategories},
              {"processor_threads", s.processorThreads}, {"gpu_cache_megabytes", s.gpuCacheMegabytes},
              {"tool_settings",
               {{"brush_color", {s.tools.brushColor[0], s.tools.brushColor[1], s.tools.brushColor[2], s.tools.brushColor[3]}},
                {"brush_size", s.tools.brushSize}, {"brush_opacity", s.tools.brushOpacity}, {"brush_softness", s.tools.brushSoftness},
                {"eraser_size", s.tools.eraserSize}, {"eraser_softness", s.tools.eraserSoftness}, {"pin_auto_key", s.tools.pinAutoKey},
                {"pin_starch", s.tools.pinStarch}, {"show_mesh", s.tools.showMesh},
                {"track_feature", s.tools.trackFeature}, {"track_search", s.tools.trackSearch}, {"track_match", s.tools.trackMatch},
                {"track_backward", s.tools.trackBackward}, {"silence_threshold", s.tools.silenceThreshold},
                {"silence_min", s.tools.silenceMin}, {"silence_padding", s.tools.silencePadding}, {"silence_ripple", s.tools.silenceRipple},
                {"level_target", s.tools.levelTarget}, {"level_even_out", s.tools.levelEvenOut}}},
              {"plugin_settings", [&s] {
                   json all = json::object();
                   for (const auto& [plugin, values] : s.pluginSettings)
                       for (const auto& [id, text] : values) all[plugin][id] = json::parse(text, nullptr, false);
                   return all;
               }()},
              {"language", s.language},
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
        s.showExpressionButtons = s.showGraphButton = false;
        s.showRulers = false;
        s.advanced = false;
    } else if (s.experience == "expert") {
        s.toolbar = {"select", "razor", "pen", "ripple", "roll", "slip", "crop", "split", "compound", "duplicate", "easy_ease", "delete"};
        s.showMeters = true;
        s.showRulers = true;
        s.showExpressionButtons = s.showGraphButton = true;
        s.advanced = true;
    } else {
        s.toolbar = ui::defaultToolbar();
        s.showMeters = true;
        s.showExpressionButtons = s.showGraphButton = true;
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
