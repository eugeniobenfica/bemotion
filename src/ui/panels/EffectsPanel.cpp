#include "ui/panels/EffectsPanel.h"

#include "app/EffectLibrary.h"
#include "app/Platform.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/PropertiesPanel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <vector>

namespace bemotion {

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

bool beginTable(const char* id) {
    if (!ImGui::BeginTable(id, 4, kTableFlags)) return false;
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 0.8f);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    return true;
}

bool matches(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(text).find(lower(filter)) != std::string::npos;
}

void faint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void applyToSelection(EditorContext& ctx, const std::vector<Effect>& effects) {
    for (Layer* l : ctx.selectedLayers()) {
        if (l->type == LayerType::Camera) continue; // cameras are not drawn
        l->effects.insert(l->effects.end(), effects.begin(), effects.end());
    }
    ctx.touch();
}

} // namespace

void EffectsPanel::draw(EditorContext& ctx) {
    if (!open) return;
    // New in this version: existing layouts get the tab next to Properties.
    if (ImGuiWindow* props = ImGui::FindWindowByName(PropertiesPanel::kWindowName); props && props->DockId)
        ImGui::SetNextWindowDockID(props->DockId, ImGuiCond_FirstUseEver);
    ui::nextPanelWindowClass();
    if (!ImGui::Begin(kWindowName, &open)) {
        ImGui::End();
        return;
    }
    if (ui::panelIsMinimized()) {
        ui::panelMinimizeButton();
        ImGui::End();
        return;
    }

    Layer* layer = ctx.selected();
    if (layer && layer->type != LayerType::Camera) {
        drawStack(ctx, *layer);
    } else {
        ImGui::TextUnformatted("Effect Controls");
        faint(layer ? "Cameras have no effects." : "Select a layer to see and edit its effects.");
    }
    ImGui::Dummy(ImVec2(0, 6));
    drawBrowser(ctx);

    ui::panelMinimizeButton();
    ImGui::End();
}

void EffectsPanel::drawStack(EditorContext& ctx, Layer& layer) {
    {
        const float h = ImGui::GetFrameHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(h, h));
        ui::drawIcon(ImGui::GetWindowDrawList(), ui::iconForLayer(layer), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.6f,
                     theme::kAccent);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Effect Controls  ·  %s", layer.name.c_str());
    }
    auto props = layer.properties();
    int removeIndex = -1, moveFrom = -1, moveTo = -1;
    const float b = ImGui::GetFrameHeight();
    for (int i = 0; i < static_cast<int>(layer.effects.size()); ++i) {
        Effect& e = layer.effects[i];
        ImGui::PushID(i);
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (ImGui::Checkbox("##on", &e.enabled)) ctx.touch();
        ui::tooltip(e.enabled ? "Turn this effect off" : "Turn this effect on");
        ImGui::SameLine();
        ImGui::SetNextItemAllowOverlap();
        const bool open = ImGui::TreeNodeEx("##fx", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                        ImGuiTreeNodeFlags_Framed,
                                            "%s", e.group.c_str());
        ImGui::SameLine(right - b * 3 - 4);
        ImGui::BeginDisabled(i == 0);
        if (ui::iconButton("##up", ui::Icon::ChevronUp, b, "Move up (applied earlier)")) moveFrom = i, moveTo = i - 1;
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(i + 1 == static_cast<int>(layer.effects.size()));
        if (ui::iconButton("##down", ui::Icon::ChevronDown, b, "Move down (applied later)")) moveFrom = i, moveTo = i + 1;
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        if (ui::iconButton("##del", ui::Icon::Trash, b, "Remove effect")) removeIndex = i;
        if (open) {
            if (beginTable("fxprops")) {
                for (const PropertyRef& p : props)
                    if (p.effectIndex == i) propertyRow(ctx, layer, p);
                if (e.type == EffectType::MotionBlur) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(theme::vec(theme::kTextDim), "Samples");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::SliderInt("##samples", &e.samples, 2, 64)) ctx.touch();
                    ui::tooltip("More samples: smoother blur, slower preview");
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        layer.effects.erase(layer.effects.begin() + removeIndex);
        ctx.clearKeySelection();
        ctx.touch();
    } else if (moveFrom >= 0) {
        std::swap(layer.effects[moveFrom], layer.effects[moveTo]);
        ctx.clearKeySelection();
        ctx.touch();
    }
    if (layer.effects.empty()) faint("No effects yet. Pick one below: it is added to every selected layer.");
    else faint("Effects run top to bottom. Every parameter can be keyframed here or in the timeline.");

    if (!layer.effects.empty() && ImGui::Button("Save Stack as Preset...")) {
        saveName_ = layer.name + " effects";
        ImGui::OpenPopup("Save Effect Preset");
    }
    if (ImGui::BeginPopup("Save Effect Preset")) {
        ImGui::TextUnformatted("Preset name");
        ImGui::SetNextItemWidth(260);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##fxPresetName", &saveName_, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ui::accentButton("Save", ImVec2(120, 0)) || enter) && !saveName_.empty()) {
            EffectPreset preset{saveName_, layer.effects};
            if (EffectLibrary::save(preset, &message_)) message_ = "Saved \"" + saveName_ + "\" to Effect Presets.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

namespace {

struct Category {
    const char* name;  // After Effects' category name
    const char* label; // short tile label
    ui::Icon icon;
};
// After Effects' categories (those with effects here), then the presets.
constexpr Category kCategories[] = {
    {"Blur & Sharpen", "Blur", ui::Icon::FxBlur},       {"Color Correction", "Color", ui::Icon::FxColor},
    {"Channel", "Channel", ui::Icon::FxChannel},        {"Distort", "Distort", ui::Icon::FxDistort},
    {"Generate", "Generate", ui::Icon::FxGenerate},     {"Perspective", "Perspective", ui::Icon::FxPerspective},
    {"Stylize", "Stylize", ui::Icon::FxStylize},        {"Time", "Time", ui::Icon::FxTime},
};
constexpr const char* kPresets = "Presets";

ui::Icon categoryIcon(const char* category) {
    for (const Category& c : kCategories)
        if (std::strcmp(c.name, category) == 0) return c.icon;
    return ui::Icon::Star;
}

// Lays out equally sized tiles in as many columns as fit.
class TileGrid {
public:
    explicit TileGrid(float minWidth = 86.0f) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        columns_ = std::max(1, static_cast<int>((avail + spacing) / (minWidth + spacing)));
        size_.x = (avail - spacing * (columns_ - 1)) / columns_;
        size_.y = std::clamp(size_.x * 0.8f, 64.0f, 88.0f);
    }
    ImVec2 next() {
        if (index_++ % columns_ != 0) ImGui::SameLine();
        return size_;
    }

private:
    int columns_ = 1, index_ = 0;
    ImVec2 size_;
};

} // namespace

void effectMenuItems(EditorContext& ctx) {
    for (const Category& c : kCategories) {
        if (!ImGui::BeginMenu(c.name)) continue;
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t) {
            const auto type = static_cast<EffectType>(t);
            if (std::strcmp(effectCategory(type), c.name) != 0) continue;
            if (ImGui::MenuItem(effectLabel(type))) applyToSelection(ctx, {makeEffect(type)});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", effectDescription(type));
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Effect Presets")) {
        for (const auto& lp : EffectLibrary::all()) {
            ImGui::PushID(&lp);
            if (ImGui::MenuItem(lp.preset.name.c_str())) applyToSelection(ctx, lp.preset.effects);
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Remove All Effects")) {
        for (Layer* l : ctx.selectedLayers()) l->effects.clear();
        ctx.clearKeySelection();
        ctx.touch();
    }
}

void EffectsPanel::drawBrowser(EditorContext& ctx) {
    ImGui::SeparatorText("Effects & Presets");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##fxsearch", "Search effects", &search_);
    const bool canApply = !ctx.selection.empty();
    if (!canApply) faint("Select layers, then click an effect to apply it.");

    auto effectTile = [&](EffectType type, TileGrid& grid) {
        ImGui::PushID(static_cast<int>(type));
        ImGui::BeginDisabled(!canApply);
        if (ui::objectTile("##fx", categoryIcon(effectCategory(type)), effectLabel(type), grid.next()))
            applyToSelection(ctx, {makeEffect(type)});
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s\n%s", effectLabel(type), effectDescription(type));
        ImGui::PopID();
    };
    std::filesystem::path toRemove;
    auto presetTile = [&](const LibraryEffectPreset& lp, TileGrid& grid) {
        ImGui::PushID(&lp);
        ImGui::BeginDisabled(!canApply);
        if (ui::objectTile("##preset", ui::Icon::Star, lp.preset.name.c_str(), grid.next()))
            applyToSelection(ctx, lp.preset.effects);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string list;
            for (const Effect& e : lp.preset.effects) list += (list.empty() ? "" : " + ") + std::string(effectLabel(e.type));
            ImGui::SetTooltip("%s\n%s", lp.preset.name.c_str(), list.c_str());
        }
        if (!lp.file.empty() && ImGui::BeginPopupContextItem("##fxpresetctx")) {
            if (ImGui::MenuItem("Delete Preset")) toRemove = lp.file;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };

    if (!search_.empty()) { // flat results
        TileGrid grid;
        int found = 0;
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t) {
            const auto type = static_cast<EffectType>(t);
            if (!matches(effectLabel(type), search_) && !matches(effectCategory(type), search_)) continue;
            effectTile(type, grid);
            ++found;
        }
        for (const auto& lp : EffectLibrary::all())
            if (matches(lp.preset.name, search_)) presetTile(lp, grid), ++found;
        if (!found) faint("No effect matches your search.");
    } else if (category_.empty()) { // the categories
        TileGrid grid;
        for (const Category& c : kCategories) {
            if (ui::objectTile(c.name, c.icon, c.label, grid.next())) category_ = c.name;
            ui::tooltip(c.name);
        }
        if (ui::objectTile("##presets", ui::Icon::Star, kPresets, grid.next())) category_ = kPresets;
    } else { // one category
        if (ui::iconButton("##back", ui::Icon::Back, ImGui::GetFrameHeight(), "All categories")) category_.clear();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(category_.c_str());
        TileGrid grid;
        if (category_ == kPresets) {
            for (const auto& lp : EffectLibrary::all()) presetTile(lp, grid);
            if (ImGui::Button("Import Preset...")) {
                if (auto file = platform::openFileDialog("Import Effect Preset", "Effect preset", ".json")) {
                    message_.clear();
                    message_ = EffectLibrary::importFile(*file, &message_) ? "Imported." : "Import failed: " + message_;
                }
            }
        } else {
            for (int t = 0; t < static_cast<int>(EffectType::Count); ++t)
                if (category_ == effectCategory(static_cast<EffectType>(t))) effectTile(static_cast<EffectType>(t), grid);
        }
    }
    if (!toRemove.empty()) EffectLibrary::remove(toRemove);
    if (!message_.empty()) faint(message_.c_str());
}

} // namespace bemotion
