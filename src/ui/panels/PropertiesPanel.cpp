#include "ui/panels/PropertiesPanel.h"

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

namespace mirage {

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

bool beginPropertyTable(const char* id) {
    if (!ImGui::BeginTable(id, 3, kTableFlags)) return false;
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 0.8f);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
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

bool propertyRow(EditorContext& ctx, const PropertyRef& prop) {
    bool changed = false;
    const double t = ctx.time;
    AnimatedBase& anim = *prop.anim;
    ImGui::PushID(prop.id);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    const int keyHere = anim.findKey(t);
    if (ui::keyframeButton("##key", keyHere >= 0, anim.isAnimated())) {
        if (keyHere >= 0) anim.removeKey(keyHere);
        else anim.addKeyAt(t);
        ctx.touch();
        changed = true;
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(anim.isAnimated() ? theme::kText : theme::kTextDim), "%s", prop.label);

    ImGui::TableSetColumnIndex(2);
    ImGui::SetNextItemWidth(-FLT_MIN);
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
        ImGui::EndPopup();
    }

    if (changed) ctx.touch();
    ImGui::PopID();
    return changed;
}

void PropertiesPanel::draw(EditorContext& ctx) {
    if (!ImGui::Begin(kWindowName)) {
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
        if (groupOpen && tableOpen) propertyRow(ctx, prop);
    }
    closeGroup();

    if (ImGui::CollapsingHeader("Layer", ImGuiTreeNodeFlags_DefaultOpen) && beginPropertyTable("layer")) {
        const double frame = ctx.comp().frameDuration();
        plainRowLabel("In");
        float in = static_cast<float>(layer.inPoint);
        if (ImGui::DragFloat("##in", &in, 0.01f, -3600.0f, static_cast<float>(layer.outPoint - frame), "%.2f s")) {
            layer.inPoint = ctx.comp().snapToFrame(std::max(0.0, static_cast<double>(in)));
            ctx.touch();
        }
        plainRowLabel("Out");
        float out = static_cast<float>(layer.outPoint);
        if (ImGui::DragFloat("##out", &out, 0.01f, static_cast<float>(layer.inPoint + frame), 3600.0f, "%.2f s")) {
            layer.outPoint = std::max(layer.inPoint + frame, static_cast<double>(out));
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
    ImGui::TextColored(theme::vec(theme::kTextFaint), "Adds keyframes at the playhead.");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    int column = 0;
    for (int i = 0; i < static_cast<int>(Preset::Count); ++i) {
        const auto preset = static_cast<Preset>(i);
        if (!presetAppliesTo(preset, layer)) continue;
        if (column++ % 2 == 1) ImGui::SameLine();
        if (ImGui::Button(presetLabel(preset), ImVec2(w, 0))) {
            applyPreset(preset, layer, ctx.time);
            ctx.touch();
        }
    }
}

} // namespace mirage
