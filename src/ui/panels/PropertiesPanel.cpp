#include "ui/panels/PropertiesPanel.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/EffectLibrary.h"
#include "app/PresetLibrary.h"
#include "core/Presets.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

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

// Non-animatable plugin parameters (toggles, choices, text) of one group.
void pluginParamRows(EditorContext& ctx, Layer& layer, const std::string& group) {
    for (PluginParam& p : layer.plugin.params) {
        if (p.group != group || p.kind == PluginParamKind::Number || p.kind == PluginParamKind::Color) continue;
        ImGui::PushID(p.id.c_str());
        plainRowLabel(p.label.c_str());
        switch (p.kind) {
        case PluginParamKind::Toggle:
            if (ImGui::Checkbox("##v", &p.toggle)) ctx.touch();
            break;
        case PluginParamKind::Choice:
            if (ImGui::BeginCombo("##v", p.choice < static_cast<int>(p.choices.size()) ? p.choices[p.choice].c_str() : "")) {
                for (int i = 0; i < static_cast<int>(p.choices.size()); ++i)
                    if (ImGui::Selectable(p.choices[i].c_str(), i == p.choice)) {
                        p.choice = i;
                        ctx.touch();
                    }
                ImGui::EndCombo();
            }
            break;
        case PluginParamKind::Text:
            if (ImGui::InputText("##v", &p.text)) ctx.touch();
            break;
        default: break;
        }
        ImGui::PopID();
    }
}

bool hasAnimatedParams(const Layer& layer, const std::string& group) {
    return std::any_of(layer.plugin.params.begin(), layer.plugin.params.end(), [&](const PluginParam& p) {
        return p.group == group && (p.kind == PluginParamKind::Number || p.kind == PluginParamKind::Color);
    });
}

std::string colorHex(Color c) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

// "label, value[, #RRGGBB]" per line; tabs (spreadsheet paste) and semicolons work too.
std::vector<DataRow> parseRows(const std::string& text) {
    std::vector<DataRow> rows;
    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        for (char& c : line)
            if (c == '\t' || c == ';') c = ',';
        std::vector<std::string> cells;
        std::stringstream ls(line);
        std::string cell;
        while (std::getline(ls, cell, ',')) {
            const size_t a = cell.find_first_not_of(" \r\"");
            const size_t b = cell.find_last_not_of(" \r\"");
            cells.push_back(a == std::string::npos ? "" : cell.substr(a, b - a + 1));
        }
        if (cells.size() < 2) continue;
        char* end = nullptr;
        const float v = std::strtof(cells[1].c_str(), &end);
        if (end == cells[1].c_str()) continue; // header line
        DataRow r;
        r.label = cells[0];
        r.value = v;
        r.color = labelColor(static_cast<int>(rows.size()));
        if (cells.size() > 2 && cells[2].size() == 7 && cells[2][0] == '#')
            r.color = Color::fromHex(static_cast<unsigned>(std::strtoul(cells[2].c_str() + 1, nullptr, 16)));
        rows.push_back(std::move(r));
    }
    return rows;
}

bool containsNoCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
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
    if (ImGui::InputInt("##w", &c.width, 0)) { c.width = std::clamp(c.width, 16, 16384); c.syncCameras(); ctx.touch(); }
    plainRowLabel("Height");
    if (ImGui::InputInt("##h", &c.height, 0)) { c.height = std::clamp(c.height, 16, 16384); c.syncCameras(); ctx.touch(); }
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
        if (prop.effectIndex >= 0) continue; // shown in the Effects section
        if (!currentGroup || std::strcmp(currentGroup, prop.group) != 0) {
            closeGroup();
            currentGroup = prop.group;
            groupOpen = ImGui::CollapsingHeader(prop.group, ImGuiTreeNodeFlags_DefaultOpen);
            if (!groupOpen) continue;
            tableOpen = beginPropertyTable(prop.group);
            if (!tableOpen) continue;

            // Group-specific, non-animatable settings.
            const std::string group = prop.group;
            if (layer.type == LayerType::Plugin) {
                pluginParamRows(ctx, layer, group);
            } else if (group == "Shape") {
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
            } else if (group == "Text" && layer.type == LayerType::Text) {
                plainRowLabel("Content");
                if (ImGui::InputTextMultiline("##text", &layer.text.text,
                                              ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.6f)))
                    ctx.touch();
                plainRowLabel("Font");
                const bool missing = !Fonts::systemScanReady() ? false : !Fonts::hasFamily(layer.text.fontFamily);
                if (ImGui::BeginCombo("##font", layer.text.fontFamily.c_str(), ImGuiComboFlags_HeightLarge)) {
                    if (ImGui::IsWindowAppearing()) {
                        fontFilter_.clear();
                        ImGui::SetKeyboardFocusHere();
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##fontFilter", "Search fonts", &fontFilter_);
                    if (!Fonts::systemScanReady()) ImGui::TextColored(theme::vec(theme::kTextFaint), "Looking for installed fonts...");
                    const auto families = Fonts::families();
                    std::vector<const std::string*> shown;
                    for (const auto& f : families)
                        if (containsNoCase(f, fontFilter_)) shown.push_back(&f);
                    ImGuiListClipper clipper; // hundreds of families: only lay out the visible ones
                    clipper.Begin(static_cast<int>(shown.size()));
                    while (clipper.Step())
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                            if (ImGui::Selectable(shown[i]->c_str(), *shown[i] == layer.text.fontFamily)) {
                                for (Layer* l : ctx.selectedLayers()) {
                                    if (l->type != LayerType::Text) continue;
                                    l->text.fontFamily = *shown[i];
                                    const auto styles = Fonts::stylesOf(*shown[i]);
                                    if (std::find(styles.begin(), styles.end(), l->text.fontStyle) == styles.end() &&
                                        !styles.empty())
                                        l->text.fontStyle = styles.front();
                                }
                                ctx.touch();
                            }
                    ImGui::EndCombo();
                }
                if (missing) ui::tooltip("This font is not installed here: Inter is used instead.");
                plainRowLabel("Style");
                if (ImGui::BeginCombo("##style", layer.text.fontStyle.c_str())) {
                    for (const auto& s : Fonts::stylesOf(layer.text.fontFamily))
                        if (ImGui::Selectable(s.c_str(), s == layer.text.fontStyle)) {
                            for (Layer* l : ctx.selectedLayers())
                                if (l->type == LayerType::Text && l->text.fontFamily == layer.text.fontFamily)
                                    l->text.fontStyle = s;
                            ctx.touch();
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

    if (layer.type == LayerType::Plugin) {
        // Groups made only of toggles / choices / text never came up above.
        std::vector<std::string> done;
        for (const PluginParam& p : layer.plugin.params) {
            if (hasAnimatedParams(layer, p.group) || std::find(done.begin(), done.end(), p.group) != done.end()) continue;
            done.push_back(p.group);
            if (ImGui::CollapsingHeader(p.group.c_str(), ImGuiTreeNodeFlags_DefaultOpen) && beginPropertyTable(p.group.c_str())) {
                pluginParamRows(ctx, layer, p.group);
                ImGui::EndTable();
            }
        }
        if (layer.plugin.hasRows) drawPluginData(ctx, layer);
        if (!PluginHost::get().findType(layer.plugin.objectType)) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
            ImGui::TextWrapped("The plugin for this object (%s) is not installed or failed to load.",
                               layer.plugin.objectType.c_str());
            ImGui::PopStyleColor();
        }
    }

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

    if (layer.type == LayerType::Camera) return; // cameras are not drawn: no effects or animation presets
    drawEffects(ctx, layer);
    drawPresets(ctx, layer);
}

void PropertiesPanel::drawPluginData(EditorContext& ctx, Layer& layer) {
    if (!ImGui::CollapsingHeader("Data", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& rows = layer.plugin.rows;
    int remove = -1, moveUp = -1;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("##rows", 4, flags)) {
        ImGui::TableSetupColumn("color", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("tools", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 2 + 4);
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            DataRow& r = rows[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::ColorEdit4("##c", &r.color.r, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                                         ImGuiColorEditFlags_AlphaPreviewHalf))
                ctx.touch();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##l", "Label", &r.label)) ctx.touch();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##v", &r.value, 0.5f, 0, 0, "%g")) ctx.touch();
            ImGui::TableNextColumn();
            if (ui::iconButton("##up", ui::Icon::ChevronUp, ImGui::GetFrameHeight(), "Move up") && i > 0) moveUp = i;
            ImGui::SameLine(0, 4);
            if (ui::iconButton("##del", ui::Icon::Trash, ImGui::GetFrameHeight(), "Remove row")) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (moveUp > 0) {
        std::swap(rows[moveUp], rows[moveUp - 1]);
        ctx.touch();
    }
    if (remove >= 0) {
        rows.erase(rows.begin() + remove);
        ctx.touch();
    }
    if (ImGui::Button("+ Add Row")) {
        DataRow r;
        r.label = "Item " + std::to_string(rows.size() + 1);
        r.value = rows.empty() ? 50.0f : rows.back().value;
        r.color = labelColor(static_cast<int>(rows.size()));
        rows.push_back(std::move(r));
        ctx.touch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste")) {
        const char* clip = ImGui::GetClipboardText();
        auto pasted = parseRows(clip ? clip : "");
        if (!pasted.empty()) {
            rows = std::move(pasted);
            ctx.touch();
        }
    }
    ui::tooltip("Replace the data with rows from the clipboard: \"label, value[, #color]\" per line.\n"
                "Cells copied from a spreadsheet work too.");
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        std::string text;
        for (const DataRow& r : rows) {
            char value[32];
            std::snprintf(value, sizeof value, "%g", r.value);
            text += r.label + ", " + value + ", " + colorHex(r.color) + "\n";
        }
        ImGui::SetClipboardText(text.c_str());
    }
    ui::tooltip("Copy the data as CSV");
}

void PropertiesPanel::drawEffects(EditorContext& ctx, Layer& layer) {
    if (!ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) return;
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
        const bool open = ImGui::TreeNodeEx("##fx", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                                            e.group.c_str());
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
            if (beginPropertyTable("fxprops")) {
                for (const PropertyRef& p : props)
                    if (p.effectIndex == i) propertyRow(ctx, layer, p);
                if (e.type == EffectType::MotionBlur) {
                    plainRowLabel("Samples");
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
    if (layer.effects.empty())
        ImGui::TextColored(theme::vec(theme::kTextFaint), "Outline, glow, shadow and motion blur.");

    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("+ Add Effect", ImVec2(w, 0))) ImGui::OpenPopup("##addEffect");
    ImGui::SameLine();
    ImGui::BeginDisabled(layer.effects.empty());
    if (ImGui::Button("Save as Preset##fx", ImVec2(w, 0))) {
        effectSaveName_ = layer.name + " effects";
        ImGui::OpenPopup("Save Effect Preset");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopup("##addEffect")) {
        for (int t = 0; t < static_cast<int>(EffectType::Count); ++t)
            if (ImGui::MenuItem(effectLabel(static_cast<EffectType>(t)))) {
                for (Layer* l : ctx.selectedLayers()) l->effects.push_back(makeEffect(static_cast<EffectType>(t)));
                ctx.touch();
            }
        ImGui::Separator();
        ui::sectionLabel("PRESETS");
        std::filesystem::path toRemove;
        for (const auto& lp : EffectLibrary::all()) {
            ImGui::PushID(&lp);
            if (ImGui::MenuItem(lp.preset.name.c_str())) {
                for (Layer* l : ctx.selectedLayers())
                    l->effects.insert(l->effects.end(), lp.preset.effects.begin(), lp.preset.effects.end());
                ctx.touch();
            }
            if (!lp.file.empty() && ImGui::BeginPopupContextItem("##fxpresetctx")) {
                if (ImGui::MenuItem("Delete Preset")) toRemove = lp.file;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!toRemove.empty()) EffectLibrary::remove(toRemove);
        ImGui::Separator();
        if (ImGui::MenuItem("Import Preset...")) {
            if (auto file = platform::openFileDialog("Import Effect Preset", "Effect preset", ".json")) {
                effectMessage_.clear();
                if (!EffectLibrary::importFile(*file, &effectMessage_)) effectMessage_ = "Import failed: " + effectMessage_;
                else effectMessage_ = "Imported. Find it under + Add Effect > Presets.";
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Save Effect Preset")) {
        ImGui::TextUnformatted("Preset name");
        ImGui::SetNextItemWidth(260);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##fxPresetName", &effectSaveName_, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ui::accentButton("Save", ImVec2(120, 0)) || enter) && !effectSaveName_.empty()) {
            EffectPreset preset{effectSaveName_, layer.effects};
            if (EffectLibrary::save(preset, &effectMessage_)) effectMessage_ = "Saved \"" + effectSaveName_ + "\".";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!effectMessage_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(theme::kTextDim), "%s", effectMessage_.c_str());
        ImGui::PopTextWrapPos();
    }
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
