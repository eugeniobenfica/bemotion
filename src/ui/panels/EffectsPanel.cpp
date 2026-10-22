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

    ImGui::BeginDisabled(layer.effects.empty());
    if (ImGui::Button("Save Stack as Preset...")) {
        saveName_ = layer.name + " effects";
        ImGui::OpenPopup("Save Effect Preset");
    }
    ImGui::EndDisabled();
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

void EffectsPanel::drawBrowser(EditorContext& ctx) {
    ImGui::SeparatorText("Effects & Presets");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##fxsearch", "Search effects", &search_);
    const bool canApply = !ctx.selection.empty();
    if (!canApply) faint("Select layers, then click an effect to apply it.");

    // Categories in After Effects' order.
    static const char* kCategories[] = {"Blur & Sharpen", "Channel",     "Color Correction", "Distort",
                                        "Generate",       "Perspective", "Stylize",          "Time"};
    for (const char* category : kCategories) {
        std::vector<EffectType> types;
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t) {
            const auto type = static_cast<EffectType>(t);
            if (std::string(effectCategory(type)) == category && (matches(effectLabel(type), search_) || matches(category, search_)))
                types.push_back(type);
        }
        if (types.empty()) continue;
        if (!search_.empty()) ImGui::SetNextItemOpen(true);
        if (!ImGui::TreeNodeEx(category, ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
        for (EffectType type : types) {
            ImGui::PushID(static_cast<int>(type));
            ImGui::BeginDisabled(!canApply);
            if (ImGui::Selectable(effectLabel(type))) applyToSelection(ctx, {makeEffect(type)});
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", effectDescription(type));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // Effect presets: built in and saved / imported by the user.
    std::vector<const LibraryEffectPreset*> presets;
    for (const auto& lp : EffectLibrary::all())
        if (matches(lp.preset.name, search_)) presets.push_back(&lp);
    if (!search_.empty() && !presets.empty()) ImGui::SetNextItemOpen(true);
    if (ImGui::TreeNodeEx("Effect Presets", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        std::filesystem::path toRemove;
        for (const auto* lp : presets) {
            ImGui::PushID(lp);
            ImGui::BeginDisabled(!canApply);
            if (ImGui::Selectable(lp->preset.name.c_str())) applyToSelection(ctx, lp->preset.effects);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                std::string list;
                for (const Effect& e : lp->preset.effects) list += (list.empty() ? "" : " + ") + std::string(effectLabel(e.type));
                ImGui::SetTooltip("%s", list.c_str());
            }
            if (!lp->file.empty() && ImGui::BeginPopupContextItem("##fxpresetctx")) {
                if (ImGui::MenuItem("Delete Preset")) toRemove = lp->file;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!toRemove.empty()) EffectLibrary::remove(toRemove);
        if (ImGui::SmallButton("Import Preset...")) {
            if (auto file = platform::openFileDialog("Import Effect Preset", "Effect preset", ".json")) {
                message_.clear();
                message_ = EffectLibrary::importFile(*file, &message_) ? "Imported. It is listed under Effect Presets."
                                                                       : "Import failed: " + message_;
            }
        }
        ImGui::TreePop();
    }
    if (!message_.empty()) faint(message_.c_str());
}

} // namespace bemotion
