// Graph editor of the timeline (see TimelinePanel.h).

#include "ui/panels/TimelinePanel.h"

#include "render/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bemotion {

namespace {

constexpr float kHit = 7.0f;       // pick radius (px)
constexpr float kListRow = 24.0f;  // property list row height

int components(const PropertyRef& p) {
    return p.kind == PropertyKind::Vec2 ? 2 : p.kind == PropertyKind::Float ? 1 : 0; // colors are not graphed
}

float keyValue(const PropertyRef& p, int i, int k) {
    if (p.kind == PropertyKind::Float) return p.asFloat().keys[i].value;
    const Vec2 v = p.asVec2().keys[i].value;
    return k == 0 ? v.x : v.y;
}

void setKeyValue(const PropertyRef& p, int i, int k, float value) {
    if (p.kind == PropertyKind::Float) {
        p.asFloat().keys[i].value = value;
    } else {
        Vec2& v = p.asVec2().keys[i].value;
        (k == 0 ? v.x : v.y) = value;
    }
}

float valueAt(const PropertyRef& p, double t, int k) {
    if (p.kind == PropertyKind::Float) return p.asFloat().valueAt(t);
    const Vec2 v = p.asVec2().valueAt(t);
    return k == 0 ? v.x : v.y;
}

// X / Y like After Effects (red / green); single values get a color per property.
ImU32 curveColor(int propIndex, int comp, int comps) {
    if (comps == 2) return comp == 0 ? IM_COL32(236, 96, 96, 255) : IM_COL32(96, 204, 120, 255);
    static constexpr ImU32 kColors[] = {IM_COL32(232, 185, 49, 255), IM_COL32(79, 142, 247, 255),
                                        IM_COL32(167, 123, 243, 255), IM_COL32(60, 198, 214, 255),
                                        IM_COL32(242, 140, 56, 255),  IM_COL32(217, 101, 168, 255)};
    return kColors[propIndex % std::size(kColors)];
}

// The segment's timing as a cubic-bezier (built-in easings start from their look-alike).
BezierEase curveOf(const AnimatedBase& anim, int i) {
    const Easing e = anim.keyEasing(i);
    if (e == Easing::Custom) return anim.keyCurve(i);
    if (e == Easing::Linear || e == Easing::Hold) return {1.0f / 3, 1.0f / 3, 2.0f / 3, 2.0f / 3};
    return bezierFor(e);
}

void setCurve(AnimatedBase& anim, int i, const BezierEase& c) {
    anim.setKeyEasing(i, Easing::Custom);
    anim.setKeyCurve(i, c);
}

std::string formatValue(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, std::fabs(v) >= 100.0f ? "%.0f" : "%.1f", v);
    return buf;
}

} // namespace

void TimelinePanel::applyEase(EditorContext& ctx, Ease ease) {
    for (const KeySelection& sel : ctx.selectedKeys) {
        Layer* layer = ctx.comp().findLayer(sel.layerId);
        if (!layer) continue;
        auto props = layer->properties();
        PropertyRef* p = layer->findProperty(props, sel.propertyId.c_str());
        if (!p) continue;
        AnimatedBase& anim = *p->anim;
        const int i = anim.findKeyByUid(sel.keyUid);
        if (i < 0) continue;
        const bool hasOut = i + 1 < anim.keyCount(), hasIn = i > 0;
        auto outgoing = [&](float x1, float y1) { // this key's side of the next segment
            if (!hasOut) return;
            BezierEase c = curveOf(anim, i);
            c.x1 = x1, c.y1 = y1;
            setCurve(anim, i, c);
        };
        auto incoming = [&](float x2, float y2) { // this key's side of the previous segment
            if (!hasIn) return;
            BezierEase c = curveOf(anim, i - 1);
            c.x2 = x2, c.y2 = y2;
            setCurve(anim, i - 1, c);
        };
        switch (ease) {
        case Ease::EasyEase: outgoing(1.0f / 3, 0.0f), incoming(2.0f / 3, 1.0f); break;
        case Ease::EaseIn: incoming(2.0f / 3, 1.0f); break;
        case Ease::EaseOut: outgoing(1.0f / 3, 0.0f); break;
        case Ease::Linear: outgoing(1.0f / 3, 1.0f / 3), incoming(2.0f / 3, 2.0f / 3); break;
        case Ease::Hold:
            if (hasOut) anim.setKeyEasing(i, Easing::Hold);
            break;
        }
    }
    if (!ctx.selectedKeys.empty()) ctx.touch();
}

void TimelinePanel::drawGraphEditor(EditorContext& ctx, float x0, float top, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    Layer* layer = ctx.selected();
    auto hint = [&](const char* text) {
        dl->AddText(ImVec2(x0 + 16, top + 14), theme::kTextFaint, text);
    };
    if (!layer) {
        hint("Select a layer to edit the curves of its animated properties.");
        return;
    }
    auto props = layer->properties();
    std::vector<PropertyRef*> animated;
    for (PropertyRef& p : props)
        if (components(p) > 0 && p.anim->keyCount() > 0) animated.push_back(&p);
    if (animated.empty()) {
        hint("No animated properties: add keyframes (the diamond next to a property) to edit their curves here.");
        return;
    }

    // --- left: property list (click to show / hide a curve) -------------------------
    dl->AddRectFilled(ImVec2(x0, top), ImVec2(trackX0_ - 1, top + height), theme::shade(30));
    for (size_t pi = 0; pi < animated.size(); ++pi) {
        const PropertyRef& p = *animated[pi];
        const float y = top + 8 + kListRow * static_cast<float>(pi);
        ImGui::SetCursorScreenPos(ImVec2(x0 + 8, y));
        ImGui::PushID(p.id);
        const bool hidden = graphHidden_.count(p.id) > 0;
        if (ImGui::InvisibleButton("##show", ImVec2(leftWidth_ - 16, kListRow))) {
            if (hidden) graphHidden_.erase(p.id);
            else graphHidden_.insert(p.id);
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hovered) dl->AddRectFilled(ImVec2(x0 + 8, y), ImVec2(x0 + leftWidth_ - 8, y + kListRow), theme::kBgHover, 4.0f);
        const int comps = components(p);
        for (int k = 0; k < comps; ++k) {
            const ImU32 c = curveColor(static_cast<int>(pi), k, comps);
            const ImVec2 sw(x0 + 18 + k * 10.0f, y + kListRow * 0.5f);
            if (hidden) dl->AddCircle(sw, 4.0f, c, 0, 1.5f);
            else dl->AddCircleFilled(sw, 4.0f, c);
        }
        char label[128];
        std::snprintf(label, sizeof label, "%s  ·  %s", p.group, p.label);
        dl->AddText(ImVec2(x0 + 46, y + (kListRow - ImGui::GetFontSize()) * 0.5f), hidden ? theme::kTextFaint : theme::kTextDim,
                    label);
    }

    // --- right: the graph ---------------------------------------------------------------
    const float gx0 = trackX0_, gx1 = trackX0_ + trackW_;
    const float gy0 = top + 14, gy1 = top + height - 14;
    if (gy1 - gy0 < 20.0f) return;

    std::vector<PropertyRef*> shown;
    for (PropertyRef* p : animated)
        if (!graphHidden_.count(p->id)) shown.push_back(p);

    // Vertical range: fit what is visible (frozen while dragging so nothing jumps).
    if (graphDrag_.kind == GraphDrag::None && !shown.empty()) {
        float lo = 1e30f, hi = -1e30f;
        for (PropertyRef* p : shown)
            for (int k = 0; k < components(*p); ++k) {
                for (int i = 0; i < p->anim->keyCount(); ++i) lo = std::min(lo, keyValue(*p, i, k)), hi = std::max(hi, keyValue(*p, i, k));
                for (float x = gx0; x <= gx1; x += 6.0f) {
                    const float v = valueAt(*p, xToTime(x), k);
                    lo = std::min(lo, v), hi = std::max(hi, v);
                }
            }
        if (hi - lo < 1e-3f) lo -= 1.0f, hi += 1.0f;
        const float pad = (hi - lo) * 0.12f;
        graphMin_ = lo - pad, graphMax_ = hi + pad;
    }
    auto yOf = [&](float v) { return gy1 - (v - graphMin_) / (graphMax_ - graphMin_) * (gy1 - gy0); };
    auto vOf = [&](float y) { return graphMin_ + (gy1 - y) / (gy1 - gy0) * (graphMax_ - graphMin_); };

    // Grid with value labels.
    dl->PushClipRect(ImVec2(gx0, top), ImVec2(gx1, top + height), true);
    for (int g = 0; g <= 4; ++g) {
        const float v = graphMin_ + (graphMax_ - graphMin_) * (0.1f + 0.8f * g / 4.0f);
        const float y = yOf(v);
        dl->AddLine(ImVec2(gx0, y), ImVec2(gx1, y), theme::ink(14));
        dl->AddText(ImVec2(gx0 + 4, y - ImGui::GetFontSize() - 1), theme::kTextFaint, formatValue(v).c_str());
    }

    // Interaction surface (drawn items are hit-tested by hand).
    ImGui::SetCursorScreenPos(ImVec2(gx0, top));
    ImGui::InvisibleButton("##graph", ImVec2(trackW_, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 m = io.MousePos;

    struct Hit {
        GraphDrag::Kind kind;
        const PropertyRef* prop;
        int comp;
        uint32_t uid;
        int which;
        ImVec2 pos;
    };
    std::vector<Hit> hits;
    const Hit* hoverHit = nullptr;

    for (size_t si = 0; si < shown.size(); ++si) {
        const PropertyRef& p = *shown[si];
        AnimatedBase& anim = *p.anim;
        const int comps = components(p);
        const int pi = static_cast<int>(std::find(animated.begin(), animated.end(), shown[si]) - animated.begin());
        for (int k = 0; k < comps; ++k) {
            const ImU32 col = curveColor(pi, k, comps);
            // The curve.
            std::vector<ImVec2> pts;
            for (float x = gx0; x <= gx1 + 2.0f; x += 2.0f) pts.emplace_back(x, yOf(valueAt(p, xToTime(x), k)));
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, 0, 1.8f);
            // Easing handles of each segment (only for selected keys' segments, like After Effects).
            for (int i = 0; i + 1 < anim.keyCount(); ++i) {
                if (anim.keyEasing(i) == Easing::Hold) continue;
                const KeySelection a{layer->id, p.id, anim.keyUid(i)}, b{layer->id, p.id, anim.keyUid(i + 1)};
                const bool selA = ctx.isKeySelected(a), selB = ctx.isKeySelected(b);
                if (!selA && !selB) continue;
                const double ta = anim.keyTime(i), tb = anim.keyTime(i + 1);
                const float va = keyValue(p, i, k), vb = keyValue(p, i + 1, k);
                if (std::fabs(vb - va) < 1e-5f) continue; // flat: the timing curve has no height here
                const BezierEase c = curveOf(anim, i);
                const ImVec2 ka(timeToX(ta), yOf(va)), kb(timeToX(tb), yOf(vb));
                const ImVec2 h1(timeToX(ta + c.x1 * (tb - ta)), yOf(va + c.y1 * (vb - va)));
                const ImVec2 h2(timeToX(ta + c.x2 * (tb - ta)), yOf(va + c.y2 * (vb - va)));
                if (selA) {
                    dl->AddLine(ka, h1, theme::ink(110), 1.0f);
                    dl->AddCircleFilled(h1, 4.0f, theme::kAccent);
                    hits.push_back({GraphDrag::Handle, &p, k, anim.keyUid(i), 1, h1});
                }
                if (selB) {
                    dl->AddLine(kb, h2, theme::ink(110), 1.0f);
                    dl->AddCircleFilled(h2, 4.0f, theme::kAccent);
                    hits.push_back({GraphDrag::Handle, &p, k, anim.keyUid(i), 2, h2});
                }
            }
            // Keyframes.
            for (int i = 0; i < anim.keyCount(); ++i) {
                const ImVec2 kp(timeToX(anim.keyTime(i)), yOf(keyValue(p, i, k)));
                const bool sel = ctx.isKeySelected({layer->id, p.id, anim.keyUid(i)});
                const float r = 4.5f;
                dl->AddRectFilled(ImVec2(kp.x - r, kp.y - r), ImVec2(kp.x + r, kp.y + r), sel ? theme::kAccent : theme::kBgDeep);
                dl->AddRect(ImVec2(kp.x - r, kp.y - r), ImVec2(kp.x + r, kp.y + r), sel ? theme::kAccentHover : col, 0, 0, 1.5f);
                hits.push_back({GraphDrag::Key, &p, k, anim.keyUid(i), 0, kp});
            }
        }
    }
    dl->PopClipRect();

    // Handles win over keys when they overlap.
    if (hovered && graphDrag_.kind == GraphDrag::None) {
        float best = kHit;
        for (const Hit& h : hits) {
            const float d = std::hypot(h.pos.x - m.x, h.pos.y - m.y) - (h.kind == GraphDrag::Handle ? 1.5f : 0.0f);
            if (d < best) best = d, hoverHit = &h;
        }
        if (hoverHit) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const AnimatedBase& anim = *hoverHit->prop->anim;
            const int i = anim.findKeyByUid(hoverHit->uid);
            if (hoverHit->kind == GraphDrag::Key && i >= 0)
                ImGui::SetTooltip("%s  %s\n%.2f s  ·  %s", hoverHit->prop->label, components(*hoverHit->prop) == 2 ? (hoverHit->comp ? "Y" : "X") : "",
                                  anim.keyTime(i), formatValue(keyValue(*hoverHit->prop, i, hoverHit->comp)).c_str());
            else if (hoverHit->kind == GraphDrag::Handle)
                ImGui::SetTooltip("Drag to shape the easing (influence and speed)");
        }
    }

    // Press: pick a key or handle, or clear the selection.
    if (ImGui::IsItemActivated() || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (hoverHit) {
            const KeySelection sel{layer->id, hoverHit->prop->id, hoverHit->uid};
            if (hoverHit->kind == GraphDrag::Key) {
                if (io.KeyShift || io.KeyCtrl) {
                    if (ctx.isKeySelected(sel) && !right) std::erase(ctx.selectedKeys, sel);
                    else if (!ctx.isKeySelected(sel)) ctx.selectedKeys.push_back(sel);
                } else if (!ctx.isKeySelected(sel)) {
                    ctx.selectedKeys = {sel};
                }
            }
            if (right) {
                if (hoverHit->kind == GraphDrag::Key) openKeyMenu_ = true;
            } else {
                graphDrag_ = {hoverHit->kind, hoverHit->prop->id, hoverHit->comp, hoverHit->uid, hoverHit->which, m};
                if (const int i = hoverHit->prop->anim->findKeyByUid(hoverHit->uid); i >= 0) {
                    graphDrag_.startTime = hoverHit->prop->anim->keyTime(i);
                    graphDrag_.startValue = keyValue(*hoverHit->prop, i, hoverHit->comp);
                }
            }
        } else if (!right && !io.KeyShift && !io.KeyCtrl) {
            ctx.clearKeySelection();
        }
    }

    // Drag.
    if (graphDrag_.kind != GraphDrag::None) {
        if (std::hypot(m.x - graphDrag_.startMouse.x, m.y - graphDrag_.startMouse.y) > 3.0f) graphDrag_.moved = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            graphDrag_.kind = GraphDrag::None;
        } else if (!graphDrag_.moved) {
            // waiting for the drag threshold
        } else if (PropertyRef* p = layer->findProperty(props, graphDrag_.prop.c_str())) {
            AnimatedBase& anim = *p->anim;
            const int i = anim.findKeyByUid(graphDrag_.uid);
            const Composition& comp = ctx.comp();
            if (i >= 0 && graphDrag_.kind == GraphDrag::Key) {
                // Time stays between the neighbors, on whole frames; value follows the mouse.
                const double fd = comp.frameDuration();
                const double lo = i > 0 ? anim.keyTime(i - 1) + fd : 0.0;
                const double hi = i + 1 < anim.keyCount() ? anim.keyTime(i + 1) - fd : 1e9;
                // Relative to the grab point; Shift locks the time (value only).
                const double t = graphDrag_.startTime + (m.x - graphDrag_.startMouse.x) / pxPerSec_;
                if (!io.KeyShift) anim.setKeyTime(i, std::clamp(comp.snapToFrame(t), lo, std::max(lo, hi)));
                setKeyValue(*p, i, graphDrag_.comp, graphDrag_.startValue + vOf(m.y) - vOf(graphDrag_.startMouse.y));
                ctx.touch();
            } else if (i >= 0 && i + 1 < anim.keyCount() && graphDrag_.kind == GraphDrag::Handle) {
                const double ta = anim.keyTime(i), tb = anim.keyTime(i + 1);
                const float va = keyValue(*p, i, graphDrag_.comp), vb = keyValue(*p, i + 1, graphDrag_.comp);
                BezierEase c = curveOf(anim, i);
                const float fx = static_cast<float>(std::clamp((xToTime(m.x) - ta) / (tb - ta), 0.0, 1.0));
                const float fy = std::clamp((vOf(m.y) - va) / (vb - va), -1.0f, 2.0f);
                if (graphDrag_.which == 1) c.x1 = fx, c.y1 = fy;
                else c.x2 = fx, c.y2 = fy;
                setCurve(anim, i, c);
                ctx.touch();
            }
        }
    }
}

} // namespace bemotion
