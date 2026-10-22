#include "render/PluginCanvas.h"

#include "render/Fonts.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace bemotion::render {

namespace {

ImFont* fontFor(bool bold) {
    static const TextProps regular = [] {
        TextProps t;
        t.fontStyle = "Medium";
        return t;
    }();
    static const TextProps heavy = [] {
        TextProps t;
        t.fontStyle = "Bold";
        return t;
    }();
    return Fonts::forText(bold ? heavy : regular);
}

// Raster sizes are quantized so zooming does not bake endless font sizes.
float quantize(float px) {
    static constexpr float kSteps[] = {8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128, 160, 192, 256};
    for (float s : kSteps)
        if (s >= px) return s;
    return kSteps[std::size(kSteps) - 1];
}

} // namespace

unsigned PluginCanvas::col(Color c) const {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, std::clamp(c.a * opacity_, 0.0f, 1.0f)));
}

void PluginCanvas::rect(Vec2 a, Vec2 b, Color color, float radius) {
    if (b.x < a.x) std::swap(a.x, b.x);
    if (b.y < a.y) std::swap(a.y, b.y);
    const float r = std::min({radius, (b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f});
    dl_->AddRectFilled(ImVec2(a.x * k_, a.y * k_), ImVec2(b.x * k_, b.y * k_), col(color), std::max(0.0f, r) * k_);
}

void PluginCanvas::rectOutline(Vec2 a, Vec2 b, Color color, float width, float radius) {
    dl_->AddRect(ImVec2(a.x * k_, a.y * k_), ImVec2(b.x * k_, b.y * k_), col(color), std::max(0.0f, radius) * k_, 0,
                 width * k_);
}

void PluginCanvas::circle(Vec2 c, float radius, Color color) {
    dl_->AddCircleFilled(ImVec2(c.x * k_, c.y * k_), radius * k_, col(color));
}

void PluginCanvas::line(Vec2 a, Vec2 b, float width, Color color) {
    dl_->AddLine(ImVec2(a.x * k_, a.y * k_), ImVec2(b.x * k_, b.y * k_), col(color), width * k_);
}

void PluginCanvas::polyline(const std::vector<Vec2>& points, float width, Color color, bool closed) {
    if (points.size() < 2) return;
    std::vector<ImVec2> pts;
    pts.reserve(points.size());
    for (const Vec2& p : points) pts.emplace_back(p.x * k_, p.y * k_);
    dl_->AddPolyline(pts.data(), static_cast<int>(pts.size()), col(color), closed ? ImDrawFlags_Closed : 0, width * k_);
}

void PluginCanvas::polygon(const std::vector<Vec2>& points, Color color) {
    if (points.size() < 3) return;
    std::vector<ImVec2> pts;
    pts.reserve(points.size());
    for (const Vec2& p : points) pts.emplace_back(p.x * k_, p.y * k_);
    // Anti-aliasing expects clockwise winding (screen space, y down).
    float area = 0.0f;
    for (size_t i = 0; i < pts.size(); ++i) {
        const ImVec2& p = pts[i];
        const ImVec2& q = pts[(i + 1) % pts.size()];
        area += p.x * q.y - q.x * p.y;
    }
    if (area < 0.0f) std::reverse(pts.begin(), pts.end());
    dl_->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), col(color));
}

void PluginCanvas::arc(Vec2 c, float r0, float r1, float fromDeg, float toDeg, Color color) {
    if (toDeg < fromDeg) std::swap(fromDeg, toDeg);
    const float span = std::min(toDeg - fromDeg, 360.0f);
    if (span <= 0.01f || r1 <= 0.0f) return;
    r0 = std::clamp(r0, 0.0f, r1);
    const int steps = std::max(2, static_cast<int>(std::ceil(span / 3.0f)));
    auto at = [&](float r, float deg) {
        const float a = (deg - 90.0f) * kPi / 180.0f;
        return Vec2{c.x + std::cos(a) * r, c.y + std::sin(a) * r};
    };
    // Built from thin convex quads so long arcs stay correctly filled.
    for (int i = 0; i < steps; ++i) {
        const float a0 = fromDeg + span * i / steps, a1 = fromDeg + span * (i + 1) / steps;
        std::vector<Vec2> quad = {at(r1, a0), at(r1, a1), at(r0, a1), at(r0, a0)};
        if (r0 <= 0.0f) quad.pop_back();
        std::vector<ImVec2> pts;
        for (const Vec2& p : quad) pts.emplace_back(p.x * k_, p.y * k_);
        // Without AA fringes the quads join seamlessly.
        const ImDrawListFlags flags = dl_->Flags;
        dl_->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        dl_->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), col(color));
        dl_->Flags = flags;
    }
    // Smooth outer edge.
    std::vector<ImVec2> edge;
    for (int i = 0; i <= steps; ++i) {
        const Vec2 p = at(r1, fromDeg + span * i / steps);
        edge.emplace_back(p.x * k_, p.y * k_);
    }
    dl_->AddPolyline(edge.data(), static_cast<int>(edge.size()), col(color), 0, 1.0f);
}

float PluginCanvas::measure(const std::string& s, float size, bool bold) const {
    if (s.empty() || size <= 0.0f) return 0.0f;
    constexpr float kRef = 64.0f;
    return fontFor(bold)->CalcTextSizeA(kRef, FLT_MAX, 0.0f, s.c_str()).x * (size / kRef);
}

void PluginCanvas::text(const std::string& s, Vec2 at, float size, Color color, TextAnchor anchor, bool bold) {
    if (s.empty() || size <= 0.0f) return;
    const unsigned c = col(color);
    if ((c & IM_COL32_A_MASK) == 0) return;
    ImFont* font = fontFor(bold);
    const float target = size * k_;           // wanted size in raster units
    const float raster = quantize(target);    // size actually rasterized
    const float w = measure(s, size, bold);
    const float x = anchor == TextAnchor::Left ? at.x : anchor == TextAnchor::Center ? at.x - w * 0.5f : at.x - w;
    const ImVec2 origin(x * k_, (at.y - size * 0.5f) * k_);
    const int start = dl_->VtxBuffer.Size;
    const ImVec4 noClip(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    font->RenderText(dl_, raster, ImVec2(0, 0), c, noClip, s.c_str(), s.c_str() + s.size());
    const float f = target / raster;
    for (int i = start; i < dl_->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl_->VtxBuffer[i];
        v.pos = ImVec2(origin.x + v.pos.x * f, origin.y + v.pos.y * f);
    }
}

} // namespace bemotion::render
