#include "render/SceneRenderer.h"

#include "core/Deform.h"
#include "core/Expressions.h"
#include "core/Geometry.h"
#include "render/Gradient.h"
#include "render/PluginCanvas.h"
#include "render/Text.h"

#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkColorMatrix.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradientShader.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkLumaColorFilter.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/effects/SkTrimPathEffect.h"

#include "include/core/SkData.h"
#include "include/core/SkShader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

namespace bemotion::render {

namespace {

SkMatrix toSk(const Projective& p) {
    const auto& m = p.m;
    return SkMatrix::MakeAll(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]);
}

// A turned 3D layer may reach behind the camera: only its part in front (w >= 1 in its own
// space) is drawn. Call after setting `m` on the canvas; false when none of it is in front.
bool clipToFront(SkCanvas* canvas, const Projective& m) {
    if (!m.hasPerspective()) return true; // the whole plane at one distance (checked by viewFor)
    constexpr float kFar = 1e5f;          // far beyond any layer, in its own pixels
    std::vector<Vec2> poly = {{-kFar, -kFar}, {kFar, -kFar}, {kFar, kFar}, {-kFar, kFar}};
    std::vector<Vec2> kept;
    const auto side = [&](Vec2 p) { return m.w(p) - 1.01f; };
    for (size_t i = 0; i < poly.size(); ++i) { // the square cut by the line w = 1 (Sutherland-Hodgman)
        const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        const float sa = side(a), sb = side(b);
        if (sa >= 0) kept.push_back(a);
        if ((sa >= 0) != (sb >= 0)) kept.push_back(a + (b - a) * (sa / (sa - sb)));
    }
    if (kept.size() < 3) return false;
    SkPathBuilder b;
    b.moveTo(kept[0].x, kept[0].y);
    for (size_t i = 1; i < kept.size(); ++i) b.lineTo(kept[i].x, kept[i].y);
    b.close();
    canvas->clipPath(b.detach(), true);
    return true;
}

SkColor4f color4(Color c, float opacity = 1.0f) { return {c.r, c.g, c.b, std::clamp(c.a * opacity, 0.0f, 1.0f)}; }

SkMatrix toSk(const Affine& m) { return SkMatrix::MakeAll(m.a, m.c, m.tx, m.b, m.d, m.ty, 0, 0, 1); }

// --- shapes ------------------------------------------------------------------------

SkPath bezierSkPath(const BezierPath& path) {
    const size_t n = path.vertices.size();
    if (n < 2) return {};
    SkPathBuilder b;
    b.moveTo(path.vertices[0].point.x, path.vertices[0].point.y);
    for (size_t i = 0; i < segmentCount(path); ++i) {
        const PathVertex& a = path.vertices[i];
        const PathVertex& c = path.vertices[(i + 1) % n];
        if (a.out == Vec2{} && c.in == Vec2{}) b.lineTo(c.point.x, c.point.y);
        else b.cubicTo(a.point.x + a.out.x, a.point.y + a.out.y, c.point.x + c.in.x, c.point.y + c.in.y, c.point.x, c.point.y);
    }
    if (path.closed) b.close();
    return b.detach();
}

// The repeater's step from one copy to the next, around the shape's own origin.
Affine repeatStep(const ShapeProps& s, double t) {
    return Affine::translate(s.repeatOffset.valueAt(t)) * Affine::rotateDeg(s.repeatRotation.valueAt(t)) *
           Affine::scale(s.repeatScale.valueAt(t) * 0.01f);
}

SkPath shapePath(const ShapeProps& shape, double t) {
    const Vec2 size = shape.size.valueAt(t);
    const float hw = std::fabs(size.x) * 0.5f, hh = std::fabs(size.y) * 0.5f;
    SkPathBuilder b;
    switch (shape.kind) {
    case ShapeKind::Rectangle: {
        const float r = std::clamp(shape.roundness.valueAt(t), 0.0f, std::min(hw, hh));
        const SkRect rect = SkRect::MakeLTRB(-hw, -hh, hw, hh);
        if (r < 0.01f) b.addRect(rect);
        else b.addRRect(SkRRect::MakeRectXY(rect, r, r));
        return b.detach();
    }
    case ShapeKind::Ellipse: b.addOval(SkRect::MakeLTRB(-hw, -hh, hw, hh), SkPathDirection::kCW, 0); return b.detach(); // from the top
    case ShapeKind::Path: return bezierSkPath(shape.path);
    default: break;
    }
    // Polygons, stars and lines: the core outline (the same points the editor uses).
    const Path p = buildShapePath(shape, t);
    if (p.points.empty()) return {};
    b.moveTo(p.points[0].x, p.points[0].y);
    for (size_t i = 1; i < p.points.size(); ++i) b.lineTo(p.points[i].x, p.points[i].y);
    if (p.closed) b.close();
    return b.detach();
}

// The fill's paint: a color, or a gradient across the outline's bounds.
SkPaint fillPaint(const ShapeProps& shape, const SkPath& path, double t, float opacity) {
    const SkColor4f c1 = color4(shape.fillColor.valueAt(t), opacity);
    SkPaint fill(c1);
    fill.setAntiAlias(true);
    fill.setShader(gradientShader(shape.fill, c1, color4(shape.fillColor2.valueAt(t), opacity), shape.fillAngle.valueAt(t), path.getBounds()));
    return fill;
}

void drawShapeCopy(SkCanvas* canvas, const ShapeProps& shape, const SkPath& path, double t, float opacity) {
    const bool line = shape.kind == ShapeKind::Line;
    const bool open = line || (shape.kind == ShapeKind::Path && !shape.path.closed);
    if (shape.fillEnabled && !line) {
        const SkPaint fill = fillPaint(shape, path, t, opacity);
        if (fill.getAlphaf() > 0.0f || fill.getShader()) canvas->drawPath(path, fill);
    }
    const float width = shape.strokeWidth.valueAt(t);
    if (shape.strokeEnabled && width > 0.0f) {
        SkPaint stroke(color4(shape.strokeColor.valueAt(t), opacity));
        if (stroke.getAlphaf() <= 0.0f) return;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(width);
        stroke.setStrokeJoin(SkPaint::kMiter_Join);
        stroke.setStrokeCap(open ? SkPaint::kRound_Cap : SkPaint::kButt_Cap);
        float a = std::clamp(shape.trimStart.valueAt(t) * 0.01f, 0.0f, 1.0f), b = std::clamp(shape.trimEnd.valueAt(t) * 0.01f, 0.0f, 1.0f);
        if (a > b) std::swap(a, b);
        if (b - a <= 1e-4f) return;
        sk_sp<SkPathEffect> effect;
        if (a > 1e-4f || b < 1.0f - 1e-4f) {
            effect = SkTrimPathEffect::Make(a, b);
            stroke.setStrokeCap(SkPaint::kButt_Cap);
        }
        const float dash = shape.dash.valueAt(t), gap = shape.dashGap.valueAt(t);
        if (dash > 0.01f && gap > 0.01f) { // dashes along what the trim leaves
            const SkScalar intervals[2] = {dash, gap};
            sk_sp<SkPathEffect> dashes = SkDashPathEffect::Make(SkSpan<const SkScalar>(intervals, 2), shape.dashOffset.valueAt(t));
            effect = effect ? SkPathEffect::MakeCompose(std::move(dashes), std::move(effect)) : std::move(dashes);
        }
        stroke.setPathEffect(std::move(effect));
        canvas->drawPath(path, stroke);
    }
}

void drawShape(SkCanvas* canvas, const Layer& layer, double t, float opacity) {
    const ShapeProps& shape = layer.shape;
    const SkPath path = shapePath(shape, t);
    if (path.isEmpty()) return;
    const int copies = shape.copiesAt(t);
    if (copies == 1) {
        drawShapeCopy(canvas, shape, path, t, opacity);
        return;
    }
    // The repeater: each copy one step further, fading toward the last; the first on top.
    const Affine step = repeatStep(shape, t);
    std::vector<Affine> at(static_cast<size_t>(copies));
    for (int i = 1; i < copies; ++i) at[i] = at[i - 1] * step;
    const float endOpacity = std::clamp(shape.repeatEndOpacity.valueAt(t) * 0.01f, 0.0f, 1.0f);
    for (int i = copies - 1; i >= 0; --i) {
        canvas->save();
        canvas->concat(toSk(at[i]));
        drawShapeCopy(canvas, shape, path, t, opacity * lerp(1.0f, endOpacity, static_cast<float>(i) / (copies - 1)));
        canvas->restore();
    }
}

// --- plugin objects ------------------------------------------------------------------

PluginDrawer& pluginDrawer() {
    static PluginDrawer drawer;
    return drawer;
}

void drawPlugin(SkCanvas* canvas, const Layer& layer, double t, float opacity) {
    PluginCanvas pc(canvas, opacity);
    if (layer.plugin.fill != ShapeFill::Solid)
        pc.setGradient(layer.plugin.fill, layer.plugin.fillColor2.valueAt(t), layer.plugin.fillAngle.valueAt(t));
    if (pluginDrawer()) {
        pluginDrawer()(layer, t, pc);
    } else { // no plugin host (tests, tools): a placeholder frame
        const Vec2 half = layer.plugin.size.valueAt(t) * 0.5f;
        pc.rectOutline(half * -1.0f, half, Color::fromHex(0x808080), 2.0f);
    }
}

// --- footage -----------------------------------------------------------------------

MediaProvider& mediaProvider() {
    static MediaProvider provider;
    return provider;
}

void drawMedia(SkCanvas* canvas, const Composition* comp, const Layer& layer, double t, float opacity) {
    if (!layer.media.hasVideo) return; // audio only
    Vec2 mn, mx; // cropped: only that part of the picture, where it was
    layer.media.cropRect(t, mn, mx);
    const SkRect dst = SkRect::MakeLTRB(mn.x, mn.y, mx.x, mx.y);
    sk_sp<SkImage> image = comp && mediaProvider() ? mediaProvider()(*comp, layer, t) : nullptr;
    if (!image) { // not loaded or missing: a placeholder frame
        SkPaint p(SkColor4f{0.24f, 0.24f, 0.28f, 0.8f * opacity});
        canvas->drawRect(dst, p);
        p.setColor4f({0.47f, 0.47f, 0.51f, 0.8f * opacity});
        p.setStyle(SkPaint::kStroke_Style);
        p.setAntiAlias(true);
        canvas->drawLine(dst.left(), dst.top(), dst.right(), dst.bottom(), p);
        canvas->drawLine(dst.right(), dst.top(), dst.left(), dst.bottom(), p);
        return;
    }
    SkPaint p;
    p.setAlphaf(std::clamp(opacity, 0.0f, 1.0f));
    // The same part of the image (it may not have the layer's size: a scaled-down preview frame).
    const MediaProps& m = layer.media;
    const Vec2 size = m.size;
    const float sx = size.x > 0 ? image->width() / size.x : 1.0f, sy = size.y > 0 ? image->height() / size.y : 1.0f;
    const SkRect src = SkRect::MakeLTRB((mn.x + size.x * 0.5f) * sx, (mn.y + size.y * 0.5f) * sy, (mx.x + size.x * 0.5f) * sx,
                                        (mx.y + size.y * 0.5f) * sy);
    // Its frame: rounded corners cut the picture, a border goes inside the edge.
    const float radius = std::clamp(m.cornerRadius.valueAt(t), 0.0f, std::min(dst.width(), dst.height()) * 0.5f);
    const SkRRect frame = SkRRect::MakeRectXY(dst, radius, radius);
    canvas->save();
    if (radius > 0.0f) canvas->clipRRect(frame, true);
    if (m.flipH || m.flipV) { // mirrored inside its own frame: bounds and handles stay
        canvas->translate(dst.centerX(), dst.centerY());
        canvas->scale(m.flipH ? -1.0f : 1.0f, m.flipV ? -1.0f : 1.0f);
        canvas->translate(-dst.centerX(), -dst.centerY());
    }
    canvas->drawImageRect(image, src, dst, SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear), &p,
                          SkCanvas::kStrict_SrcRectConstraint);
    canvas->restore();
    const float border = std::min(m.borderWidth.valueAt(t), std::min(dst.width(), dst.height()) * 0.5f);
    if (border > 0.0f) {
        SkPaint line(color4(m.borderColor.valueAt(t), opacity));
        line.setAntiAlias(true);
        line.setStyle(SkPaint::kStroke_Style);
        line.setStrokeWidth(border);
        SkRRect inner = frame;
        inner.inset(border * 0.5f, border * 0.5f);
        canvas->drawRRect(inner, line);
    }
}

// A background: the whole composition frame (local space = composition space).
void drawBackground(SkCanvas* canvas, const Composition* comp, const Layer& layer, double t, float opacity) {
    const float w = comp ? static_cast<float>(comp->width) : 1920.0f, h = comp ? static_cast<float>(comp->height) : 1080.0f;
    const SkRect frame = SkRect::MakeWH(w, h);
    const BackgroundProps& b = layer.background;
    auto color4 = [&](Color c) { return SkColor4f{c.r, c.g, c.b, std::clamp(c.a * opacity, 0.0f, 1.0f)}; };
    const SkColor4f c1 = color4(b.color.valueAt(t)), c2 = color4(b.color2.valueAt(t));
    SkPaint paint(c1);
    switch (b.fill) {
    case BackgroundFill::Linear: {
        // Across the frame along the angle, end to end.
        const float a = b.angle.valueAt(t) * 3.14159265f / 180.0f;
        const float dx = std::cos(a), dy = std::sin(a);
        const float half = (std::fabs(w * dx) + std::fabs(h * dy)) * 0.5f;
        const SkPoint pts[2] = {{w * 0.5f - dx * half, h * 0.5f - dy * half}, {w * 0.5f + dx * half, h * 0.5f + dy * half}};
        const SkColor4f colors[2] = {c1, c2};
        paint.setShader(SkGradientShader::MakeLinear(pts, colors, nullptr, nullptr, 2, SkTileMode::kClamp));
        break;
    }
    case BackgroundFill::Radial: {
        const SkColor4f colors[2] = {c1, c2};
        paint.setShader(SkGradientShader::MakeRadial({w * 0.5f, h * 0.5f}, std::hypot(w, h) * 0.5f, colors, nullptr, nullptr, 2,
                                                     SkTileMode::kClamp));
        break;
    }
    default: break;
    }
    canvas->drawRect(frame, paint);
    if (b.fill == BackgroundFill::Image && comp && mediaProvider()) {
        const MediaItem* item = comp->findMedia(b.mediaId);
        // The media layer it would be (its clip, from the background's start).
        Layer proxy;
        proxy.id = layer.id;
        proxy.type = LayerType::Media;
        proxy.inPoint = layer.inPoint;
        proxy.outPoint = layer.outPoint;
        proxy.media.mediaId = b.mediaId;
        proxy.media.start = layer.inPoint;
        proxy.media.hasVideo = true;
        sk_sp<SkImage> image = item ? mediaProvider()(*comp, proxy, t) : nullptr;
        if (image) {
            const float iw = static_cast<float>(image->width()), ih = static_cast<float>(image->height());
            float sw = w, sh = h; // stretch
            if (b.fit != BackgroundFit::Stretch) {
                const float k = b.fit == BackgroundFit::Cover ? std::max(w / iw, h / ih) : std::min(w / iw, h / ih);
                sw = iw * k, sh = ih * k;
            }
            const float zoom = std::max(0.01f, b.zoom.valueAt(t) * 0.01f);
            sw *= zoom, sh *= zoom;
            const Vec2 off = b.offset.valueAt(t);
            const SkRect dst = SkRect::MakeXYWH((w - sw) * 0.5f + off.x, (h - sh) * 0.5f + off.y, sw, sh);
            SkPaint p;
            p.setAlphaf(std::clamp(opacity, 0.0f, 1.0f));
            canvas->save();
            canvas->clipRect(frame);
            canvas->drawImageRect(image, dst, SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear), &p);
            canvas->restore();
        }
    }
    if (const float dark = std::clamp(b.darken.valueAt(t) * 0.01f, 0.0f, 1.0f); dark > 0.0f)
        canvas->drawRect(frame, SkPaint(SkColor4f{0, 0, 0, dark * opacity}));
}

// --- effects -------------------------------------------------------------------------

// Color effects on straight colors (Skia hands color filters premultiplied colors).
//   0 brightness & contrast   a = brightness, b = contrast
//   1 hue / saturation        a = hue (deg), b = saturation, c = lightness
//   2 tint                    color = white maps to, amount
//   3 fill                    color, amount
//   4 invert                  amount
constexpr const char* kColorOps = R"(
uniform int mode; uniform float a; uniform float b; uniform float c; uniform half4 color; uniform float amount;
half3 hueShift(half3 v, float deg) {
    const half3x3 toYIQ = half3x3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const half3x3 toRGB = half3x3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    half3 yiq = toYIQ * v;
    float r = radians(deg); float cs = cos(r); float sn = sin(r);
    yiq.yz = half2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return toRGB * yiq;
}
half4 main(half4 p) {
    if (p.a < 0.0001) return half4(0);
    half3 v = p.rgb / p.a;
    half luma = dot(v, half3(0.2126, 0.7152, 0.0722));
    if (mode == 0) {
        v += a / 300.0;
        float k = b >= 0.0 ? 1.0 / max(0.01, 1.0 - b / 101.0) : 1.0 + b / 100.0;
        v = (v - 0.5) * k + 0.5;
    } else if (mode == 1) {
        v = hueShift(v, -a); // positive = toward the next hue on the wheel, like After Effects
        half l = dot(v, half3(0.2126, 0.7152, 0.0722));
        v = mix(half3(l), v, 1.0 + b / 100.0);
        v = c >= 0.0 ? mix(v, half3(1.0), c / 100.0) : v * (1.0 + c / 100.0);
    } else if (mode == 2) {
        v = mix(v, color.rgb * luma, amount * color.a);
    } else if (mode == 3) {
        v = mix(v, color.rgb, amount * color.a);
    } else {
        v = mix(v, 1.0 - v, amount);
    }
    v = clamp(v, 0.0, 1.0);
    return half4(v * p.a, p.a);
})";

// Wave warp: pixels move across the wave direction.
constexpr const char* kWave = R"(
uniform shader src; uniform float2 dir; uniform float height; uniform float width; uniform float phase;
half4 main(float2 p) {
    float d = height * sin(6.2831853 * (dot(p, dir) / max(width, 1.0) - phase));
    return src.eval(p + float2(-dir.y, dir.x) * d);
})";

sk_sp<SkRuntimeEffect> effect(const char* sksl, bool colorFilter) {
    auto result = colorFilter ? SkRuntimeEffect::MakeForColorFilter(SkString(sksl)) : SkRuntimeEffect::MakeForShader(SkString(sksl));
    if (!result.effect) std::fprintf(stderr, "effect shader error: %s\n", result.errorText.c_str());
    return result.effect;
}

sk_sp<SkRuntimeEffect> colorOps() {
    static const sk_sp<SkRuntimeEffect> e = effect(kColorOps, true);
    return e;
}

sk_sp<SkImageFilter> colorOp(sk_sp<SkImageFilter> input, int mode, float a, float b, float c, Color color, float amount) {
    if (!colorOps()) return input;
    SkRuntimeColorFilterBuilder builder(colorOps());
    builder.uniform("mode") = mode;
    builder.uniform("a") = a;
    builder.uniform("b") = b;
    builder.uniform("c") = c;
    builder.uniform("color") = SkV4{color.r, color.g, color.b, color.a};
    builder.uniform("amount") = std::clamp(amount, 0.0f, 1.0f);
    return SkImageFilters::ColorFilter(builder.makeColorFilter(), std::move(input));
}

// --- Apply LUT, Curves, Chroma Key ---------------------------------------------------

std::filesystem::path& lutFolder() {
    static std::filesystem::path folder;
    return folder;
}

std::filesystem::path lutPath(const std::string& file) {
    constexpr std::string_view kBundled = "bundled:";
    const bool bundled = file.rfind(kBundled, 0) == 0;
    const std::string name = bundled ? file.substr(kBundled.size()) : file;
    std::filesystem::path p(std::u8string(name.begin(), name.end()));
    return bundled ? lutFolder() / p : p;
}

// A LUT as an image the shader samples: its blue slices side by side (size * size wide, size
// high), red across each slice and green down.
struct LoadedLut {
    std::filesystem::file_time_type stamp;
    sk_sp<SkImage> image;
    int size = 0;
    std::string title, error;
};

LoadedLut loadLut(const std::string& file) { // a copy: the cache may change on another thread
    static std::mutex mutex; // exports render on threads of their own
    static std::map<std::string, LoadedLut> cache;
    std::lock_guard lock(mutex);
    const std::filesystem::path path = lutPath(file);
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    LoadedLut& entry = cache[file];
    if ((entry.image || !entry.error.empty()) && !ec && entry.stamp == stamp) return entry;
    entry = {};
    entry.stamp = stamp;
    if (ec) {
        entry.error = "The LUT file was not found.";
        return entry;
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    Lut3D lut;
    if (!in || !parseCubeLut(text.str(), lut, &entry.error)) {
        if (entry.error.empty()) entry.error = "The LUT file could not be read.";
        return entry;
    }
    const int n = lut.size;
    std::vector<float> rgba(static_cast<size_t>(n) * n * n * 4);
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const float* src = &lut.rgb[((static_cast<size_t>(b) * n + g) * n + r) * 3];
                float* dst = &rgba[(static_cast<size_t>(g) * n * n + b * n + r) * 4];
                dst[0] = std::clamp(src[0], 0.0f, 1.0f), dst[1] = std::clamp(src[1], 0.0f, 1.0f), dst[2] = std::clamp(src[2], 0.0f, 1.0f);
                dst[3] = 1.0f;
            }
    const SkImageInfo info = SkImageInfo::Make(n * n, n, kRGBA_F32_SkColorType, kOpaque_SkAlphaType);
    entry.image = SkImages::RasterFromData(info, SkData::MakeWithCopy(rgba.data(), rgba.size() * sizeof(float)),
                                           static_cast<size_t>(n) * n * 4 * sizeof(float));
    entry.size = n;
    entry.title = lut.title;
    if (!entry.image) entry.error = "The LUT could not be prepared.";
    return entry;
}

// Straight colors through the LUT: two neighboring blue slices, each sampled bilinearly, mixed.
constexpr const char* kLutShader = R"(
uniform shader lut; uniform float n; uniform float amount;
half4 main(half4 p) {
    if (p.a < 0.0001) return half4(0);
    half3 v = clamp(p.rgb / p.a, 0.0, 1.0);
    float b = v.b * (n - 1.0);
    float b0 = floor(b);
    float b1 = min(b0 + 1.0, n - 1.0);
    float2 rg = float2(v.r, v.g) * (n - 1.0) + 0.5;
    half3 c0 = lut.eval(float2(rg.x + b0 * n, rg.y)).rgb;
    half3 c1 = lut.eval(float2(rg.x + b1 * n, rg.y)).rgb;
    half3 c = mix(c0, c1, half(b - b0));
    return half4(mix(v, c, half(amount)) * p.a, p.a);
})";

// Chroma key on straight colors: transparent where the color's chroma (Cb, Cr) is near the
// key's, soft in between; spill takes the key's chroma out of what stays (a green rim).
constexpr const char* kChromaKey = R"(
uniform half3 key; uniform float tolerance; uniform float softness; uniform float spill;
half2 chroma(half3 c) { return half2(dot(c, half3(-0.1687, -0.3313, 0.5)), dot(c, half3(0.5, -0.4187, -0.0813))); }
half4 main(half4 p) {
    if (p.a < 0.0001) return half4(0);
    half3 v = p.rgb / p.a;
    half2 kc = chroma(key);
    half2 cc = chroma(v);
    float d = distance(cc, kc);
    float lo = tolerance * 0.45;
    float alpha = smoothstep(lo, lo + softness * 0.35 + 0.002, d);
    float klen = length(kc);
    if (spill > 0.0 && klen > 0.01) {
        half2 dir = kc / klen;
        float along = max(0.0, dot(cc, dir));
        half2 c2 = cc - dir * along * spill;
        half y = dot(v, half3(0.299, 0.587, 0.114));
        v = clamp(half3(y + 1.402 * c2.y, y - 0.344136 * c2.x - 0.714136 * c2.y, y + 1.772 * c2.x), 0.0, 1.0);
    }
    half a = p.a * half(alpha);
    return half4(v * a, a);
})";

sk_sp<SkImageFilter> applyLut(sk_sp<SkImageFilter> input, const Effect& e, float amount) {
    if (e.file.empty() || amount <= 0.0f) return input;
    const LoadedLut& lut = loadLut(e.file);
    static const sk_sp<SkRuntimeEffect> fx = effect(kLutShader, true);
    if (!lut.image || !fx) return input;
    SkRuntimeColorFilterBuilder builder(fx);
    builder.child("lut") = lut.image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kLinear));
    builder.uniform("n") = static_cast<float>(lut.size);
    builder.uniform("amount") = std::clamp(amount, 0.0f, 1.0f);
    return SkImageFilters::ColorFilter(builder.makeColorFilter(), std::move(input));
}

sk_sp<SkImageFilter> applyCurves(sk_sp<SkImageFilter> input, const Effect& e, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0f || std::all_of(e.curves.begin(), e.curves.end(), [](const auto& c) { return curveIsIdentity(c); }))
        return input;
    uint8_t tables[3][256];
    for (int ch = 0; ch < 3; ++ch)
        for (int i = 0; i < 256; ++i) {
            const float x = static_cast<float>(i) / 255.0f;
            const float y = curveValue(e.curves[0], curveValue(e.curves[ch + 1], x)); // the channel's, then the RGB curve
            tables[ch][i] = static_cast<uint8_t>(std::lround(std::clamp(x + (y - x) * amount, 0.0f, 1.0f) * 255.0f));
        }
    return SkImageFilters::ColorFilter(SkColorFilters::TableARGB(nullptr, tables[0], tables[1], tables[2]), std::move(input));
}

sk_sp<SkImageFilter> applyChromaKey(sk_sp<SkImageFilter> input, const Effect& e, double t, float zoom) {
    static const sk_sp<SkRuntimeEffect> fx = effect(kChromaKey, true);
    if (!fx) return input;
    const Color key = e.color.valueAt(t);
    SkRuntimeColorFilterBuilder builder(fx);
    builder.uniform("key") = SkV3{key.r, key.g, key.b};
    builder.uniform("tolerance") = std::clamp(e.value1.valueAt(t) * 0.01f, 0.0f, 1.0f);
    builder.uniform("softness") = std::clamp(e.value2.valueAt(t) * 0.01f, 0.0f, 1.0f);
    builder.uniform("spill") = std::clamp(e.value3.valueAt(t) * 0.01f, 0.0f, 1.0f);
    sk_sp<SkImageFilter> keyed = SkImageFilters::ColorFilter(builder.makeColorFilter(), std::move(input));
    // Choke: the matte pulled in by about that many pixels (a blurred alpha cut high up its edge,
    // as Stroke grows one; no slow morphology), then the keyed picture kept only inside it.
    const float choke = std::max(0.0f, e.size.valueAt(t)) * zoom;
    if (choke < 0.05f) return keyed;
    constexpr float k = 6.0f, level = 0.84f; // a straight edge blurred by sigma = choke crosses 0.84 that far in
    SkColorMatrix cut;
    const float rows[20] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, k, 0.5f - k * level};
    cut.setRowMajor(rows);
    sk_sp<SkImageFilter> matte = SkImageFilters::ColorFilter(SkColorFilters::Matrix(cut), SkImageFilters::Blur(choke, choke, SkTileMode::kDecal, keyed));
    return SkImageFilters::Blend(SkBlendMode::kDstIn, keyed, matte);
}

// The input's silhouette in one color.
sk_sp<SkImageFilter> silhouette(sk_sp<SkImageFilter> input, Color color, float alpha) {
    return SkImageFilters::ColorFilter(SkColorFilters::Blend(color4(color, alpha), nullptr, SkBlendMode::kSrcIn), std::move(input));
}

// The layer's effect stack as one filter chain, in output pixels (`zoom` = composition -> output).
sk_sp<SkImageFilter> effectChain(const Layer& layer, double t, float zoom) {
    sk_sp<SkImageFilter> current; // null = the layer as drawn
    for (const Effect& e : layer.effects) {
        if (!e.enabled || e.type == EffectType::MotionBlur) continue;
        const float v1 = e.value1.valueAt(t), v2 = e.value2.valueAt(t), v3 = e.value3.valueAt(t);
        const float size = std::max(0.0f, e.size.valueAt(t)) * zoom;
        const float strength = std::max(0.0f, e.opacity.valueAt(t)) * 0.01f;
        const Color color = e.color.valueAt(t);
        switch (e.type) {
        case EffectType::GaussianBlur:
            if (size > 0.01f) current = SkImageFilters::Blur(size * 0.5f, size * 0.5f, SkTileMode::kDecal, current);
            break;
        case EffectType::DirectionalBlur: {
            if (size <= 0.01f) break;
            // A blur along one axis: turn the direction onto x, blur horizontally only, turn back.
            // (A sampling shader is far too slow on the CPU.) Spread like a box of `size`: sigma = size / sqrt(12).
            const float degrees = v1 - 90.0f; // 0° = vertical, like After Effects
            const SkMatrix toX = SkMatrix::RotateDeg(-degrees), back = SkMatrix::RotateDeg(degrees);
            const SkSamplingOptions linear(SkFilterMode::kLinear);
            current = SkImageFilters::MatrixTransform(toX, linear, current);
            current = SkImageFilters::Blur(size * 0.2887f, 0.0f, SkTileMode::kDecal, current);
            current = SkImageFilters::MatrixTransform(back, linear, current);
            break;
        }
        case EffectType::BrightnessContrast: current = colorOp(current, 0, v1, v2, 0, {}, 0); break;
        case EffectType::HueSaturation: current = colorOp(current, 1, v1, v2, v3, {}, 0); break;
        case EffectType::Tint: current = colorOp(current, 2, 0, 0, 0, color, strength); break;
        case EffectType::Fill: current = colorOp(current, 3, 0, 0, 0, color, strength); break;
        case EffectType::Invert: current = colorOp(current, 4, 0, 0, 0, {}, strength); break;
        case EffectType::Lut: current = applyLut(current, e, strength); break;
        case EffectType::Curves: current = applyCurves(current, e, strength); break;
        case EffectType::ChromaKey: current = applyChromaKey(current, e, t, zoom); break;
        case EffectType::WaveWarp: {
            static const sk_sp<SkRuntimeEffect> fx = effect(kWave, false);
            if (!fx) break;
            const float a = degToRad(e.shutter.valueAt(t));
            SkRuntimeShaderBuilder builder(fx);
            builder.uniform("dir") = SkV2{std::cos(a), std::sin(a)};
            builder.uniform("height") = v1 * zoom;
            builder.uniform("width") = std::max(2.0f, v2) * zoom;
            builder.uniform("phase") = static_cast<float>(v3 * (t - layer.inPoint));
            current = SkImageFilters::RuntimeShader(builder, std::fabs(v1 * zoom), "src", current);
            break;
        }
        case EffectType::Outline: { // a grown silhouette underneath
            if (size <= 0.01f) break;
            // Grown by blurring the alpha and cutting it where a straight edge would have
            // faded at `size` (Skia's Dilate is far too slow on the CPU). The cut is ~1 px soft.
            const float sigma = std::max(0.5f, size * 0.5f);
            const float k = std::clamp(9.25f * size, 4.0f, 60.0f);
            SkColorMatrix cut;
            // alpha' = k (alpha - t): reaches 1/2 near the straight-edge level 0.0228, and stays 0
            // where nothing was drawn (t > 0), so the layer does not spread over the whole frame.
            const float t = std::max(0.004f, 0.0228f - 0.5f / k);
            const float rows[20] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, k, -k * t};
            cut.setRowMajor(rows);
            sk_sp<SkImageFilter> grown = SkImageFilters::ColorFilter(
                SkColorFilters::Matrix(cut), SkImageFilters::Blur(sigma, sigma, SkTileMode::kDecal, current));
            sk_sp<SkImageFilter> under = silhouette(grown, color, std::min(strength, 1.0f));
            current = SkImageFilters::Merge(std::move(under), current);
            break;
        }
        case EffectType::Glow: { // a blurred silhouette underneath; intensity above 100% brightens it
            sk_sp<SkImageFilter> halo = silhouette(current, color, 1.0f);
            if (size > 0.01f) halo = SkImageFilters::Blur(size * 0.5f, size * 0.5f, SkTileMode::kDecal, halo);
            if (std::fabs(strength - 1.0f) > 1e-3f) {
                SkColorMatrix gain;
                gain.setScale(strength, strength, strength, strength);
                halo = SkImageFilters::ColorFilter(SkColorFilters::Matrix(gain), halo);
            }
            current = SkImageFilters::Merge(std::move(halo), current);
            break;
        }
        case EffectType::DropShadow: {
            const Vec2 offset = e.offset.valueAt(t) * zoom;
            current = SkImageFilters::DropShadow(offset.x, offset.y, size * 0.5f, size * 0.5f,
                                                 color4(color, std::min(strength, 1.0f)).toSkColor(), current);
            break;
        }
        default: break;
        }
    }
    return current;
}

// Moments to render: one, or several across the shutter for motion blur.
std::vector<double> momentsOf(const Layer& layer, double t, double frameDuration) {
    for (const Effect& e : layer.effects)
        if (e.enabled && e.type == EffectType::MotionBlur) {
            const double shutter = std::clamp(e.shutter.valueAt(t), 0.0f, 720.0f) / 360.0 * frameDuration;
            const int n = shutter > 1e-6 ? std::clamp(e.samples, 2, 64) : 1;
            std::vector<double> times;
            for (int i = 0; i < n; ++i) times.push_back(t - shutter * (n > 1 ? double(i) / (n - 1) : 0.0));
            return times;
        }
    return {t};
}

// How the top-level layers of a composition reach the output: the view, the camera.
struct Frame {
    const Composition* comp = nullptr;
    Affine screen;            // composition -> output
    bool throughCamera = true;
    const Lens* lens = nullptr; // the 3D view's eye
    float zoom = 1.0f;        // composition -> output scale
    Affine flat;              // the 2D camera
};

// Where the layers beside one land: at the top of a frame (each seen by the camera), or inside a
// compound (through its transform). Track mattes are drawn through it.
struct Place {
    const Frame* frame = nullptr;
    bool top = true;
    Projective parent; // inside a compound: compound space -> output
    float zoom = 1.0f;

    // Layer -> output and the effects' scale; false when the camera does not see the layer.
    bool locate(const Layer& l, double t, Projective& m, float& z) const {
        if (!top || !frame || !frame->comp) {
            m = parent * l.matrixAt(t);
            z = zoom;
            return true;
        }
        const Composition& comp = *frame->comp;
        Projective camera;
        if (frame->lens ? !comp.viewThrough(*frame->lens, l, t, camera, true, true) : !comp.viewFor(l, t, camera, frame->throughCamera))
            return false; // behind the camera
        z = frame->zoom;
        if (l.threeD || frame->lens) { // effects shrink with distance (as the 2D camera's own zoom leaves them)
            const float s = camera.localScale(l.transform.position.valueAt(t));
            z *= s / std::max(1e-6f, std::sqrt(std::fabs(frame->flat.determinant())));
        }
        m = Projective(frame->screen) * camera * l.matrixAt(t);
        return true;
    }
};

void drawLayerIn(SkCanvas* canvas, const Layer& layer, double time, const Projective& layerToOutput, float zoom, const Composition* comp,
                 float opacityOverride, const Place* place, const Frame* frame);

// Drawn by itself: not someone's track matte, not hidden by solo.
bool drawnAlone(const Composition& comp, const Layer& l) { return !comp.isMatte(l.id) && !comp.soloedOut(l); }

// The layer's own content in its local space (the canvas carries its transform).
void drawContent(SkCanvas* canvas, const Layer& layer, double t, float opacity, const Composition* comp, const Projective& toOutput,
                 float zoom, const Frame* frame) {
    switch (layer.type) {
    case LayerType::Compound: {
        if (!comp) return;
        // Children, bottom first, in the compound's space; its opacity multiplies theirs.
        const Place inside{frame, false, toOutput, zoom};
        for (auto it = comp->layers.rbegin(); it != comp->layers.rend(); ++it) {
            if (it->parent != layer.id || !it->activeAt(t) || it->type == LayerType::Camera || !drawnAlone(*comp, *it)) continue;
            const float own = std::clamp(it->transform.opacity.valueAt(t) * 0.01f, 0.0f, 1.0f);
            drawLayerIn(canvas, *it, t, toOutput * it->matrixAt(t), zoom, comp, own * opacity, &inside, frame);
        }
        return;
    }
    case LayerType::Media: drawMedia(canvas, comp, layer, t, opacity); return;
    case LayerType::Text: drawText(canvas, layer.text, t, opacity, {layer.inPoint, layer.outPoint}); return;
    case LayerType::Plugin: drawPlugin(canvas, layer, t, opacity); return;
    case LayerType::Background: drawBackground(canvas, comp, layer, t, opacity); return;
    case LayerType::Camera: return;
    default: drawShape(canvas, layer, t, opacity); return;
    }
}

// Paint strokes (Brush / Eraser) over the layer's picture, up to `paint_reveal` percent of their
// total length in order (Write On). An eraser stroke clears what is under it in the layer.
SkPath strokePath(const PaintStroke& s) {
    SkPathBuilder b;
    if (s.points.empty()) return b.detach();
    b.moveTo(s.points[0].x, s.points[0].y);
    if (s.points.size() == 1) b.lineTo(s.points[0].x + 0.01f, s.points[0].y); // a dot
    // Smoothed: quadratic curves through the midpoints, the points as their controls.
    for (size_t i = 1; i + 1 < s.points.size(); ++i) {
        const Vec2 mid = (s.points[i] + s.points[i + 1]) * 0.5f;
        b.quadTo(s.points[i].x, s.points[i].y, mid.x, mid.y);
    }
    if (s.points.size() >= 2) b.lineTo(s.points.back().x, s.points.back().y);
    return b.detach();
}

void drawStrokes(SkCanvas* canvas, const Layer& layer, double t, float opacity) {
    std::vector<SkPath> paths;
    paths.reserve(layer.paint.size());
    float total = 0.0f;
    for (const PaintStroke& s : layer.paint) {
        paths.push_back(strokePath(s));
        total += SkPathMeasure(paths.back(), false).getLength();
    }
    float left = total * std::clamp(layer.paintReveal.valueAt(t) * 0.01f, 0.0f, 1.0f);
    for (size_t i = 0; i < layer.paint.size() && left > 0.0f; ++i) {
        const PaintStroke& s = layer.paint[i];
        SkPathMeasure measure(paths[i], false);
        const float length = measure.getLength();
        SkPath shown = paths[i];
        if (left < length) { // the stroke being written
            SkPathBuilder part;
            measure.getSegment(0.0f, left, &part, true);
            shown = part.detach();
        }
        left -= length;
        SkPaint paint(color4(s.color, opacity));
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(s.width);
        paint.setStrokeCap(SkPaint::kRound_Cap);
        paint.setStrokeJoin(SkPaint::kRound_Join);
        if (s.erase) paint.setBlendMode(SkBlendMode::kClear);
        if (s.softness > 0.01f) paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, s.softness * 0.5f));
        canvas->drawPath(shown, paint);
    }
}

// The layer's picture with its paint on top (a layer of its own when an eraser must cut it).
void drawPainted(SkCanvas* canvas, const Layer& layer, double t, float opacity, const Composition* comp, const Projective& toOutput,
                 float zoom, const Frame* frame) {
    if (layer.paint.empty()) {
        drawContent(canvas, layer, t, opacity, comp, toOutput, zoom, frame);
        return;
    }
    const bool erases = std::any_of(layer.paint.begin(), layer.paint.end(), [](const PaintStroke& s) { return s.erase; });
    if (erases) {
        Vec2 lo, hi;
        SkRect bounds;
        const bool bounded = localBounds(layer, t, lo, hi, comp);
        if (bounded) bounds = SkRect::MakeLTRB(lo.x, lo.y, hi.x, hi.y);
        canvas->saveLayer(bounded ? &bounds : nullptr, nullptr);
    }
    canvas->save(); // a compound's children set the canvas' matrix of their own: the strokes are in its space
    drawContent(canvas, layer, t, opacity, comp, toOutput, zoom, frame);
    canvas->restore();
    drawStrokes(canvas, layer, t, opacity);
    if (erases) canvas->restore();
}

// The Puppet mesh covers what the picture shows (core/Deform): which cells of its grid hold
// something, read once from the picture drawn small (again when its box or content changes).
void updatePuppetShape(SkCanvas* canvas, const Layer& layer, double t, const Composition* comp, float zoom, const Frame* frame) {
    Vec2 lo, hi;
    if (layer.pins.empty() || !pictureBounds(layer, t, lo, hi, comp)) return;
    for (const PaintStroke& s : layer.paint) // strokes reach past the picture (as drawSource sees it)
        for (const Vec2& p : s.points) {
            lo = {std::min(lo.x, p.x - s.width), std::min(lo.y, p.y - s.width)};
            hi = {std::max(hi.x, p.x + s.width), std::max(hi.y, p.y + s.width)};
        }
    char key[160];
    std::snprintf(key, sizeof key, "%.1f,%.1f,%.1f,%.1f|%zu|%llu|", lo.x, lo.y, hi.x, hi.y, layer.paint.size(),
                  static_cast<unsigned long long>(layer.media.mediaId));
    std::string shapeKey = key + (layer.type == LayerType::Text ? layer.text.text : std::string());
    if (puppetShapeKey(layer.id) == shapeKey) return;
    PuppetShape shape;
    shape.key = std::move(shapeKey);
    shape.lo = lo, shape.hi = hi;
    puppetGrid(lo, hi, shape.cols, shape.rows);
    constexpr int kSub = 4; // samples per cell side
    const SkImageInfo info = SkImageInfo::MakeN32Premul(shape.cols * kSub, shape.rows * kSub);
    sk_sp<SkSurface> small = canvas->makeSurface(info); // where the canvas draws: a video's frame may be on the graphics card
    if (!small) small = SkSurfaces::Raster(info);
    std::vector<uint32_t> pixels(static_cast<size_t>(info.width() * info.height()), 0);
    if (small) {
        SkCanvas* c = small->getCanvas();
        c->clear(SK_ColorTRANSPARENT);
        const Affine toImage = Affine::scale(Vec2{info.width() / std::max(1e-3f, hi.x - lo.x), info.height() / std::max(1e-3f, hi.y - lo.y)}) *
                               Affine::translate(lo * -1.0f);
        c->setMatrix(toSk(Projective(toImage)));
        drawPainted(c, layer, t, 1.0f, comp, Projective(toImage), zoom, frame);
        small->readPixels(SkPixmap(info, pixels.data(), info.minRowBytes()), 0, 0);
    }
    shape.inside.assign(static_cast<size_t>(shape.cols * shape.rows), 0);
    for (int y = 0; y < info.height(); ++y)
        for (int x = 0; x < info.width(); ++x)
            if ((pixels[static_cast<size_t>(y * info.width() + x)] >> 24) > 16) // alpha: the top byte in RGBA and BGRA alike
                shape.inside[static_cast<size_t>((y / kSub) * shape.cols + x / kSub)] = 1;
    setPuppetShape(layer.id, std::move(shape));
}

// The picture bent by Puppet pins and the Mesh Warp (core/Deform.h): drawn into an image of its
// bounds at about the size it shows, then through a grid of triangles whose corners moved.
void drawSource(SkCanvas* canvas, const Layer& layer, double t, float opacity, const Composition* comp, const Projective& toOutput,
                float zoom, const Frame* frame) {
    updatePuppetShape(canvas, layer, t, comp, zoom, frame);
    Vec2 lo, hi;
    if (!deforms(layer, t) || !pictureBounds(layer, t, lo, hi, comp)) {
        drawPainted(canvas, layer, t, opacity, comp, toOutput, zoom, frame);
        return;
    }
    if (!layer.paint.empty()) // strokes reach past the picture
        for (const PaintStroke& s : layer.paint)
            for (const Vec2& p : s.points) {
                lo = {std::min(lo.x, p.x - s.width), std::min(lo.y, p.y - s.width)};
                hi = {std::max(hi.x, p.x + s.width), std::max(hi.y, p.y + s.width)};
            }
    const float w = std::max(1.0f, hi.x - lo.x), h = std::max(1.0f, hi.y - lo.y);
    // Pixels per layer unit where it shows (the canvas' scale), at most 4096 on a side.
    const SkMatrix m = canvas->getTotalMatrix();
    const float scale = std::clamp(std::sqrt(std::fabs(m.getScaleX() * m.getScaleY() - m.getSkewX() * m.getSkewY())), 0.05f, 4.0f);
    const float s = std::min(scale, 4096.0f / std::max(w, h));
    const int iw = std::max(1, static_cast<int>(std::ceil(w * s))), ih = std::max(1, static_cast<int>(std::ceil(h * s)));
    const SkImageInfo info = SkImageInfo::MakeN32Premul(iw, ih);
    sk_sp<SkSurface> surface = canvas->makeSurface(info); // the canvas' own kind: the graphics card when it draws there
    if (!surface) surface = SkSurfaces::Raster(info);
    if (!surface) return;
    SkCanvas* c = surface->getCanvas();
    c->clear(SK_ColorTRANSPARENT);
    // Layer space -> the image (a compound's children are placed with it).
    const Affine toImage = Affine::scale(s) * Affine::translate(lo * -1.0f);
    c->setMatrix(toSk(Projective(toImage)));
    drawPainted(c, layer, t, opacity, comp, Projective(toImage), zoom, frame);
    const sk_sp<SkImage> picture = surface->makeImageSnapshot();
    // A 32 x 32 grid of cells: fine enough for smooth bends.
    constexpr int kCells = 32;
    std::vector<SkPoint> positions, coords;
    positions.reserve((kCells + 1) * (kCells + 1));
    coords.reserve(positions.capacity());
    for (int y = 0; y <= kCells; ++y)
        for (int x = 0; x <= kCells; ++x) {
            const Vec2 rest{lo.x + w * x / kCells, lo.y + h * y / kCells};
            const Vec2 moved = deformPoint(layer, t, rest, lo, hi);
            positions.push_back({moved.x, moved.y});
            coords.push_back({(rest.x - lo.x) * s, (rest.y - lo.y) * s});
        }
    std::vector<uint16_t> indices;
    indices.reserve(kCells * kCells * 6);
    for (int y = 0; y < kCells; ++y)
        for (int x = 0; x < kCells; ++x) {
            const auto a = static_cast<uint16_t>(y * (kCells + 1) + x), b = static_cast<uint16_t>(a + 1);
            const auto c2 = static_cast<uint16_t>(a + kCells + 1), d = static_cast<uint16_t>(c2 + 1);
            indices.insert(indices.end(), {a, b, d, a, d, c2});
        }
    const sk_sp<SkVertices> mesh = SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, static_cast<int>(positions.size()), positions.data(),
                                                        coords.data(), nullptr, static_cast<int>(indices.size()), indices.data());
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(picture->makeShader(SkSamplingOptions(SkFilterMode::kLinear)));
    canvas->drawVertices(mesh, SkBlendMode::kModulate, paint);
}

// How much of a mask's coverage the next one leaves (its mode), drawn over the masks above it.
SkBlendMode maskBlend(MaskMode mode) {
    switch (mode) {
    case MaskMode::Subtract: return SkBlendMode::kDstOut;
    case MaskMode::Intersect: return SkBlendMode::kDstIn;
    case MaskMode::Difference: return SkBlendMode::kXor;
    default: return SkBlendMode::kSrcOver;
    }
}

// One mask's coverage, white, in layer space: its outline grown or shrunk, softened, maybe inverted.
void drawMaskCoverage(SkCanvas* canvas, const Mask& m, double t) {
    const SkPath path = bezierSkPath(m.pathAt(t));
    SkPaint white(SkColors::kWhite);
    white.setAntiAlias(true);
    if (const float feather = m.feather.valueAt(t); feather > 0.01f) white.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, feather * 0.5f));
    if (m.inverted) canvas->saveLayer(nullptr, nullptr);
    canvas->drawPath(path, white);
    if (const float grow = m.expansion.valueAt(t); std::fabs(grow) > 0.01f) {
        SkPaint edge = white;
        edge.setStyle(SkPaint::kStroke_Style);
        edge.setStrokeWidth(std::fabs(grow) * 2.0f);
        edge.setStrokeJoin(SkPaint::kRound_Join);
        if (grow < 0.0f) edge.setBlendMode(SkBlendMode::kDstOut);
        canvas->drawPath(path, edge);
    }
    if (m.inverted) { // white where the mask was not
        SkPaint rest(SkColors::kWhite);
        rest.setBlendMode(SkBlendMode::kSrcOut);
        canvas->drawPaint(rest);
        canvas->restore();
    }
}

// The content cut by the layer's masks (in layer space, the canvas carrying its transform).
void drawMasked(SkCanvas* canvas, const Layer& layer, double t, float opacity, const Composition* comp, const Projective& toOutput,
                float zoom, const Frame* frame) {
    const bool masked = std::any_of(layer.masks.begin(), layer.masks.end(), [](const Mask& m) { return m.enabled && (!m.path.empty() || !m.pathKeys.empty()); });
    if (!masked) {
        drawSource(canvas, layer, t, opacity, comp, toOutput, zoom, frame);
        return;
    }
    Vec2 lo, hi;
    SkRect bounds;
    const SkRect* hint = nullptr;
    if (localBounds(layer, t, lo, hi, comp)) {
        const float pad = 2.0f + 0.02f * std::max(hi.x - lo.x, hi.y - lo.y);
        bounds = SkRect::MakeLTRB(lo.x - pad, lo.y - pad, hi.x + pad, hi.y + pad);
        hint = &bounds;
    }
    canvas->saveLayer(hint, nullptr);
    drawSource(canvas, layer, t, opacity, comp, toOutput, zoom, frame);
    SkPaint keep;
    keep.setBlendMode(SkBlendMode::kDstIn);
    canvas->saveLayer(hint, &keep);
    bool first = true;
    for (const Mask& m : layer.masks) {
        if (!m.enabled || (m.path.empty() && m.pathKeys.empty())) continue;
        // A first mask that takes away starts from everything (as in After Effects).
        if (first && (m.mode == MaskMode::Subtract || m.mode == MaskMode::Intersect)) canvas->drawPaint(SkPaint(SkColors::kWhite));
        SkPaint compose;
        compose.setAlphaf(std::clamp(m.opacity.valueAt(t) * 0.01f, 0.0f, 1.0f));
        compose.setBlendMode(first && m.mode == MaskMode::Difference ? SkBlendMode::kSrcOver : maskBlend(m.mode));
        canvas->saveLayer(hint, &compose);
        drawMaskCoverage(canvas, m, t);
        canvas->restore();
        first = false;
    }
    canvas->restore();
    canvas->restore();
}

SkBlendMode skBlend(BlendMode mode) {
    switch (mode) {
    case BlendMode::Multiply: return SkBlendMode::kMultiply;
    case BlendMode::Screen: return SkBlendMode::kScreen;
    case BlendMode::Overlay: return SkBlendMode::kOverlay;
    case BlendMode::Darken: return SkBlendMode::kDarken;
    case BlendMode::Lighten: return SkBlendMode::kLighten;
    case BlendMode::ColorDodge: return SkBlendMode::kColorDodge;
    case BlendMode::ColorBurn: return SkBlendMode::kColorBurn;
    case BlendMode::HardLight: return SkBlendMode::kHardLight;
    case BlendMode::SoftLight: return SkBlendMode::kSoftLight;
    case BlendMode::Difference: return SkBlendMode::kDifference;
    case BlendMode::Exclusion: return SkBlendMode::kExclusion;
    case BlendMode::Hue: return SkBlendMode::kHue;
    case BlendMode::Saturation: return SkBlendMode::kSaturation;
    case BlendMode::Color: return SkBlendMode::kColor;
    case BlendMode::Luminosity: return SkBlendMode::kLuminosity;
    case BlendMode::Add: return SkBlendMode::kPlus;
    default: return SkBlendMode::kSrcOver;
    }
}

// Output pixels the layer covers at these moments (`parent`: its parent space -> output); false
// when unbounded (or reaching behind the camera).
bool outputBounds(const Layer& layer, const std::vector<double>& times, const Projective& parent, const Composition* comp, SkRect& out) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (double ti : times) {
        Vec2 lo, hi;
        if (!localBounds(layer, ti, lo, hi, comp)) return false;
        const Projective m = parent * layer.matrixAt(ti);
        for (Vec2 corner : {lo, Vec2{hi.x, lo.y}, hi, Vec2{lo.x, hi.y}}) {
            if (!m.inFront(corner)) return false;
            const Vec2 q = m.apply(corner);
            x0 = std::min(x0, q.x), y0 = std::min(y0, q.y), x1 = std::max(x1, q.x), y1 = std::max(y1, q.y);
        }
    }
    out = SkRect::MakeLTRB(x0 - 4, y0 - 4, x1 + 4, y1 + 4); // antialiasing and strokes past the bounds
    return !out.isEmpty();
}

void drawLayerCore(SkCanvas* canvas, const Layer& layer, double time, const Projective& layerToOutput, float zoom, const Composition* comp,
                   float opacityOverride, const Frame* frame);

// A layer with its blending mode and track matte around the rest (drawLayerCore).
void drawLayerIn(SkCanvas* canvas, const Layer& layer, double time, const Projective& layerToOutput, float zoom, const Composition* comp,
                 float opacityOverride, const Place* place, const Frame* frame) {
    const bool matted = layer.matte != MatteMode::None && comp && place;
    if (layer.blend == BlendMode::Normal && !matted) {
        drawLayerCore(canvas, layer, time, layerToOutput, zoom, comp, opacityOverride, frame);
        return;
    }
    SkRect bounds;
    const Projective parent = layerToOutput * layer.matrixAt(time).inverse();
    const SkRect* hint = outputBounds(layer, momentsOf(layer, time, comp ? comp->frameDuration() : 1.0 / 30.0), parent, comp, bounds) ? &bounds : nullptr;
    canvas->save();
    canvas->resetMatrix();
    SkPaint blend;
    blend.setBlendMode(skBlend(layer.blend));
    canvas->saveLayer(hint, &blend);
    drawLayerCore(canvas, layer, time, layerToOutput, zoom, comp, opacityOverride, frame);
    if (matted) {
        // The matte's picture (its alpha, or its brightness) keeps or removes the layer's pixels.
        SkPaint cut;
        const bool inverted = layer.matte == MatteMode::AlphaInverted || layer.matte == MatteMode::LumaInverted;
        cut.setBlendMode(inverted ? SkBlendMode::kDstOut : SkBlendMode::kDstIn);
        if (layer.matte == MatteMode::Luma || layer.matte == MatteMode::LumaInverted) cut.setColorFilter(SkLumaColorFilter::Make());
        canvas->saveLayer(hint, &cut);
        const Layer* matte = comp->findLayer(layer.matteLayer);
        Projective m;
        float z = zoom;
        // Seen while it exists (its eye may be off: it is not drawn by itself anyway).
        if (matte && time >= matte->inPoint && time < matte->outPoint && place->locate(*matte, time, m, z))
            drawLayerIn(canvas, *matte, time, m, z, comp, -1.0f, place, frame);
        canvas->restore();
    }
    canvas->restore();
    canvas->restore();
}

} // namespace

void setPluginDrawer(PluginDrawer drawer) { pluginDrawer() = std::move(drawer); }
void setMediaProvider(MediaProvider provider) { mediaProvider() = std::move(provider); }
void setLutFolder(const std::filesystem::path& folder) { lutFolder() = folder; }

std::vector<std::string> bundledLuts() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(lutFolder(), ec))
        if (entry.path().extension() == ".cube") {
            const std::u8string name = entry.path().filename().u8string();
            names.emplace_back(name.begin(), name.end());
        }
    std::sort(names.begin(), names.end());
    return names;
}

bool lutSwatch(const std::string& file, int w, int h, std::vector<uint8_t>& rgba) {
    if (w <= 0 || h <= 0) return false;
    Lut3D lut;
    if (!file.empty()) {
        std::ifstream in(lutPath(file), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        if (!in || !parseCubeLut(text.str(), lut) || lut.size < 2) return false;
    }
    const int n = lut.size;
    auto at = [&](int r, int g, int b, int c) { return lut.rgb[((static_cast<size_t>(b) * n + g) * n + r) * 3 + c]; };
    auto apply = [&](float rgb[3]) { // trilinear, as the shader samples
        if (n < 2) return;
        float pos[3];
        int i0[3], i1[3];
        for (int c = 0; c < 3; ++c) {
            pos[c] = std::clamp(rgb[c], 0.0f, 1.0f) * (n - 1);
            i0[c] = std::min(n - 2, static_cast<int>(pos[c]));
            i1[c] = i0[c] + 1;
            pos[c] -= i0[c];
        }
        for (int c = 0; c < 3; ++c) {
            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            const float c00 = lerp(at(i0[0], i0[1], i0[2], c), at(i1[0], i0[1], i0[2], c), pos[0]);
            const float c10 = lerp(at(i0[0], i1[1], i0[2], c), at(i1[0], i1[1], i0[2], c), pos[0]);
            const float c01 = lerp(at(i0[0], i0[1], i1[2], c), at(i1[0], i0[1], i1[2], c), pos[0]);
            const float c11 = lerp(at(i0[0], i1[1], i1[2], c), at(i1[0], i1[1], i1[2], c), pos[0]);
            rgb[c] = lerp(lerp(c00, c10, pos[1]), lerp(c01, c11, pos[1]), pos[2]);
        }
    };
    rgba.assign(static_cast<size_t>(w) * h * 4, 255);
    const int grayFrom = h * 3 / 4;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float rgb[3];
            const float u = w > 1 ? static_cast<float>(x) / (w - 1) : 0.0f;
            if (y >= grayFrom) {
                rgb[0] = rgb[1] = rgb[2] = u;
            } else { // hue across, value down (a soft saturation: skin, sky and foliage tones)
                const float v = 0.95f - 0.7f * static_cast<float>(y) / std::max(1, grayFrom - 1), s = 0.6f;
                const float hh = u * 6.0f, f = hh - std::floor(hh);
                const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
                switch (static_cast<int>(hh) % 6) {
                case 0: rgb[0] = v, rgb[1] = t, rgb[2] = p; break;
                case 1: rgb[0] = q, rgb[1] = v, rgb[2] = p; break;
                case 2: rgb[0] = p, rgb[1] = v, rgb[2] = t; break;
                case 3: rgb[0] = p, rgb[1] = q, rgb[2] = v; break;
                case 4: rgb[0] = t, rgb[1] = p, rgb[2] = v; break;
                default: rgb[0] = v, rgb[1] = p, rgb[2] = q; break;
                }
            }
            apply(rgb);
            uint8_t* px = rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>(std::lround(std::clamp(rgb[c], 0.0f, 1.0f) * 255.0f));
        }
    return true;
}

bool lutInfo(const std::string& file, std::string& title, std::string& error) {
    const LoadedLut& lut = loadLut(file);
    title = lut.title;
    error = lut.error;
    return lut.image != nullptr;
}

bool hasActiveEffects(const Layer& layer) {
    return std::any_of(layer.effects.begin(), layer.effects.end(), [](const Effect& e) { return e.enabled; });
}

void drawLayer(SkCanvas* canvas, const Layer& layer, double time, const Projective& layerToOutput, float zoom,
               const Composition* comp, float opacityOverride) {
    const Place beside{nullptr, false, layerToOutput * layer.matrixAt(time).inverse(), zoom};
    drawLayerIn(canvas, layer, time, layerToOutput, zoom, comp, opacityOverride, &beside, nullptr);
}

namespace {
void drawLayerCore(SkCanvas* canvas, const Layer& layer, double time, const Projective& layerToOutput, float zoom, const Composition* comp,
                   float opacityOverride, const Frame* frame) {
    const float opacity = opacityOverride >= 0.0f ? std::min(opacityOverride, 1.0f)
                                                  : std::clamp(layer.transform.opacity.valueAt(time) * 0.01f, 0.0f, 1.0f);
    if (opacity <= 0.0f || layer.type == LayerType::Camera) return;
    // Transforms are absolute (`layerToOutput` is the whole chain), so nested compounds do not stack them twice.
    if (!hasActiveEffects(layer)) {
        canvas->save();
        canvas->setMatrix(toSk(layerToOutput));
        if (clipToFront(canvas, layerToOutput)) drawMasked(canvas, layer, time, opacity, comp, layerToOutput, zoom, frame);
        canvas->restore();
        return;
    }

    // With effects: draw into a layer filtered in output space (no transform on the filter).
    // `layerToOutput` is for `time`; other moments (motion blur) rebuild it from the parent.
    const Projective parent = layerToOutput * layer.matrixAt(time).inverse();
    const std::vector<double> times = momentsOf(layer, time, comp ? comp->frameDuration() : 1.0 / 30.0);
    // The offscreen layer only needs to hold what the layer draws (in output pixels): image
    // filters on the CPU cost per pixel of the layer.
    SkRect bounds;
    const SkRect* hint = outputBounds(layer, times, parent, comp, bounds) ? &bounds : nullptr;
    canvas->save();
    canvas->resetMatrix();
    SkPaint filtered;
    filtered.setImageFilter(effectChain(layer, time, zoom));
    filtered.setAlphaf(opacity);
    canvas->saveLayer(hint, &filtered);
    // Motion blur sums its moments, each a 1/n share: in 8 bits every share rounds, and a dark
    // color comes out lighter. The sum goes into a layer of half floats.
    if (times.size() > 1) canvas->saveLayer(SkCanvas::SaveLayerRec(hint, nullptr, SkCanvas::kF16ColorType));
    for (double ti : times) {
        const bool average = times.size() > 1;
        if (average) {
            SkPaint add;
            add.setBlendMode(SkBlendMode::kPlus);
            add.setAlphaf(1.0f / static_cast<float>(times.size()));
            canvas->saveLayer(hint, &add);
        }
        const Projective m = parent * layer.matrixAt(ti);
        canvas->save();
        canvas->setMatrix(toSk(m));
        if (clipToFront(canvas, m)) drawMasked(canvas, layer, ti, 1.0f, comp, m, zoom, frame);
        canvas->restore();
        if (average) canvas->restore();
    }
    if (times.size() > 1) canvas->restore();
    canvas->restore();
    canvas->restore();
}
} // namespace

// Drawing order in depth, refined with each layer's size: a layer is judged by its far end, so a
// big plane (a floor, a wall) goes before what stands in front of it or on it. Sorted: each run of
// neighboring 3D layers through the camera, everything in the 3D view. The free view keeps its order.
std::vector<const Layer*> depthOrder(const Composition& comp, double time, bool cameras, bool throughCamera, const Lens* lens) {
    std::vector<const Layer*> order = comp.drawOrder(time, cameras, lens);
    if (!throughCamera && !lens) return order;
    auto farEnd = [&](const Layer* l) {
        Projective view;
        if (l->type == LayerType::Camera) return -1e30f;
        if (lens ? !comp.viewThrough(*lens, *l, time, view, true, true) : !comp.viewFor(*l, time, view, true)) return -1e30f;
        Vec2 mn, mx;
        const Projective m = view * comp.worldMatrix(*l, time);
        if (!localBounds(*l, time, mn, mx, &comp)) return view.w(l->transform.position.valueAt(time));
        float far = -1e30f;
        for (const Vec2& c : {mn, Vec2{mx.x, mn.y}, mx, Vec2{mn.x, mx.y}}) far = std::max(far, m.w(c));
        return far;
    };
    auto sortRange = [&](size_t i, size_t j) {
        std::vector<std::pair<float, const Layer*>> keyed;
        for (size_t k = i; k < j; ++k) keyed.emplace_back(farEnd(order[k]), order[k]);
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t k = i; k < j; ++k) order[k] = keyed[k - i].second;
    };
    if (lens) {
        sortRange(0, order.size());
        return order;
    }
    for (size_t i = 0; i < order.size();) {
        size_t j = i;
        while (j < order.size() && order[j]->threeD) ++j;
        if (j > i + 1) sortRange(i, j);
        i = j > i ? j : i + 1;
    }
    return order;
}

void drawComposition(SkCanvas* canvas, const Composition& comp, double time, const View& view, bool background,
                     bool throughCamera, const Lens* lens) {
    const Vec2 a = view.toScreen({0, 0});
    const Vec2 b = view.toScreen({static_cast<float>(comp.width), static_cast<float>(comp.height)});
    const SkRect frame = SkRect::MakeLTRB(a.x, a.y, b.x, b.y);
    const ExpressionScope expressions(comp, true); // what expressions read while drawing (unchanged meanwhile)
    if (background && !lens) {
        const Color bg = comp.background;
        canvas->drawRect(frame, SkPaint(SkColor4f{bg.r, bg.g, bg.b, 1.0f}));
    }
    canvas->save();
    if (!lens) canvas->clipRect(frame, true); // layers are clipped to the composition frame (not in the 3D view)
    // Backgrounds stay on the frame whatever the camera does. 3D layers are seen in perspective
    // through the camera (the free view shows them from the front; the 3D view shows every layer
    // in space, from its own eye).
    const Frame f{&comp, Affine::translate(view.origin) * Affine::scale(view.zoom), throughCamera, lens, view.zoom,
                  throughCamera && !lens ? comp.cameraAt(time) : Affine{}};
    const Place top{&f, true, {}, view.zoom};
    for (const Layer* layer : depthOrder(comp, time, false, throughCamera, lens)) {
        if (!drawnAlone(comp, *layer)) continue; // a track matte, or hidden by solo
        Projective m;
        float zoom = view.zoom;
        if (!top.locate(*layer, time, m, zoom)) continue; // behind the camera
        drawLayerIn(canvas, *layer, time, m, zoom, &comp, -1.0f, &top, &f);
    }
    canvas->restore();
}

bool pictureBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax, const Composition* comp) {
    if (layer.type == LayerType::Compound) { // everything inside, in the compound's space
        outMin = {1e9f, 1e9f};
        outMax = {-1e9f, -1e9f};
        bool any = false;
        if (comp)
            for (const Layer& child : comp->layers) {
                Vec2 mn, mx;
                if (child.parent != layer.id || child.type == LayerType::Camera || !child.activeAt(time) ||
                    !localBounds(child, time, mn, mx, comp))
                    continue;
                const Affine m = child.matrixAt(time);
                for (const Vec2& c : {mn, Vec2{mx.x, mn.y}, mx, Vec2{mn.x, mx.y}}) {
                    const Vec2 p = m.apply(c);
                    outMin = {std::min(outMin.x, p.x), std::min(outMin.y, p.y)};
                    outMax = {std::max(outMax.x, p.x), std::max(outMax.y, p.y)};
                }
                any = true;
            }
        if (!any) outMin = {-20, -20}, outMax = {20, 20}; // empty: still something to grab
        return true;
    }
    if (layer.type == LayerType::Camera) { // the camera frame: what it sees at its zoom
        const float zoom = std::max(0.01f, layer.camera.zoom.valueAt(time) * 0.01f);
        outMin = layer.camera.frameSize * (-0.5f / zoom);
        outMax = layer.camera.frameSize * (0.5f / zoom);
        return true;
    }
    if (layer.type == LayerType::Media) { // audio has no picture to pick
        if (!layer.media.hasVideo) return false;
        layer.media.cropRect(time, outMin, outMax);
        return true;
    }
    if (layer.type == LayerType::Background) { // the frame
        outMin = {0, 0};
        outMax = comp ? Vec2{static_cast<float>(comp->width), static_cast<float>(comp->height)} : Vec2{1920, 1080};
        return true;
    }
    if (layer.type == LayerType::Plugin) {
        const Vec2 half = layer.plugin.size.valueAt(time) * 0.5f;
        outMin = half * -1.0f;
        outMax = half;
        return true;
    }
    if (layer.type == LayerType::Text) {
        textBounds(layer.text, time, outMin, outMax, {layer.inPoint, layer.outPoint});
        return true;
    }
    const ShapeProps& s = layer.shape;
    float pad = s.strokeEnabled ? s.strokeWidth.valueAt(time) * 0.5f : 0.0f;
    if (s.kind == ShapeKind::Line || (s.kind == ShapeKind::Path && !s.path.closed)) pad = std::max(pad, 6.0f);
    Vec2 lo, hi;
    if (s.kind == ShapeKind::Path) {
        if (!bezierBounds(s.path, lo, hi)) lo = {-20, -20}, hi = {20, 20}; // empty: still something to grab
    } else {
        const Vec2 size = s.size.valueAt(time);
        hi = {std::fabs(size.x) * 0.5f, std::fabs(size.y) * 0.5f};
        lo = hi * -1.0f;
    }
    lo = lo - Vec2{pad, pad}, hi = hi + Vec2{pad, pad};
    outMin = lo, outMax = hi;
    if (const int copies = s.copiesAt(time); copies > 1) { // every copy of the repeater
        const Affine step = repeatStep(s, time);
        Affine at;
        for (int i = 1; i < copies; ++i) {
            at = at * step;
            for (const Vec2& c : {lo, Vec2{hi.x, lo.y}, hi, Vec2{lo.x, hi.y}}) {
                const Vec2 q = at.apply(c);
                outMin = {std::min(outMin.x, q.x), std::min(outMin.y, q.y)};
                outMax = {std::max(outMax.x, q.x), std::max(outMax.y, q.y)};
            }
        }
    }
    return true;
}

bool localBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax, const Composition* comp) {
    if (!pictureBounds(layer, time, outMin, outMax, comp)) return false;
    if (layer.type == LayerType::Compound || layer.type == LayerType::Camera) return true;
    const Vec2 lo = outMin, hi = outMax;
    // The box grows with what is painted (an eraser only takes away). A shape with nothing of its
    // own to show (the Brush's "Paint" layer) is only as big as its paint.
    const bool painted = std::any_of(layer.paint.begin(), layer.paint.end(), [](const PaintStroke& s) { return !s.erase && !s.points.empty(); });
    if (painted && layer.type == LayerType::Shape && !layer.shape.fillEnabled && !layer.shape.strokeEnabled)
        outMin = {1e9f, 1e9f}, outMax = {-1e9f, -1e9f};
    for (const PaintStroke& s : layer.paint) {
        if (s.erase) continue;
        for (const Vec2& p : s.points) {
            const float r = s.width * 0.5f + s.softness;
            outMin = {std::min(outMin.x, p.x - r), std::min(outMin.y, p.y - r)};
            outMax = {std::max(outMax.x, p.x + r), std::max(outMax.y, p.y + r)};
        }
    }
    if (deforms(layer, time)) { // where the bent picture's edge goes
        const Vec2 a = outMin, b = outMax;
        constexpr int kSteps = 16;
        for (int i = 0; i <= kSteps; ++i) {
            const float u = static_cast<float>(i) / kSteps;
            for (const Vec2& e : {Vec2{a.x + (b.x - a.x) * u, a.y}, Vec2{a.x + (b.x - a.x) * u, b.y}, Vec2{a.x, a.y + (b.y - a.y) * u},
                                  Vec2{b.x, a.y + (b.y - a.y) * u}}) {
                const Vec2 q = deformPoint(layer, time, e, lo, hi);
                outMin = {std::min(outMin.x, q.x), std::min(outMin.y, q.y)};
                outMax = {std::max(outMax.x, q.x), std::max(outMax.y, q.y)};
            }
        }
    }
    return true;
}

namespace {
// The composition point in the layer's own space (false: the camera does not see it).
bool toLocal(const Composition& comp, const Layer& layer, double time, Vec2 compPoint, bool throughCamera, Vec2& out,
             const Lens* lens = nullptr) {
    Projective camera;
    if (lens ? !comp.viewThrough(*lens, layer, time, camera, true, true) : !comp.viewFor(layer, time, camera, throughCamera)) return false;
    const Projective m = camera * comp.worldMatrix(layer, time);
    out = m.inverse().apply(compPoint);
    return m.inFront(out); // a point seen there, not its mirror behind the camera
}
} // namespace

uint64_t hitTestInside(const Composition& comp, double time, Vec2 compPoint, uint64_t compoundId, bool throughCamera) {
    for (const auto& layer : comp.layers) {
        if (layer.parent != compoundId || !comp.visibleAt(layer, time) || layer.locked || layer.type == LayerType::Camera ||
            layer.type == LayerType::Background) // a click on the empty frame should not pick the background
            continue;
        Vec2 mn, mx, p;
        if (!localBounds(layer, time, mn, mx, &comp) || !toLocal(comp, layer, time, compPoint, throughCamera, p)) continue;
        if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y) return layer.id;
    }
    return 0;
}

uint64_t hitTest(const Composition& comp, double time, Vec2 compPoint, bool throughCamera, bool includeCameras, const Lens* lens) {
    const std::vector<const Layer*> order = depthOrder(comp, time, !lens, throughCamera, lens);
    for (auto it = order.rbegin(); it != order.rend(); ++it) { // the top-most first, as drawn
        const Layer& layer = **it;
        if (layer.locked || (layer.type == LayerType::Camera && !includeCameras) || layer.type == LayerType::Background) continue;
        Vec2 mn, mx, p;
        if (!localBounds(layer, time, mn, mx, &comp) || !toLocal(comp, layer, time, compPoint, throughCamera, p, lens)) continue;
        if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y) return layer.id;
    }
    return 0;
}

} // namespace bemotion::render
