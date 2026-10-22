#include "ui/panels/PropertiesPanel.h"

#include "app/Platform.h"
#include "app/PresetLibrary.h"
#include "core/Presets.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace bemotion {

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

bool beginPropertyTable(const char* id) {
    if (!ImGui::BeginTable(id, 4, kTableFlags)) return false;
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 0.8f);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    return true;
}

// A non-animatable row (label + widget) laid out like propertyRow.
void plainRowLabel(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(theme::kTextDim), "%s", label);
    ImGui::TableSetColumnIndex(2);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void jumpToKey(EditorContext& ctx, const AnimatedBase& anim, int direction) {
    double best = direction > 0 ? DBL_MAX : -DBL_MAX;
    for (int i = 0; i < anim.keyCount(); ++i) {
        const double kt = anim.keyTime(i);
        if (direction > 0 && kt > ctx.time + kTimeEpsilon) best = std::min(best, kt);
        if (direction < 0 && kt < ctx.time - kTimeEpsilon) best = std::max(best, kt);
    }
    if (best != DBL_MAX && best != -DBL_MAX) ctx.setTime(best);
}

} // namespace

bool propertyValueWidget(EditorContext& ctx, const PropertyRef& prop, float width) {
    const double t = ctx.time;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    const ImGuiSliderFlags clampFlags = prop.min < prop.max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
    switch (prop.kind) {
    case PropertyKind::Float: {
        float v = prop.asFloat().valueAt(t);
        if (ImGui::DragFloat("##v", &v, prop.speed, prop.min, prop.max, prop.format, clampFlags)) {
            prop.asFloat().set(t, v);
            changed = true;
        }
        break;
    }
    case PropertyKind::Vec2: {
        Vec2 v = prop.asVec2().valueAt(t);
        if (ImGui::DragFloat2("##v", &v.x, prop.speed, prop.min, prop.max, prop.format, clampFlags)) {
            prop.asVec2().set(t, v);
            changed = true;
        }
        break;
    }
    case PropertyKind::Color: {
        Color v = prop.asColor().valueAt(t);
        if (ImGui::ColorEdit4("##v", &v.r, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex)) {
            prop.asColor().set(t, v);
            changed = true;
        }
        break;
    }
    }
    if (changed) ctx.touch();
    return changed;
}

bool keyframeToggle(EditorContext& ctx, const Layer& layer, const PropertyRef& prop) {
    AnimatedBase& anim = *prop.anim;
    const int keyHere = anim.findKey(ctx.time);
    // Keyframes only make sense where the clip exists.
    const bool inClip = ctx.time >= layer.inPoint - kTimeEpsilon && ctx.time <= layer.outPoint + kTimeEpsilon;
    ImGui::BeginDisabled(!inClip && keyHere < 0);
    const bool pressed = ui::keyframeButton("##key", keyHere >= 0, anim.isAnimated());
    ImGui::EndDisabled();
    if (!inClip && keyHere < 0) ui::tooltip("The playhead is outside this clip");
    if (!pressed) return false;
    if (keyHere >= 0) anim.removeKey(keyHere);
    else anim.addKeyAt(ctx.time);
    ctx.touch();
    return true;
}

bool resetButton(EditorContext& ctx, const Layer& layer, const PropertyRef& prop) {
    const std::vector<float> def = defaultPropertyValue(layer, prop.id, ctx.comp().center());
    if (def.empty()) return false;
    const std::vector<float> cur = propertyValue(prop, ctx.time);
    bool differs = false;
    for (size_t i = 0; i < def.size() && i < cur.size(); ++i) differs |= std::fabs(def[i] - cur[i]) > 1e-4f;
    ImGui::BeginDisabled(!differs);
    const bool pressed = ui::iconButton("##reset", ui::Icon::Reset, ImGui::GetFrameHeight(), "Reset to the original value");
    ImGui::EndDisabled();
    if (!pressed) return false;
    setPropertyValue(prop, ctx.time, def);
    ctx.touch();
    return true;
}

bool propertyRow(EditorContext& ctx, Layer& layer, const PropertyRef& prop) {
    bool changed = false;
    const double t = ctx.time;
    AnimatedBase& anim = *prop.anim;
    ImGui::PushID(prop.id);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    changed |= keyframeToggle(ctx, layer, prop);

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(anim.isAnimated() ? theme::kText : theme::kTextDim), "%s", prop.label);

    ImGui::TableSetColumnIndex(2);
    changed |= propertyValueWidget(ctx, prop, -FLT_MIN);

    const int keyHere = anim.findKey(t);
    if (ImGui::BeginPopupContextItem("##ctx")) {
        if (ImGui::MenuItem(keyHere >= 0 ? "Remove Keyframe" : "Add Keyframe")) {
            if (keyHere >= 0) anim.removeKey(keyHere);
            else anim.addKeyAt(t);
            changed = true;
        }
        if (ImGui::MenuItem("Previous Keyframe", "J", false, anim.isAnimated())) jumpToKey(ctx, anim, -1);
        if (ImGui::MenuItem("Next Keyframe", "K", false, anim.isAnimated())) jumpToKey(ctx, anim, +1);
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Animation", nullptr, false, anim.isAnimated())) {
            anim.clearKeys(t);
            changed = true;
        }
        if (ImGui::MenuItem("Reset and Clear Animation")) {
            anim.clearKeys(t);
            setPropertyValue(prop, t, defaultPropertyValue(layer, prop.id, ctx.comp().center()));
            changed = true;
        }
        ImGui::EndPopup();
    }

    ImGui::TableSetColumnIndex(3);
    changed |= resetButton(ctx, layer, prop);

    if (changed) ctx.touch();
    ImGui::PopID();
    return changed;
}

void PropertiesPanel::draw(EditorContext& ctx) {
    if (!open) return;
    ui::nextPanelWindowClass();
    if (!ImGui::Begin(kWindowName, &open)) {
        ImGui::End();
        return;
    }
    if (Layer* layer = ctx.selected()) drawLayer(ctx, *layer);
    else drawComposition(ctx);
    ImGui::End();
}

void PropertiesPanel::drawComposition(EditorContext& ctx) {
    Composition& c = ctx.comp();
    ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 1.1f);
    ImGui::TextUnformatted("Composition");
    ImGui::PopFont();
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Select a layer to edit it.");
    ImGui::Dummy(ImVec2(0, 4));

    if (!beginPropertyTable("comp")) return;
    plainRowLabel("Width");
    if (ImGui::InputInt("##w", &c.width, 0)) { c.width = std::clamp(c.width, 16, 16384); ctx.touch(); }
    plainRowLabel("Height");
    if (ImGui::InputInt("##h", &c.height, 0)) { c.height = std::clamp(c.height, 16, 16384); ctx.touch(); }
    plainRowLabel("Frame Rate");
    float fps = static_cast<float>(c.fps);
    if (ImGui::DragFloat("##fps", &fps, 0.1f, 1.0f, 240.0f, "%.2f fps", ImGuiSliderFlags_AlwaysClamp)) {
        c.fps = fps;
        ctx.setTime(ctx.time);
        ctx.touch();
    }
    plainRowLabel("Duration");
    float dur = static_cast<float>(c.duration);
    if (ImGui::DragFloat("##dur", &dur, 0.05f, 0.1f, 3600.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
        c.duration = dur;
        ctx.setTime(ctx.time);
        ctx.touch();
    }
    plainRowLabel("Background");
    if (ImGui::ColorEdit3("##bg", &c.background.r, ImGuiColorEditFlags_DisplayHex)) ctx.touch();
    ImGui::EndTable();
}

void PropertiesPanel::drawLayer(EditorContext& ctx, Layer& layer) {
    // Header: type icon + editable name.
    {
        const float h = ImGui::GetFrameHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(h, h));
        ui::drawIcon(ImGui::GetWindowDrawList(), ui::iconForLayer(layer), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.6f,
                     theme::kAccent);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", &layer.name)) ctx.touch();
    }
    if (ctx.selection.size() > 1) {
        ImGui::TextColored(theme::vec(theme::kAccent), "%d layers selected", static_cast<int>(ctx.selection.size()));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
        ImGui::TextWrapped("Properties below belong to this layer. Quick animations apply to all selected layers.");
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 2));

    auto props = layer.properties();
    const char* currentGroup = nullptr;
    bool groupOpen = false;
    bool tableOpen = false;

    auto closeGroup = [&] {
        if (tableOpen) ImGui::EndTable();
        tableOpen = false;
    };

    for (const PropertyRef& prop : props) {
        if (!currentGroup || std::strcmp(currentGroup, prop.group) != 0) {
            closeGroup();
            currentGroup = prop.group;
            groupOpen = ImGui::CollapsingHeader(prop.group, ImGuiTreeNodeFlags_DefaultOpen);
            if (!groupOpen) continue;
            tableOpen = beginPropertyTable(prop.group);
            if (!tableOpen) continue;

            // Group-specific, non-animatable settings.
            const std::string group = prop.group;
            if (group == "Shape") {
                plainRowLabel("Type");
                int kind = static_cast<int>(layer.shape.kind);
                if (ImGui::BeginCombo("##kind", shapeKindLabel(layer.shape.kind))) {
                    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i)
                        if (ImGui::Selectable(shapeKindLabel(static_cast<ShapeKind>(i)), i == kind)) {
                            layer.shape.kind = static_cast<ShapeKind>(i);
                            ctx.touch();
                        }
                    ImGui::EndCombo();
                }
            } else if (group == "Fill") {
                plainRowLabel("Enabled");
                if (ImGui::Checkbox("##fill", &layer.shape.fillEnabled)) ctx.touch();
            } else if (group == "Stroke") {
                plainRowLabel("Enabled");
                if (ImGui::Checkbox("##stroke", &layer.shape.strokeEnabled)) ctx.touch();
            } else if (group == "Text") {
                plainRowLabel("Content");
                if (ImGui::InputTextMultiline("##text", &layer.text.text,
                                              ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.6f)))
                    ctx.touch();
                plainRowLabel("Weight");
                if (ImGui::BeginCombo("##weight", fontWeightLabel(layer.text.weight))) {
                    for (int i = 0; i < static_cast<int>(FontWeight::Count); ++i) {
                        const auto w = static_cast<FontWeight>(i);
                        ImGui::PushFont(Fonts::forWeight(w), 0.0f);
                        if (ImGui::Selectable(fontWeightLabel(w), w == layer.text.weight)) {
                            layer.text.weight = w;
                            ctx.touch();
                        }
                        ImGui::PopFont();
                    }
                    ImGui::EndCombo();
                }
                plainRowLabel("Align");
                static const char* kAlign[] = {"Left", "Center", "Right"};
                const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
                for (int i = 0; i < 3; ++i) {
                    if (i) ImGui::SameLine();
                    const bool on = static_cast<int>(layer.text.align) == i;
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccentSoft);
                    if (ImGui::Button(kAlign[i], ImVec2(w, 0))) {
                        layer.text.align = static_cast<TextAlign>(i);
                        ctx.touch();
                    }
                    if (on) ImGui::PopStyleColor();
                }
            }
        }
        if (groupOpen && tableOpen) propertyRow(ctx, layer, prop);
    }
    closeGroup();

    if (ImGui::CollapsingHeader("Layer", ImGuiTreeNodeFlags_DefaultOpen) && beginPropertyTable("layer")) {
        // In/out stay inside the composition, between neighboring clips, at least one frame long.
        const Composition& comp = ctx.comp();
        const double frame = comp.frameDuration();
        double minIn = 0, maxOut = 0;
        comp.clipLimits(layer.id, minIn, maxOut);
        plainRowLabel("In");
        float in = static_cast<float>(layer.inPoint);
        if (ImGui::DragFloat("##in", &in, 0.01f, static_cast<float>(minIn), static_cast<float>(layer.outPoint - frame),
                             "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
            layer.inPoint = std::clamp(comp.snapToFrame(in), minIn, layer.outPoint - frame);
            ctx.touch();
        }
        plainRowLabel("Out");
        float out = static_cast<float>(layer.outPoint);
        if (ImGui::DragFloat("##out", &out, 0.01f, static_cast<float>(layer.inPoint + frame), static_cast<float>(maxOut),
                             "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
            layer.outPoint = std::clamp(static_cast<double>(out), layer.inPoint + frame, maxOut);
            ctx.touch();
        }
        plainRowLabel("Label");
        for (int i = 0; i < kLabelColorCount; ++i) {
            if (i) ImGui::SameLine(0, 4);
            const Color lc = labelColor(i);
            ImGui::PushID(i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float s = ImGui::GetFrameHeight() * 0.7f;
            if (ImGui::InvisibleButton("##label", ImVec2(s, s))) {
                layer.label = i;
                ctx.touch();
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), ImGui::ColorConvertFloat4ToU32(ImVec4(lc.r, lc.g, lc.b, 1)), 4);
            if (layer.label == i) dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + s + 2, p.y + s + 2), theme::kText, 5);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    drawPresets(ctx, layer);
}

void PropertiesPanel::drawPresets(EditorContext& ctx, Layer& layer) {
    if (!ImGui::CollapsingHeader("Quick Animations", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Adds keyframes at the playhead (replacing overlapping ones).");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    // Built-in categories first, then whatever imported presets use.
    std::vector<std::string> categories = {"Entrance", "Emphasis", "Exit"};
    for (const auto& lp : PresetLibrary::all())
        if (std::find(categories.begin(), categories.end(), lp.preset.category) == categories.end())
            categories.push_back(lp.preset.category);

    std::filesystem::path toRemove;
    for (const std::string& category : categories) {
        int column = 0;
        for (const auto& lp : PresetLibrary::all()) {
            if (lp.preset.category != category || !presetAppliesTo(lp.preset, layer)) continue;
            if (column == 0) ui::sectionLabel(category.c_str());
            if (column++ % 2 == 1) ImGui::SameLine();
            ImGui::PushID(&lp);
            if (ImGui::Button(lp.preset.name.c_str(), ImVec2(w, 0))) {
                for (Layer* l : ctx.selectedLayers())
                    if (presetAppliesTo(lp.preset, *l)) applyPreset(lp.preset, *l, ctx.time);
                ctx.touch();
            }
            if (!lp.file.empty() && ImGui::BeginPopupContextItem("##presetctx")) {
                if (ImGui::MenuItem("Show File")) platform::revealInFileManager(lp.file);
                if (ImGui::MenuItem("Delete Preset")) toRemove = lp.file;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    if (!toRemove.empty()) PresetLibrary::remove(toRemove);

    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Import...", ImVec2(w, 0))) {
        if (auto file = platform::openFileDialog("Import Quick Animation", "Quick Animation preset", ".json")) {
            presetMessage_.clear();
            if (!PresetLibrary::importFile(*file, &presetMessage_)) presetMessage_ = "Import failed: " + presetMessage_;
        }
    }
    ui::tooltip("Add a Quick Animation from a .json preset file");
    ImGui::SameLine();
    if (ImGui::Button("Save as Preset...", ImVec2(w, 0))) {
        saveName_ = layer.name + " animation";
        ImGui::OpenPopup("Save Quick Animation");
    }
    ui::tooltip("Turn this layer's keyframes into a reusable Quick Animation");
    if (!presetMessage_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(theme::kTextDim), "%s", presetMessage_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (ImGui::BeginPopup("Save Quick Animation")) {
        ImGui::TextUnformatted("Preset name");
        ImGui::SetNextItemWidth(260);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##presetName", &saveName_, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ui::accentButton("Save", ImVec2(120, 0)) || enter) && !saveName_.empty()) {
            AnimationPreset preset = presetFromLayer(layer, layer.inPoint, layer.outPoint, saveName_);
            if (preset.tracks.empty()) presetMessage_ = "This layer has no keyframes to save.";
            else if (PresetLibrary::save(preset, &presetMessage_))
                presetMessage_ = "Saved \"" + saveName_ + "\" to Quick Animations (Custom).";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace bemotion
