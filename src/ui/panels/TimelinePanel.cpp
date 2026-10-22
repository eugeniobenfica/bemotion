#include "ui/panels/TimelinePanel.h"

#include "render/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bemotion {

namespace {

constexpr float kRulerHeight = 26.0f;
constexpr float kLayerRowHeight = 30.0f;
constexpr float kPropRowHeight = 24.0f;
constexpr float kEdgeGrab = 6.0f;
constexpr float kKeyRadius = 5.0f;

ImU32 toU32(Color c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alpha)); }

double snapDelta(double seconds, double fps) { return std::round(seconds * fps) / fps; }

// Keyframe glyph: the shape hints at the easing of the outgoing segment.
void drawKeyGlyph(ImDrawList* dl, ImVec2 c, float r, Easing easing, ImU32 fill, ImU32 outline) {
    if (easing == Easing::Linear) {
        ui::drawDiamond(dl, c, r, fill, outline);
    } else if (easing == Easing::Hold) {
        const ImVec2 a(c.x - r * 0.8f, c.y - r * 0.8f), b(c.x + r * 0.8f, c.y + r * 0.8f);
        dl->AddRectFilled(a, b, fill, 1.5f);
        dl->AddRect(a, b, outline, 1.5f, 0, 1.3f);
    } else {
        dl->AddCircleFilled(c, r * 0.85f, fill);
        dl->AddCircle(c, r * 0.85f, outline, 0, 1.3f);
    }
}

std::string formatValue(const PropertyRef& p, double t) {
    char buf[64];
    switch (p.kind) {
    case PropertyKind::Float: std::snprintf(buf, sizeof(buf), "%.1f", p.asFloat().valueAt(t)); break;
    case PropertyKind::Vec2: {
        const Vec2 v = p.asVec2().valueAt(t);
        std::snprintf(buf, sizeof(buf), "%.1f, %.1f", v.x, v.y);
        break;
    }
    case PropertyKind::Color: return {};
    }
    return buf;
}

} // namespace

void TimelinePanel::draw(EditorContext& ctx) {
    if (!ImGui::Begin(kWindowName, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    Composition& comp = ctx.comp();
    drawToolbar(ctx);

    const ImVec2 area = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    leftWidth_ = std::clamp(leftWidth_, 220.0f, std::max(220.0f, avail.x * 0.5f));
    trackX0_ = area.x + leftWidth_;
    trackW_ = std::max(50.0f, avail.x - leftWidth_);

    const double minPx = trackW_ / (comp.duration * 4.0);
    if (pxPerSec_ <= 0.0) {
        pxPerSec_ = (trackW_ - 24.0) / comp.duration;
        scroll_ = 0.0;
    }
    pxPerSec_ = std::clamp(pxPerSec_, minPx, 4000.0);
    scroll_ = std::clamp(scroll_, 0.0, std::max(0.0, comp.duration - (trackW_ - 24.0) / pxPerSec_));

    drawRuler(ctx, area.x, area.y, avail.x);

    // --- body ----------------------------------------------------------------
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(ImVec2(area.x, area.y + kRulerHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
    const ImGuiWindowFlags bodyFlags = ImGuiWindowFlags_NoScrollbar | (io.KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0);
    ImGui::BeginChild("##body", ImVec2(avail.x, std::max(1.0f, avail.y - kRulerHeight)), ImGuiChildFlags_None, bodyFlags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    const float x0 = winPos.x;

    // Zoom (Ctrl+wheel) around the mouse, horizontal scroll (Shift+wheel).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            const double mouseT = xToTime(io.MousePos.x);
            pxPerSec_ = std::clamp(pxPerSec_ * std::pow(1.2, io.MouseWheel), minPx, 4000.0);
            scroll_ = std::max(0.0, mouseT - (io.MousePos.x - trackX0_) / pxPerSec_);
        } else if (io.KeyShift) {
            scroll_ = std::max(0.0, scroll_ - io.MouseWheel * 60.0 / pxPerSec_);
        }
    }

    rows_.clear();
    auto& layers = comp.layers;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        Layer& layer = layers[i];
        ImGui::PushID(static_cast<int>(layer.id));
        drawLayerRow(ctx, layer, i, x0, avail.x);
        if (expanded_.count(layer.id)) {
            for (const PropertyRef& prop : layer.properties()) {
                if (animatedOnly_ && !prop.anim->isAnimated()) continue;
                drawPropertyRow(ctx, layer, prop, x0, avail.x);
            }
        }
        ImGui::PopID();
    }

    // Empty space below the rows: click to deselect.
    const float y = ImGui::GetCursorScreenPos().y;
    rowsBottom_ = y;
    const float rest = std::max(kLayerRowHeight, winPos.y + winSize.y - y);
    ImGui::SetCursorScreenPos(ImVec2(x0, y));
    if (ImGui::InvisibleButton("##empty", ImVec2(avail.x, rest)) && !io.KeyCtrl && !io.KeyShift) {
        ctx.select(0);
        ctx.clearKeySelection();
    }
    if (layers.empty()) {
        const char* hint = "Add text or graphics from the Objects panel to start animating.";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(winPos.x + (winSize.x - ts.x) * 0.5f, y + 24), theme::kTextFaint, hint);
    }

    // Column divider, area past the end of the composition, playhead.
    const float bodyTop = winPos.y, bodyBottom = winPos.y + winSize.y;
    dl->AddLine(ImVec2(trackX0_ - 1, bodyTop), ImVec2(trackX0_ - 1, bodyBottom), theme::kBorder);
    dl->PushClipRect(ImVec2(trackX0_, bodyTop), ImVec2(trackX0_ + trackW_, bodyBottom), true);
    const float endX = timeToX(comp.duration);
    if (endX < trackX0_ + trackW_)
        dl->AddRectFilled(ImVec2(endX, bodyTop), ImVec2(trackX0_ + trackW_, bodyBottom), IM_COL32(0, 0, 0, 80));
    const float ph = timeToX(ctx.time);
    dl->AddLine(ImVec2(ph, bodyTop), ImVec2(ph, bodyBottom), theme::kAccent, 1.5f);

    // Razor tool: show where the cut will happen.
    const bool overTracks = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                            io.MousePos.x >= trackX0_ && io.MousePos.y < rowsBottom_;
    if (ctx.tool == Tool::Razor && overTracks) {
        const float cx = timeToX(comp.snapToFrame(xToTime(io.MousePos.x)));
        for (float yy = bodyTop; yy < rowsBottom_; yy += 8.0f)
            dl->AddLine(ImVec2(cx, yy), ImVec2(cx, std::min(yy + 4.0f, rowsBottom_)), theme::kDanger, 1.5f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    dl->PopClipRect();

    if (restacking_) drawDropIndicator(dl, x0, avail.x);

    ImGui::EndChild();
    ImGui::PopStyleVar();

    drawKeyContextMenu(ctx);
    ImGui::End();
}

void TimelinePanel::drawToolbar(EditorContext& ctx) {
    Composition& comp = ctx.comp();
    const float s = 26.0f;
    const float toolbarRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 4));
    if (ui::iconButton("##toolSelect", ui::Icon::Pointer, s, "Selection tool (V)", ctx.tool == Tool::Select))
        ctx.tool = Tool::Select;
    ImGui::SameLine();
    if (ui::iconButton("##toolRazor", ui::Icon::Razor, s, "Cut tool (C): click a layer to split it",
                       ctx.tool == Tool::Razor))
        ctx.tool = Tool::Razor;
    ImGui::SameLine();
    if (ui::iconButton("##split", ui::Icon::SplitPlayhead, s, "Split selected layers at the playhead (Ctrl+K)"))
        ctx.splitSelectedAtPlayhead();
    ImGui::SameLine(0, 14);
    if (ui::iconButton("##start", ui::Icon::ToStart, s, "Go to start (Home)")) {
        ctx.playing = false;
        ctx.setTime(0.0);
    }
    ImGui::SameLine();
    if (ui::iconButton("##prev", ui::Icon::StepBack, s, "Previous frame (Left)")) ctx.stepFrames(-1);
    ImGui::SameLine();
    if (ui::iconButton("##play", ctx.playing ? ui::Icon::Pause : ui::Icon::Play, s, "Play / Pause (Space)", true))
        ctx.playing = !ctx.playing;
    ImGui::SameLine();
    if (ui::iconButton("##next", ui::Icon::StepForward, s, "Next frame (Right)")) ctx.stepFrames(1);
    ImGui::SameLine();
    if (ui::iconButton("##end", ui::Icon::ToEnd, s, "Go to end (End)")) {
        ctx.playing = false;
        ctx.setTime(comp.duration);
    }
    ImGui::SameLine();
    if (ui::iconButton("##loop", ui::Icon::Loop, s, "Loop playback", ctx.loop)) ctx.loop = !ctx.loop;
    ImGui::PopStyleVar();

    ImGui::SameLine(0, 16);
    ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 1.15f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    ImGui::TextColored(theme::vec(theme::kAccent), "%s", ui::formatTimecode(ctx.time, comp.fps).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
    ImGui::TextColored(theme::vec(theme::kTextFaint), "frame %d / %d  ·  %.4g fps", comp.frameAt(ctx.time),
                       comp.frameCount() - 1, comp.fps);

    // Right-aligned controls.
    const float rightW = 330.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, toolbarRight - rightW));
    ImGui::Checkbox("Animated only", &animatedOnly_);
    ImGui::SameLine();
    float zoom = static_cast<float>(pxPerSec_);
    ImGui::SetNextItemWidth(110);
    if (ImGui::SliderFloat("##zoom", &zoom, 10.0f, 2000.0f, "Zoom", ImGuiSliderFlags_Logarithmic)) pxPerSec_ = zoom;
    ui::tooltip("Timeline zoom (Ctrl+Wheel)");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) pxPerSec_ = 0.0;
    ui::tooltip("Fit the whole composition");
}

void TimelinePanel::drawRuler(EditorContext& ctx, float x0, float y0, float width) {
    const Composition& comp = ctx.comp();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + width, y0 + kRulerHeight), theme::kBgDeep, 5.0f);
    ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 0.8f);
    dl->AddText(ImVec2(x0 + 12, y0 + 6), theme::kTextFaint, "LAYERS");
    ImGui::PopFont();

    // Scrub area.
    ImGui::SetCursorScreenPos(ImVec2(trackX0_, y0));
    ImGui::InvisibleButton("##ruler", ImVec2(trackW_, kRulerHeight));
    if (ImGui::IsItemActive()) {
        ctx.playing = false;
        ctx.setTime(xToTime(ImGui::GetIO().MousePos.x));
    }
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    dl->PushClipRect(ImVec2(trackX0_, y0), ImVec2(trackX0_ + trackW_, y0 + kRulerHeight), true);

    // Pick a tick spacing that keeps labels readable.
    const double fd = comp.frameDuration();
    const double candidates[] = {fd, fd * 2, fd * 5, fd * 10, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300};
    double major = candidates[std::size(candidates) - 1];
    for (double c : candidates)
        if (c * pxPerSec_ >= 64.0) {
            major = c;
            break;
        }
    const double minor = major / 5.0 >= fd * 0.999 && (major / 5.0) * pxPerSec_ >= 6.0 ? major / 5.0 : 0.0;

    const double tStart = std::floor(scroll_ / major) * major;
    const double tEnd = xToTime(trackX0_ + trackW_);
    for (double t = tStart; t <= tEnd + major; t += major) {
        const float x = timeToX(t);
        dl->AddLine(ImVec2(x, y0 + kRulerHeight - 10), ImVec2(x, y0 + kRulerHeight), theme::kTextFaint);
        char label[32];
        const double whole = std::round(t);
        if (std::fabs(t - whole) < 1e-6) std::snprintf(label, sizeof(label), "%.0fs", whole);
        else std::snprintf(label, sizeof(label), "%df", static_cast<int>(std::lround((t - std::floor(t)) * comp.fps)));
        dl->AddText(ImVec2(x + 4, y0 + 4), theme::kTextDim, label);
        if (minor > 0.0)
            for (int i = 1; i < 5; ++i) {
                const float mx = timeToX(t + minor * i);
                dl->AddLine(ImVec2(mx, y0 + kRulerHeight - 5), ImVec2(mx, y0 + kRulerHeight), theme::kBorder);
            }
    }

    const float endX = timeToX(comp.duration);
    dl->AddRectFilled(ImVec2(endX, y0), ImVec2(trackX0_ + trackW_, y0 + kRulerHeight), IM_COL32(0, 0, 0, 90));

    // Playhead handle.
    const float ph = timeToX(ctx.time);
    const ImVec2 head[5] = {ImVec2(ph - 6, y0 + 3), ImVec2(ph + 6, y0 + 3), ImVec2(ph + 6, y0 + 14), ImVec2(ph, y0 + 20),
                            ImVec2(ph - 6, y0 + 14)};
    dl->AddConvexPolyFilled(head, 5, theme::kAccent);
    dl->AddLine(ImVec2(ph, y0 + 18), ImVec2(ph, y0 + kRulerHeight), theme::kAccent, 1.5f);
    dl->PopClipRect();
}

void TimelinePanel::drawLayerRow(EditorContext& ctx, Layer& layer, int index, float x0, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float y = ImGui::GetCursorScreenPos().y;
    const float h = kLayerRowHeight;
    const bool selected = ctx.isSelected(layer.id);
    const bool primary = ctx.primaryId() == layer.id;
    rows_.push_back({index, y, y + h});

    if (selected)
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + h), IM_COL32(232, 185, 49, primary ? 24 : 14));
    dl->AddLine(ImVec2(x0, y + h - 1), ImVec2(x0 + width, y + h - 1), IM_COL32(255, 255, 255, 10));

    // --- left column: controls and name ---------------------------------------
    const float btn = 20.0f;
    const float by = y + (h - btn) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(x0 + 6, by));
    const bool isExpanded = expanded_.count(layer.id) > 0;
    if (ui::iconButton("##expand", isExpanded ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, btn, "Show properties")) {
        if (isExpanded) expanded_.erase(layer.id);
        else expanded_.insert(layer.id);
    }
    ImGui::SameLine();
    if (ui::iconButton("##vis", layer.visible ? ui::Icon::Eye : ui::Icon::EyeOff, btn,
                       layer.visible ? "Hide layer" : "Show layer")) {
        layer.visible = !layer.visible;
        ctx.touch();
    }
    ImGui::SameLine();
    if (ui::iconButton("##lock", layer.locked ? ui::Icon::Lock : ui::Icon::Unlock, btn,
                       layer.locked ? "Unlock layer" : "Lock layer", layer.locked)) {
        layer.locked = !layer.locked;
        ctx.touch();
    }
    ImGui::SameLine();
    const ImVec2 chip = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(chip.x + 2, y + 8), ImVec2(chip.x + 5, y + h - 8), toU32(labelColor(layer.label)), 2.0f);
    ui::drawIcon(dl, ui::iconForLayer(layer), ImVec2(chip.x + 18, y + h * 0.5f), 14.0f,
                 selected ? theme::kAccent : theme::kTextDim);

    const float nameX = chip.x + 32;
    const float nameW = std::max(10.0f, x0 + leftWidth_ - nameX - 8);
    ImGui::SetCursorScreenPos(ImVec2(nameX, y + (h - ImGui::GetFrameHeight()) * 0.5f));
    if (renaming_ == layer.id) {
        ImGui::SetNextItemWidth(nameW);
        if (focusRename_) {
            ImGui::SetKeyboardFocusHere();
            focusRename_ = false;
        }
        if (ImGui::InputText("##rename", &layer.name, ImGuiInputTextFlags_AutoSelectAll)) ctx.touch();
        if (ImGui::IsItemDeactivated()) renaming_ = 0;
    } else {
        ImGui::SetCursorScreenPos(ImVec2(nameX, y));
        ImGui::InvisibleButton("##name", ImVec2(nameW, h));
        if (ImGui::IsItemActivated()) {
            clickLayer(ctx, layer.id);
            if (!io.KeyCtrl && !io.KeyShift) beginLayerDrag(ctx, Drag::LayerName, layer.id);
        }
        if (ImGui::IsItemActive() && drag_ == Drag::LayerName) updateLayerDrag(ctx);
        if (ImGui::IsItemDeactivated() && drag_ == Drag::LayerName) endLayerDrag(ctx);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            renaming_ = layer.id;
            focusRename_ = true;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !ctx.isSelected(layer.id)) ctx.select(layer.id);
        if (ImGui::BeginPopupContextItem("##layerctx")) {
            if (ImGui::MenuItem("Rename")) {
                renaming_ = layer.id;
                focusRename_ = true;
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ctx.duplicateSelected();
            if (ImGui::MenuItem("Split at Playhead", "Ctrl+K")) ctx.splitSelectedAtPlayhead();
            ImGui::Separator();
            if (ImGui::MenuItem("Bring to Front", "Ctrl+Shift+]")) ctx.shiftSelected(-1000);
            if (ImGui::MenuItem("Bring Forward", "Ctrl+]")) ctx.shiftSelected(-1);
            if (ImGui::MenuItem("Send Backward", "Ctrl+[")) ctx.shiftSelected(1);
            if (ImGui::MenuItem("Send to Back", "Ctrl+Shift+[")) ctx.shiftSelected(1000);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) ctx.deleteSelected();
            ImGui::EndPopup();
        }
        const ImU32 col = layer.visible ? (selected ? theme::kText : theme::kTextDim) : theme::kTextFaint;
        dl->PushClipRect(ImVec2(nameX, y), ImVec2(nameX + nameW, y + h), true);
        dl->AddText(ImVec2(nameX, y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, layer.name.c_str());
        dl->PopClipRect();
    }

    // --- track: layer bar ------------------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2(trackX0_, y));
    ImGui::InvisibleButton("##track", ImVec2(trackW_, h));
    const bool hovered = ImGui::IsItemHovered();
    const float bx1 = timeToX(layer.inPoint), bx2 = timeToX(layer.outPoint);
    const float mx = io.MousePos.x;

    const bool onEdgeIn = std::fabs(mx - bx1) < kEdgeGrab, onEdgeOut = std::fabs(mx - bx2) < kEdgeGrab;
    const bool onBar = mx > bx1 && mx < bx2;

    if (ctx.tool == Tool::Razor) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && onBar) ctx.splitLayerAt(layer.id, xToTime(mx));
    } else {
        if (hovered && drag_ == Drag::None && (onEdgeIn || onEdgeOut)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) {
            const Drag mode = onEdgeIn ? Drag::LayerIn : onEdgeOut ? Drag::LayerOut : onBar ? Drag::LayerMove : Drag::None;
            if (mode != Drag::None) {
                clickLayer(ctx, layer.id);
                if (!io.KeyCtrl && !io.KeyShift) beginLayerDrag(ctx, mode, layer.id);
            } else if (!io.KeyCtrl && !io.KeyShift) {
                ctx.clearKeySelection();
            }
        }
        const bool barDrag = drag_ == Drag::LayerMove || drag_ == Drag::LayerIn || drag_ == Drag::LayerOut;
        if (ImGui::IsItemActive() && barDrag && dragLayer_ == layer.id) updateLayerDrag(ctx);
        if (ImGui::IsItemDeactivated() && barDrag && dragLayer_ == layer.id) endLayerDrag(ctx);
    }

    dl->PushClipRect(ImVec2(trackX0_, y), ImVec2(trackX0_ + trackW_, y + h), true);
    const Color lc = labelColor(layer.label);
    const ImVec2 a(bx1, y + 5), b(bx2, y + h - 5);
    dl->AddRectFilled(a, b, toU32(lc, layer.visible ? (selected ? 0.42f : 0.26f) : 0.1f), 5.0f);
    if (selected) dl->AddRect(a, b, primary ? theme::kText : toU32(lc, 0.9f), 5.0f, 0, primary ? 1.5f : 1.2f);
    dl->AddRectFilled(ImVec2(bx1 + 2, y + 9), ImVec2(bx1 + 4, y + h - 9), toU32(lc, 0.8f), 1.0f);
    dl->AddRectFilled(ImVec2(bx2 - 4, y + 9), ImVec2(bx2 - 2, y + h - 9), toU32(lc, 0.8f), 1.0f);
    dl->PushClipRect(ImVec2(std::max(bx1, trackX0_), y), ImVec2(std::min(bx2, trackX0_ + trackW_), y + h), true);
    dl->AddText(ImVec2(std::max(bx1, trackX0_) + 10, y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                IM_COL32(255, 255, 255, 150), layer.name.c_str());
    dl->PopClipRect();

    // Summary of all keyframe times on the layer.
    if (!isExpanded) {
        for (const PropertyRef& p : layer.properties())
            for (int k = 0; k < p.anim->keyCount(); ++k) {
                const double kt = p.anim->keyTime(k);
                if (kt < layer.inPoint - kTimeEpsilon || kt > layer.outPoint + kTimeEpsilon) continue; // e.g. after a cut
                ui::drawDiamond(dl, ImVec2(timeToX(kt), y + h - 7), 3.0f, IM_COL32(230, 230, 235, 200), 0);
            }
    }
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(x0, y + h));
    ImGui::Dummy(ImVec2(0, 0));
}

void TimelinePanel::drawPropertyRow(EditorContext& ctx, Layer& layer, const PropertyRef& prop, float x0, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float y = ImGui::GetCursorScreenPos().y;
    const float h = kPropRowHeight;
    AnimatedBase& anim = *prop.anim;
    ImGui::PushID(prop.id);

    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + h), IM_COL32(0, 0, 0, 40));

    // --- left: keyframe toggle, label, current value ---------------------------
    ImGui::SetCursorScreenPos(ImVec2(x0 + 36, y + (h - ImGui::GetFrameHeight()) * 0.5f));
    const int keyHere = anim.findKey(ctx.time);
    if (ui::keyframeButton("##key", keyHere >= 0, anim.isAnimated())) {
        if (keyHere >= 0) anim.removeKey(keyHere);
        else anim.addKeyAt(ctx.time);
        ctx.touch();
    }
    const float textY = y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    char label[96];
    std::snprintf(label, sizeof(label), "%s  %s", prop.group, prop.label);
    dl->AddText(ImVec2(x0 + 62, textY), anim.isAnimated() ? theme::kText : theme::kTextDim, label);
    const float valueRight = x0 + leftWidth_ - 12;
    if (prop.kind == PropertyKind::Color) {
        const Color c = prop.asColor().valueAt(ctx.time);
        dl->AddRectFilled(ImVec2(valueRight - 22, y + 6), ImVec2(valueRight, y + h - 6), toU32(c), 3.0f);
    } else {
        const std::string v = formatValue(prop, ctx.time);
        const float w = ImGui::CalcTextSize(v.c_str()).x;
        dl->AddText(ImVec2(valueRight - w, textY), theme::kTextFaint, v.c_str());
    }

    // --- track: keyframes ------------------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2(trackX0_, y));
    ImGui::InvisibleButton("##keys", ImVec2(trackW_, h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const float cy = y + h * 0.5f;

    int hoveredKey = -1;
    if (ImGui::IsItemHovered())
        for (int k = 0; k < anim.keyCount(); ++k)
            if (std::fabs(io.MousePos.x - timeToX(anim.keyTime(k))) <= kKeyRadius + 2) hoveredKey = k;

    if (ImGui::IsItemActivated()) {
        if (hoveredKey >= 0) {
            ctx.select(layer.id); // key selection is limited to one layer
            const KeySelection ks{layer.id, prop.id, anim.keyUid(hoveredKey)};
            const bool isSel = ctx.isKeySelected(ks);
            if (io.KeyShift || io.KeyCtrl) {
                if (isSel) std::erase(ctx.selectedKeys, ks);
                else ctx.selectedKeys.push_back(ks);
            } else if (!isSel) {
                ctx.selectedKeys = {ks};
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) openKeyMenu_ = true;
            else beginKeyDrag(ctx);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ctx.setTime(anim.keyTime(hoveredKey));
        } else if (!io.KeyShift) {
            ctx.clearKeySelection();
            ctx.select(layer.id);
        }
    }
    if (ImGui::IsItemActive() && drag_ == Drag::Keys) updateKeyDrag(ctx);
    if (ImGui::IsItemDeactivated() && drag_ == Drag::Keys) endKeyDrag(ctx);

    dl->PushClipRect(ImVec2(trackX0_, y), ImVec2(trackX0_ + trackW_, y + h), true);
    for (int k = 0; k + 1 < anim.keyCount(); ++k) {
        const float xa = timeToX(anim.keyTime(k)), xb = timeToX(anim.keyTime(k + 1));
        const ImU32 lineCol = anim.keyEasing(k) == Easing::Hold ? IM_COL32(255, 255, 255, 25) : IM_COL32(232, 185, 49, 70);
        dl->AddLine(ImVec2(xa, cy), ImVec2(xb, cy), lineCol, 1.5f);
    }
    for (int k = 0; k < anim.keyCount(); ++k) {
        const bool sel = ctx.isKeySelected({layer.id, prop.id, anim.keyUid(k)});
        const ImU32 fill = sel ? theme::kAccent : (k == hoveredKey ? theme::kText : IM_COL32(190, 190, 200, 255));
        const ImU32 outline = sel ? theme::kAccentHover : IM_COL32(20, 20, 24, 255);
        drawKeyGlyph(dl, ImVec2(timeToX(anim.keyTime(k)), cy), kKeyRadius + (sel ? 1.0f : 0.0f), anim.keyEasing(k), fill,
                     outline);
    }
    if (hoveredKey >= 0) ui::tooltip(easingLabel(anim.keyEasing(hoveredKey)));
    dl->PopClipRect();

    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(x0, y + h));
    ImGui::Dummy(ImVec2(0, 0));
}

void TimelinePanel::clickLayer(EditorContext& ctx, uint64_t id) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) ctx.toggleSelected(id);
    else if (io.KeyShift) ctx.selectRange(id);
    else if (!ctx.isSelected(id)) ctx.select(id);
    else if (ctx.primaryId() != id) { // keep the group, make this one primary
        std::erase(ctx.selection, id);
        ctx.selection.push_back(id);
        ctx.clearKeySelection();
    }
}

void TimelinePanel::beginLayerDrag(EditorContext& ctx, Drag mode, uint64_t id) {
    const Layer* layer = ctx.comp().findLayer(id);
    if (!layer) return;
    drag_ = mode;
    dragLayer_ = id;
    dragStartX_ = ImGui::GetIO().MousePos.x;
    dragStartY_ = ImGui::GetIO().MousePos.y;
    dragOrigIn_ = layer->inPoint;
    dragOrigOut_ = layer->outPoint;
    dragApplied_ = 0.0;
    restacking_ = false;
}

void TimelinePanel::updateLayerDrag(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    Composition& comp = ctx.comp();
    const float dx = io.MousePos.x - dragStartX_, dy = io.MousePos.y - dragStartY_;
    if ((drag_ == Drag::LayerName || drag_ == Drag::LayerMove) && std::fabs(dy) > kLayerRowHeight * 0.6f)
        restacking_ = true;
    if (restacking_) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    Layer* layer = comp.findLayer(dragLayer_);
    if (!layer) return;
    double delta = snapDelta(dx / pxPerSec_, comp.fps);
    if (restacking_ && std::fabs(dx) < 12.0f) delta = 0.0; // vertical drags shouldn't nudge time
    const double fd = comp.frameDuration();
    switch (drag_) {
    case Drag::LayerMove:
        if (delta != dragApplied_) {
            for (Layer* l : ctx.selectedLayers()) l->shiftTime(delta - dragApplied_);
            dragApplied_ = delta;
            ctx.touch();
        }
        break;
    case Drag::LayerIn:
        layer->inPoint = std::min(dragOrigIn_ + delta, layer->outPoint - fd);
        ctx.touch();
        break;
    case Drag::LayerOut:
        layer->outPoint = std::max(dragOrigOut_ + delta, layer->inPoint + fd);
        ctx.touch();
        break;
    default: break;
    }
}

void TimelinePanel::endLayerDrag(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    if (restacking_) {
        const int gap = dropGapAt(io.MousePos.y);
        int selectedAbove = 0; // selected layers above the gap leave the list first
        for (int i = 0; i < gap && i < static_cast<int>(ctx.comp().layers.size()); ++i)
            if (ctx.isSelected(ctx.comp().layers[i].id)) ++selectedAbove;
        ctx.moveSelectedTo(gap - selectedAbove);
    } else if (std::fabs(io.MousePos.x - dragStartX_) < 3.0f && std::fabs(io.MousePos.y - dragStartY_) < 3.0f &&
               !io.KeyCtrl && !io.KeyShift) {
        ctx.select(dragLayer_); // plain click inside a multi-selection keeps just this layer
    }
    drag_ = Drag::None;
    restacking_ = false;
}

int TimelinePanel::dropGapAt(float y) const {
    int gap = 0;
    for (const LayerRow& r : rows_)
        if (y > (r.top + r.bottom) * 0.5f) gap = r.index + 1;
    return gap;
}

void TimelinePanel::drawDropIndicator(ImDrawList* dl, float x0, float width) const {
    if (rows_.empty()) return;
    const int gap = dropGapAt(ImGui::GetIO().MousePos.y);
    float y = rowsBottom_;
    for (const LayerRow& r : rows_)
        if (r.index == gap) y = r.top;
    dl->AddLine(ImVec2(x0 + 4, y), ImVec2(x0 + width - 4, y), theme::kAccent, 2.0f);
    dl->AddCircleFilled(ImVec2(x0 + 6, y), 4.0f, theme::kAccent);
}

void TimelinePanel::beginKeyDrag(EditorContext& ctx) {
    drag_ = Drag::Keys;
    dragStartX_ = ImGui::GetIO().MousePos.x;
    dragKeys_.clear();
    for (const auto& sel : ctx.selectedKeys) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) dragKeys_.emplace_back(sel, p->anim->keyTime(i));
    }
}

void TimelinePanel::updateKeyDrag(EditorContext& ctx) {
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) return;
    const double delta = snapDelta((ImGui::GetIO().MousePos.x - dragStartX_) / pxPerSec_, ctx.comp().fps);
    for (const auto& [sel, orig] : dragKeys_) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) {
                p->anim->setKeyTime(i, std::max(0.0, orig + delta));
                p->anim->sortKeys();
            }
    }
    ctx.touch();
}

void TimelinePanel::endKeyDrag(EditorContext& ctx) {
    drag_ = Drag::None;
    for (const auto& [sel, orig] : dragKeys_) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str())) p->anim->removeDuplicateKeys();
    }
    // Drop selections whose key was merged away.
    std::erase_if(ctx.selectedKeys, [&](const KeySelection& sel) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) return true;
        auto props = layer->properties();
        PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str());
        return !p || p->anim->findKeyByUid(sel.keyUid) < 0;
    });
    dragKeys_.clear();
}

void TimelinePanel::drawKeyContextMenu(EditorContext& ctx) {
    if (openKeyMenu_) {
        ImGui::OpenPopup("##keymenu");
        openKeyMenu_ = false;
    }
    if (!ImGui::BeginPopup("##keymenu")) return;
    ImGui::TextColored(theme::vec(theme::kTextFaint), "%d keyframe%s", static_cast<int>(ctx.selectedKeys.size()),
                       ctx.selectedKeys.size() == 1 ? "" : "s");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        if (ImGui::MenuItem(easingLabel(e))) ctx.setSelectedKeysEasing(e);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del")) ctx.deleteSelectedKeys();
    ImGui::EndPopup();
}

} // namespace bemotion
