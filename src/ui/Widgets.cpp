#include "ui/Widgets.h"

#include "app/PluginHost.h"
#include "core/Math.h"
#include "render/Fonts.h"
#include "render/GL.h"
#include "render/LogoMark.h"
#include "ui/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace bemotion::ui {

Icon iconForPlugin(const std::string& name) {
    if (name == "bars" || name == "chart") return Icon::BarChart;
    if (name == "line_chart") return Icon::LineChart;
    if (name == "pie" || name == "donut") return Icon::PieChart;
    if (name == "text") return Icon::Text;
    for (int i = 0; i < static_cast<int>(ShapeKind::Count); ++i) {
        std::string shape = shapeKindLabel(static_cast<ShapeKind>(i));
        for (char& ch : shape) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (shape == name) return iconForShape(static_cast<ShapeKind>(i));
    }
    return Icon::Plugin;
}

Icon iconForShape(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Rectangle: return Icon::Rectangle;
    case ShapeKind::Ellipse: return Icon::Ellipse;
    case ShapeKind::Polygon: return Icon::Polygon;
    case ShapeKind::Star: return Icon::Star;
    case ShapeKind::Line: return Icon::Line;
    case ShapeKind::Count: break;
    }
    return Icon::Rectangle;
}

namespace {

// The mark as a white texture with coverage in alpha, every mip level rasterized from the
// exact shape (render/LogoMark.h) so it stays crisp from 12 px to full screen.
constexpr float kLogoFill = 0.46f; // mark radius / texture size: leaves room for antialiasing

ImTextureID logoTexture() {
    static GLuint tex = 0;
    if (tex) return static_cast<ImTextureID>(static_cast<uintptr_t>(tex));
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    std::vector<unsigned char> px;
    for (int level = 0, size = 256; size >= 1; ++level, size /= 2) {
        px.assign(static_cast<size_t>(size) * size * 4, 255);
        const float r = size * kLogoFill;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const Vec2 p{(x + 0.5f - size * 0.5f) / r, (y + 0.5f - size * 0.5f) / r};
                const float d = logo::distance(p) * r; // in texels
                px[(static_cast<size_t>(y) * size + x) * 4 + 3] =
                    static_cast<unsigned char>(std::clamp(0.5f - d, 0.0f, 1.0f) * 255.0f);
            }
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl::kClampToEdge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl::kClampToEdge);
    return static_cast<ImTextureID>(static_cast<uintptr_t>(tex));
}

} // namespace

void drawLogoMark(ImDrawList* dl, ImVec2 center, float radius, ImU32 color) {
    const float half = radius * 0.5f / kLogoFill;
    dl->AddImage(logoTexture(), ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half),
                 ImVec2(0, 0), ImVec2(1, 1), color);
}

Icon iconForMedia(MediaKind kind) {
    switch (kind) {
    case MediaKind::Image: return Icon::Image;
    case MediaKind::Audio: return Icon::Audio;
    default: return Icon::Film;
    }
}

Icon iconForLayer(const Layer& layer) {
    if (layer.type == LayerType::Camera) return Icon::Camera;
    if (layer.type == LayerType::Compound) return Icon::Compound;
    if (layer.type == LayerType::Media) return layer.media.hasVideo ? Icon::Film : Icon::Audio;
    if (layer.type == LayerType::Plugin) {
        const PluginObjectType* type = PluginHost::get().findType(layer.plugin.objectType);
        return type ? iconForPlugin(type->icon) : Icon::Plugin;
    }
    return layer.type == LayerType::Text ? Icon::Text : iconForShape(layer.shape.kind);
}

namespace {

void regularShape(ImDrawList* dl, ImVec2 c, float r, int points, float innerRatio, ImU32 col, float th) {
    const int n = innerRatio > 0.0f ? points * 2 : points;
    ImVec2 pts[32];
    for (int i = 0; i < n; ++i) {
        const float a = -kPi * 0.5f + 2.0f * kPi * i / n;
        const float rr = (innerRatio > 0.0f && (i % 2)) ? r * innerRatio : r;
        pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    dl->AddPolyline(pts, n, col, ImDrawFlags_Closed, th);
}

} // namespace

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col) {
    const float h = size * 0.5f;
    const float th = std::max(1.25f, size * 0.09f);
    switch (icon) {
    case Icon::Text: {
        dl->AddLine(ImVec2(c.x - h * 0.75f, c.y - h * 0.7f), ImVec2(c.x + h * 0.75f, c.y - h * 0.7f), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.7f), ImVec2(c.x, c.y + h * 0.75f), col, th * 1.2f);
        break;
    }
    case Icon::Rectangle:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.6f), ImVec2(c.x + h * 0.8f, c.y + h * 0.6f), col, size * 0.12f, 0,
                    th);
        break;
    case Icon::Ellipse: dl->AddCircle(c, h * 0.78f, col, 0, th); break;
    case Icon::Polygon: regularShape(dl, c, h * 0.82f, 6, 0.0f, col, th); break;
    case Icon::Star: regularShape(dl, c, h * 0.88f, 5, 0.45f, col, th); break;
    case Icon::Line: dl->AddLine(ImVec2(c.x - h * 0.75f, c.y + h * 0.55f), ImVec2(c.x + h * 0.75f, c.y - h * 0.55f), col, th); break;
    case Icon::Eye:
    case Icon::EyeOff: {
        const float w = h * 0.95f;
        dl->AddEllipse(c, ImVec2(w, h * 0.55f), col, 0.0f, 0, th);
        dl->AddCircleFilled(c, h * 0.26f, col);
        if (icon == Icon::EyeOff) dl->AddLine(ImVec2(c.x - w * 0.8f, c.y + w * 0.8f), ImVec2(c.x + w * 0.8f, c.y - w * 0.8f), col, th);
        break;
    }
    case Icon::Lock:
    case Icon::Unlock: {
        const ImVec2 a(c.x - h * 0.6f, c.y - h * 0.05f), b(c.x + h * 0.6f, c.y + h * 0.75f);
        dl->AddRectFilled(a, b, col, 2.0f);
        const float sx = icon == Icon::Unlock ? h * 0.45f : 0.0f;
        dl->PathArcTo(ImVec2(c.x + sx, c.y - h * 0.3f), h * 0.38f, kPi, kPi * 2.0f);
        dl->PathStroke(col, 0, th);
        dl->AddLine(ImVec2(c.x - h * 0.38f + sx, c.y - h * 0.3f), ImVec2(c.x - h * 0.38f + sx, c.y), col, th);
        dl->AddLine(ImVec2(c.x + h * 0.38f + sx, c.y - h * 0.3f), ImVec2(c.x + h * 0.38f + sx, c.y), col, th);
        break;
    }
    case Icon::Play:
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.5f, c.y - h * 0.65f), ImVec2(c.x + h * 0.7f, c.y),
                              ImVec2(c.x - h * 0.5f, c.y + h * 0.65f), col);
        break;
    case Icon::Pause:
        dl->AddRectFilled(ImVec2(c.x - h * 0.55f, c.y - h * 0.6f), ImVec2(c.x - h * 0.15f, c.y + h * 0.6f), col, 1.5f);
        dl->AddRectFilled(ImVec2(c.x + h * 0.15f, c.y - h * 0.6f), ImVec2(c.x + h * 0.55f, c.y + h * 0.6f), col, 1.5f);
        break;
    case Icon::StepBack:
    case Icon::ToStart: {
        const float bar = icon == Icon::ToStart ? h * 0.6f : h * 0.35f;
        dl->AddRectFilled(ImVec2(c.x - bar - th, c.y - h * 0.55f), ImVec2(c.x - bar, c.y + h * 0.55f), col);
        dl->AddTriangleFilled(ImVec2(c.x + h * 0.5f, c.y - h * 0.55f), ImVec2(c.x + h * 0.5f, c.y + h * 0.55f),
                              ImVec2(c.x - bar + th * 0.3f, c.y), col);
        break;
    }
    case Icon::StepForward:
    case Icon::ToEnd: {
        const float bar = icon == Icon::ToEnd ? h * 0.6f : h * 0.35f;
        dl->AddRectFilled(ImVec2(c.x + bar, c.y - h * 0.55f), ImVec2(c.x + bar + th, c.y + h * 0.55f), col);
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.5f, c.y - h * 0.55f), ImVec2(c.x + bar - th * 0.3f, c.y),
                              ImVec2(c.x - h * 0.5f, c.y + h * 0.55f), col);
        break;
    }
    case Icon::Loop: {
        dl->PathArcTo(c, h * 0.62f, -kPi * 0.15f, kPi * 1.45f);
        dl->PathStroke(col, 0, th);
        const ImVec2 tip(c.x + std::cos(-kPi * 0.15f) * h * 0.62f, c.y + std::sin(-kPi * 0.15f) * h * 0.62f);
        dl->AddTriangleFilled(ImVec2(tip.x - h * 0.3f, tip.y - h * 0.1f), ImVec2(tip.x + h * 0.25f, tip.y - h * 0.25f),
                              ImVec2(tip.x + h * 0.05f, tip.y + h * 0.3f), col);
        break;
    }
    case Icon::Plus:
        dl->AddLine(ImVec2(c.x - h * 0.6f, c.y), ImVec2(c.x + h * 0.6f, c.y), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.6f), ImVec2(c.x, c.y + h * 0.6f), col, th * 1.2f);
        break;
    case Icon::Folder: {
        const ImVec2 a(c.x - h * 0.8f, c.y - h * 0.45f), b(c.x + h * 0.8f, c.y + h * 0.6f);
        dl->AddRect(a, b, col, 2.0f, 0, th);
        dl->AddLine(ImVec2(a.x + 1, a.y - h * 0.18f), ImVec2(c.x - h * 0.1f, a.y - h * 0.18f), col, th);
        break;
    }
    case Icon::ChevronRight:
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.25f, c.y - h * 0.4f), ImVec2(c.x + h * 0.3f, c.y),
                              ImVec2(c.x - h * 0.25f, c.y + h * 0.4f), col);
        break;
    case Icon::ChevronDown:
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.4f, c.y - h * 0.2f), ImVec2(c.x + h * 0.4f, c.y - h * 0.2f),
                              ImVec2(c.x, c.y + h * 0.32f), col);
        break;
    case Icon::ChevronUp:
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.4f, c.y + h * 0.2f), ImVec2(c.x, c.y - h * 0.32f),
                              ImVec2(c.x + h * 0.4f, c.y + h * 0.2f), col);
        break;
    case Icon::Diamond: drawDiamond(dl, c, h * 0.6f, col, col); break;
    case Icon::Trash: {
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y - h * 0.5f), ImVec2(c.x + h * 0.7f, c.y - h * 0.5f), col, th);
        dl->AddRect(ImVec2(c.x - h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.7f), col, 2.0f, 0, th);
        dl->AddLine(ImVec2(c.x - h * 0.2f, c.y - h * 0.75f), ImVec2(c.x + h * 0.2f, c.y - h * 0.75f), col, th);
        break;
    }
    case Icon::Logo:
        drawLogoMark(dl, c, h * 0.95f, col);
        break;
    case Icon::Pointer: {
        const ImVec2 pts[7] = {ImVec2(c.x - h * 0.45f, c.y - h * 0.8f), ImVec2(c.x + h * 0.55f, c.y + h * 0.15f),
                               ImVec2(c.x + h * 0.05f, c.y + h * 0.2f), ImVec2(c.x + h * 0.35f, c.y + h * 0.8f),
                               ImVec2(c.x + h * 0.12f, c.y + h * 0.9f), ImVec2(c.x - h * 0.15f, c.y + h * 0.3f),
                               ImVec2(c.x - h * 0.45f, c.y + h * 0.6f)};
        dl->AddConcavePolyFilled(pts, 7, col);
        break;
    }
    case Icon::Razor: {
        // Scissors: two rings and crossing blades.
        dl->AddCircle(ImVec2(c.x - h * 0.42f, c.y + h * 0.5f), h * 0.26f, col, 0, th);
        dl->AddCircle(ImVec2(c.x + h * 0.42f, c.y + h * 0.5f), h * 0.26f, col, 0, th);
        dl->AddLine(ImVec2(c.x - h * 0.25f, c.y + h * 0.3f), ImVec2(c.x + h * 0.45f, c.y - h * 0.85f), col, th);
        dl->AddLine(ImVec2(c.x + h * 0.25f, c.y + h * 0.3f), ImVec2(c.x - h * 0.45f, c.y - h * 0.85f), col, th);
        break;
    }
    case Icon::SplitPlayhead: {
        dl->AddLine(ImVec2(c.x, c.y - h * 0.9f), ImVec2(c.x, c.y + h * 0.9f), col, th);
        dl->AddRectFilled(ImVec2(c.x - h * 0.9f, c.y - h * 0.3f), ImVec2(c.x - h * 0.2f, c.y + h * 0.3f), col, 1.5f);
        dl->AddRectFilled(ImVec2(c.x + h * 0.2f, c.y - h * 0.3f), ImVec2(c.x + h * 0.9f, c.y + h * 0.3f), col, 1.5f);
        break;
    }
    case Icon::Export: {
        dl->AddLine(ImVec2(c.x, c.y + h * 0.3f), ImVec2(c.x, c.y - h * 0.8f), col, th);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.4f), ImVec2(c.x, c.y - h * 0.8f), col, th);
        dl->AddLine(ImVec2(c.x + h * 0.4f, c.y - h * 0.4f), ImVec2(c.x, c.y - h * 0.8f), col, th);
        const ImVec2 tray[4] = {ImVec2(c.x - h * 0.8f, c.y), ImVec2(c.x - h * 0.8f, c.y + h * 0.8f),
                                ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), ImVec2(c.x + h * 0.8f, c.y)};
        dl->AddPolyline(tray, 4, col, ImDrawFlags_None, th);
        break;
    }
    case Icon::Reset: {
        const float r = h * 0.62f;
        dl->PathArcTo(c, r, kPi * 0.15f, kPi * 1.75f);
        dl->PathStroke(col, 0, th);
        const ImVec2 tip(c.x + std::cos(kPi * 1.75f) * r, c.y + std::sin(kPi * 1.75f) * r);
        dl->AddTriangleFilled(ImVec2(tip.x + h * 0.38f, tip.y - h * 0.05f), ImVec2(tip.x - h * 0.08f, tip.y - h * 0.38f),
                              ImVec2(tip.x - h * 0.02f, tip.y + h * 0.3f), col);
        break;
    }
    case Icon::Magnet: {
        const float r = h * 0.5f;
        dl->PathArcTo(ImVec2(c.x, c.y + h * 0.05f), r, 0.0f, kPi);
        dl->PathStroke(col, 0, th * 1.8f);
        dl->AddLine(ImVec2(c.x - r, c.y + h * 0.05f), ImVec2(c.x - r, c.y - h * 0.75f), col, th * 1.8f);
        dl->AddLine(ImVec2(c.x + r, c.y + h * 0.05f), ImVec2(c.x + r, c.y - h * 0.75f), col, th * 1.8f);
        dl->AddLine(ImVec2(c.x - r - th, c.y - h * 0.42f), ImVec2(c.x - r + th, c.y - h * 0.42f), theme::kBgPanel, th);
        dl->AddLine(ImVec2(c.x + r - th, c.y - h * 0.42f), ImVec2(c.x + r + th, c.y - h * 0.42f), theme::kBgPanel, th);
        break;
    }
    case Icon::Gear: {
        for (int i = 0; i < 8; ++i) {
            const float a = kPi * 2.0f * i / 8.0f;
            dl->AddLine(ImVec2(c.x + std::cos(a) * h * 0.45f, c.y + std::sin(a) * h * 0.45f),
                        ImVec2(c.x + std::cos(a) * h * 0.85f, c.y + std::sin(a) * h * 0.85f), col, th * 1.6f);
        }
        dl->AddCircle(c, h * 0.52f, col, 0, th * 1.4f);
        dl->AddCircleFilled(c, h * 0.18f, col);
        break;
    }
    case Icon::Camera: {
        dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.45f), ImVec2(c.x + h * 0.35f, c.y + h * 0.5f), col, 2.0f, 0, th);
        dl->AddTriangleFilled(ImVec2(c.x + h * 0.4f, c.y), ImVec2(c.x + h * 0.9f, c.y - h * 0.4f),
                              ImVec2(c.x + h * 0.9f, c.y + h * 0.45f), col);
        dl->AddCircle(ImVec2(c.x - h * 0.25f, c.y + h * 0.02f), h * 0.2f, col, 0, th);
        break;
    }
    case Icon::Plugin: {
        dl->AddRect(ImVec2(c.x - h * 0.6f, c.y - h * 0.35f), ImVec2(c.x + h * 0.6f, c.y + h * 0.75f), col, 3.0f, 0, th);
        dl->AddLine(ImVec2(c.x - h * 0.28f, c.y - h * 0.35f), ImVec2(c.x - h * 0.28f, c.y - h * 0.85f), col, th * 1.4f);
        dl->AddLine(ImVec2(c.x + h * 0.28f, c.y - h * 0.35f), ImVec2(c.x + h * 0.28f, c.y - h * 0.85f), col, th * 1.4f);
        break;
    }
    case Icon::BarChart: {
        const float base = c.y + h * 0.75f;
        dl->AddLine(ImVec2(c.x - h * 0.85f, base), ImVec2(c.x + h * 0.85f, base), col, th);
        const float xs[3] = {-0.55f, 0.0f, 0.55f}, hs[3] = {0.7f, 1.35f, 1.0f};
        for (int i = 0; i < 3; ++i)
            dl->AddRectFilled(ImVec2(c.x + h * (xs[i] - 0.18f), base - h * hs[i]), ImVec2(c.x + h * (xs[i] + 0.18f), base - th),
                              col, 1.5f);
        break;
    }
    case Icon::LineChart: {
        const ImVec2 pts[4] = {ImVec2(c.x - h * 0.85f, c.y + h * 0.45f), ImVec2(c.x - h * 0.25f, c.y - h * 0.15f),
                               ImVec2(c.x + h * 0.2f, c.y + h * 0.2f), ImVec2(c.x + h * 0.85f, c.y - h * 0.6f)};
        dl->AddPolyline(pts, 4, col, 0, th * 1.2f);
        for (const ImVec2& p : pts) dl->AddCircleFilled(p, th * 1.3f, col);
        dl->AddLine(ImVec2(c.x - h * 0.85f, c.y + h * 0.8f), ImVec2(c.x + h * 0.85f, c.y + h * 0.8f), col, th);
        break;
    }
    case Icon::PieChart: {
        dl->AddCircle(c, h * 0.8f, col, 0, th);
        dl->PathLineTo(c);
        dl->PathArcTo(c, h * 0.8f, -kPi * 0.5f, kPi * 0.15f);
        dl->PathFillConvex(col);
        break;
    }
    case Icon::FxBlur:
        for (int i = 0; i < 3; ++i)
            dl->AddCircle(c, h * (0.35f + 0.25f * i), (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(255 - i * 80) << IM_COL32_A_SHIFT),
                          0, th);
        break;
    case Icon::FxColor:
        dl->AddCircle(c, h * 0.8f, col, 0, th);
        dl->PathArcTo(c, h * 0.8f, -kPi * 0.5f, kPi * 0.5f);
        dl->PathFillConvex(col);
        break;
    case Icon::FxChannel:
        for (int i = 0; i < 3; ++i) {
            const float a = -kPi * 0.5f + i * kPi * 2.0f / 3.0f;
            dl->AddCircle(ImVec2(c.x + std::cos(a) * h * 0.35f, c.y + std::sin(a) * h * 0.35f), h * 0.5f, col, 0, th);
        }
        break;
    case Icon::FxDistort: {
        ImVec2 pts[17];
        for (int i = 0; i <= 16; ++i) {
            const float x = -0.85f + 1.7f * i / 16.0f;
            pts[i] = ImVec2(c.x + x * h, c.y + std::sin(x * kPi * 1.6f) * h * 0.45f);
        }
        dl->AddPolyline(pts, 17, col, 0, th * 1.2f);
        break;
    }
    case Icon::FxGenerate: {
        const ImVec2 star[8] = {ImVec2(c.x, c.y - h * 0.85f), ImVec2(c.x + h * 0.2f, c.y - h * 0.2f), ImVec2(c.x + h * 0.85f, c.y),
                                ImVec2(c.x + h * 0.2f, c.y + h * 0.2f), ImVec2(c.x, c.y + h * 0.85f), ImVec2(c.x - h * 0.2f, c.y + h * 0.2f),
                                ImVec2(c.x - h * 0.85f, c.y), ImVec2(c.x - h * 0.2f, c.y - h * 0.2f)};
        dl->AddConcavePolyFilled(star, 8, col);
        break;
    }
    case Icon::FxPerspective:
        dl->AddRectFilled(ImVec2(c.x - h * 0.4f, c.y - h * 0.4f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f),
                          (col & ~IM_COL32_A_MASK) | (90u << IM_COL32_A_SHIFT), 2.0f);
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.8f), ImVec2(c.x + h * 0.4f, c.y + h * 0.4f), col, 2.0f, 0, th);
        break;
    case Icon::FxStylize:
        dl->AddCircleFilled(c, h * 0.35f, col);
        for (int i = 0; i < 8; ++i) {
            const float a = i * kPi * 0.25f;
            dl->AddLine(ImVec2(c.x + std::cos(a) * h * 0.55f, c.y + std::sin(a) * h * 0.55f),
                        ImVec2(c.x + std::cos(a) * h * 0.85f, c.y + std::sin(a) * h * 0.85f), col, th);
        }
        break;
    case Icon::FxTime:
        dl->AddCircle(c, h * 0.8f, col, 0, th);
        dl->AddLine(c, ImVec2(c.x, c.y - h * 0.5f), col, th);
        dl->AddLine(c, ImVec2(c.x + h * 0.35f, c.y + h * 0.15f), col, th);
        break;
    case Icon::Palette:
        dl->AddCircle(c, h * 0.8f, col, 0, th);
        dl->AddCircleFilled(ImVec2(c.x - h * 0.35f, c.y - h * 0.25f), h * 0.13f, col);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.1f, c.y - h * 0.45f), h * 0.13f, col);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.45f, c.y - h * 0.1f), h * 0.13f, col);
        dl->AddCircleFilled(ImVec2(c.x - h * 0.05f, c.y + h * 0.4f), h * 0.18f, col);
        break;
    case Icon::Sliders:
        for (int i = 0; i < 3; ++i) {
            const float y = c.y + (i - 1) * h * 0.6f, k = i == 1 ? 0.3f : -0.3f;
            dl->AddLine(ImVec2(c.x - h * 0.8f, y), ImVec2(c.x + h * 0.8f, y), col, th);
            dl->AddCircleFilled(ImVec2(c.x + k * h, y), h * 0.18f, col);
        }
        break;
    case Icon::Graph: {
        dl->AddLine(ImVec2(c.x - h * 0.85f, c.y + h * 0.8f), ImVec2(c.x + h * 0.85f, c.y + h * 0.8f), col, th);
        const ImVec2 p0(c.x - h * 0.8f, c.y + h * 0.55f), p3(c.x + h * 0.8f, c.y - h * 0.65f);
        dl->AddBezierCubic(p0, ImVec2(c.x + h * 0.1f, p0.y), ImVec2(c.x - h * 0.1f, p3.y), p3, col, th * 1.2f);
        break;
    }
    case Icon::Compound: // two stacked frames
        dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.35f), ImVec2(c.x + h * 0.35f, c.y + h * 0.85f), col, 2.5f, 0, th);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.35f), ImVec2(c.x - h * 0.4f, c.y - h * 0.85f), col, th);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y - h * 0.85f), col, th);
        dl->AddLine(ImVec2(c.x + h * 0.85f, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y + h * 0.4f), col, th);
        dl->AddLine(ImVec2(c.x + h * 0.85f, c.y + h * 0.4f), ImVec2(c.x + h * 0.35f, c.y + h * 0.4f), col, th);
        break;
    case Icon::Back:
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x + h * 0.7f, c.y), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x - h * 0.2f, c.y - h * 0.5f), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x - h * 0.2f, c.y + h * 0.5f), col, th * 1.2f);
        break;
    case Icon::Film: { // a frame with sprocket holes
        const ImVec2 a(c.x - h * 0.8f, c.y - h * 0.6f), b(c.x + h * 0.8f, c.y + h * 0.6f);
        dl->AddRect(a, b, col, 2.0f, 0, th);
        dl->AddLine(ImVec2(a.x + h * 0.35f, a.y), ImVec2(a.x + h * 0.35f, b.y), col, th);
        dl->AddLine(ImVec2(b.x - h * 0.35f, a.y), ImVec2(b.x - h * 0.35f, b.y), col, th);
        for (int i = -1; i <= 1; ++i) {
            const float y = c.y + i * h * 0.36f;
            dl->AddRectFilled(ImVec2(a.x + h * 0.1f, y - h * 0.08f), ImVec2(a.x + h * 0.25f, y + h * 0.08f), col);
            dl->AddRectFilled(ImVec2(b.x - h * 0.25f, y - h * 0.08f), ImVec2(b.x - h * 0.1f, y + h * 0.08f), col);
        }
        break;
    }
    case Icon::Image: { // a frame with a mountain and a sun
        const ImVec2 a(c.x - h * 0.8f, c.y - h * 0.6f), b(c.x + h * 0.8f, c.y + h * 0.6f);
        dl->AddRect(a, b, col, 2.0f, 0, th);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.35f, c.y - h * 0.22f), h * 0.14f, col);
        dl->AddTriangleFilled(ImVec2(a.x + h * 0.15f, b.y - h * 0.12f), ImVec2(c.x - h * 0.25f, c.y - h * 0.15f),
                              ImVec2(c.x + h * 0.3f, b.y - h * 0.12f), col);
        dl->AddTriangleFilled(ImVec2(c.x, b.y - h * 0.12f), ImVec2(c.x + h * 0.35f, c.y + h * 0.1f),
                              ImVec2(b.x - h * 0.15f, b.y - h * 0.12f), col);
        break;
    }
    case Icon::Audio: { // a waveform
        const float bars[] = {0.25f, 0.55f, 0.85f, 0.45f, 0.7f, 0.3f};
        for (int i = 0; i < 6; ++i) {
            const float x = c.x - h * 0.75f + i * h * 0.3f;
            dl->AddLine(ImVec2(x, c.y - h * bars[i]), ImVec2(x, c.y + h * bars[i]), col, th * 1.3f);
        }
        break;
    }
    case Icon::Import: { // an arrow into a tray
        dl->AddLine(ImVec2(c.x, c.y - h * 0.8f), ImVec2(c.x, c.y + h * 0.2f), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.2f), ImVec2(c.x, c.y + h * 0.2f), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x + h * 0.4f, c.y - h * 0.2f), ImVec2(c.x, c.y + h * 0.2f), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.15f), ImVec2(c.x - h * 0.8f, c.y + h * 0.75f), col, th);
        dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.75f), ImVec2(c.x + h * 0.8f, c.y + h * 0.75f), col, th);
        dl->AddLine(ImVec2(c.x + h * 0.8f, c.y + h * 0.75f), ImVec2(c.x + h * 0.8f, c.y + h * 0.15f), col, th);
        break;
    }
    case Icon::Duplicate: { // two overlapping sheets
        dl->AddRect(ImVec2(c.x - h * 0.75f, c.y - h * 0.75f), ImVec2(c.x + h * 0.25f, c.y + h * 0.25f), col, 2.0f, 0, th);
        dl->AddRectFilled(ImVec2(c.x - h * 0.25f, c.y - h * 0.25f), ImVec2(c.x + h * 0.75f, c.y + h * 0.75f), col, 2.0f);
        break;
    }
    case Icon::Ungroup: { // a frame with its corners breaking apart
        const float g = h * 0.3f;
        const ImVec2 corners[4] = {ImVec2(c.x - h * 0.8f, c.y - h * 0.8f), ImVec2(c.x + h * 0.8f, c.y - h * 0.8f),
                                   ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), ImVec2(c.x - h * 0.8f, c.y + h * 0.8f)};
        for (int i = 0; i < 4; ++i) {
            const float sx = (i == 1 || i == 2) ? -1.0f : 1.0f, sy = i >= 2 ? -1.0f : 1.0f;
            dl->AddLine(corners[i], ImVec2(corners[i].x + sx * g * 1.6f, corners[i].y), col, th);
            dl->AddLine(corners[i], ImVec2(corners[i].x, corners[i].y + sy * g * 1.6f), col, th);
        }
        dl->AddRectFilled(ImVec2(c.x - h * 0.22f, c.y - h * 0.22f), ImVec2(c.x + h * 0.22f, c.y + h * 0.22f), col, 1.5f);
        break;
    }
    case Icon::Ease: { // an S-curve between two keyframes
        dl->AddBezierCubic(ImVec2(c.x - h * 0.75f, c.y + h * 0.6f), ImVec2(c.x + h * 0.1f, c.y + h * 0.6f),
                           ImVec2(c.x - h * 0.1f, c.y - h * 0.6f), ImVec2(c.x + h * 0.75f, c.y - h * 0.6f), col, th * 1.2f);
        drawDiamond(dl, ImVec2(c.x - h * 0.75f, c.y + h * 0.6f), h * 0.22f, col, 0);
        drawDiamond(dl, ImVec2(c.x + h * 0.75f, c.y - h * 0.6f), h * 0.22f, col, 0);
        break;
    }
    case Icon::LayerUp:
    case Icon::LayerDown: { // two stacked layers and an arrow
        const float dir = icon == Icon::LayerUp ? -1.0f : 1.0f;
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.15f), ImVec2(c.x + h * 0.2f, c.y + h * 0.75f), col, 2.0f, 0, th);
        dl->AddRectFilled(ImVec2(c.x - h * 0.55f, c.y - h * 0.4f + dir * h * 0.25f),
                          ImVec2(c.x + h * 0.0f, c.y + h * 0.1f + dir * h * 0.25f), col, 1.5f);
        const float ax = c.x + h * 0.6f;
        dl->AddLine(ImVec2(ax, c.y - h * 0.7f), ImVec2(ax, c.y + h * 0.7f), col, th);
        const float tip = c.y + dir * h * 0.75f;
        dl->AddTriangleFilled(ImVec2(ax, tip), ImVec2(ax - h * 0.28f, tip - dir * h * 0.38f),
                              ImVec2(ax + h * 0.28f, tip - dir * h * 0.38f), col);
        break;
    }
    case Icon::ZoomIn:
    case Icon::ZoomOut: {
        const ImVec2 lens(c.x - h * 0.15f, c.y - h * 0.15f);
        dl->AddCircle(lens, h * 0.55f, col, 0, th);
        dl->AddLine(ImVec2(c.x + h * 0.25f, c.y + h * 0.25f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col, th * 1.3f);
        dl->AddLine(ImVec2(lens.x - h * 0.28f, lens.y), ImVec2(lens.x + h * 0.28f, lens.y), col, th);
        if (icon == Icon::ZoomIn)
            dl->AddLine(ImVec2(lens.x, lens.y - h * 0.28f), ImVec2(lens.x, lens.y + h * 0.28f), col, th);
        break;
    }
    }
}

void drawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 outline) {
    const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
    if (fill & IM_COL32_A_MASK) dl->AddConvexPolyFilled(pts, 4, fill);
    if (outline & IM_COL32_A_MASK) dl->AddPolyline(pts, 4, outline, ImDrawFlags_Closed, 1.3f);
}

void tooltip(const char* text) {
    if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

bool iconButton(const char* id, Icon icon, float size, const char* tip, bool active) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || ImGui::IsItemActive())
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), ImGui::IsItemActive() ? theme::kBorder : theme::kBgHover,
                          5.0f);
    const ImU32 col = active ? theme::kAccent : (hovered ? theme::kText : theme::kTextDim);
    drawIcon(dl, icon, ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.58f, col);
    tooltip(tip);
    return pressed;
}

bool accentButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    ImGui::PushFont(Fonts::uiBold(), 0.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool objectTile(const char* id, Icon icon, const char* label, ImVec2 size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, end, held ? theme::kBgHover : (hovered ? theme::kBgHover : theme::kBgRaised), 8.0f);
    if (hovered) dl->AddRect(pos, end, theme::kAccent, 8.0f, 0, 1.0f);
    const float iconSize = std::min(size.x, size.y) * 0.34f;
    drawIcon(dl, icon, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.42f), iconSize,
             hovered ? theme::kAccent : theme::kText);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->PushClipRect(ImVec2(pos.x + 4, pos.y), ImVec2(end.x - 4, end.y), true); // long labels are clipped, not spilled
    dl->AddText(ImVec2(pos.x + std::max(4.0f, (size.x - ts.x) * 0.5f), end.y - ts.y - size.y * 0.12f),
                hovered ? theme::kText : theme::kTextDim, label);
    dl->PopClipRect();
    return pressed;
}

void sectionLabel(const char* text) {
    ImGui::PushFont(Fonts::uiBold(), Fonts::kUiSize * 0.8f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextFaint);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool keyframeButton(const char* id, bool hasKeyHere, bool animated) {
    const float size = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size * 0.8f, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 c(pos.x + size * 0.4f, pos.y + size * 0.5f);
    const ImU32 outline = animated || hasKeyHere ? theme::kAccent : (hovered ? theme::kText : theme::kTextFaint);
    drawDiamond(ImGui::GetWindowDrawList(), c, size * 0.24f, hasKeyHere ? theme::kAccent : 0, outline);
    tooltip(hasKeyHere ? "Remove keyframe" : "Add keyframe");
    return pressed;
}

std::string formatTimecode(double seconds, double fps) {
    if (seconds < 0) seconds = 0;
    const long long totalFrames = static_cast<long long>(std::floor(seconds * fps + 1e-6));
    const long long f = static_cast<long long>(fps + 0.5);
    const long long frames = f > 0 ? totalFrames % f : 0;
    const long long totalSec = f > 0 ? totalFrames / f : 0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld:%02lld", totalSec / 3600, (totalSec / 60) % 60, totalSec % 60,
                  frames);
    return buf;
}

namespace {

// The split that decides the node's size along `axis` (the closest ancestor split).
ImGuiDockNode* splitChild(ImGuiDockNode* node, int& axis) {
    while (node && node->ParentNode) {
        if (node->ParentNode->SplitAxis != ImGuiAxis_None) {
            axis = node->ParentNode->SplitAxis;
            return node;
        }
        node = node->ParentNode;
    }
    return nullptr;
}

// How small a node gets when a neighbor is maximized (just its tab bar).
float minimizedSize(const ImGuiDockNode* node, int axis) {
    const float bar = node->TabBar ? node->TabBar->BarRect.GetHeight() : ImGui::GetFrameHeight() + 4.0f;
    return axis == ImGuiAxis_Y ? bar + 4.0f : 46.0f;
}

// Shrunk to its tab bar (how older versions minimized).
bool nodeMinimized(ImGuiDockNode* node) {
    int axis = 0;
    ImGuiDockNode* child = splitChild(node, axis);
    return child && child->Size[axis] <= minimizedSize(node, axis) + 6.0f;
}

// Sets a docked node's size along its parent's split axis (the sibling takes the rest).
void resizeDockNode(ImGuiDockNode* node, int axis, float size) {
    ImGuiDockNode* parent = node->ParentNode;
    ImGuiDockNode* sibling = parent->ChildNodes[0] == node ? parent->ChildNodes[1] : parent->ChildNodes[0];
    size = std::clamp(size, 1.0f, std::max(1.0f, parent->Size[axis] - 60.0f));
    // Only the resized node is locked: ImGui then gives the sibling the rest.
    node->SizeRef[axis] = node->Size[axis] = size;
    sibling->SizeRef[axis] = parent->Size[axis] - size;
    node->WantLockSizeOnce = true;
}

// Hidden (minimized) panels: the windows of one dock node, and where their restore handle goes.
enum class Edge { Left, Right, Top, Bottom };
struct HiddenPanel {
    std::vector<std::string> windows;
    Edge edge = Edge::Left;
    float along = 0.5f; // handle position along that edge (0..1 of the work area)
};
std::vector<HiddenPanel>& hiddenPanels() {
    static std::vector<HiddenPanel> hidden;
    return hidden;
}

} // namespace

bool panelHidden(const char* windowName) {
    for (const HiddenPanel& h : hiddenPanels())
        for (const std::string& w : h.windows)
            if (w == windowName) return true;
    return false;
}

void restoreHiddenPanels() { hiddenPanels().clear(); }

void drawHiddenPanelHandles() {
    auto& hidden = hiddenPanels();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 wp = vp->WorkPos, ws = vp->WorkSize;
    const float len = 56.0f, thick = 18.0f;
    for (size_t i = 0; i < hidden.size(); ++i) {
        const HiddenPanel& h = hidden[i];
        const bool vertical = h.edge == Edge::Left || h.edge == Edge::Right; // the handle stands upright
        const ImVec2 size = vertical ? ImVec2(thick, len) : ImVec2(len, thick);
        ImVec2 pos;
        if (vertical) {
            pos.x = h.edge == Edge::Left ? wp.x + 2.0f : wp.x + ws.x - thick - 2.0f;
            pos.y = std::clamp(wp.y + ws.y * h.along - len * 0.5f, wp.y + 4.0f, wp.y + ws.y - len - 4.0f);
        } else {
            pos.y = h.edge == Edge::Top ? wp.y + 2.0f : wp.y + ws.y - thick - 2.0f;
            pos.x = std::clamp(wp.x + ws.x * h.along - len * 0.5f, wp.x + 4.0f, wp.x + ws.x - len - 4.0f);
        }
        // A tiny window of its own, kept above the panels, so the click does not reach them.
        char id[32];
        std::snprintf(id, sizeof id, "##restorePanel%d", static_cast<int>(i));
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar(2);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const bool clicked = ImGui::InvisibleButton("##restore", size);
        const bool hot = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), hot ? theme::kAccent : theme::withAlpha(theme::kBgHover, 240), 5.0f);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), hot ? theme::kAccent : theme::kBorder, 5.0f);
        // The chevron points to where the panel comes back from.
        const Icon icon = h.edge == Edge::Left ? Icon::ChevronRight : h.edge == Edge::Right ? Icon::Back
                          : h.edge == Edge::Top ? Icon::ChevronDown : Icon::ChevronUp;
        drawIcon(dl, icon, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f), 12.0f, hot ? theme::kOnAccent : theme::kText);
        if (hot) {
            std::string names;
            for (const std::string& w : h.windows) names += (names.empty() ? "" : ", ") + w.substr(0, w.find('#'));
            ImGui::SetTooltip("Restore %s", names.c_str());
        }
        ImGui::End();
        if (clicked) {
            hidden.erase(hidden.begin() + static_cast<std::ptrdiff_t>(i));
            break; // indices changed; the rest draw next frame
        }
    }
}

bool panelIsMinimized() {
    // Layouts saved by older versions minimized by shrinking the node to its tab bar:
    // give such a node a usable size again (minimizing now hides the panel instead).
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->DockNode && nodeMinimized(window->DockNode)) {
        int axis = 0;
        if (ImGuiDockNode* child = splitChild(window->DockNode, axis))
            resizeDockNode(child, axis, std::max(axis == ImGuiAxis_Y ? 180.0f : 280.0f, child->ParentNode->Size[axis] * 0.25f));
    }
    return false;
}

namespace {

// Maximized panel: the sizes along its path to the root, to restore them.
struct Maximized {
    ImGuiID node = 0;
    std::vector<std::tuple<ImGuiID, int, float>> sizes; // split child id, axis, size
};
Maximized& maximized() {
    static Maximized m;
    return m;
}

// Every split between the node and the dockspace root, innermost first.
std::vector<std::pair<ImGuiDockNode*, int>> splitPath(ImGuiDockNode* node) {
    std::vector<std::pair<ImGuiDockNode*, int>> path;
    for (ImGuiDockNode* n = node; n && n->ParentNode; n = n->ParentNode)
        if (n->ParentNode->SplitAxis != ImGuiAxis_None) path.emplace_back(n, n->ParentNode->SplitAxis);
    return path;
}

void toggleMaximize(ImGuiDockNode* node) {
    Maximized& m = maximized();
    if (m.node) { // restore the layout (outermost split first, so inner sizes fit)
        for (auto it = m.sizes.rbegin(); it != m.sizes.rend(); ++it)
            if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(std::get<0>(*it))) resizeDockNode(n, std::get<1>(*it), std::get<2>(*it));
        const bool same = m.node == node->ID;
        m = {};
        if (same) return;
    }
    m.node = node->ID;
    for (auto [child, axis] : splitPath(node)) {
        m.sizes.emplace_back(child->ID, axis, child->Size[axis]);
        ImGuiDockNode* sibling = child->ParentNode->ChildNodes[0] == child ? child->ParentNode->ChildNodes[1] : child->ParentNode->ChildNodes[0];
        resizeDockNode(child, axis, child->ParentNode->Size[axis] - minimizedSize(sibling, axis));
    }
}

} // namespace

void panelMinimizeButton() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;
    if (!node || !node->TabBar || !node->HostWindow) return; // floating or without a tab bar
    if (node->TabBar->SelectedTabId != window->TabId) return; // only the visible tab owns the controls
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

    // Maximize / restore: double-click the tab bar, or ` (backquote) with the mouse over the panel.
    const ImRect bar = node->TabBar->BarRect;
    const ImRect area(node->Pos, ImVec2(node->Pos.x + node->Size.x, node->Pos.y + node->Size.y));
    const bool overHost = g.HoveredWindow && g.HoveredWindow->RootWindowDockTree == node->HostWindow->RootWindowDockTree;
    if (!popup && overHost &&
        ((ImGui::IsMouseHoveringRect(bar.Min, bar.Max, false) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
         (ImGui::IsMouseHoveringRect(area.Min, area.Max, false) && !ImGui::GetIO().WantTextInput &&
          ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)))) {
        toggleMaximize(node);
        return;
    }

    int axis = 0;
    ImGuiDockNode* child = splitChild(node, axis);
    if (!child) return; // alone in the dockspace
    // The handle sits on the edge facing the neighbor panel, centered on it.
    const bool farSide = child->ParentNode->ChildNodes[1] == child; // right of / below its sibling
    const Icon toEdge = axis == ImGuiAxis_Y ? Icon::ChevronDown : (farSide ? Icon::ChevronRight : Icon::Back);
    auto toggle = [&] {
        // Hide every tab of this place; the restore handle goes on the window edge the panel came from.
        HiddenPanel h;
        for (ImGuiWindow* w : node->Windows) h.windows.emplace_back(w->Name);
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 c(node->Pos.x + node->Size.x * 0.5f, node->Pos.y + node->Size.y * 0.5f);
        if (axis == ImGuiAxis_X) {
            h.edge = farSide ? Edge::Right : Edge::Left;
            h.along = (c.y - vp->WorkPos.y) / std::max(1.0f, vp->WorkSize.y);
        } else {
            h.edge = farSide ? Edge::Bottom : Edge::Top;
            h.along = (c.x - vp->WorkPos.x) / std::max(1.0f, vp->WorkSize.x);
        }
        hiddenPanels().push_back(std::move(h));
        maximized() = {};
    };

    const ImRect r(child->Pos, ImVec2(child->Pos.x + child->Size.x, child->Pos.y + child->Size.y));
    const float len = 44.0f, thick = 14.0f;
    ImVec2 a, b;
    if (axis == ImGuiAxis_X) {
        const float x = farSide ? r.Min.x + 1.0f : r.Max.x - thick - 1.0f;
        const float cy = r.GetCenter().y;
        a = ImVec2(x, cy - len * 0.5f), b = ImVec2(x + thick, cy + len * 0.5f);
    } else {
        const float y = farSide ? r.Min.y + 1.0f : r.Max.y - thick - 1.0f;
        const float cx = r.GetCenter().x;
        a = ImVec2(cx - len * 0.5f, y), b = ImVec2(cx + len * 0.5f, y + thick);
    }
    // Shown when the mouse is near that edge, like editors' collapse handles.
    const float reach = 60.0f;
    const bool nearEdge = ImGui::IsMouseHoveringRect(ImVec2(a.x - reach, a.y - reach), ImVec2(b.x + reach, b.y + reach), false);
    const bool hovered = !popup && overHost && ImGui::IsMouseHoveringRect(a, b, false);
    if (!(nearEdge || hovered) || popup) return; // never over menus
    ImDrawList* dl = ImGui::GetForegroundDrawList(node->HostWindow->Viewport);
    dl->AddRectFilled(a, b, hovered ? theme::kAccent : theme::withAlpha(theme::kBgHover, 235), 5.0f);
    dl->AddRect(a, b, hovered ? theme::kAccent : theme::kBorder, 5.0f);
    drawIcon(dl, toEdge, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 12.0f, hovered ? theme::kOnAccent : theme::kText);
    if (hovered) {
        ImGui::SetTooltip("Minimize panel  (double-click its tabs to maximize)");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) toggle();
    }
}

void nextPanelWindowClass() {
    ImGuiWindowClass wc;
    wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
    ImGui::SetNextWindowClass(&wc);
}

} // namespace bemotion::ui
