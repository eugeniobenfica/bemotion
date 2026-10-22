#include "ui/Widgets.h"

#include "app/PluginHost.h"
#include "core/Math.h"
#include "render/Fonts.h"
#include "ui/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
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

Icon iconForLayer(const Layer& layer) {
    if (layer.type == LayerType::Camera) return Icon::Camera;
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
    case Icon::Logo: {
        // A geometric lowercase "b": tall stem plus a round bowl.
        const float stroke = std::max(2.0f, size * 0.15f);
        const float r = h * 0.42f;
        const ImVec2 bowl(c.x + h * 0.12f, c.y + h * 0.36f);
        const float stemX = bowl.x - r;
        dl->AddLine(ImVec2(stemX, c.y - h * 0.9f), ImVec2(stemX, bowl.y + r * 0.35f), col, stroke);
        dl->AddCircle(bowl, r, col, 0, stroke);
        break;
    }
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
    case Icon::Back:
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x + h * 0.7f, c.y), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x - h * 0.2f, c.y - h * 0.5f), col, th * 1.2f);
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x - h * 0.2f, c.y + h * 0.5f), col, th * 1.2f);
        break;
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

// Minimized = the dock node is (about) as small as its tab bar. The state is
// derived from the size, so it survives restarts and is shared by every tab
// of the node; only the size to restore to is remembered (per node).
std::unordered_map<ImGuiID, float>& savedSizes() {
    static std::unordered_map<ImGuiID, float> sizes;
    return sizes;
}

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

float minimizedSize(const ImGuiDockNode* node, int axis) {
    const float bar = node->TabBar ? node->TabBar->BarRect.GetHeight() : ImGui::GetFrameHeight() + 4.0f;
    return axis == ImGuiAxis_Y ? bar + 4.0f : 46.0f;
}

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

} // namespace

bool panelIsMinimized() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    return window->DockNode && nodeMinimized(window->DockNode);
}

void panelMinimizeButton() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;
    if (!node || !node->TabBar || !node->HostWindow) return; // floating or without a tab bar
    if (node->TabBar->SelectedTabId != window->TabId) return; // only the visible tab owns the button
    int axis = 0;
    ImGuiDockNode* child = splitChild(node, axis);
    if (!child) return; // alone in the dockspace
    const bool minimized = nodeMinimized(node);
    // Arrows point where the panel goes: toward its edge to minimize, back to restore.
    const bool farSide = child->ParentNode->ChildNodes[1] == child; // right (or bottom) of its split
    const Icon toEdge = axis == ImGuiAxis_Y ? Icon::ChevronDown : (farSide ? Icon::ChevronRight : Icon::Back);
    const Icon fromEdge = axis == ImGuiAxis_Y ? Icon::ChevronUp : (farSide ? Icon::Back : Icon::ChevronRight);
    auto toggle = [&] {
        float& saved = savedSizes()[child->ID];
        if (minimized) {
            const float fallback = child->ParentNode->Size[axis] * (axis == ImGuiAxis_Y ? 0.35f : 0.22f);
            resizeDockNode(child, axis, saved > minimizedSize(node, axis) + 20.0f ? saved : fallback);
        } else {
            saved = child->Size[axis];
            resizeDockNode(child, axis, minimizedSize(node, axis));
        }
    };
    if (minimized && axis == ImGuiAxis_X) {
        // A narrow strip: its tab bar is too small for a button, so the whole
        // strip restores the panel (arrow + the panel name written downwards).
        const ImVec2 p = window->InnerRect.Min;
        const ImVec2 size(window->InnerRect.GetWidth(), window->InnerRect.GetHeight());
        ImGui::SetCursorScreenPos(p);
        const bool clicked = ImGui::InvisibleButton("##restore", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)));
        const bool hot = ImGui::IsItemHovered();
        ImDrawList* dl = window->DrawList;
        if (hot) dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), theme::kBgHover);
        const float cx = p.x + size.x * 0.5f;
        drawIcon(dl, fromEdge, ImVec2(cx, p.y + 18), 12.0f, hot ? theme::kAccent : theme::kTextDim);
        float y = p.y + 36;
        for (const char* c = window->Name; *c && *c != '#'; ++c, y += ImGui::GetFontSize()) {
            const char ch[2] = {*c, 0};
            dl->AddText(ImVec2(cx - ImGui::CalcTextSize(ch).x * 0.5f, y), hot ? theme::kText : theme::kTextFaint, ch);
        }
        if (hot) ImGui::SetTooltip("Restore panel");
        if (clicked) toggle();
        return;
    }
    // The tab bar belongs to the dock host window, where this window's widgets
    // cannot be clicked: draw on the host and hit-test by hand.
    const ImRect bar = node->TabBar->BarRect;
    const float s = std::min(bar.GetHeight() - 2.0f, ImGui::GetFrameHeight());
    const float cy = std::floor(bar.GetCenter().y) + 0.5f; // centered on the tab row
    const ImVec2 a(bar.Max.x - s - 6.0f, cy - s * 0.5f), b(a.x + s, a.y + s);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const bool overHost = g.HoveredWindow && g.HoveredWindow->RootWindowDockTree == node->HostWindow->RootWindowDockTree;
    const bool hovered = overHost && ImGui::IsMouseHoveringRect(a, b, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    ImDrawList* dl = node->HostWindow->DrawList;
    if (hovered) dl->AddRectFilled(a, b, theme::kBgHover, 4.0f);
    const Icon icon = minimized ? fromEdge : toEdge;
    drawIcon(dl, icon, ImVec2((a.x + b.x) * 0.5f, cy), s * 0.55f, hovered ? theme::kText : theme::kTextDim);
    if (hovered) {
        ImGui::SetTooltip(minimized ? "Restore panel" : "Minimize panel");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) toggle();
    }
}

void nextPanelWindowClass() {
    ImGuiWindowClass wc;
    wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
    ImGui::SetNextWindowClass(&wc);
}

} // namespace bemotion::ui
