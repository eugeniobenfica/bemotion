#include "render/SceneRenderer.h"

#include "core/Expressions.h"
#include "core/Geometry.h"
#include "render/PluginCanvas.h"
#include "render/Text.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkColorMatrix.h"
#include "include/effects/SkGradientShader.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/effects/SkTrimPathEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// --- shapes ------------------------------------------------------------------------

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

void drawShape(SkCanvas* canvas, const Layer& layer, double t, float opacity) {
    const ShapeProps& shape = layer.shape;
    const SkPath path = shapePath(shape, t);
    if (path.isEmpty()) return;
    const bool closed = shape.kind != ShapeKind::Line;
    if (shape.fillEnabled && closed) {
        SkPaint fill(color4(shape.fillColor.valueAt(t), opacity));
        fill.setAntiAlias(true);
        if (fill.getAlphaf() > 0.0f) canvas->drawPath(path, fill);
    }
    const float width = shape.strokeWidth.valueAt(t);
    if (shape.strokeEnabled && width > 0.0f) {
        SkPaint stroke(color4(shape.strokeColor.valueAt(t), opacity));
        if (stroke.getAlphaf() <= 0.0f) return;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(width);
        stroke.setStrokeJoin(SkPaint::kMiter_Join);
        stroke.setStrokeCap(closed ? SkPaint::kButt_Cap : SkPaint::kRound_Cap);
        float a = std::clamp(shape.trimStart.valueAt(t) * 0.01f, 0.0f, 1.0f), b = std::clamp(shape.trimEnd.valueAt(t) * 0.01f, 0.0f, 1.0f);
        if (a > b) std::swap(a, b);
        if (b - a <= 1e-4f) return;
        if (a > 1e-4f || b < 1.0f - 1e-4f) {
            stroke.setPathEffect(SkTrimPathEffect::Make(a, b));
            stroke.setStrokeCap(SkPaint::kButt_Cap);
        }
        canvas->drawPath(path, stroke);
    }
}

// --- plugin objects ------------------------------------------------------------------

PluginDrawer& pluginDrawer() {
    static PluginDrawer drawer;
    return drawer;
}

void drawPlugin(SkCanvas* canvas, const Layer& layer, double t, float opacity) {
    PluginCanvas pc(canvas, opacity);
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
    const Vec2 half = layer.media.size * 0.5f;
    const SkRect dst = SkRect::MakeLTRB(-half.x, -half.y, half.x, half.y);
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
    canvas->drawImageRect(image, dst, SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear), &p);
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

// The layer's own content in its local space (the canvas carries its transform).
void drawContent(SkCanvas* canvas, const Layer& layer, double t, float opacity, const Composition* comp, const Projective& toOutput,
                 float zoom) {
    switch (layer.type) {
    case LayerType::Compound:
        if (!comp) return;
        // Children, bottom first, in the compound's space; its opacity multiplies theirs.
        for (auto it = comp->layers.rbegin(); it != comp->layers.rend(); ++it) {
            if (it->parent != layer.id || !it->activeAt(t) || it->type == LayerType::Camera) continue;
            const float own = std::clamp(it->transform.opacity.valueAt(t) * 0.01f, 0.0f, 1.0f);
            drawLayer(canvas, *it, t, toOutput * it->matrixAt(t), zoom, comp, own * opacity);
        }
        return;
    case LayerType::Media: drawMedia(canvas, comp, layer, t, opacity); return;
    case LayerType::Text: drawText(canvas, layer.text, t, opacity, {layer.inPoint, layer.outPoint}); return;
    case LayerType::Plugin: drawPlugin(canvas, layer, t, opacity); return;
    case LayerType::Background: drawBackground(canvas, comp, layer, t, opacity); return;
    case LayerType::Camera: return;
    default: drawShape(canvas, layer, t, opacity); return;
    }
}

} // namespace

void setPluginDrawer(PluginDrawer drawer) { pluginDrawer() = std::move(drawer); }
void setMediaProvider(MediaProvider provider) { mediaProvider() = std::move(provider); }

bool hasActiveEffects(const Layer& layer) {
    return std::any_of(layer.effects.begin(), layer.effects.end(), [](const Effect& e) { return e.enabled; });
}

void drawLayer(SkCanvas* canvas, const Layer& layer, double time, const Projective& layerToOutput, float zoom,
               const Composition* comp, float opacityOverride) {
    const float opacity = opacityOverride >= 0.0f ? std::min(opacityOverride, 1.0f)
                                                  : std::clamp(layer.transform.opacity.valueAt(time) * 0.01f, 0.0f, 1.0f);
    if (opacity <= 0.0f || layer.type == LayerType::Camera) return;
    // Transforms are absolute (`layerToOutput` is the whole chain), so nested compounds do not stack them twice.
    if (!hasActiveEffects(layer)) {
        canvas->save();
        canvas->setMatrix(toSk(layerToOutput));
        if (clipToFront(canvas, layerToOutput)) drawContent(canvas, layer, time, opacity, comp, layerToOutput, zoom);
        canvas->restore();
        return;
    }

    // With effects: draw into a layer filtered in output space (no transform on the filter).
    // `layerToOutput` is for `time`; other moments (motion blur) rebuild it from the parent.
    const Projective parent = layerToOutput * layer.matrixAt(time).inverse();
    const std::vector<double> times = momentsOf(layer, time, comp ? comp->frameDuration() : 1.0 / 30.0);
    // The offscreen layer only needs to hold what the layer draws (in output pixels): image
    // filters on the CPU cost per pixel of the layer.
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    bool bounded = true;
    for (double ti : times) {
        Vec2 lo, hi;
        if (!localBounds(layer, ti, lo, hi, comp)) {
            bounded = false;
            break;
        }
        const Projective m = parent * layer.matrixAt(ti);
        for (Vec2 corner : {lo, Vec2{hi.x, lo.y}, hi, Vec2{lo.x, hi.y}}) {
            if (!m.inFront(corner)) bounded = false; // reaches behind the camera: no usable bounds
            const Vec2 q = m.apply(corner);
            x0 = std::min(x0, q.x), y0 = std::min(y0, q.y), x1 = std::max(x1, q.x), y1 = std::max(y1, q.y);
        }
    }
    const SkRect bounds = SkRect::MakeLTRB(x0 - 4, y0 - 4, x1 + 4, y1 + 4); // antialiasing and strokes past the bounds
    const SkRect* hint = bounded && !bounds.isEmpty() ? &bounds : nullptr;
    canvas->save();
    canvas->resetMatrix();
    SkPaint filtered;
    filtered.setImageFilter(effectChain(layer, time, zoom));
    filtered.setAlphaf(opacity);
    canvas->saveLayer(hint, &filtered);
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
        if (clipToFront(canvas, m)) drawContent(canvas, layer, ti, 1.0f, comp, m, zoom);
        canvas->restore();
        if (average) canvas->restore();
    }
    canvas->restore();
    canvas->restore();
}

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
    const ExpressionScope expressions(comp); // what expressions read while drawing
    if (background && !lens) {
        const Color bg = comp.background;
        canvas->drawRect(frame, SkPaint(SkColor4f{bg.r, bg.g, bg.b, 1.0f}));
    }
    canvas->save();
    if (!lens) canvas->clipRect(frame, true); // layers are clipped to the composition frame (not in the 3D view)
    const Affine screen = Affine::translate(view.origin) * Affine::scale(view.zoom);
    const Affine flat = throughCamera && !lens ? comp.cameraAt(time) : Affine{};
    for (const Layer* layer : depthOrder(comp, time, false, throughCamera, lens)) {
        // Backgrounds stay on the frame whatever the camera does. 3D layers are seen in
        // perspective through the camera (the free view shows them from the front; the 3D
        // view shows every layer in space, from its own eye).
        Projective camera;
        if (lens ? !comp.viewThrough(*lens, *layer, time, camera, true, true) : !comp.viewFor(*layer, time, camera, throughCamera))
            continue; // behind the camera
        float zoom = view.zoom;
        if (layer->threeD || lens) { // effects shrink with distance (as the 2D camera's own zoom leaves them)
            const float s = camera.localScale(layer->transform.position.valueAt(time));
            zoom *= s / std::max(1e-6f, std::sqrt(std::fabs(flat.determinant())));
        }
        drawLayer(canvas, *layer, time, screen * camera * layer->matrixAt(time), zoom, &comp);
    }
    canvas->restore();
}

bool localBounds(const Layer& layer, double time, Vec2& outMin, Vec2& outMax, const Composition* comp) {
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
        outMin = layer.media.size * -0.5f;
        outMax = layer.media.size * 0.5f;
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
    const Vec2 size = s.size.valueAt(time);
    float pad = s.strokeEnabled ? s.strokeWidth.valueAt(time) * 0.5f : 0.0f;
    if (s.kind == ShapeKind::Line) pad = std::max(pad, 6.0f);
    outMin = {-std::fabs(size.x) * 0.5f - pad, -std::fabs(size.y) * 0.5f - pad};
    outMax = {std::fabs(size.x) * 0.5f + pad, std::fabs(size.y) * 0.5f + pad};
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
