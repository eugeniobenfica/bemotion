#include "ui/panels/ViewportPanel.h"

#include "core/Snapping.h"
#include "render/Fonts.h"
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
constexpr float kRuler = 20.0f; // ruler band thickness
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
    h.pivot = view.sceneToScreen(layer.transform.position.valueAt(t));
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

// Axis-aligned bounds of a layer in composition pixels (as seen through `camera`).
bool compBox(const Layer& layer, double t, const Affine& camera, Vec2& mn, Vec2& mx) {
    Vec2 lmn, lmx;
    if (!render::localBounds(layer, t, lmn, lmx)) return false;
    const Affine m = camera * layer.matrixAt(t);
    const Vec2 corners[4] = {m.apply(lmn), m.apply({lmx.x, lmn.y}), m.apply(lmx), m.apply({lmn.x, lmx.y})};
    mn = mx = corners[0];
    for (const Vec2& c : corners) {
        mn = {std::min(mn.x, c.x), std::min(mn.y, c.y)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y)};
    }
    return true;
}

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
    world_ = throughCamera_ ? c.cameraAt(ctx.time) : Affine{};

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, kCanvasColor);

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // the wheel zooms here, nothing else scrolls

    snapLinesX_.clear();
    snapLinesY_.clear();
    handleDrop(ctx);
    handleNavigation(hovered);
    handleSelectionInput(ctx, hovered);

    dl->PushClipRect(p0, p1, true);
    const render::View view = sceneView();
    const Vec2 compMax = view.toScreen(compSize_);
    dl->AddRectFilled(ImVec2(origin_.x + 4, origin_.y + 6), ImVec2(compMax.x + 4, compMax.y + 6), IM_COL32(0, 0, 0, 90), 2);
    render::drawComposition(dl, c, ctx.time, view, true, nullptr, throughCamera_);
    dl->AddRect(im(origin_), im(compMax), theme::kBorder);
    drawGuides(ctx);
    drawOverlay(ctx, hovered);
    dl->PopClipRect();

    handleGuides(ctx); // rulers are items on top of the canvas
    drawRulers(ctx);
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
        if (ctx.snapping && !ImGui::GetIO().KeyCtrl) { // the new object's center snaps too
            collectSnapTargets(ctx, {});
            dropPos_ = dropPos_ + snapMove(dropPos_, dropPos_, {0, 0});
        }
        dropHover_ = true;
        if (payload->IsDelivery()) {
            const Vec2 scenePos = world_.inverse().apply(dropPos_); // where the camera shows that point
            if (dropKind_ == kDropCamera) ctx.addCamera();
            else if (dropKind_ < 0) ctx.addText(scenePos);
            else ctx.addShape(static_cast<ShapeKind>(dropKind_), scenePos);
            dropHover_ = false;
        }
    }
    ImGui::EndDragDropTarget();
}

// --- selection and transform -------------------------------------------------------------

void ViewportPanel::handleSelectionInput(EditorContext& ctx, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    const Composition& comp = ctx.comp();
    const render::View view = sceneView();
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
    if (int key = -1, part = 0; hovered && drag_ == Drag::None && !spaceHeld() && pathHit(ctx, mouse, key, part))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        drag_ = Drag::None;
        dragMoved_ = false;
        dragStartScreen_ = mouse;
        dragStartMouse_ = mouseComp;
        const int guide = guideAt(ctx, mouse);
        if (spaceHeld() || io.KeyAlt) {
            drag_ = Drag::Pan;
            if (spaceHeld()) spaceUsed_ = true;
        } else if (guide >= 0 && !ctx.lockGuides) {
            guideDrag_ = guide; // guides win over the layers underneath
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                guideEdit_ = guide;
                ImGui::OpenPopup("##guideEdit");
            }
        } else if (int key = -1, part = 0; !additive && pathHit(ctx, mouse, key, part)) {
            drag_ = part == 0 ? Drag::PathKey : Drag::PathHandle;
            pathKey_ = key;
            pathPart_ = part;
            dragLayer_ = ctx.selected()->id;
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
                    const Vec2 d = mouse - view.sceneToScreen(dragStartPosition_);
                    dragStartAngle_ = radToDeg(std::atan2(d.y, d.x));
                }
            }
            if (drag_ == Drag::None) {
                const uint64_t hit = render::hitTest(comp, t, view.screenToScene(mouse), !throughCamera_);
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
                        std::vector<uint64_t> moving;
                        dragBoxMin_ = {1e9f, 1e9f};
                        dragBoxMax_ = {-1e9f, -1e9f};
                        for (Layer* l : ctx.selectedLayers()) {
                            if (l->locked) continue;
                            dragStartPositions_.emplace_back(l->id, l->transform.position.valueAt(t));
                            moving.push_back(l->id);
                            Vec2 mn, mx;
                            if (l->activeAt(t) && compBox(*l, t, world_, mn, mx)) {
                                dragBoxMin_ = {std::min(dragBoxMin_.x, mn.x), std::min(dragBoxMin_.y, mn.y)};
                                dragBoxMax_ = {std::max(dragBoxMax_.x, mx.x), std::max(dragBoxMax_.y, mx.y)};
                            }
                        }
                        collectSnapTargets(ctx, moving);
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
        if (ctx.snapping && !io.KeyCtrl) delta = delta + snapMove(dragBoxMin_, dragBoxMax_, delta);
        const Vec2 sceneDelta = world_.inverse().applyVector(delta); // the camera may zoom or rotate the view
        for (const auto& [id, start] : dragStartPositions_)
            if (Layer* l = ctx.comp().findLayer(id)) l->transform.position.set(t, start + sceneDelta);
        break;
    }
    case Drag::Scale: {
        Layer* layer = ctx.comp().findLayer(dragLayer_);
        if (!layer) break;
        auto& tr = layer->transform;
        const Affine pre = Affine::translate(dragStartPosition_) * Affine::rotateDeg(dragStartRotation_);
        const Vec2 q = pre.inverse().apply(view.screenToScene(mouse));
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
        const Vec2 d = mouse - view.sceneToScreen(dragStartPosition_);
        float r = dragStartRotation_ + radToDeg(std::atan2(d.y, d.x)) - dragStartAngle_;
        if (io.KeyShift) r = std::round(r / 15.0f) * 15.0f;
        layer->transform.rotation.set(t, r);
        break;
    }
    case Drag::PathKey:
    case Drag::PathHandle: {
        Layer* layer = ctx.comp().findLayer(dragLayer_);
        if (!layer || pathKey_ < 0 || pathKey_ >= layer->transform.position.keyCount()) break;
        auto& keys = layer->transform.position.keys;
        auto& k = keys[pathKey_];
        const Vec2 p = view.screenToScene(mouse);
        if (drag_ == Drag::PathKey) {
            k.value = p;
        } else {
            Vec2 in, out;
            pathTangents(keys, static_cast<size_t>(pathKey_), in, out);
            const Vec2 handle = p - k.value;
            // Handles stay mirrored (smooth) unless Alt breaks them into a corner.
            const auto mirror = [&](Vec2 other) {
                const float hl = handle.length(), ol = other.length();
                if (io.KeyAlt || hl < 1e-4f) return other;
                return handle * (-(ol > 1e-4f ? ol : hl) / hl); // opposite direction, own length
            };
            if (pathPart_ == 1) {
                k.inTangent = handle;
                k.outTangent = mirror(out);
            } else {
                k.outTangent = handle;
                k.inTangent = mirror(in);
            }
            k.path = PathMode::Custom;
        }
        break;
    }
    default: break;
    }
    ctx.touch();
}

// --- motion path ------------------------------------------------------------------

Layer* ViewportPanel::pathLayer(EditorContext& ctx) const {
    if (ctx.selection.size() != 1) return nullptr;
    Layer* l = ctx.selected();
    if (!l || l->locked || !l->visible || l->transform.position.keyCount() < 2) return nullptr;
    return l;
}

bool ViewportPanel::pathHit(EditorContext& ctx, Vec2 screen, int& key, int& part) const {
    Layer* l = pathLayer(ctx);
    if (!l) return false;
    const render::View view = sceneView();
    const auto& keys = l->transform.position.keys;
    const int n = static_cast<int>(keys.size());
    for (int i = 0; i < n; ++i) {
        Vec2 in, out;
        pathTangents(keys, static_cast<size_t>(i), in, out);
        if (i > 0 && near(screen, view.sceneToScreen(keys[i].value + in), kHandleRadius + 2)) return key = i, part = 1, true;
        if (i + 1 < n && near(screen, view.sceneToScreen(keys[i].value + out), kHandleRadius + 2)) return key = i, part = 2, true;
    }
    for (int i = 0; i < n; ++i) {
        // A key at the playhead sits on the layer itself: let the normal move handle it.
        if (std::fabs(keys[i].time - ctx.time) < kTimeEpsilon) continue;
        if (near(screen, view.sceneToScreen(keys[i].value), kHandleRadius + 2)) return key = i, part = 0, true;
    }
    return false;
}

void ViewportPanel::drawMotionPath(EditorContext& ctx) {
    Layer* l = pathLayer(ctx);
    if (!l) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::View view = sceneView();
    const Composition& comp = ctx.comp();
    const auto& keys = l->transform.position.keys;
    const int n = static_cast<int>(keys.size());
    const ImU32 line = IM_COL32(232, 185, 49, 150);

    // The curve itself.
    for (int i = 0; i + 1 < n; ++i) {
        const PathSegment seg = pathSegment(keys, static_cast<size_t>(i));
        constexpr int kSteps = 32;
        ImVec2 pts[kSteps + 1];
        for (int s = 0; s <= kSteps; ++s) pts[s] = im(view.sceneToScreen(seg.at(static_cast<float>(s) / kSteps)));
        dl->AddPolyline(pts, kSteps + 1, line, 0, 1.5f);
    }
    // One dot per frame: the spacing shows the speed.
    const int f0 = comp.frameAt(keys.front().time), f1 = comp.frameAt(keys.back().time);
    for (int f = f0; f <= f1; ++f)
        dl->AddCircleFilled(im(view.sceneToScreen(l->transform.position.valueAt(comp.timeOfFrame(f)))), 1.6f,
                            IM_COL32(255, 255, 255, 120));
    // Keys and their handles.
    for (int i = 0; i < n; ++i) {
        const Vec2 k = view.sceneToScreen(keys[i].value);
        Vec2 in, out;
        pathTangents(keys, static_cast<size_t>(i), in, out);
        const bool custom = keys[i].path == PathMode::Custom;
        const ImU32 handleCol = custom ? theme::kAccent : IM_COL32(255, 255, 255, 140);
        for (int side = 0; side < 2; ++side) {
            if ((side == 0 && i == 0) || (side == 1 && i + 1 == n)) continue;
            const Vec2 h = view.sceneToScreen(keys[i].value + (side == 0 ? in : out));
            dl->AddLine(im(k), im(h), IM_COL32(255, 255, 255, 70), 1.0f);
            dl->AddCircleFilled(im(h), 3.5f, handleCol);
        }
        const bool current = std::fabs(keys[i].time - ctx.time) < kTimeEpsilon;
        const ImVec2 a(k.x - 4, k.y - 4), b(k.x + 4, k.y + 4);
        dl->AddRectFilled(a, b, current ? theme::kAccent : theme::kBgDeep);
        dl->AddRect(a, b, theme::kAccent, 0.0f, 0, 1.5f);
    }
}

// --- drawing ------------------------------------------------------------------------

void ViewportPanel::drawOverlay(EditorContext& ctx, bool hovered) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::View view = sceneView();
    const ImGuiIO& io = ImGui::GetIO();
    const double t = ctx.time;
    Composition& comp = ctx.comp();

    // Hover outline for the layer under the cursor.
    int pathKey = -1, pathPart = 0;
    if (drag_ == Drag::None && hovered && !spaceHeld() && !pathHit(ctx, vec(io.MousePos), pathKey, pathPart)) {
        const uint64_t hover = render::hitTest(comp, t, view.screenToScene(vec(io.MousePos)), !throughCamera_);
        Handles h{};
        if (hover && !ctx.isSelected(hover) && computeHandles(*comp.findLayer(hover), t, view, h))
            outline(dl, h, IM_COL32(232, 185, 49, 110), 1.0f);
    }

    // Free view: show what each camera sees.
    if (!throughCamera_)
        for (const Layer& l : comp.layers) {
            Handles h{};
            if (l.type != LayerType::Camera || !l.activeAt(t) || !computeHandles(l, t, view, h)) continue;
            outline(dl, h, IM_COL32(64, 196, 255, 220), 1.5f);
            const ImVec2 at(std::max(h.screen[0].x + 6, canvasMin_.x + kRuler + 6), std::max(h.screen[0].y + 4, canvasMin_.y + kRuler + 4));
            dl->AddText(at, IM_COL32(64, 196, 255, 255), l.name.c_str()); // kept clear of the rulers
        }

    drawMotionPath(ctx);

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

    // Smart-guide lines for whatever snapped this frame.
    for (float x : snapLinesX_) {
        const float sx = view.toScreen({x, 0}).x;
        dl->AddLine(ImVec2(sx, canvasMin_.y), ImVec2(sx, canvasMax_.y), theme::kSnap, 1.0f);
    }
    for (float y : snapLinesY_) {
        const float sy = view.toScreen({0, y}).y;
        dl->AddLine(ImVec2(canvasMin_.x, sy), ImVec2(canvasMax_.x, sy), theme::kSnap, 1.0f);
    }

    // Ghost of an object being dragged in from the Objects panel.
    if (dropHover_) {
        const Vec2 scenePos = world_.inverse().apply(dropPos_);
        Layer ghost = dropKind_ == kDropCamera ? makeCameraLayer(compSize_, comp.duration)
                      : dropKind_ < 0          ? makeTextLayer("Your text", scenePos, comp.duration)
                                               : makeShapeLayer(static_cast<ShapeKind>(dropKind_), scenePos, comp.duration);
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
    float x = canvasMax_.x - 12 - (btn * 3 + comboW + 46 + 4 * 3 + 12);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 30, 34, 220));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 30, 34, 220));
    const bool hasCamera = std::any_of(c.layers.begin(), c.layers.end(), [](const Layer& l) { return l.type == LayerType::Camera; });
    if (hasCamera) {
        ImGui::SetCursorScreenPos(ImVec2(x - btn - 6, y));
        if (ui::iconButton("##camView", ui::Icon::Camera, btn,
                           throughCamera_ ? "Looking through the camera (click for the free view)"
                                          : "Free view: camera frames shown (click to look through the camera)",
                           throughCamera_))
            throughCamera_ = !throughCamera_;
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (ui::iconButton("##snap", ui::Icon::Magnet, btn, "Snapping (S) - hold Ctrl while dragging to bypass", ctx.snapping))
        ctx.snapping = !ctx.snapping;
    x += btn + 12;
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

// --- snapping & guides ------------------------------------------------------------------

void ViewportPanel::collectSnapTargets(EditorContext& ctx, const std::vector<uint64_t>& exclude) {
    const Composition& comp = ctx.comp();
    snapTargetsX_ = {0.0, comp.width * 0.5, static_cast<double>(comp.width)};
    snapTargetsY_ = {0.0, comp.height * 0.5, static_cast<double>(comp.height)};
    if (ctx.showGuides)
        for (const Guide& g : comp.guides) (g.vertical ? snapTargetsX_ : snapTargetsY_).push_back(g.position);
    for (const Layer& l : comp.layers) {
        if (!l.activeAt(ctx.time) || std::find(exclude.begin(), exclude.end(), l.id) != exclude.end()) continue;
        Vec2 mn, mx;
        if (!compBox(l, ctx.time, world_, mn, mx)) continue;
        snapTargetsX_.insert(snapTargetsX_.end(), {mn.x, (mn.x + mx.x) * 0.5, mx.x});
        snapTargetsY_.insert(snapTargetsY_.end(), {mn.y, (mn.y + mx.y) * 0.5, mx.y});
    }
}

Vec2 ViewportPanel::snapMove(Vec2 boxMin, Vec2 boxMax, Vec2 delta) {
    const double threshold = 8.0 / zoom_; // 8 screen pixels
    Vec2 correction{0, 0};
    const SnapResult sx = snap1D({boxMin.x + delta.x, (boxMin.x + boxMax.x) * 0.5 + delta.x, boxMax.x + delta.x},
                                 snapTargetsX_, threshold);
    if (sx.snapped) {
        correction.x = static_cast<float>(sx.offset);
        snapLinesX_.push_back(static_cast<float>(sx.target));
    }
    const SnapResult sy = snap1D({boxMin.y + delta.y, (boxMin.y + boxMax.y) * 0.5 + delta.y, boxMax.y + delta.y},
                                 snapTargetsY_, threshold);
    if (sy.snapped) {
        correction.y = static_cast<float>(sy.offset);
        snapLinesY_.push_back(static_cast<float>(sy.target));
    }
    return correction;
}

int ViewportPanel::guideAt(EditorContext& ctx, Vec2 screen) const {
    if (!ctx.showGuides) return -1;
    const render::View view = sceneView();
    const auto& guides = ctx.comp().guides;
    for (int i = static_cast<int>(guides.size()) - 1; i >= 0; --i) {
        const Vec2 s = view.toScreen({guides[i].position, guides[i].position});
        if (guides[i].vertical ? std::fabs(screen.x - s.x) <= 4.0f : std::fabs(screen.y - s.y) <= 4.0f) return i;
    }
    return -1;
}

void ViewportPanel::handleGuides(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    const render::View view = sceneView();
    const Vec2 mouse = vec(io.MousePos);
    const Vec2 mouseComp = view.toComp(mouse);
    auto& guides = ctx.comp().guides;

    // Drag a guide out of a ruler.
    if (ctx.showRulers && ctx.showGuides) {
        ImGui::SetCursorScreenPos(ImVec2(canvasMin_.x + kRuler, canvasMin_.y));
        ImGui::InvisibleButton("##rulerTop", ImVec2(std::max(1.0f, canvasMax_.x - canvasMin_.x - kRuler), kRuler));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ui::tooltip("Drag into the canvas to add a horizontal guide");
        if (ImGui::IsItemActivated() && !ctx.lockGuides) {
            guides.push_back({false, std::round(mouseComp.y)});
            guideDrag_ = static_cast<int>(guides.size()) - 1;
        }
        ImGui::SetCursorScreenPos(ImVec2(canvasMin_.x, canvasMin_.y + kRuler));
        ImGui::InvisibleButton("##rulerLeft", ImVec2(kRuler, std::max(1.0f, canvasMax_.y - canvasMin_.y - kRuler)));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ui::tooltip("Drag into the canvas to add a vertical guide");
        if (ImGui::IsItemActivated() && !ctx.lockGuides) {
            guides.push_back({true, std::round(mouseComp.x)});
            guideDrag_ = static_cast<int>(guides.size()) - 1;
        }
    }

    // Hover feedback over an existing guide.
    const bool inCanvas =
        mouse.x >= canvasMin_.x && mouse.x < canvasMax_.x && mouse.y >= canvasMin_.y && mouse.y < canvasMax_.y;
    if (guideDrag_ < 0 && drag_ == Drag::None && inCanvas && !ctx.lockGuides && ImGui::IsWindowHovered())
        if (const int g = guideAt(ctx, mouse); g >= 0)
            ImGui::SetMouseCursor(guides[g].vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);

    // Move the guide being dragged; dropping it on a ruler (or outside) deletes it.
    if (guideDrag_ >= 0 && guideDrag_ < static_cast<int>(guides.size())) {
        Guide& g = guides[guideDrag_];
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double pos = std::round(g.vertical ? mouseComp.x : mouseComp.y);
            const double size = g.vertical ? compSize_.x : compSize_.y;
            if (ctx.snapping && !io.KeyCtrl)
                if (const SnapResult s = snap1D({pos}, {0.0, size * 0.5, size}, 8.0 / zoom_); s.snapped) pos = s.target;
            g.position = static_cast<float>(pos);
            ImGui::SetMouseCursor(g.vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        } else {
            const bool overRuler = mouse.x < canvasMin_.x + kRuler || mouse.y < canvasMin_.y + kRuler;
            if (overRuler || !inCanvas) guides.erase(guides.begin() + guideDrag_);
            guideDrag_ = -1;
        }
        ctx.touch();
    } else {
        guideDrag_ = -1;
    }

    // Double-click on a guide: exact position.
    if (ImGui::BeginPopup("##guideEdit")) {
        if (guideEdit_ >= 0 && guideEdit_ < static_cast<int>(guides.size())) {
            Guide& g = guides[guideEdit_];
            ImGui::TextUnformatted(g.vertical ? "Vertical guide" : "Horizontal guide");
            ImGui::SetNextItemWidth(160);
            if (ImGui::DragFloat("##pos", &g.position, 1.0f, -100000.0f, 100000.0f, "%.0f px")) ctx.touch();
            if (ImGui::Button("Delete Guide")) {
                guides.erase(guides.begin() + guideEdit_);
                guideEdit_ = -1;
                ctx.touch();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void ViewportPanel::drawGuides(EditorContext& ctx) {
    if (!ctx.showGuides) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::View view = sceneView();
    const auto& guides = ctx.comp().guides;
    for (int i = 0; i < static_cast<int>(guides.size()); ++i) {
        const Guide& g = guides[i];
        const Vec2 s = view.toScreen({g.position, g.position});
        const bool active = i == guideDrag_;
        const ImU32 col = active ? theme::kText : theme::kGuide;
        if (g.vertical) dl->AddLine(ImVec2(s.x, canvasMin_.y), ImVec2(s.x, canvasMax_.y), col, 1.0f);
        else dl->AddLine(ImVec2(canvasMin_.x, s.y), ImVec2(canvasMax_.x, s.y), col, 1.0f);
        if (active) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f px", g.position);
            const ImVec2 at = g.vertical ? ImVec2(s.x + 6, canvasMin_.y + kRuler + 6)
                                         : ImVec2(canvasMin_.x + kRuler + 6, s.y + 4);
            dl->AddText(at, theme::kText, buf);
        }
    }
}

void ViewportPanel::drawRulers(EditorContext& ctx) {
    if (!ctx.showRulers) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::View view = sceneView();
    const ImVec2 a = im(canvasMin_), b = im(canvasMax_);
    const ImU32 bg = IM_COL32(20, 20, 23, 245);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + kRuler), bg);
    dl->AddRectFilled(ImVec2(a.x, a.y + kRuler), ImVec2(a.x + kRuler, b.y), bg);
    dl->AddLine(ImVec2(a.x, a.y + kRuler), ImVec2(b.x, a.y + kRuler), theme::kBorder);
    dl->AddLine(ImVec2(a.x + kRuler, a.y), ImVec2(a.x + kRuler, b.y), theme::kBorder);

    // Tick spacing in composition pixels, at least ~60 screen pixels apart.
    float step = 5000.0f;
    for (float s : {1.f, 2.f, 5.f, 10.f, 25.f, 50.f, 100.f, 250.f, 500.f, 1000.f, 2500.f, 5000.f})
        if (s * zoom_ >= 60.0f) {
            step = s;
            break;
        }
    ImFont* font = Fonts::ui();
    const float fs = 10.5f;
    char label[16];
    // Top ruler (x).
    dl->PushClipRect(ImVec2(a.x + kRuler, a.y), ImVec2(b.x, a.y + kRuler), true);
    const float x0 = view.toComp(Vec2(a.x + kRuler, 0)).x, x1 = view.toComp(Vec2(b.x, 0)).x;
    for (float x = std::floor(x0 / step) * step; x <= x1; x += step) {
        const float sx = view.toScreen({x, 0}).x;
        dl->AddLine(ImVec2(sx, a.y + kRuler - 8), ImVec2(sx, a.y + kRuler), theme::kTextFaint);
        for (int k = 1; k < 5; ++k) {
            const float mx = view.toScreen({x + step * k / 5.0f, 0}).x;
            dl->AddLine(ImVec2(mx, a.y + kRuler - 3), ImVec2(mx, a.y + kRuler), theme::kBorder);
        }
        std::snprintf(label, sizeof(label), "%.0f", x);
        dl->AddText(font, fs, ImVec2(sx + 3, a.y + 2), theme::kTextFaint, label);
    }
    dl->PopClipRect();
    // Left ruler (y).
    dl->PushClipRect(ImVec2(a.x, a.y + kRuler), ImVec2(a.x + kRuler, b.y), true);
    const float y0 = view.toComp(Vec2(0, a.y + kRuler)).y, y1 = view.toComp(Vec2(0, b.y)).y;
    for (float y = std::floor(y0 / step) * step; y <= y1; y += step) {
        const float sy = view.toScreen({0, y}).y;
        dl->AddLine(ImVec2(a.x + kRuler - 8, sy), ImVec2(a.x + kRuler, sy), theme::kTextFaint);
        for (int k = 1; k < 5; ++k) {
            const float my = view.toScreen({0, y + step * k / 5.0f}).y;
            dl->AddLine(ImVec2(a.x + kRuler - 3, my), ImVec2(a.x + kRuler, my), theme::kBorder);
        }
        std::snprintf(label, sizeof(label), "%.0f", y);
        dl->AddText(font, fs, ImVec2(a.x + 2, sy + 2), theme::kTextFaint, label);
    }
    dl->PopClipRect();

    // Mouse position markers.
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (m.x > a.x + kRuler && m.x < b.x && m.y > a.y && m.y < b.y) {
        dl->AddLine(ImVec2(m.x, a.y), ImVec2(m.x, a.y + kRuler), theme::kAccent);
        dl->AddLine(ImVec2(a.x, m.y), ImVec2(a.x + kRuler, m.y), theme::kAccent);
    }
}

} // namespace bemotion
