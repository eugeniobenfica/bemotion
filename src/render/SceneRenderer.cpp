#include "render/SceneRenderer.h"

#include "core/Geometry.h"
#include "render/EffectRenderer.h"
#include "render/Fonts.h"
#include "render/PluginCanvas.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace bemotion::render {

namespace {

ImU32 toU32(Color c, float opacity) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, std::clamp(c.a * opacity, 0.0f, 1.0f)));
}

// Average scale factor of a transform, used to pick a rasterization density.
float densityOf(const Affine& m) { return std::sqrt(std::fabs(m.determinant())); }

void transformVertices(ImDrawList* dl, int start, const Affine& m) {
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const Vec2 p = m.apply({v.pos.x, v.pos.y});
        v.pos = ImVec2(p.x, p.y);
    }
}

// --- text layout -------------------------------------------------------------

int utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

struct Glyph {
    const char* begin;
    int length;
    float x; // local x of the glyph's left edge
};

struct TextLine {
    std::vector<Glyph> glyphs;
    float width = 0.0f;
    float y = 0.0f; // local y of the line top
};

struct TextLayout {
    std::vector<TextLine> lines;
    float fontSize = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float top = 0.0f;
    int glyphCount = 0;
};

// Glyph advances are measured at a fixed reference size and scaled, so layout
// does not depend on the zoom level and does not bake extra font sizes.
constexpr float kMeasureSize = 64.0f;

TextLayout layoutText(const TextProps& text, double t) {
    TextLayout layout;
    ImFont* font = Fonts::forText(text);
    const float size = std::max(1.0f, text.fontSize.valueAt(t));
    const float tracking = text.tracking.valueAt(t);
    const float advance = size * std::max(0.1f, text.lineHeight.valueAt(t) * 0.01f);
    const float measureScale = size / kMeasureSize;
    layout.fontSize = size;

    layout.lines.emplace_back();
    const std::string& s = text.text;
    for (size_t i = 0; i < s.size();) {
        const int len = std::min<int>(utf8Length(static_cast<unsigned char>(s[i])), static_cast<int>(s.size() - i));
        const char* p = s.data() + i;
        i += len;
        if (*p == '\n') {
            layout.lines.emplace_back();
            continue;
        }
        if (*p == '\r') continue;
        TextLine& line = layout.lines.back();
        const float w = font->CalcTextSizeA(kMeasureSize, FLT_MAX, 0.0f, p, p + len).x * measureScale;
        if (!line.glyphs.empty()) line.width += tracking;
        line.glyphs.push_back({p, len, line.width});
        line.width += w;
        ++layout.glyphCount;
    }

    layout.height = advance * static_cast<float>(layout.lines.size());
    layout.top = -layout.height * 0.5f;
    for (size_t i = 0; i < layout.lines.size(); ++i) {
        TextLine& line = layout.lines[i];
        layout.width = std::max(layout.width, line.width);
        line.y = layout.top + advance * static_cast<float>(i) + (advance - size) * 0.5f;
        const float offset = text.align == TextAlign::Left     ? 0.0f
                             : text.align == TextAlign::Center ? -line.width * 0.5f
                                                               : -line.width;
        for (auto& g : line.glyphs) g.x += offset;
    }
    return layout;
}

float alignedLeft(const TextProps& text, float width) {
    return text.align == TextAlign::Left ? 0.0f : text.align == TextAlign::Center ? -width * 0.5f : -width;
}

// Quantized raster sizes keep the dynamic font atlas small while zooming.
float quantizeRasterSize(float px) {
    static constexpr float kSteps[] = {6,   8,   10,  12,  14,  16,  18,  20,  24,  28,  32,  40,  48,
                                       56,  64,  80,  96,  112, 128, 160, 192, 224, 256, 320, 384, 448, 512};
    for (float s : kSteps)
        if (s >= px) return s;
    return kSteps[std::size(kSteps) - 1];
}

void drawText(ImDrawList* dl, const Layer& layer, double t, const Affine& toScreen, float opacity) {
    const TextProps& text = layer.text;
    const TextLayout layout = layoutText(text, t);
    if (layout.glyphCount == 0) return;

    const ImU32 col = toU32(text.color.valueAt(t), opacity);
    if ((col & IM_COL32_A_MASK) == 0) return;

    const float rasterSize = quantizeRasterSize(layout.fontSize * densityOf(toScreen));
    const float k = rasterSize / layout.fontSize; // layer units -> raster units
    const float reveal = std::clamp(text.reveal.valueAt(t), 0.0f, 100.0f) * 0.01f;
    const int visibleGlyphs = static_cast<int>(std::floor(reveal * layout.glyphCount + 1e-4f));

    ImFont* font = Fonts::forText(text);
    // Culling happens before our vertex transform, so disable it entirely.
    const ImVec4 noClip(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    const int start = dl->VtxBuffer.Size;
    int drawn = 0;
    for (const auto& line : layout.lines) {
        for (const auto& g : line.glyphs) {
            if (drawn++ >= visibleGlyphs) break;
            font->RenderText(dl, rasterSize, ImVec2(g.x * k, line.y * k), col, noClip, g.begin, g.begin + g.length);
        }
    }
    transformVertices(dl, start, toScreen * Affine::scale(1.0f / k));
}

// --- shapes ------------------------------------------------------------------

void drawShape(ImDrawList* dl, const Layer& layer, double t, const Affine& toScreen, float opacity) {
    const ShapeProps& shape = layer.shape;
    Path path = buildShapePath(shape, t);
    if (path.points.empty()) return;

    // Anti-aliased fills expect clockwise winding on screen; a mirrored
    // transform flips it, so flip the points back.
    if (toScreen.determinant() < 0.0f) std::reverse(path.points.begin(), path.points.end());

    const float k = std::clamp(densityOf(toScreen), 0.01f, 64.0f);
    auto toRaster = [k](const std::vector<Vec2>& pts) {
        std::vector<ImVec2> out;
        out.reserve(pts.size());
        for (const Vec2& p : pts) out.emplace_back(p.x * k, p.y * k);
        return out;
    };

    const int start = dl->VtxBuffer.Size;

    if (shape.fillEnabled && path.closed && path.points.size() >= 3) {
        const ImU32 fill = toU32(shape.fillColor.valueAt(t), opacity);
        if (fill & IM_COL32_A_MASK) {
            const auto pts = toRaster(path.points);
            if (path.convex) dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), fill);
            else dl->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), fill);
        }
    }

    const float strokeWidth = shape.strokeWidth.valueAt(t);
    if (shape.strokeEnabled && strokeWidth > 0.0f) {
        const ImU32 stroke = toU32(shape.strokeColor.valueAt(t), opacity);
        const float a = shape.trimStart.valueAt(t) * 0.01f, b = shape.trimEnd.valueAt(t) * 0.01f;
        const bool trimmed = std::fabs(a) > 1e-4f || std::fabs(b - 1.0f) > 1e-4f;
        const Path outline = trimmed ? trimPath(path, a, b) : path;
        if ((stroke & IM_COL32_A_MASK) && outline.points.size() >= 2) {
            const auto pts = toRaster(outline.points);
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), stroke,
                            outline.closed ? ImDrawFlags_Closed : ImDrawFlags_None, strokeWidth * k);
        }
    }

    transformVertices(dl, start, toScreen * Affine::scale(1.0f / k));
}

// --- plugin objects ------------------------------------------------------------

PluginDrawer& pluginDrawer() {
    static PluginDrawer drawer;
    return drawer;
}

void drawPlugin(ImDrawList* dl, const Layer& layer, double t, const Affine& toScreen, float opacity) {
    const float k = std::clamp(densityOf(toScreen), 0.01f, 64.0f);
    const int start = dl->VtxBuffer.Size;
    PluginCanvas canvas(dl, k, opacity);
    if (pluginDrawer()) {
        pluginDrawer()(layer, t, canvas);
    } else { // no plugin host (tests, tools): a placeholder frame
        const Vec2 half = layer.plugin.size.valueAt(t) * 0.5f;
        canvas.rectOutline(half * -1.0f, half, Color::fromHex(0x808080), 2.0f);
    }
    transformVertices(dl, start, toScreen * Affine::scale(1.0f / k));
}

} // namespace

void setPluginDrawer(PluginDrawer drawer) { pluginDrawer() = std::move(drawer); }

void drawLayer(ImDrawList* dl, const Layer& layer, double time, const Affine& layerToScreen, float opacityOverride) {
    const float opacity = opacityOverride >= 0.0f ? std::min(opacityOverride, 1.0f)
                                                  : std::clamp(layer.transform.opacity.valueAt(time) * 0.01f, 0.0f, 1.0f);
    if (opacity <= 0.0f || layer.type == LayerType::Camera) return;
    if (layer.type == LayerType::Text) drawText(dl, layer, time, layerToScreen, opacity);
    else if (layer.type == LayerType::Plugin) drawPlugin(dl, layer, time, layerToScreen, opacity);
    else drawShape(dl, layer, time, layerToScreen, opacity);
}

void drawComposition(ImDrawList* dl, const Composition& comp, double time, const View& view, bool background,
                     EffectRenderer* effects, bool throughCamera) {
    if (!effects) effects = &screenEffects();
    const Vec2 a = view.toScreen({0, 0});
    const Vec2 b = view.toScreen({static_cast<float>(comp.width), static_cast<float>(comp.height)});
    const Color bg = comp.background;
    if (background)
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y),
                          ImGui::ColorConvertFloat4ToU32(ImVec4(bg.r, bg.g, bg.b, 1.0f)));

    // Layers are clipped to the composition frame.
    dl->PushClipRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), true);
    const Affine viewMatrix = Affine::translate(view.origin) * Affine::scale(view.zoom) *
                              (throughCamera ? comp.cameraAt(time) : Affine{});
    for (auto it = comp.layers.rbegin(); it != comp.layers.rend(); ++it) {
        if (!it->activeAt(time) || it->type == LayerType::Camera) continue;
        if (hasActiveEffects(*it)) effects->drawLayer(dl, *it, time, comp.frameDuration(), viewMatrix, view.zoom);
        else drawLayer(dl, *it, time, viewMatrix * it->matrixAt(time));
    }
    dl->PopClipRect();
}

bool localBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax) {
    if (layer.type == LayerType::Camera) { // the camera frame
        outMin = layer.camera.frameSize * -0.5f;
        outMax = layer.camera.frameSize * 0.5f;
        return true;
    }
    if (layer.type == LayerType::Plugin) {
        const Vec2 half = layer.plugin.size.valueAt(time) * 0.5f;
        outMin = half * -1.0f;
        outMax = half;
        return true;
    }
    if (layer.type == LayerType::Text) {
        const TextLayout layout = layoutText(layer.text, time);
        const float left = alignedLeft(layer.text, layout.width);
        outMin = {left, layout.top};
        outMax = {left + std::max(layout.width, layout.fontSize * 0.25f), layout.top + layout.height};
        return true;
    }
    const ShapeProps& s = layer.shape;
    const Vec2 size = s.size.valueAt(time);
    float pad = s.strokeEnabled ? s.strokeWidth.valueAt(time) * 0.5f : 0.0f;
    if (s.kind == ShapeKind::Line) pad = std::max(pad, 6.0f);
    outMin = {-size.x * 0.5f - pad, -size.y * 0.5f - pad};
    outMax = {size.x * 0.5f + pad, size.y * 0.5f + pad};
    return true;
}

uint64_t hitTest(const Composition& comp, double time, Vec2 scenePoint, bool includeCameras) {
    for (const auto& layer : comp.layers) {
        if (!layer.activeAt(time) || layer.locked) continue;
        if (layer.type == LayerType::Camera && !includeCameras) continue;
        Vec2 mn, mx;
        if (!localBounds(layer, time, mn, mx)) continue;
        const Vec2 p = layer.matrixAt(time).inverse().apply(scenePoint);
        if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y) return layer.id;
    }
    return 0;
}

} // namespace bemotion::render
