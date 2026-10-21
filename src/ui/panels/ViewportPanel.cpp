#include "ui/panels/ViewportPanel.h"

#include "render/SceneRenderer.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mirage {

namespace {

constexpr float kMinZoom = 0.02f;
constexpr float kMaxZoom = 32.0f;
constexpr float kHandleRadius = 5.0f;
constexpr float kRotateOffset = 26.0f;
// Neutral gray around the frame so the composition edge is always visible.
constexpr ImU32 kCanvasColor = IM_COL32(30, 30, 34, 255);

ImVec2 im(Vec2 v) { return ImVec2(v.x, v.y); }
Vec2 vec(ImVec2 v) { return Vec2(v.x, v.y); }

struct Handles {
    Vec2 local[4];  // bounds corners in layer space (TL, TR, BR, BL)
    Vec2 screen[4]; // same corners on screen
    Vec2 rotate;    // rotation handle on screen
    Vec2 topMid;
    Vec2 pivot;     // layer position on screen
};

bool computeHandles(const Layer& layer, double t, const render::View& view, Handles& h) {
    Vec2 mn, mx;
    if (!render::localBounds(layer, t, mn, mx)) return false;
    h.local[0] = {mn.x, mn.y};
    h.local[1] = {mx.x, mn.y};
    h.local[2] = {mx.x, mx.y};
    h.local[3] = {mn.x, mx.y};
    const Affine m = view.matrix() * layer.matrixAt(t);
    for (int i = 0; i < 4; ++i) h.screen[i] = m.apply(h.local[i]);
    h.topMid = (h.screen[0] + h.screen[1]) * 0.5f;
    const Vec2 center = (h.screen[0] + h.screen[2]) * 0.5f;
    Vec2 dir = h.topMid - center;
    const float len = dir.length();
    dir = len > 1e-3f ? dir / len : Vec2{0.0f, -1.0f};
    // A mirrored layer flips its local "up"; keep the handle outside the box.
    if (m.determinant() < 0.0f) dir = dir * -1.0f, h.topMid = (h.screen[2] + h.screen[3]) * 0.5f;
    h.rotate = h.topMid + dir * kRotateOffset;
    h.pivot = view.toScreen(layer.transform.position.valueAt(t));
    return true;
}

bool near(Vec2 a, Vec2 b, float r) { return (a - b).length() <= r; }

} // namespace

void ViewportPanel::draw(EditorContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin(kWindowName, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    Composition& c = ctx.comp();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.0f);
    size.y = std::max(size.y, 1.0f);
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);

    if (autoFit_) {
        zoom_ = std::clamp(std::min((size.x - 64.0f) / c.width, (size.y - 64.0f) / c.height), kMinZoom, kMaxZoom);
        pan_ = {0.0f, 0.0f};
    }
    const Vec2 canvasCenter{p0.x + size.x * 0.5f, p0.y + size.y * 0.5f};
    origin_ = canvasCenter + pan_ - Vec2(static_cast<float>(c.width), static_cast<float>(c.height)) * (zoom_ * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, kCanvasColor);

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

    dl->PushClipRect(p0, p1, true);
    const render::View view{origin_, zoom_};
    const Vec2 compMax = view.toScreen({static_cast<float>(c.width), static_cast<float>(c.height)});
    dl->AddRectFilled(ImVec2(origin_.x + 4, origin_.y + 6), ImVec2(compMax.x + 4, compMax.y + 6), IM_COL32(0, 0, 0, 90), 2);
    render::drawComposition(dl, c, ctx.time, view);
    dl->AddRect(im(origin_), im(compMax), theme::kBorder);
    handleInput(ctx, vec(p0), vec(p1));
    drawSelection(ctx);
    dl->PopClipRect();

    // HUD: resolution + zoom, and a fit button.
    char hud[96];
    std::snprintf(hud, sizeof(hud), "%d × %d   ·   %.0f%%", c.width, c.height, zoom_ * 100.0f);
    dl->AddText(ImVec2(p0.x + 12, p1.y - ImGui::GetTextLineHeight() - 10), theme::kTextFaint, hud);

    ImGui::SetCursorScreenPos(ImVec2(p1.x - 58, p0.y + 10));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 30, 34, 200));
    if (ImGui::Button("Fit", ImVec2(46, 0))) requestFit();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit composition to view (Ctrl+0)");

    ImGui::End();
}

void ViewportPanel::handleInput(EditorContext& ctx, Vec2 canvasMin, Vec2 canvasMax) {
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const Composition& comp = ctx.comp();
    const render::View view{origin_, zoom_};
    const Vec2 mouse = vec(io.MousePos);
    const Vec2 mouseComp = view.toComp(mouse);
    const double t = ctx.time;
    const Vec2 canvasCenter = (canvasMin + canvasMax) * 0.5f;
    const Vec2 compSize{static_cast<float>(comp.width), static_cast<float>(comp.height)};

    // Zoom around the cursor.
    if (hovered && io.MouseWheel != 0.0f) {
        const float newZoom = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), kMinZoom, kMaxZoom);
        const Vec2 newOrigin = mouse - mouseComp * newZoom;
        pan_ = newOrigin + compSize * (newZoom * 0.5f) - canvasCenter;
        zoom_ = newZoom;
        autoFit_ = false;
    }

    // Pan with the middle mouse button.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        pan_ += vec(io.MouseDelta);
        autoFit_ = false;
    }

    Layer* sel = ctx.selected();
    const bool editable = sel && !sel->locked && sel->activeAt(t);
    Handles h{};
    const bool haveHandles = editable && computeHandles(*sel, t, view, h);

    // Cursor feedback.
    if (hovered && drag_ == Drag::None && haveHandles) {
        for (int i = 0; i < 4; ++i)
            if (near(mouse, h.screen[i], kHandleRadius + 3))
                ImGui::SetMouseCursor(i % 2 == 0 ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
        if (near(mouse, h.rotate, kHandleRadius + 4)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        drag_ = Drag::None;
        if (io.KeyAlt) {
            drag_ = Drag::Pan;
        } else {
            if (haveHandles) {
                for (int i = 0; i < 4; ++i)
                    if (near(mouse, h.screen[i], kHandleRadius + 3)) {
                        drag_ = Drag::Scale;
                        dragCorner_ = h.local[i];
                    }
                if (near(mouse, h.rotate, kHandleRadius + 4)) drag_ = Drag::Rotate;
            }
            if (drag_ == Drag::None) {
                const uint64_t hit = render::hitTest(comp, t, mouseComp);
                ctx.select(hit);
                sel = ctx.selected();
                if (sel) drag_ = Drag::Move;
            }
            if (sel && drag_ != Drag::None) {
                dragLayer_ = sel->id;
                dragStartMouse_ = mouseComp;
                dragStartPosition_ = sel->transform.position.valueAt(t);
                dragStartScale_ = sel->transform.scale.valueAt(t);
                dragStartRotation_ = sel->transform.rotation.valueAt(t);
                const Vec2 d = mouse - view.toScreen(dragStartPosition_);
                dragStartAngle_ = radToDeg(std::atan2(d.y, d.x));
            }
        }
    }

    if (!active || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        drag_ = Drag::None;
        return;
    }
    if (drag_ == Drag::Pan) {
        pan_ += vec(io.MouseDelta);
        autoFit_ = false;
        return;
    }
    Layer* layer = ctx.comp().findLayer(dragLayer_);
    if (!layer || drag_ == Drag::None || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) return;

    auto& tr = layer->transform;
    switch (drag_) {
    case Drag::Move: {
        Vec2 delta = mouseComp - dragStartMouse_;
        if (io.KeyShift) (std::fabs(delta.x) > std::fabs(delta.y) ? delta.y : delta.x) = 0.0f;
        tr.position.set(t, dragStartPosition_ + delta);
        break;
    }
    case Drag::Scale: {
        const Affine pre = Affine::translate(dragStartPosition_) * Affine::rotateDeg(dragStartRotation_);
        const Vec2 q = pre.inverse().apply(mouseComp);
        const Vec2 v = dragCorner_ - tr.anchor.valueAt(t);
        Vec2 s = dragStartScale_;
        if (io.KeyShift) {
            const Vec2 v0 = v * dragStartScale_ * 0.01f;
            const float dd = v0.x * v0.x + v0.y * v0.y;
            if (dd > 1e-6f) s = dragStartScale_ * ((q.x * v0.x + q.y * v0.y) / dd);
        } else {
            if (std::fabs(v.x) > 1e-3f) s.x = q.x / v.x * 100.0f;
            if (std::fabs(v.y) > 1e-3f) s.y = q.y / v.y * 100.0f;
        }
        tr.scale.set(t, s);
        break;
    }
    case Drag::Rotate: {
        const Vec2 d = mouse - view.toScreen(dragStartPosition_);
        float r = dragStartRotation_ + radToDeg(std::atan2(d.y, d.x)) - dragStartAngle_;
        if (io.KeyShift) r = std::round(r / 15.0f) * 15.0f;
        tr.rotation.set(t, r);
        break;
    }
    default: break;
    }
    ctx.touch();
}

void ViewportPanel::drawSelection(EditorContext& ctx) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::View view{origin_, zoom_};
    const double t = ctx.time;

    // Hover outline for the layer under the cursor.
    if (drag_ == Drag::None && ImGui::IsItemHovered()) {
        const uint64_t hover = render::hitTest(ctx.comp(), t, view.toComp(vec(ImGui::GetIO().MousePos)));
        if (hover && hover != ctx.selectedLayer) {
            Handles h{};
            if (computeHandles(*ctx.comp().findLayer(hover), t, view, h)) {
                ImVec2 pts[4];
                for (int i = 0; i < 4; ++i) pts[i] = im(h.screen[i]);
                dl->AddPolyline(pts, 4, IM_COL32(232, 185, 49, 110), ImDrawFlags_Closed, 1.0f);
            }
        }
    }

    Layer* sel = ctx.selected();
    if (!sel || !sel->activeAt(t)) return;
    Handles h{};
    if (!computeHandles(*sel, t, view, h)) return;

    ImVec2 pts[4];
    for (int i = 0; i < 4; ++i) pts[i] = im(h.screen[i]);
    dl->AddPolyline(pts, 4, theme::kAccent, ImDrawFlags_Closed, 1.5f);

    // Pivot (layer position / anchor).
    dl->AddCircle(im(h.pivot), 5.0f, theme::kAccent, 0, 1.5f);
    dl->AddLine(ImVec2(h.pivot.x - 8, h.pivot.y), ImVec2(h.pivot.x + 8, h.pivot.y), theme::kAccent, 1.0f);
    dl->AddLine(ImVec2(h.pivot.x, h.pivot.y - 8), ImVec2(h.pivot.x, h.pivot.y + 8), theme::kAccent, 1.0f);

    if (sel->locked) return;
    dl->AddLine(im(h.topMid), im(h.rotate), theme::kAccent, 1.0f);
    dl->AddCircleFilled(im(h.rotate), kHandleRadius, theme::kBgPanel);
    dl->AddCircle(im(h.rotate), kHandleRadius, theme::kAccent, 0, 1.5f);
    for (const Vec2& c : h.screen) {
        const ImVec2 a(c.x - kHandleRadius, c.y - kHandleRadius), b(c.x + kHandleRadius, c.y + kHandleRadius);
        dl->AddRectFilled(a, b, theme::kBgPanel, 2.0f);
        dl->AddRect(a, b, theme::kAccent, 2.0f, 0, 1.5f);
    }
}

} // namespace mirage
