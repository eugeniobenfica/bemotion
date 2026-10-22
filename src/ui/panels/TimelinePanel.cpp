#include "ui/panels/TimelinePanel.h"

#include "core/Snapping.h"
#include "render/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/PropertiesPanel.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bemotion {

namespace {

constexpr float kRulerHeight = 26.0f;
constexpr float kLayerRowHeight = 36.0f;
constexpr float kPropRowHeight = 30.0f;
constexpr float kGroupRowHeight = 24.0f;
constexpr float kEdgeGrab = 6.0f;
constexpr float kKeyRadius = 5.0f;

ImU32 toU32(Color c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alpha)); }

// Cubic-bezier timing editor: drag the two handles. Returns true while edited.
bool curveEditor(BezierEase& c, float size) {
    const ImVec2 pad(10.0f, 34.0f); // vertical room for overshoot
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 total(size + pad.x * 2, size + pad.y * 2);
    ImGui::InvisibleButton("##curve", total);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0(origin.x + pad.x, origin.y + pad.y + size); // (0,0) bottom-left
    auto toScreen = [&](float x, float y) { return ImVec2(p0.x + x * size, p0.y - y * size); };
    dl->AddRectFilled(origin, ImVec2(origin.x + total.x, origin.y + total.y), theme::kBgDeep, 4.0f);
    dl->AddRect(toScreen(0, 1), toScreen(1, 0), IM_COL32(255, 255, 255, 30));
    for (int i = 1; i < 4; ++i) {
        const float g = i / 4.0f;
        dl->AddLine(toScreen(g, 0), toScreen(g, 1), IM_COL32(255, 255, 255, 12));
        dl->AddLine(toScreen(0, g), toScreen(1, g), IM_COL32(255, 255, 255, 12));
    }
    const ImVec2 a = toScreen(0, 0), b = toScreen(1, 1);
    const ImVec2 h1 = toScreen(c.x1, c.y1), h2 = toScreen(c.x2, c.y2);
    dl->AddLine(a, h1, theme::kTextFaint, 1.0f);
    dl->AddLine(b, h2, theme::kTextFaint, 1.0f);
    dl->AddBezierCubic(a, h1, h2, b, theme::kAccent, 2.5f);

    // Handle dragging: the grabbed handle is remembered in window storage.
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID("##curvegrab");
    int grab = st->GetInt(key, 0);
    const ImVec2 m = ImGui::GetIO().MousePos;
    auto dist = [&](ImVec2 p) { return std::hypot(p.x - m.x, p.y - m.y); };
    if (ImGui::IsItemActivated()) grab = dist(h1) <= dist(h2) ? 1 : 2;
    if (!ImGui::IsItemActive()) grab = 0;
    st->SetInt(key, grab);
    bool changed = false;
    if (grab) {
        const float x = std::clamp((m.x - p0.x) / size, 0.0f, 1.0f);
        const float y = std::clamp((p0.y - m.y) / size, -0.5f, 1.5f);
        (grab == 1 ? c.x1 : c.x2) = x;
        (grab == 1 ? c.y1 : c.y2) = y;
        changed = true;
    }
    for (int i = 1; i <= 2; ++i) {
        const ImVec2 h = i == 1 ? h1 : h2;
        const bool hot = grab == i || (grab == 0 && ImGui::IsItemHovered() && dist(h) < 9.0f);
        dl->AddCircleFilled(h, hot ? 6.0f : 5.0f, hot ? theme::kText : theme::kAccent);
    }
    return changed;
}

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

} // namespace

void TimelinePanel::draw(EditorContext& ctx) {
    if (!open) return;
    ui::nextPanelWindowClass();
    if (!ImGui::Begin(kWindowName, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    snapLineTime_ = -1.0;
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
    clips_.clear();
    auto& layers = comp.layers;
    const auto trackRows = comp.trackRows();
    for (int r = 0; r < static_cast<int>(trackRows.size()); ++r) {
        const TrackRow& row = trackRows[r];
        ImGui::PushID(static_cast<int>(row.track));
        drawTrackRow(ctx, row, r, x0, avail.x);
        if (expanded_.count(row.track)) {
            // Property rows belong to the primary clip if it is on this track, else the first clip.
            Layer* focus = &layers[row.first];
            for (int i = row.first; i < row.first + row.count; ++i)
                if (layers[i].id == ctx.primaryId()) focus = &layers[i];
            ImGui::PushID(static_cast<int>(focus->id));
            const char* group = nullptr;
            bool groupOpen = true;
            for (const PropertyRef& prop : focus->properties()) {
                if (animatedOnly_ && !prop.anim->isAnimated()) continue;
                if (!group || std::strcmp(group, prop.group) != 0) {
                    group = prop.group;
                    groupOpen = drawGroupRow(row.track, group, x0, avail.x);
                }
                if (groupOpen) drawPropertyRow(ctx, *focus, prop, x0, avail.x);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // Empty space below the rows: click to deselect, drag to box-select.
    const float y = ImGui::GetCursorScreenPos().y;
    rowsBottom_ = y;
    const float rest = std::max(kLayerRowHeight, winPos.y + winSize.y - y);
    ImGui::SetCursorScreenPos(ImVec2(x0, y));
    ImGui::InvisibleButton("##empty", ImVec2(avail.x, rest));
    if (ImGui::IsItemActivated()) beginMarquee(ctx);

    applyPendingDrop(ctx);
    updateMarquee(ctx, dl);
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
    if (snapLineTime_ >= 0.0) { // what the dragged item snapped to
        const float sx = timeToX(snapLineTime_);
        dl->AddLine(ImVec2(sx, bodyTop), ImVec2(sx, bodyBottom), theme::kSnap, 1.0f);
    }

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

    if (restacking_ && drag_ != Drag::None)
        drawDropIndicator(dl, dropTargetAt(ctx, io.MousePos.y, drag_), x0, avail.x);

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
    ImGui::SameLine();
    if (ui::iconButton("##snap", ui::Icon::Magnet, s, "Snapping (S): clips, keyframes and objects snap. Hold Ctrl to bypass", ctx.snapping))
        ctx.snapping = !ctx.snapping;
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
    if (ImGui::IsItemActivated()) collectTimeSnapTargets(ctx, {}, false);
    if (ImGui::IsItemActive()) {
        ctx.playing = false;
        double t = xToTime(ImGui::GetIO().MousePos.x);
        if (ImGui::GetIO().KeyShift) { // Shift: the playhead snaps to clip edges and keyframes
            std::erase(timeTargets_, ctx.time);
            t = snappedDelta(ctx, {0.0}, t);
        }
        ctx.setTime(t);
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

void TimelinePanel::drawTrackRow(EditorContext& ctx, const TrackRow& row, int rowIndex, float x0, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    auto& layers = ctx.comp().layers;
    const float y = ImGui::GetCursorScreenPos().y;
    const float h = kLayerRowHeight;
    rows_.push_back({rowIndex, row.track, y, y + h});

    // The clip that represents the track in the left column: the primary
    // selection if it is here, otherwise the first clip.
    Layer* focus = &layers[row.first];
    bool anySelected = false;
    for (int i = row.first; i < row.first + row.count; ++i) {
        anySelected |= ctx.isSelected(layers[i].id);
        if (layers[i].id == ctx.primaryId()) focus = &layers[i];
    }
    const bool hasPrimary = focus->id == ctx.primaryId();

    if (anySelected) {
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + h), IM_COL32(232, 185, 49, hasPrimary ? 34 : 24));
        dl->AddRectFilled(ImVec2(x0, y + 2), ImVec2(x0 + 3, y + h - 2), theme::kAccent, 1.5f);
    }
    dl->AddLine(ImVec2(x0, y + h - 1), ImVec2(x0 + width, y + h - 1), IM_COL32(255, 255, 255, 10));

    // --- left column: controls and name ---------------------------------------
    const float btn = 20.0f;
    const float by = y + (h - btn) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(x0 + 6, by));
    const bool isExpanded = expanded_.count(row.track) > 0;
    if (ui::iconButton("##expand", isExpanded ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, btn, "Show properties")) {
        if (isExpanded) expanded_.erase(row.track);
        else expanded_.insert(row.track);
    }
    // Visibility and lock act on the whole track.
    ImGui::SameLine();
    if (ui::iconButton("##vis", focus->visible ? ui::Icon::Eye : ui::Icon::EyeOff, btn,
                       focus->visible ? "Hide track" : "Show track")) {
        const bool visible = !focus->visible;
        for (int i = row.first; i < row.first + row.count; ++i) layers[i].visible = visible;
        ctx.touch();
    }
    ImGui::SameLine();
    if (ui::iconButton("##lock", focus->locked ? ui::Icon::Lock : ui::Icon::Unlock, btn,
                       focus->locked ? "Unlock track" : "Lock track", focus->locked)) {
        const bool locked = !focus->locked;
        for (int i = row.first; i < row.first + row.count; ++i) layers[i].locked = locked;
        ctx.touch();
    }
    ImGui::SameLine();
    const ImVec2 chip = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(chip.x + 2, y + 8), ImVec2(chip.x + 5, y + h - 8), toU32(labelColor(focus->label)), 2.0f);
    ui::drawIcon(dl, ui::iconForLayer(*focus), ImVec2(chip.x + 18, y + h * 0.5f), 14.0f,
                 anySelected ? theme::kAccent : theme::kTextDim);

    const float nameX = chip.x + 32;
    const float nameW = std::max(10.0f, x0 + leftWidth_ - nameX - 8);
    ImGui::SetCursorScreenPos(ImVec2(nameX, y + (h - ImGui::GetFrameHeight()) * 0.5f));
    if (renaming_ == focus->id) {
        ImGui::SetNextItemWidth(nameW);
        if (focusRename_) {
            ImGui::SetKeyboardFocusHere();
            focusRename_ = false;
        }
        if (ImGui::InputText("##rename", &focus->name, ImGuiInputTextFlags_AutoSelectAll)) ctx.touch();
        if (ImGui::IsItemDeactivated()) renaming_ = 0;
    } else {
        ImGui::SetCursorScreenPos(ImVec2(nameX, y));
        ImGui::InvisibleButton("##name", ImVec2(nameW, h));
        if (ImGui::IsItemActivated()) {
            clickTrack(ctx, row);
            if (!io.KeyCtrl && !io.KeyShift) beginLayerDrag(ctx, Drag::LayerName, focus->id);
        }
        if (ImGui::IsItemActive() && drag_ == Drag::LayerName) updateLayerDrag(ctx);
        if (ImGui::IsItemDeactivated() && drag_ == Drag::LayerName) endLayerDrag(ctx);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            renaming_ = focus->id;
            focusRename_ = true;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !anySelected) clickTrack(ctx, row);
        if (ImGui::BeginPopupContextItem("##trackctx")) {
            if (ImGui::MenuItem("Rename")) {
                renaming_ = focus->id;
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
        const ImU32 col = focus->visible ? (anySelected ? theme::kText : theme::kTextDim) : theme::kTextFaint;
        dl->PushClipRect(ImVec2(nameX, y), ImVec2(nameX + nameW, y + h), true);
        const float textY = y + (h - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(nameX, textY), col, focus->name.c_str());
        if (row.count > 1) { // e.g. a track that was cut into pieces
            char badge[16];
            std::snprintf(badge, sizeof(badge), "%d clips", row.count);
            const float bw = ImGui::CalcTextSize(badge).x;
            dl->AddText(ImVec2(nameX + nameW - bw, textY), theme::kTextFaint, badge);
        }
        dl->PopClipRect();
    }

    // --- track: clip bars ------------------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2(trackX0_, y));
    ImGui::InvisibleButton("##track", ImVec2(trackW_, h));
    const bool hovered = ImGui::IsItemHovered();
    const float mx = io.MousePos.x;

    // Clip under the mouse; edges win over bodies so adjacent clips can be trimmed.
    int hit = -1;
    Drag hitMode = Drag::None;
    for (int i = row.first; i < row.first + row.count; ++i) {
        const float bx1 = timeToX(layers[i].inPoint), bx2 = timeToX(layers[i].outPoint);
        if (std::fabs(mx - bx1) < kEdgeGrab) hit = i, hitMode = Drag::LayerIn;
        else if (std::fabs(mx - bx2) < kEdgeGrab && hitMode != Drag::LayerIn) hit = i, hitMode = Drag::LayerOut;
        else if (mx > bx1 && mx < bx2 && hit < 0) hit = i, hitMode = Drag::LayerMove;
    }

    if (ctx.tool == Tool::Razor) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && hit >= 0) ctx.splitLayerAt(layers[hit].id, xToTime(mx));
    } else {
        if (hovered && drag_ == Drag::None && (hitMode == Drag::LayerIn || hitMode == Drag::LayerOut))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) {
            if (hit >= 0) {
                const uint64_t id = layers[hit].id;
                clickLayer(ctx, id);
                if (!io.KeyCtrl && !io.KeyShift) beginLayerDrag(ctx, hitMode, id);
            } else {
                beginMarquee(ctx); // empty track space: box selection
            }
        }
        const bool barDrag = drag_ == Drag::LayerMove || drag_ == Drag::LayerIn || drag_ == Drag::LayerOut;
        // The dragged clip may change track mid-frame; match by row activity instead of clip.
        if (ImGui::IsItemActive() && barDrag) updateLayerDrag(ctx);
        if (ImGui::IsItemDeactivated() && barDrag) endLayerDrag(ctx);
    }

    dl->PushClipRect(ImVec2(trackX0_, y), ImVec2(trackX0_ + trackW_, y + h), true);
    for (int i = row.first; i < row.first + row.count; ++i) {
        const Layer& layer = layers[i];
        const bool selected = ctx.isSelected(layer.id);
        const bool primary = ctx.primaryId() == layer.id;
        const float bx1 = timeToX(layer.inPoint), bx2 = timeToX(layer.outPoint);
        clips_.push_back({layer.id, y, y + h, bx1, bx2});

        const Color lc = labelColor(layer.label);
        const ImVec2 a(bx1 + 1, y + 4), b(bx2 - 1, y + h - 4); // 1px gap shows cut points
        dl->AddRectFilled(a, b, toU32(lc, layer.visible ? (selected ? 0.42f : 0.26f) : 0.1f), 5.0f);
        if (selected)
            dl->AddRect(a, b, primary ? theme::kText : IM_COL32(255, 255, 255, 170), 5.0f, 0, primary ? 1.8f : 1.3f);
        dl->AddRectFilled(ImVec2(bx1 + 3, y + 9), ImVec2(bx1 + 5, y + h - 9), toU32(lc, 0.8f), 1.0f);
        dl->AddRectFilled(ImVec2(bx2 - 5, y + 9), ImVec2(bx2 - 3, y + h - 9), toU32(lc, 0.8f), 1.0f);
        dl->PushClipRect(ImVec2(std::max(bx1, trackX0_), y), ImVec2(std::min(bx2, trackX0_ + trackW_), y + h), true);
        dl->AddText(ImVec2(std::max(bx1, trackX0_) + 10, y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                    IM_COL32(255, 255, 255, 150), layer.name.c_str());
        dl->PopClipRect();

        // Summary of the clip's keyframes (within the clip only, e.g. after a cut).
        if (!isExpanded) {
            for (const PropertyRef& p : const_cast<Layer&>(layer).properties())
                for (int k = 0; k < p.anim->keyCount(); ++k) {
                    const double kt = p.anim->keyTime(k);
                    if (kt < layer.inPoint - kTimeEpsilon || kt > layer.outPoint + kTimeEpsilon) continue;
                    ui::drawDiamond(dl, ImVec2(timeToX(kt), y + h - 7), 3.0f, IM_COL32(230, 230, 235, 200), 0);
                }
        }
    }
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(x0, y + h));
    ImGui::Dummy(ImVec2(0, 0));
}

bool TimelinePanel::drawGroupRow(uint64_t track, const char* group, float x0, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = ImGui::GetCursorScreenPos().y;
    const float h = kGroupRowHeight;
    const std::string key = std::to_string(track) + "/" + group;
    const bool open = !collapsedGroups_.count(key);

    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + h), IM_COL32(0, 0, 0, 60));
    ImGui::SetCursorScreenPos(ImVec2(x0 + 24, y));
    ImGui::PushID(group);
    if (ImGui::InvisibleButton("##group", ImVec2(leftWidth_ - 30, h))) {
        if (open) collapsedGroups_.insert(key);
        else collapsedGroups_.erase(key);
    }
    ImGui::PopID();
    const ImU32 col = ImGui::IsItemHovered() ? theme::kText : theme::kTextDim;
    ui::drawIcon(dl, open ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, ImVec2(x0 + 36, y + h * 0.5f), 12.0f, col);
    ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 0.82f);
    dl->AddText(ImVec2(x0 + 48, y + (h - ImGui::GetFontSize()) * 0.5f), col, group);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(x0, y + h));
    ImGui::Dummy(ImVec2(0, 0));
    return open;
}

void TimelinePanel::drawPropertyRow(EditorContext& ctx, Layer& layer, const PropertyRef& prop, float x0, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float y = ImGui::GetCursorScreenPos().y;
    const float h = kPropRowHeight;
    AnimatedBase& anim = *prop.anim;
    ImGui::PushID(prop.id);

    // Selection tint: light for rows of the selected layer, stronger where keys are selected.
    const bool hasSelectedKey = std::any_of(ctx.selectedKeys.begin(), ctx.selectedKeys.end(), [&](const KeySelection& k) {
        return k.layerId == layer.id && k.propertyId == prop.id;
    });
    const ImU32 rowBg = hasSelectedKey ? IM_COL32(232, 185, 49, 40)
                        : ctx.isSelected(layer.id) ? IM_COL32(232, 185, 49, 14)
                                                   : IM_COL32(0, 0, 0, 40);
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + h), rowBg);
    if (hasSelectedKey) dl->AddRectFilled(ImVec2(x0, y + 2), ImVec2(x0 + 3, y + h - 2), theme::kAccent, 1.5f);

    // --- left: keyframe toggle, label, editable value, reset ---------------------
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 2));
    const float frameH = ImGui::GetFrameHeight();
    const float widgetY = y + (h - frameH) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(x0 + 30, widgetY));
    keyframeToggle(ctx, layer, prop);
    const float textY = y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(x0 + 56, textY), anim.isAnimated() ? theme::kText : theme::kTextDim, prop.label);

    const float resetX = x0 + leftWidth_ - 8 - frameH;
    const float valueW = prop.kind == PropertyKind::Vec2 ? 128.0f : prop.kind == PropertyKind::Color ? 110.0f : 72.0f;
    ImGui::SetCursorScreenPos(ImVec2(resetX - 4 - valueW, widgetY));
    propertyValueWidget(ctx, prop, valueW);
    ImGui::SetCursorScreenPos(ImVec2(resetX, widgetY));
    resetButton(ctx, layer, prop);
    ImGui::PopStyleVar();

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
    // Shade time where the clip does not exist: keyframes can't go there.
    const float clipX1 = timeToX(layer.inPoint), clipX2 = timeToX(layer.outPoint);
    dl->AddRectFilled(ImVec2(trackX0_, y), ImVec2(clipX1, y + h), IM_COL32(0, 0, 0, 70));
    dl->AddRectFilled(ImVec2(clipX2, y), ImVec2(trackX0_ + trackW_, y + h), IM_COL32(0, 0, 0, 70));
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

namespace {
std::vector<uint64_t> clipsOfTrack(const Composition& comp, uint64_t track) {
    std::vector<uint64_t> ids;
    for (const Layer& l : comp.layers)
        if (l.track == track) ids.push_back(l.id);
    return ids;
}
} // namespace

void TimelinePanel::clickTrack(EditorContext& ctx, const TrackRow& row) {
    const ImGuiIO& io = ImGui::GetIO();
    const auto ids = clipsOfTrack(ctx.comp(), row.track);
    const bool all = std::all_of(ids.begin(), ids.end(), [&](uint64_t id) { return ctx.isSelected(id); });
    if (io.KeyCtrl) {
        for (uint64_t id : ids)
            if (all) std::erase(ctx.selection, id);
            else if (!ctx.isSelected(id)) ctx.selection.push_back(id);
        ctx.clearKeySelection();
    } else if (io.KeyShift) {
        ctx.selectRange(ids.front());
    } else if (!all) { // keep a multi-track selection when clicking one of its tracks
        ctx.select(ids.front());
        for (size_t i = 1; i < ids.size(); ++i) ctx.selection.push_back(ids[i]);
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
    dragClips_.clear();
    std::vector<uint64_t> moving = mode == Drag::LayerMove ? ctx.selection : std::vector<uint64_t>{id};
    for (uint64_t m : moving)
        if (const Layer* l = ctx.comp().findLayer(m)) dragClips_.push_back({m, l->inPoint, l->outPoint});
    collectTimeSnapTargets(ctx, moving, false);
}

void TimelinePanel::collectTimeSnapTargets(EditorContext& ctx, const std::vector<uint64_t>& movingClips, bool movingKeys) {
    const Composition& comp = ctx.comp();
    timeTargets_ = {0.0, comp.duration, ctx.time}; // composition ends and the playhead
    auto isDraggedKey = [&](uint64_t layerId, const char* prop, uint32_t uid) {
        if (!movingKeys) return false;
        for (const auto& [sel, orig] : dragKeys_)
            if (sel.layerId == layerId && sel.propertyId == prop && sel.keyUid == uid) return true;
        return false;
    };
    for (const Layer& l : comp.layers) {
        if (std::find(movingClips.begin(), movingClips.end(), l.id) != movingClips.end()) continue;
        timeTargets_.push_back(l.inPoint);
        timeTargets_.push_back(l.outPoint);
        for (const PropertyRef& p : const_cast<Layer&>(l).properties())
            for (int k = 0; k < p.anim->keyCount(); ++k)
                if (!isDraggedKey(l.id, p.id, p.anim->keyUid(k))) timeTargets_.push_back(p.anim->keyTime(k));
    }
}

double TimelinePanel::snappedDelta(EditorContext& ctx, const std::vector<double>& base, double rawDelta) {
    // Test the raw (unrounded) position so a target is caught before frame rounding jumps past it.
    if (ctx.snapping && !ImGui::GetIO().KeyCtrl) {
        std::vector<double> edges;
        for (double b : base) edges.push_back(b + rawDelta);
        if (const SnapResult r = snap1D(edges, timeTargets_, 8.0 / pxPerSec_); r.snapped) { // 8 screen pixels
            snapLineTime_ = r.target;
            return rawDelta + r.offset;
        }
    }
    return snapDelta(rawDelta, ctx.comp().fps);
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
    const double raw = dx / pxPerSec_;
    const bool frozen = restacking_ && std::fabs(dx) < 12.0f; // vertical drags shouldn't nudge time
    const double fd = comp.frameDuration();
    switch (drag_) {
    case Drag::LayerMove: {
        std::vector<double> edges;
        for (const ClipStart& c : dragClips_) edges.insert(edges.end(), {c.in, c.out});
        const double delta = frozen ? 0.0 : snappedDelta(ctx, edges, raw);
        // Clips never overlap others on their track: stop at the neighbors.
        double lo = 0, hi = 0;
        comp.timeShiftLimits(ctx.selection, lo, hi);
        const double step = std::clamp(delta - dragApplied_, lo, hi);
        if (std::fabs(step) > 1e-9) {
            for (Layer* l : ctx.selectedLayers()) l->shiftTime(step);
            dragApplied_ += step;
            ctx.touch();
        }
        break;
    }
    case Drag::LayerIn: {
        double minIn = 0, maxOut = 0;
        comp.clipLimits(layer->id, minIn, maxOut);
        const double in = dragOrigIn_ + snappedDelta(ctx, {dragOrigIn_}, raw);
        layer->inPoint = std::clamp(in, minIn, std::max(minIn, layer->outPoint - fd));
        ctx.touch();
        break;
    }
    case Drag::LayerOut: {
        double minIn = 0, maxOut = 0;
        comp.clipLimits(layer->id, minIn, maxOut);
        const double out = dragOrigOut_ + snappedDelta(ctx, {dragOrigOut_}, raw);
        layer->outPoint = std::clamp(out, std::min(maxOut, layer->inPoint + fd), maxOut);
        ctx.touch();
        break;
    }
    default: break;
    }
}

void TimelinePanel::endLayerDrag(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    if (restacking_) {
        // This runs while the dragged row is drawn, before the rows below it
        // are laid out: finish the drop once the whole list is known.
        pendingDrop_ = true;
        pendingDropY_ = io.MousePos.y;
        pendingDropMode_ = drag_;
    } else if (std::fabs(io.MousePos.x - dragStartX_) < 3.0f && std::fabs(io.MousePos.y - dragStartY_) < 3.0f &&
               !io.KeyCtrl && !io.KeyShift) {
        // A plain click inside a multi-selection keeps just this clip (or this track's clips).
        if (drag_ == Drag::LayerName) {
            if (const Layer* l = ctx.comp().findLayer(dragLayer_)) {
                const auto ids = clipsOfTrack(ctx.comp(), l->track);
                ctx.select(ids.front());
                for (size_t i = 1; i < ids.size(); ++i) ctx.selection.push_back(ids[i]);
            }
        } else {
            ctx.select(dragLayer_);
        }
    }
    drag_ = Drag::None;
    restacking_ = false;
}

TimelinePanel::DropTarget TimelinePanel::dropTargetAt(EditorContext& ctx, float y, Drag mode) const {
    DropTarget t;
    if (rows_.empty()) return t;
    t.valid = true;
    t.gap = dropGapAt(y);
    if (mode != Drag::LayerMove) return t; // name drags reorder whole tracks

    // Clips: the middle of a row joins that track, its edges (or the space
    // between rows) create a new track.
    constexpr float kEdge = 7.0f;
    for (const RowRect& r : rows_) {
        if (y < r.top || y >= r.bottom) continue;
        if (y < r.top + kEdge) t.gap = r.index;
        else if (y > r.bottom - kEdge) t.gap = r.index + 1;
        else {
            t.newTrack = false;
            t.track = r.track;
            const auto& sel = ctx.selection;
            const bool alreadyThere = std::all_of(sel.begin(), sel.end(), [&](uint64_t id) {
                const Layer* l = ctx.comp().findLayer(id);
                return l && l->track == r.track;
            });
            if (alreadyThere) t.valid = false;
            else t.fits = ctx.comp().clipsFitTrack(sel, r.track);
        }
        break;
    }
    return t;
}

void TimelinePanel::applyPendingDrop(EditorContext& ctx) {
    if (!pendingDrop_) return;
    pendingDrop_ = false;
    const DropTarget t = dropTargetAt(ctx, pendingDropY_, pendingDropMode_);
    if (!t.valid) return;
    if (pendingDropMode_ == Drag::LayerName) {
        const auto tracks = ctx.selectedTracks();
        int above = 0; // selected tracks above the gap leave the list first
        for (const RowRect& r : rows_)
            if (r.index < t.gap && std::find(tracks.begin(), tracks.end(), r.track) != tracks.end()) ++above;
        ctx.moveSelectedTracksTo(t.gap - above);
    } else if (t.newTrack) {
        ctx.moveSelectedClipsToNewTrack(t.gap);
    } else if (t.fits) {
        ctx.moveSelectedClipsToTrack(t.track);
    }
}

void TimelinePanel::beginMarquee(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool additive = io.KeyCtrl || io.KeyShift;
    marquee_ = true;
    marqueeStart_ = io.MousePos;
    marqueeBase_ = additive ? ctx.selection : std::vector<uint64_t>{};
    if (!additive) ctx.select(0);
    ctx.clearKeySelection();
}

void TimelinePanel::updateMarquee(EditorContext& ctx, ImDrawList* dl) {
    if (!marquee_) return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        marquee_ = false;
        return;
    }
    const ImVec2 m = ImGui::GetIO().MousePos;
    const ImVec2 a(std::min(marqueeStart_.x, m.x), std::min(marqueeStart_.y, m.y));
    const ImVec2 b(std::max(marqueeStart_.x, m.x), std::max(marqueeStart_.y, m.y));
    if (b.x - a.x < 3.0f && b.y - a.y < 3.0f) return; // still a click

    // Clips whose row the box touches; inside the track area the box must also touch the clip.
    ctx.selection = marqueeBase_;
    for (const ClipRect& c : clips_) {
        const bool vertical = c.bottom > a.y && c.top < b.y;
        const bool horizontal = a.x < trackX0_ || (c.x2 > a.x && c.x1 < b.x);
        if (vertical && horizontal && !ctx.isSelected(c.id)) ctx.selection.push_back(c.id);
    }
    dl->AddRectFilled(a, b, IM_COL32(232, 185, 49, 22));
    dl->AddRect(a, b, IM_COL32(232, 185, 49, 160));
}

int TimelinePanel::dropGapAt(float y) const {
    int gap = 0;
    for (const RowRect& r : rows_)
        if (y > (r.top + r.bottom) * 0.5f) gap = r.index + 1;
    return gap;
}

void TimelinePanel::drawDropIndicator(ImDrawList* dl, const DropTarget& target, float x0, float width) const {
    if (!target.valid) return;
    if (!target.newTrack) { // joining an existing track: outline that row
        for (const RowRect& r : rows_) {
            if (r.track != target.track) continue;
            const ImU32 col = target.fits ? theme::kAccent : theme::kDanger;
            dl->AddRectFilled(ImVec2(trackX0_, r.top + 1), ImVec2(x0 + width - 2, r.bottom - 1),
                              target.fits ? IM_COL32(232, 185, 49, 30) : IM_COL32(229, 96, 77, 30), 4.0f);
            dl->AddRect(ImVec2(trackX0_, r.top + 1), ImVec2(x0 + width - 2, r.bottom - 1), col, 4.0f, 0, 2.0f);
        }
        return;
    }
    float y = rowsBottom_;
    for (const RowRect& r : rows_)
        if (r.index == target.gap) y = r.top;
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
    collectTimeSnapTargets(ctx, {}, true);
}

void TimelinePanel::updateKeyDrag(EditorContext& ctx) {
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) return;
    std::vector<double> edges;
    for (const auto& [sel, orig] : dragKeys_) edges.push_back(orig);
    const double delta = snappedDelta(ctx, edges, (ImGui::GetIO().MousePos.x - dragStartX_) / pxPerSec_);
    for (const auto& [sel, orig] : dragKeys_) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        if (PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str()))
            if (const int i = p->anim->findKeyByUid(sel.keyUid); i >= 0) {
                // Keys stay where the clip exists.
                p->anim->setKeyTime(i, std::clamp(orig + delta, layer->inPoint, layer->outPoint));
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
        if (e == Easing::Custom) continue;
        if (ImGui::MenuItem(easingLabel(e))) ctx.setSelectedKeysEasing(e);
    }
    if (ImGui::BeginMenu("Custom Curve")) {
        if (ImGui::IsWindowAppearing()) curveEdit_ = ctx.selectedKeysCurve();
        if (curveEditor(curveEdit_, 180.0f)) ctx.setSelectedKeysCurve(curveEdit_);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::DragFloat4("##bez", &curveEdit_.x1, 0.005f, -1.0f, 2.0f, "%.2f")) {
            curveEdit_.x1 = std::clamp(curveEdit_.x1, 0.0f, 1.0f);
            curveEdit_.x2 = std::clamp(curveEdit_.x2, 0.0f, 1.0f);
            ctx.setSelectedKeysCurve(curveEdit_);
        }
        ImGui::TextColored(theme::vec(theme::kTextFaint), "Start from:");
        const Easing starts[] = {Easing::Linear, Easing::EaseIn, Easing::EaseOut, Easing::EaseInOut, Easing::BackOut};
        for (Easing e : starts) {
            if (e != starts[0]) ImGui::SameLine();
            const char* label = e == Easing::EaseInOut ? "In Out" : e == Easing::BackOut ? "Back" : e == Easing::EaseIn ? "In"
                              : e == Easing::EaseOut ? "Out" : "Lin";
            if (ImGui::SmallButton(label)) {
                curveEdit_ = bezierFor(e);
                ctx.setSelectedKeysCurve(curveEdit_);
            }
        }
        ImGui::EndMenu();
    }
    if (ctx.selectionHasPathKeys()) {
        ImGui::Separator();
        ImGui::TextColored(theme::vec(theme::kTextFaint), "Motion path");
        if (ImGui::MenuItem("Auto Bezier")) ctx.setSelectedKeysPath(PathMode::Auto);
        if (ImGui::MenuItem("Linear (corner)")) ctx.setSelectedKeysPath(PathMode::Linear);
        if (ImGui::MenuItem("Custom Handles")) ctx.setSelectedKeysPath(PathMode::Custom);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del")) ctx.deleteSelectedKeys();
    ImGui::EndPopup();
}

} // namespace bemotion
