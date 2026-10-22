#include "ui/panels/ViewportPanel.h"

#include "render/SceneRenderer.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bemotion {

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

// Exact overlap between a layer's (possibly rotated) box on screen and an
// axis-aligned rectangle, using the separating axis test on both shapes.
bool layerOverlapsRect(const Layer& layer, double t, const render::View& view, Vec2 rectMin, Vec2 rectMax) {
    Handles h{};
    if (!computeHandles(layer, t, view, h)) return false;
    const Vec2 rect[4] = {rectMin, {rectMax.x, rectMin.y}, rectMax, {rectMin.x, rectMax.y}};
    const Vec2 axes[4] = {{1, 0}, {0, 1}, h.screen[1] - h.screen[0], h.screen[3] - h.screen[0]};
    for (const Vec2& axis : axes) {
        float aMin = 1e30f, aMax = -1e30f, bMin = 1e30f, bMax = -1e30f;
        for (int i = 0; i < 4; ++i) {
            const float a = h.screen[i].x * axis.x + h.screen[i].y * axis.y;
            const float b = rect[i].x * axis.x + rect[i].y * axis.y;
            aMin = std::min(aMin, a), aMax = std::max(aMax, a);
            bMin = std::min(bMin, b), bMax = std::max(bMax, b);
        }
        if (aMax < bMin || bMax < aMin) return false;
    }
    return true;
}

void outline(ImDrawList* dl, const Handles& h, ImU32 col, float thickness) {
    ImVec2 pts[4];
    for (int i = 0; i < 4; ++i) pts[i] = im(h.screen[i]);
    dl->AddPolyline(pts, 4, col, ImDrawFlags_Closed, thickness);
}

bool near(Vec2 a, Vec2 b, float r) { return (a - b).length() <= r; }

bool spaceHeld() { return ImGui::IsKeyDown(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput; }

} // namespace

void ViewportPanel::draw(EditorContext& ctx) {
    if (!open) return;
    ui::nextPanelWindowClass();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin(kWindowName, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    Composition& c = ctx.comp();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.0f);
    size.y = std::max(size.y, 1.0f);
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    canvasMin_ = vec(p0);
    canvasMax_ = vec(p1);
    compSize_ = {static_cast<float>(c.width), static_cast<float>(c.height)};

    if (autoFit_) {
        zoom_ = std::clamp(std::min((size.x - 64.0f) / c.width, (size.y - 64.0f) / c.height), kMinZoom, kMaxZoom);
        pan_ = {0.0f, 0.0f};
    }
    origin_ = (canvasMin_ + canvasMax_) * 0.5f + pan_ - compSize_ * (zoom_ * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, kCanvasColor);

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // the wheel zooms here, nothing else scrolls

    handleDrop(ctx);
    handleNavigation(hovered);
    handleSelectionInput(ctx, hovered);

    dl->PushClipRect(p0, p1, true);
    const render::View view{origin_, zoom_};
    const Vec2 compMax = view.toScreen(compSize_);
    dl->AddRectFilled(ImVec2(origin_.x + 4, origin_.y + 6), ImVec2(compMax.x + 4, compMax.y + 6), IM_COL32(0, 0, 0, 90), 2);
    render::drawComposition(dl, c, ctx.time, view);
    dl->AddRect(im(origin_), im(compMax), theme::kBorder);
    drawOverlay(ctx, hovered);
    dl->PopClipRect();

    drawHud(ctx);
    ImGui::End();
}

// --- navigation ---------------------------------------------------------------------

void ViewportPanel::zoomAround(Vec2 screenPoint, float newZoom) {
    newZoom = std::clamp(newZoom, kMinZoom, kMaxZoom);
    const Vec2 compPoint = (screenPoint - origin_) / zoom_;
    const Vec2 newOrigin = screenPoint - compPoint * newZoom;
    pan_ = newOrigin + compSize_ * (newZoom * 0.5f) - (canvasMin_ + canvasMax_) * 0.5f;
    origin_ = newOrigin;
    zoom_ = newZoom;
    autoFit_ = false;
}

void ViewportPanel::zoomBy(float factor) { zoomAround((canvasMin_ + canvasMax_) * 0.5f, zoom_ * factor); }

void ViewportPanel::setZoom(float zoom) { zoomAround((canvasMin_ + canvasMax_) * 0.5f, zoom); }

void ViewportPanel::handleNavigation(bool hovered) {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 mouse = vec(io.MousePos);

    if (hovered && io.MouseWheel != 0.0f) zoomAround(mouse, zoom_ * std::pow(1.15f, io.MouseWheel));

    // Middle or right button drag pans the view.
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
        pan_ += vec(io.MouseDelta);
        origin_ += vec(io.MouseDelta);
        autoFit_ = false;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) requestFit();

    if (hovered && drag_ == Drag::None && (spaceHeld() || io.KeyAlt)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

void ViewportPanel::handleDrop(EditorContext& ctx) {
    dropHover_ = false;
    if (!ImGui::BeginDragDropTarget()) return;
    constexpr ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjectPayload, flags)) {
        const Vec2 mouse = vec(ImGui::GetIO().MousePos);
        dropKind_ = *static_cast<const int*>(payload->Data);
        dropPos_ = (mouse - origin_) / zoom_;
        dropPos_ = {std::round(dropPos_.x), std::round(dropPos_.y)};
        dropHover_ = true;
        if (payload->IsDelivery()) {
            if (dropKind_ < 0) ctx.addText(dropPos_);
            else ctx.addShape(static_cast<ShapeKind>(dropKind_), dropPos_);
            dropHover_ = false;
        }
    }
    ImGui::EndDragDropTarget();
}

// --- selection and transform -------------------------------------------------------------

void ViewportPanel::handleSelectionInput(EditorContext& ctx, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    const Composition& comp = ctx.comp();
    const render::View view{origin_, zoom_};
    const Vec2 mouse = vec(io.MousePos);
    const Vec2 mouseComp = view.toComp(mouse);
    const double t = ctx.time;
    const bool additive = io.KeyShift || io.KeyCtrl;

    // Scale/rotate handles exist only for a single editable layer.
    Layer* single = ctx.selection.size() == 1 ? ctx.selected() : nullptr;
    if (single && (single->locked || !single->activeAt(t))) single = nullptr;
    Handles h{};
    const bool haveHandles = single && computeHandles(*single, t, view, h);

    if (hovered && drag_ == Drag::None && haveHandles && !spaceHeld()) {
        for (int i = 0; i < 4; ++i)
            if (near(mouse, h.screen[i], kHandleRadius + 3))
                ImGui::SetMouseCursor(i % 2 == 0 ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
        if (near(mouse, h.rotate, kHandleRadius + 4)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        drag_ = Drag::None;
        dragMoved_ = false;
        dragStartScreen_ = mouse;
        dragStartMouse_ = mouseComp;
        if (spaceHeld() || io.KeyAlt) {
            drag_ = Drag::Pan;
            if (spaceHeld()) spaceUsed_ = true;
        } else {
            if (haveHandles && !additive) {
                for (int i = 0; i < 4; ++i)
                    if (near(mouse, h.screen[i], kHandleRadius + 3)) {
                        drag_ = Drag::Scale;
                        dragCorner_ = h.local[i];
                    }
                if (near(mouse, h.rotate, kHandleRadius + 4)) drag_ = Drag::Rotate;
                if (drag_ != Drag::None) {
                    dragLayer_ = single->id;
                    dragStartPosition_ = single->transform.position.valueAt(t);
                    dragStartScale_ = single->transform.scale.valueAt(t);
                    dragStartRotation_ = single->transform.rotation.valueAt(t);
                    const Vec2 d = mouse - view.toScreen(dragStartPosition_);
                    dragStartAngle_ = radToDeg(std::atan2(d.y, d.x));
                }
            }
            if (drag_ == Drag::None) {
                const uint64_t hit = render::hitTest(comp, t, mouseComp);
                if (hit) {
                    if (additive) ctx.toggleSelected(hit);
                    else if (!ctx.isSelected(hit)) ctx.select(hit);
                    else { // keep the group, make the clicked layer primary
                        std::erase(ctx.selection, hit);
                        ctx.selection.push_back(hit);
                    }
                    if (ctx.isSelected(hit)) {
                        drag_ = Drag::Move;
                        dragLayer_ = hit;
                        dragStartPositions_.clear();
                        for (Layer* l : ctx.selectedLayers())
                            if (!l->locked) dragStartPositions_.emplace_back(l->id, l->transform.position.valueAt(t));
                    }
                } else {
                    drag_ = Drag::Marquee;
                    marqueeBase_ = additive ? ctx.selection : std::vector<uint64_t>{};
                    if (!additive) ctx.select(0);
                }
            }
        }
    }

    if (drag_ == Drag::None) return;
    if (!ImGui::IsItemActive() || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // A plain click inside a multi-selection narrows it to the clicked layer.
        if (drag_ == Drag::Move && !dragMoved_ && !additive) ctx.select(dragLayer_);
        drag_ = Drag::None;
        return;
    }
    if ((mouse - dragStartScreen_).length() > 2.0f) dragMoved_ = true;

    switch (drag_) {
    case Drag::Pan:
        pan_ += vec(io.MouseDelta);
        autoFit_ = false;
        return;
    case Drag::Marquee: {
        const Vec2 a{std::min(dragStartScreen_.x, mouse.x), std::min(dragStartScreen_.y, mouse.y)};
        const Vec2 b{std::max(dragStartScreen_.x, mouse.x), std::max(dragStartScreen_.y, mouse.y)};
        ctx.selection = marqueeBase_;
        for (const Layer& l : comp.layers)
            if (l.activeAt(t) && !l.locked && !ctx.isSelected(l.id) && layerOverlapsRect(l, t, view, a, b))
                ctx.selection.push_back(l.id);
        ctx.clearKeySelection();
        return;
    }
    default: break;
    }

    if (!dragMoved_) return;
    switch (drag_) {
    case Drag::Move: {
        Vec2 delta = mouseComp - dragStartMouse_;
        if (io.KeyShift) (std::fabs(delta.x) > std::fabs(delta.y) ? delta.y : delta.x) = 0.0f;
        for (const auto& [id, start] : dragStartPositions_)
            if (Layer* l = ctx.comp().findLayer(id)) l->transform.position.set(t, start + delta);
        break;
    }
    case Drag::Scale: {
        Layer* layer = ctx.comp().findLayer(dragLayer_);
        if (!layer) break;
        auto& tr = layer->transform;
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
        Layer* layer = ctx.comp().findLayer(dragLayer_);
        if (!layer) break;
        const Vec2 d = mouse - view.toScreen(dragStartPosition_);
        float r = dragStartRotation_ + radToDeg(std::atan2(d.y, d.x)) - dragStartAngle_;
        if (io.KeyShift) r = std::round(r / 15.0f) * 15.0f;
        layer->transform.rotation.set(t, r);
        break;
    }
    default: break;
    }
    ctx.touch();
}

// --- drawing ------------------------------------------------------------------------

void ViewportPanel::drawOverlay(EditorContext& ctx, bool hovered) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::View view{origin_, zoom_};
    const ImGuiIO& io = ImGui::GetIO();
    const double t = ctx.time;
    Composition& comp = ctx.comp();

    // Hover outline for the layer under the cursor.
    if (drag_ == Drag::None && hovered && !spaceHeld()) {
        const uint64_t hover = render::hitTest(comp, t, view.toComp(vec(io.MousePos)));
        Handles h{};
        if (hover && !ctx.isSelected(hover) && computeHandles(*comp.findLayer(hover), t, view, h))
            outline(dl, h, IM_COL32(232, 185, 49, 110), 1.0f);
    }

    // Selection outlines.
    const auto selected = ctx.selectedLayers();
    Vec2 groupMin{1e9f, 1e9f}, groupMax{-1e9f, -1e9f};
    int visibleSelected = 0;
    for (const Layer* l : selected) {
        Handles h{};
        if (!l->activeAt(t) || !computeHandles(*l, t, view, h)) continue;
        outline(dl, h, theme::kAccent, l->id == ctx.primaryId() ? 1.5f : 1.0f);
        for (const Vec2& p : h.screen) {
            groupMin = {std::min(groupMin.x, p.x), std::min(groupMin.y, p.y)};
            groupMax = {std::max(groupMax.x, p.x), std::max(groupMax.y, p.y)};
        }
        ++visibleSelected;
    }
    if (visibleSelected > 1)
        dl->AddRect(ImVec2(groupMin.x - 4, groupMin.y - 4), ImVec2(groupMax.x + 4, groupMax.y + 4),
                    IM_COL32(232, 185, 49, 90), 2.0f, 0, 1.0f);

    // Handles for a single selection.
    if (selected.size() == 1 && selected[0]->activeAt(t)) {
        const Layer* sel = selected[0];
        Handles h{};
        if (computeHandles(*sel, t, view, h)) {
            dl->AddCircle(im(h.pivot), 5.0f, theme::kAccent, 0, 1.5f);
            dl->AddLine(ImVec2(h.pivot.x - 8, h.pivot.y), ImVec2(h.pivot.x + 8, h.pivot.y), theme::kAccent, 1.0f);
            dl->AddLine(ImVec2(h.pivot.x, h.pivot.y - 8), ImVec2(h.pivot.x, h.pivot.y + 8), theme::kAccent, 1.0f);
            if (!sel->locked) {
                dl->AddLine(im(h.topMid), im(h.rotate), theme::kAccent, 1.0f);
                dl->AddCircleFilled(im(h.rotate), kHandleRadius, theme::kBgPanel);
                dl->AddCircle(im(h.rotate), kHandleRadius, theme::kAccent, 0, 1.5f);
                for (const Vec2& c : h.screen) {
                    const ImVec2 a(c.x - kHandleRadius, c.y - kHandleRadius), b(c.x + kHandleRadius, c.y + kHandleRadius);
                    dl->AddRectFilled(a, b, theme::kBgPanel, 2.0f);
                    dl->AddRect(a, b, theme::kAccent, 2.0f, 0, 1.5f);
                }
            }
        }
    }

    // Box selection.
    if (drag_ == Drag::Marquee && dragMoved_) {
        const ImVec2 a(std::min(dragStartScreen_.x, io.MousePos.x), std::min(dragStartScreen_.y, io.MousePos.y));
        const ImVec2 b(std::max(dragStartScreen_.x, io.MousePos.x), std::max(dragStartScreen_.y, io.MousePos.y));
        dl->AddRectFilled(a, b, IM_COL32(232, 185, 49, 22));
        dl->AddRect(a, b, IM_COL32(232, 185, 49, 160));
    }

    // Live position readout while moving.
    if (drag_ == Drag::Move && dragMoved_) {
        if (const Layer* l = comp.findLayer(dragLayer_)) {
            const Vec2 p = l->transform.position.valueAt(t);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "X %.0f   Y %.0f", p.x, p.y);
            const ImVec2 at(io.MousePos.x + 16, io.MousePos.y + 18);
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddRectFilled(ImVec2(at.x - 6, at.y - 3), ImVec2(at.x + ts.x + 6, at.y + ts.y + 3), IM_COL32(14, 14, 16, 220), 4);
            dl->AddText(at, theme::kText, buf);
        }
    }

    // Ghost of an object being dragged in from the Objects panel.
    if (dropHover_) {
        Layer ghost = dropKind_ < 0 ? makeTextLayer("Your text", dropPos_, comp.duration)
                                    : makeShapeLayer(static_cast<ShapeKind>(dropKind_), dropPos_, comp.duration);
        ghost.transform.opacity.base = 55.0f;
        render::drawLayer(dl, ghost, t, view.matrix() * ghost.matrixAt(t));
        const Vec2 s = view.toScreen(dropPos_);
        dl->AddLine(ImVec2(s.x - 10, s.y), ImVec2(s.x + 10, s.y), theme::kAccent, 1.5f);
        dl->AddLine(ImVec2(s.x, s.y - 10), ImVec2(s.x, s.y + 10), theme::kAccent, 1.5f);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "X %.0f   Y %.0f", dropPos_.x, dropPos_.y);
        dl->AddText(ImVec2(s.x + 14, s.y - ImGui::GetTextLineHeight() - 12), theme::kAccent, buf); // above the drag tooltip
    }
}

void ViewportPanel::drawHud(EditorContext& ctx) {
    const Composition& c = ctx.comp();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lineH = ImGui::GetFrameHeight();
    const float y = canvasMax_.y - lineH - 10;

    char info[64];
    std::snprintf(info, sizeof(info), "%d × %d", c.width, c.height);
    dl->AddText(ImVec2(canvasMin_.x + 12, y + (lineH - ImGui::GetTextLineHeight()) * 0.5f), theme::kTextFaint, info);

    // Zoom controls, bottom right: [-] [ 50% v ] [+] [Fit]
    const float comboW = 76.0f, btn = lineH;
    float x = canvasMax_.x - 12 - (btn * 2 + comboW + 46 + 4 * 3);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 30, 34, 220));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 30, 34, 220));
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (ui::iconButton("##zoomOut", ui::Icon::ZoomOut, btn, "Zoom out (Ctrl+-)")) zoomBy(1.0f / 1.25f);
    x += btn + 4;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(comboW);
    char label[16];
    std::snprintf(label, sizeof(label), "%.0f%%", zoom_ * 100.0f);
    if (ImGui::BeginCombo("##zoomLevel", label, ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Fit", autoFit_)) requestFit();
        for (float z : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
            char item[16];
            std::snprintf(item, sizeof(item), "%.0f%%", z * 100.0f);
            if (ImGui::Selectable(item, !autoFit_ && std::fabs(zoom_ - z) < 1e-3f)) setZoom(z);
        }
        ImGui::EndCombo();
    }
    x += comboW + 4;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (ui::iconButton("##zoomIn", ui::Icon::ZoomIn, btn, "Zoom in (Ctrl+=)")) zoomBy(1.25f);
    x += btn + 4;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (ImGui::Button("Fit", ImVec2(46, 0))) requestFit();
    ui::tooltip("Fit composition to view (Ctrl+0)\nWheel: zoom  ·  Middle/right drag or Space+drag: pan");
    ImGui::PopStyleColor(2);
}

} // namespace bemotion
