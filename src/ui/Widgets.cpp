#include "ui/Widgets.h"

#include "app/PluginHost.h"
#include "core/Math.h"
#include "render/LogoMark.h"
#include "ui/AppSignals.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QDrag>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace bemotion::ui {

Icon iconForPlugin(const std::string& name) {
    if (name == "bars" || name == "chart") return Icon::BarChart;
    if (name == "line_chart") return Icon::LineChart;
    if (name == "pie" || name == "donut") return Icon::PieChart;
    if (name == "table") return Icon::Table;
    if (name == "cube" || name == "model") return Icon::Cube;
    if (name == "mic" || name == "microphone") return Icon::Microphone;
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

Icon iconForMedia(MediaKind kind) {
    switch (kind) {
    case MediaKind::Image: return Icon::Image;
    case MediaKind::Audio: return Icon::Audio;
    default: return Icon::Film;
    }
}

Icon iconForLayer(const Layer& layer) {
    if (layer.type == LayerType::Camera) return Icon::Camera;
    if (layer.type == LayerType::Background) return Icon::Background;
    if (layer.type == LayerType::Compound) return Icon::Compound;
    if (layer.type == LayerType::Media) return layer.media.hasVideo ? Icon::Film : Icon::Audio;
    if (layer.type == LayerType::Plugin) {
        const PluginObjectType* type = PluginHost::get().findType(layer.plugin.objectType);
        return type ? iconForPlugin(type->icon) : Icon::Plugin;
    }
    return layer.type == LayerType::Text ? Icon::Text : iconForShape(layer.shape.kind);
}

void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color) {
    // The exact mark, rasterized at the device size (and cached) from the shared geometry.
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const int px = std::max(4, static_cast<int>(std::ceil(radius * 2.0f * dpr)) + 2);
    static QHash<quint64, QImage> cache;
    const quint64 key = (static_cast<quint64>(px) << 32) | color;
    auto it = cache.find(key);
    if (it == cache.end()) {
        QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
        const float r = (px - 2) * 0.5f;
        for (int y = 0; y < px; ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < px; ++x) {
                const Vec2 q{(x + 0.5f - px * 0.5f) / r, (y + 0.5f - px * 0.5f) / r};
                const float cover = std::clamp(0.5f - logo::distance(q) * r, 0.0f, 1.0f) * alpha(color) / 255.0f;
                line[x] = qPremultiply(qRgba(red(color), green(color), blue(color), static_cast<int>(cover * 255.0f + 0.5f)));
            }
        }
        img.setDevicePixelRatio(dpr);
        if (cache.size() > 64) cache.clear();
        it = cache.insert(key, img);
    }
    const qreal half = px / dpr * 0.5;
    p.drawImage(QRectF(center.x - half, center.y - half, half * 2, half * 2), *it);
}

namespace {

void regularShape(Draw& dl, Pt c, float r, int points, float innerRatio, Rgba col, float th) {
    const int n = innerRatio > 0.0f ? points * 2 : points;
    Pt pts[32];
    for (int i = 0; i < n; ++i) {
        const float a = -kPi * 0.5f + 2.0f * kPi * i / n;
        const float rr = (innerRatio > 0.0f && (i % 2)) ? r * innerRatio : r;
        pts[i] = Pt(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    dl.polyline(pts, n, col, true, th);
}

} // namespace

void drawIcon(Draw& dl, Icon icon, Pt c, float size, Rgba col) {
    const float h = size * 0.5f;
    const float th = std::max(1.25f, size * 0.09f);
    switch (icon) {
    case Icon::Text: {
        dl.line(Pt(c.x - h * 0.75f, c.y - h * 0.7f), Pt(c.x + h * 0.75f, c.y - h * 0.7f), col, th * 1.2f);
        dl.line(Pt(c.x, c.y - h * 0.7f), Pt(c.x, c.y + h * 0.75f), col, th * 1.2f);
        break;
    }
    case Icon::Rectangle:
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.6f), Pt(c.x + h * 0.8f, c.y + h * 0.6f), col, size * 0.12f, th);
        break;
    case Icon::Ellipse: dl.circle(c, h * 0.78f, col, th); break;
    case Icon::Polygon: regularShape(dl, c, h * 0.82f, 6, 0.0f, col, th); break;
    case Icon::Star: regularShape(dl, c, h * 0.88f, 5, 0.45f, col, th); break;
    case Icon::Line: dl.line(Pt(c.x - h * 0.75f, c.y + h * 0.55f), Pt(c.x + h * 0.75f, c.y - h * 0.55f), col, th); break;
    case Icon::Eye:
    case Icon::EyeOff: {
        const float w = h * 0.95f;
        dl.ellipse(c, Pt(w, h * 0.55f), col, th);
        dl.circleFilled(c, h * 0.26f, col);
        if (icon == Icon::EyeOff) dl.line(Pt(c.x - w * 0.8f, c.y + w * 0.8f), Pt(c.x + w * 0.8f, c.y - w * 0.8f), col, th);
        break;
    }
    case Icon::Lock:
    case Icon::Unlock: {
        const Pt a(c.x - h * 0.6f, c.y - h * 0.05f), b(c.x + h * 0.6f, c.y + h * 0.75f);
        dl.rectFilled(a, b, col, 2.0f);
        const float sx = icon == Icon::Unlock ? h * 0.45f : 0.0f;
        dl.arc(Pt(c.x + sx, c.y - h * 0.3f), h * 0.38f, kPi, kPi * 2.0f, col, th);
        dl.line(Pt(c.x - h * 0.38f + sx, c.y - h * 0.3f), Pt(c.x - h * 0.38f + sx, c.y), col, th);
        dl.line(Pt(c.x + h * 0.38f + sx, c.y - h * 0.3f), Pt(c.x + h * 0.38f + sx, c.y), col, th);
        break;
    }
    case Icon::Play:
        dl.triangleFilled(Pt(c.x - h * 0.5f, c.y - h * 0.65f), Pt(c.x + h * 0.7f, c.y),
                              Pt(c.x - h * 0.5f, c.y + h * 0.65f), col);
        break;
    case Icon::Pause:
        dl.rectFilled(Pt(c.x - h * 0.55f, c.y - h * 0.6f), Pt(c.x - h * 0.15f, c.y + h * 0.6f), col, 1.5f);
        dl.rectFilled(Pt(c.x + h * 0.15f, c.y - h * 0.6f), Pt(c.x + h * 0.55f, c.y + h * 0.6f), col, 1.5f);
        break;
    case Icon::StepBack:
    case Icon::ToStart: {
        const float bar = icon == Icon::ToStart ? h * 0.6f : h * 0.35f;
        dl.rectFilled(Pt(c.x - bar - th, c.y - h * 0.55f), Pt(c.x - bar, c.y + h * 0.55f), col);
        dl.triangleFilled(Pt(c.x + h * 0.5f, c.y - h * 0.55f), Pt(c.x + h * 0.5f, c.y + h * 0.55f),
                              Pt(c.x - bar + th * 0.3f, c.y), col);
        break;
    }
    case Icon::StepForward:
    case Icon::ToEnd: {
        const float bar = icon == Icon::ToEnd ? h * 0.6f : h * 0.35f;
        dl.rectFilled(Pt(c.x + bar, c.y - h * 0.55f), Pt(c.x + bar + th, c.y + h * 0.55f), col);
        dl.triangleFilled(Pt(c.x - h * 0.5f, c.y - h * 0.55f), Pt(c.x + bar - th * 0.3f, c.y),
                              Pt(c.x - h * 0.5f, c.y + h * 0.55f), col);
        break;
    }
    case Icon::Loop: {
        dl.arc(c, h * 0.62f, -kPi * 0.15f, kPi * 1.45f, col, th);
        const Pt tip(c.x + std::cos(-kPi * 0.15f) * h * 0.62f, c.y + std::sin(-kPi * 0.15f) * h * 0.62f);
        dl.triangleFilled(Pt(tip.x - h * 0.3f, tip.y - h * 0.1f), Pt(tip.x + h * 0.25f, tip.y - h * 0.25f),
                              Pt(tip.x + h * 0.05f, tip.y + h * 0.3f), col);
        break;
    }
    case Icon::Plus:
        dl.line(Pt(c.x - h * 0.6f, c.y), Pt(c.x + h * 0.6f, c.y), col, th * 1.2f);
        dl.line(Pt(c.x, c.y - h * 0.6f), Pt(c.x, c.y + h * 0.6f), col, th * 1.2f);
        break;
    case Icon::Folder: {
        const Pt a(c.x - h * 0.8f, c.y - h * 0.45f), b(c.x + h * 0.8f, c.y + h * 0.6f);
        dl.rect(a, b, col, 2.0f, th);
        dl.line(Pt(a.x + 1, a.y - h * 0.18f), Pt(c.x - h * 0.1f, a.y - h * 0.18f), col, th);
        break;
    }
    case Icon::ChevronRight:
        dl.triangleFilled(Pt(c.x - h * 0.25f, c.y - h * 0.4f), Pt(c.x + h * 0.3f, c.y),
                              Pt(c.x - h * 0.25f, c.y + h * 0.4f), col);
        break;
    case Icon::ChevronDown:
        dl.triangleFilled(Pt(c.x - h * 0.4f, c.y - h * 0.2f), Pt(c.x + h * 0.4f, c.y - h * 0.2f),
                              Pt(c.x, c.y + h * 0.32f), col);
        break;
    case Icon::ChevronLeft:
        dl.triangleFilled(Pt(c.x + h * 0.25f, c.y - h * 0.4f), Pt(c.x - h * 0.3f, c.y),
                              Pt(c.x + h * 0.25f, c.y + h * 0.4f), col);
        break;
    case Icon::Minimize:
        dl.line(Pt(c.x - h * 0.55f, c.y + h * 0.1f), Pt(c.x + h * 0.55f, c.y + h * 0.1f), col, th * 1.3f);
        break;
    case Icon::Fullscreen: {
        const float a = h * 0.78f, b = h * 0.38f;
        dl.line({c.x - a, c.y - b}, {c.x - a, c.y - a}, col, th);
        dl.line({c.x - a, c.y - a}, {c.x - b, c.y - a}, col, th);
        dl.line({c.x + a, c.y - b}, {c.x + a, c.y - a}, col, th);
        dl.line({c.x + a, c.y - a}, {c.x + b, c.y - a}, col, th);
        dl.line({c.x - a, c.y + b}, {c.x - a, c.y + a}, col, th);
        dl.line({c.x - a, c.y + a}, {c.x - b, c.y + a}, col, th);
        dl.line({c.x + a, c.y + b}, {c.x + a, c.y + a}, col, th);
        dl.line({c.x + a, c.y + a}, {c.x + b, c.y + a}, col, th);
        break;
    }
    case Icon::Copy:
        dl.rect(Pt(c.x - h * 0.7f, c.y - h * 0.7f), Pt(c.x + h * 0.25f, c.y + h * 0.35f), col, 2.0f, th);
        dl.rect(Pt(c.x - h * 0.25f, c.y - h * 0.25f), Pt(c.x + h * 0.7f, c.y + h * 0.8f), col, 2.0f, th);
        break;
    case Icon::Paste:
        dl.rect(Pt(c.x - h * 0.6f, c.y - h * 0.6f), Pt(c.x + h * 0.6f, c.y + h * 0.8f), col, 2.0f, th);
        dl.rectFilled(Pt(c.x - h * 0.3f, c.y - h * 0.8f), Pt(c.x + h * 0.3f, c.y - h * 0.45f), col, 1.5f);
        dl.line(Pt(c.x - h * 0.3f, c.y), Pt(c.x + h * 0.3f, c.y), col, th);
        dl.line(Pt(c.x - h * 0.3f, c.y + h * 0.35f), Pt(c.x + h * 0.15f, c.y + h * 0.35f), col, th);
        break;
    case Icon::Transition: { // two overlapping clips
        dl.rect(Pt(c.x - h * 0.85f, c.y - h * 0.55f), Pt(c.x + h * 0.15f, c.y + h * 0.55f), col, 2.0f, th);
        dl.rectFilled(Pt(c.x - h * 0.15f, c.y - h * 0.55f), Pt(c.x + h * 0.85f, c.y + h * 0.55f), theme::withAlpha(col, 110), 2.0f);
        dl.rect(Pt(c.x - h * 0.15f, c.y - h * 0.55f), Pt(c.x + h * 0.85f, c.y + h * 0.55f), col, 2.0f, th);
        break;
    }
    case Icon::Background: { // a frame with a sun and hills: the picture behind everything
        const Pt a(c.x - h * 0.85f, c.y - h * 0.65f), b(c.x + h * 0.85f, c.y + h * 0.65f);
        dl.rect(a, b, col, 2.0f, th);
        dl.circleFilled(Pt(c.x + h * 0.4f, c.y - h * 0.25f), h * 0.14f, col);
        const Pt hills[4] = {Pt(a.x + h * 0.15f, b.y - h * 0.12f), Pt(c.x - h * 0.25f, c.y - h * 0.05f), Pt(c.x + h * 0.15f, b.y - h * 0.35f),
                             Pt(b.x - h * 0.12f, b.y - h * 0.12f)};
        dl.polyline(hills, 4, col, false, th);
        break;
    }
    case Icon::Coffee: { // a cup with a handle and steam
        const Pt a(c.x - h * 0.65f, c.y - h * 0.15f), b(c.x + h * 0.35f, c.y + h * 0.7f);
        dl.rect(a, b, col, 3.0f, th);
        dl.circle(Pt(b.x + h * 0.12f, c.y + h * 0.25f), h * 0.2f, col, th);
        dl.line(Pt(c.x - h * 0.35f, c.y - h * 0.75f), Pt(c.x - h * 0.35f, c.y - h * 0.4f), col, th);
        dl.line(Pt(c.x - h * 0.05f, c.y - h * 0.85f), Pt(c.x - h * 0.05f, c.y - h * 0.4f), col, th);
        break;
    }
    case Icon::Microphone: { // a capsule on a stand
        dl.rectFilled(Pt(c.x - h * 0.25f, c.y - h * 0.85f), Pt(c.x + h * 0.25f, c.y + h * 0.2f), col, h * 0.25f);
        const Pt arc[5] = {Pt(c.x - h * 0.5f, c.y - h * 0.1f), Pt(c.x - h * 0.45f, c.y + h * 0.3f), Pt(c.x, c.y + h * 0.5f),
                           Pt(c.x + h * 0.45f, c.y + h * 0.3f), Pt(c.x + h * 0.5f, c.y - h * 0.1f)};
        dl.polyline(arc, 5, col, false, th);
        dl.line(Pt(c.x, c.y + h * 0.5f), Pt(c.x, c.y + h * 0.85f), col, th);
        dl.line(Pt(c.x - h * 0.3f, c.y + h * 0.85f), Pt(c.x + h * 0.3f, c.y + h * 0.85f), col, th);
        break;
    }
    case Icon::Cube: { // an outlined cube in a three-quarter view
        const float s = h * 0.62f, d = h * 0.32f;
        const Pt f0(c.x - s, c.y - s + d), f1(c.x + s - d, c.y - s + d), f2(c.x + s - d, c.y + s), f3(c.x - s, c.y + s);
        const Pt b0(c.x - s + d, c.y - s), b1(c.x + s, c.y - s), b2(c.x + s, c.y + s - d);
        dl.line(f0, f1, col, th);
        dl.line(f1, f2, col, th);
        dl.line(f2, f3, col, th);
        dl.line(f3, f0, col, th);
        dl.line(f0, b0, col, th);
        dl.line(b0, b1, col, th);
        dl.line(f1, b1, col, th);
        dl.line(b1, b2, col, th);
        dl.line(f2, b2, col, th);
        break;
    }
    case Icon::Table: {
        const Pt a(c.x - h * 0.8f, c.y - h * 0.65f), b(c.x + h * 0.8f, c.y + h * 0.65f);
        dl.rect(a, b, col, 2.0f, th);
        dl.line(Pt(a.x, c.y - h * 0.2f), Pt(b.x, c.y - h * 0.2f), col, th);
        dl.line(Pt(a.x, c.y + h * 0.22f), Pt(b.x, c.y + h * 0.22f), col, th);
        dl.line(Pt(c.x - h * 0.2f, a.y), Pt(c.x - h * 0.2f, b.y), col, th);
        break;
    }
    case Icon::ChevronUp:
        dl.triangleFilled(Pt(c.x - h * 0.4f, c.y + h * 0.2f), Pt(c.x, c.y - h * 0.32f),
                              Pt(c.x + h * 0.4f, c.y + h * 0.2f), col);
        break;
    case Icon::Diamond: drawDiamond(dl, c, h * 0.6f, col, col); break;
    case Icon::Trash: {
        dl.line(Pt(c.x - h * 0.7f, c.y - h * 0.5f), Pt(c.x + h * 0.7f, c.y - h * 0.5f), col, th);
        dl.rect(Pt(c.x - h * 0.5f, c.y - h * 0.5f), Pt(c.x + h * 0.5f, c.y + h * 0.7f), col, 2.0f, th);
        dl.line(Pt(c.x - h * 0.2f, c.y - h * 0.75f), Pt(c.x + h * 0.2f, c.y - h * 0.75f), col, th);
        break;
    }
    case Icon::Logo:
        drawLogoMark(dl.painter(), c, h * 0.95f, col);
        break;
    case Icon::Pointer: {
        const Pt pts[7] = {Pt(c.x - h * 0.45f, c.y - h * 0.8f), Pt(c.x + h * 0.55f, c.y + h * 0.15f),
                               Pt(c.x + h * 0.05f, c.y + h * 0.2f), Pt(c.x + h * 0.35f, c.y + h * 0.8f),
                               Pt(c.x + h * 0.12f, c.y + h * 0.9f), Pt(c.x - h * 0.15f, c.y + h * 0.3f),
                               Pt(c.x - h * 0.45f, c.y + h * 0.6f)};
        dl.polyFilled(pts, 7, col);
        break;
    }
    case Icon::Razor: {
        // Scissors: two rings and crossing blades.
        dl.circle(Pt(c.x - h * 0.42f, c.y + h * 0.5f), h * 0.26f, col, th);
        dl.circle(Pt(c.x + h * 0.42f, c.y + h * 0.5f), h * 0.26f, col, th);
        dl.line(Pt(c.x - h * 0.25f, c.y + h * 0.3f), Pt(c.x + h * 0.45f, c.y - h * 0.85f), col, th);
        dl.line(Pt(c.x + h * 0.25f, c.y + h * 0.3f), Pt(c.x - h * 0.45f, c.y - h * 0.85f), col, th);
        break;
    }
    case Icon::SplitPlayhead: {
        dl.line(Pt(c.x, c.y - h * 0.9f), Pt(c.x, c.y + h * 0.9f), col, th);
        dl.rectFilled(Pt(c.x - h * 0.9f, c.y - h * 0.3f), Pt(c.x - h * 0.2f, c.y + h * 0.3f), col, 1.5f);
        dl.rectFilled(Pt(c.x + h * 0.2f, c.y - h * 0.3f), Pt(c.x + h * 0.9f, c.y + h * 0.3f), col, 1.5f);
        break;
    }
    case Icon::Export: {
        dl.line(Pt(c.x, c.y + h * 0.3f), Pt(c.x, c.y - h * 0.8f), col, th);
        dl.line(Pt(c.x - h * 0.4f, c.y - h * 0.4f), Pt(c.x, c.y - h * 0.8f), col, th);
        dl.line(Pt(c.x + h * 0.4f, c.y - h * 0.4f), Pt(c.x, c.y - h * 0.8f), col, th);
        const Pt tray[4] = {Pt(c.x - h * 0.8f, c.y), Pt(c.x - h * 0.8f, c.y + h * 0.8f),
                                Pt(c.x + h * 0.8f, c.y + h * 0.8f), Pt(c.x + h * 0.8f, c.y)};
        dl.polyline(tray, 4, col, false, th);
        break;
    }
    case Icon::Reset: {
        const float r = h * 0.62f;
        dl.arc(c, r, kPi * 0.15f, kPi * 1.75f, col, th);
        const Pt tip(c.x + std::cos(kPi * 1.75f) * r, c.y + std::sin(kPi * 1.75f) * r);
        dl.triangleFilled(Pt(tip.x + h * 0.38f, tip.y - h * 0.05f), Pt(tip.x - h * 0.08f, tip.y - h * 0.38f),
                              Pt(tip.x - h * 0.02f, tip.y + h * 0.3f), col);
        break;
    }
    case Icon::Magnet: {
        const float r = h * 0.5f;
        dl.arc(Pt(c.x, c.y + h * 0.05f), r, 0.0f, kPi, col, th * 1.8f);
        dl.line(Pt(c.x - r, c.y + h * 0.05f), Pt(c.x - r, c.y - h * 0.75f), col, th * 1.8f);
        dl.line(Pt(c.x + r, c.y + h * 0.05f), Pt(c.x + r, c.y - h * 0.75f), col, th * 1.8f);
        dl.line(Pt(c.x - r - th, c.y - h * 0.42f), Pt(c.x - r + th, c.y - h * 0.42f), theme::kBgPanel, th);
        dl.line(Pt(c.x + r - th, c.y - h * 0.42f), Pt(c.x + r + th, c.y - h * 0.42f), theme::kBgPanel, th);
        break;
    }
    case Icon::Gear: {
        for (int i = 0; i < 8; ++i) {
            const float a = kPi * 2.0f * i / 8.0f;
            dl.line(Pt(c.x + std::cos(a) * h * 0.45f, c.y + std::sin(a) * h * 0.45f),
                        Pt(c.x + std::cos(a) * h * 0.85f, c.y + std::sin(a) * h * 0.85f), col, th * 1.6f);
        }
        dl.circle(c, h * 0.52f, col, th * 1.4f);
        dl.circleFilled(c, h * 0.18f, col);
        break;
    }
    case Icon::Camera: {
        dl.rect(Pt(c.x - h * 0.85f, c.y - h * 0.45f), Pt(c.x + h * 0.35f, c.y + h * 0.5f), col, 2.0f, th);
        dl.triangleFilled(Pt(c.x + h * 0.4f, c.y), Pt(c.x + h * 0.9f, c.y - h * 0.4f),
                              Pt(c.x + h * 0.9f, c.y + h * 0.45f), col);
        dl.circle(Pt(c.x - h * 0.25f, c.y + h * 0.02f), h * 0.2f, col, th);
        break;
    }
    case Icon::Plugin: {
        dl.rect(Pt(c.x - h * 0.6f, c.y - h * 0.35f), Pt(c.x + h * 0.6f, c.y + h * 0.75f), col, 3.0f, th);
        dl.line(Pt(c.x - h * 0.28f, c.y - h * 0.35f), Pt(c.x - h * 0.28f, c.y - h * 0.85f), col, th * 1.4f);
        dl.line(Pt(c.x + h * 0.28f, c.y - h * 0.35f), Pt(c.x + h * 0.28f, c.y - h * 0.85f), col, th * 1.4f);
        break;
    }
    case Icon::BarChart: {
        const float base = c.y + h * 0.75f;
        dl.line(Pt(c.x - h * 0.85f, base), Pt(c.x + h * 0.85f, base), col, th);
        const float xs[3] = {-0.55f, 0.0f, 0.55f}, hs[3] = {0.7f, 1.35f, 1.0f};
        for (int i = 0; i < 3; ++i)
            dl.rectFilled(Pt(c.x + h * (xs[i] - 0.18f), base - h * hs[i]), Pt(c.x + h * (xs[i] + 0.18f), base - th),
                              col, 1.5f);
        break;
    }
    case Icon::LineChart: {
        const Pt pts[4] = {Pt(c.x - h * 0.85f, c.y + h * 0.45f), Pt(c.x - h * 0.25f, c.y - h * 0.15f),
                               Pt(c.x + h * 0.2f, c.y + h * 0.2f), Pt(c.x + h * 0.85f, c.y - h * 0.6f)};
        dl.polyline(pts, 4, col, false, th * 1.2f);
        for (const Pt& p : pts) dl.circleFilled(p, th * 1.3f, col);
        dl.line(Pt(c.x - h * 0.85f, c.y + h * 0.8f), Pt(c.x + h * 0.85f, c.y + h * 0.8f), col, th);
        break;
    }
    case Icon::PieChart: {
        dl.circle(c, h * 0.8f, col, th);
        dl.pie(c, h * 0.8f, -kPi * 0.5f, kPi * 0.15f, col);
        break;
    }
    case Icon::FxBlur:
        for (int i = 0; i < 3; ++i)
            dl.circle(c, h * (0.35f + 0.25f * i), theme::withAlpha(col, 255 - i * 80), th);
        break;
    case Icon::FxColor:
        dl.circle(c, h * 0.8f, col, th);
        dl.pie(c, h * 0.8f, -kPi * 0.5f, kPi * 0.5f, col);
        break;
    case Icon::FxChannel:
        for (int i = 0; i < 3; ++i) {
            const float a = -kPi * 0.5f + i * kPi * 2.0f / 3.0f;
            dl.circle(Pt(c.x + std::cos(a) * h * 0.35f, c.y + std::sin(a) * h * 0.35f), h * 0.5f, col, th);
        }
        break;
    case Icon::FxDistort: {
        Pt pts[17];
        for (int i = 0; i <= 16; ++i) {
            const float x = -0.85f + 1.7f * i / 16.0f;
            pts[i] = Pt(c.x + x * h, c.y + std::sin(x * kPi * 1.6f) * h * 0.45f);
        }
        dl.polyline(pts, 17, col, false, th * 1.2f);
        break;
    }
    case Icon::FxGenerate: {
        const Pt star[8] = {Pt(c.x, c.y - h * 0.85f), Pt(c.x + h * 0.2f, c.y - h * 0.2f), Pt(c.x + h * 0.85f, c.y),
                                Pt(c.x + h * 0.2f, c.y + h * 0.2f), Pt(c.x, c.y + h * 0.85f), Pt(c.x - h * 0.2f, c.y + h * 0.2f),
                                Pt(c.x - h * 0.85f, c.y), Pt(c.x - h * 0.2f, c.y - h * 0.2f)};
        dl.polyFilled(star, 8, col);
        break;
    }
    case Icon::FxPerspective:
        dl.rectFilled(Pt(c.x - h * 0.4f, c.y - h * 0.4f), Pt(c.x + h * 0.8f, c.y + h * 0.8f),
                          theme::withAlpha(col, 90), 2.0f);
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.8f), Pt(c.x + h * 0.4f, c.y + h * 0.4f), col, 2.0f, th);
        break;
    case Icon::FxStylize:
        dl.circleFilled(c, h * 0.35f, col);
        for (int i = 0; i < 8; ++i) {
            const float a = i * kPi * 0.25f;
            dl.line(Pt(c.x + std::cos(a) * h * 0.55f, c.y + std::sin(a) * h * 0.55f),
                        Pt(c.x + std::cos(a) * h * 0.85f, c.y + std::sin(a) * h * 0.85f), col, th);
        }
        break;
    case Icon::FxTime:
        dl.circle(c, h * 0.8f, col, th);
        dl.line(c, Pt(c.x, c.y - h * 0.5f), col, th);
        dl.line(c, Pt(c.x + h * 0.35f, c.y + h * 0.15f), col, th);
        break;
    case Icon::Palette:
        dl.circle(c, h * 0.8f, col, th);
        dl.circleFilled(Pt(c.x - h * 0.35f, c.y - h * 0.25f), h * 0.13f, col);
        dl.circleFilled(Pt(c.x + h * 0.1f, c.y - h * 0.45f), h * 0.13f, col);
        dl.circleFilled(Pt(c.x + h * 0.45f, c.y - h * 0.1f), h * 0.13f, col);
        dl.circleFilled(Pt(c.x - h * 0.05f, c.y + h * 0.4f), h * 0.18f, col);
        break;
    case Icon::Sliders:
        for (int i = 0; i < 3; ++i) {
            const float y = c.y + (i - 1) * h * 0.6f, k = i == 1 ? 0.3f : -0.3f;
            dl.line(Pt(c.x - h * 0.8f, y), Pt(c.x + h * 0.8f, y), col, th);
            dl.circleFilled(Pt(c.x + k * h, y), h * 0.18f, col);
        }
        break;
    case Icon::Graph: {
        dl.line(Pt(c.x - h * 0.85f, c.y + h * 0.8f), Pt(c.x + h * 0.85f, c.y + h * 0.8f), col, th);
        const Pt p0(c.x - h * 0.8f, c.y + h * 0.55f), p3(c.x + h * 0.8f, c.y - h * 0.65f);
        dl.bezier(p0, Pt(c.x + h * 0.1f, p0.y), Pt(c.x - h * 0.1f, p3.y), p3, col, th * 1.2f);
        break;
    }
    case Icon::Compound: // two stacked frames
        dl.rect(Pt(c.x - h * 0.85f, c.y - h * 0.35f), Pt(c.x + h * 0.35f, c.y + h * 0.85f), col, 2.5f, th);
        dl.line(Pt(c.x - h * 0.4f, c.y - h * 0.35f), Pt(c.x - h * 0.4f, c.y - h * 0.85f), col, th);
        dl.line(Pt(c.x - h * 0.4f, c.y - h * 0.85f), Pt(c.x + h * 0.85f, c.y - h * 0.85f), col, th);
        dl.line(Pt(c.x + h * 0.85f, c.y - h * 0.85f), Pt(c.x + h * 0.85f, c.y + h * 0.4f), col, th);
        dl.line(Pt(c.x + h * 0.85f, c.y + h * 0.4f), Pt(c.x + h * 0.35f, c.y + h * 0.4f), col, th);
        break;
    case Icon::Back:
        dl.line(Pt(c.x - h * 0.7f, c.y), Pt(c.x + h * 0.7f, c.y), col, th * 1.2f);
        dl.line(Pt(c.x - h * 0.7f, c.y), Pt(c.x - h * 0.2f, c.y - h * 0.5f), col, th * 1.2f);
        dl.line(Pt(c.x - h * 0.7f, c.y), Pt(c.x - h * 0.2f, c.y + h * 0.5f), col, th * 1.2f);
        break;
    case Icon::Film: { // a frame with sprocket holes
        const Pt a(c.x - h * 0.8f, c.y - h * 0.6f), b(c.x + h * 0.8f, c.y + h * 0.6f);
        dl.rect(a, b, col, 2.0f, th);
        dl.line(Pt(a.x + h * 0.35f, a.y), Pt(a.x + h * 0.35f, b.y), col, th);
        dl.line(Pt(b.x - h * 0.35f, a.y), Pt(b.x - h * 0.35f, b.y), col, th);
        for (int i = -1; i <= 1; ++i) {
            const float y = c.y + i * h * 0.36f;
            dl.rectFilled(Pt(a.x + h * 0.1f, y - h * 0.08f), Pt(a.x + h * 0.25f, y + h * 0.08f), col);
            dl.rectFilled(Pt(b.x - h * 0.25f, y - h * 0.08f), Pt(b.x - h * 0.1f, y + h * 0.08f), col);
        }
        break;
    }
    case Icon::Image: { // a frame with a mountain and a sun
        const Pt a(c.x - h * 0.8f, c.y - h * 0.6f), b(c.x + h * 0.8f, c.y + h * 0.6f);
        dl.rect(a, b, col, 2.0f, th);
        dl.circleFilled(Pt(c.x + h * 0.35f, c.y - h * 0.22f), h * 0.14f, col);
        dl.triangleFilled(Pt(a.x + h * 0.15f, b.y - h * 0.12f), Pt(c.x - h * 0.25f, c.y - h * 0.15f),
                              Pt(c.x + h * 0.3f, b.y - h * 0.12f), col);
        dl.triangleFilled(Pt(c.x, b.y - h * 0.12f), Pt(c.x + h * 0.35f, c.y + h * 0.1f),
                              Pt(b.x - h * 0.15f, b.y - h * 0.12f), col);
        break;
    }
    case Icon::Audio: { // a waveform
        const float bars[] = {0.25f, 0.55f, 0.85f, 0.45f, 0.7f, 0.3f};
        for (int i = 0; i < 6; ++i) {
            const float x = c.x - h * 0.75f + i * h * 0.3f;
            dl.line(Pt(x, c.y - h * bars[i]), Pt(x, c.y + h * bars[i]), col, th * 1.3f);
        }
        break;
    }
    case Icon::Import: { // an arrow into a tray
        dl.line(Pt(c.x, c.y - h * 0.8f), Pt(c.x, c.y + h * 0.2f), col, th * 1.2f);
        dl.line(Pt(c.x - h * 0.4f, c.y - h * 0.2f), Pt(c.x, c.y + h * 0.2f), col, th * 1.2f);
        dl.line(Pt(c.x + h * 0.4f, c.y - h * 0.2f), Pt(c.x, c.y + h * 0.2f), col, th * 1.2f);
        dl.line(Pt(c.x - h * 0.8f, c.y + h * 0.15f), Pt(c.x - h * 0.8f, c.y + h * 0.75f), col, th);
        dl.line(Pt(c.x - h * 0.8f, c.y + h * 0.75f), Pt(c.x + h * 0.8f, c.y + h * 0.75f), col, th);
        dl.line(Pt(c.x + h * 0.8f, c.y + h * 0.75f), Pt(c.x + h * 0.8f, c.y + h * 0.15f), col, th);
        break;
    }
    case Icon::Duplicate: { // two overlapping sheets
        dl.rect(Pt(c.x - h * 0.75f, c.y - h * 0.75f), Pt(c.x + h * 0.25f, c.y + h * 0.25f), col, 2.0f, th);
        dl.rectFilled(Pt(c.x - h * 0.25f, c.y - h * 0.25f), Pt(c.x + h * 0.75f, c.y + h * 0.75f), col, 2.0f);
        break;
    }
    case Icon::Ungroup: { // a frame with its corners breaking apart
        const float g = h * 0.3f;
        const Pt corners[4] = {Pt(c.x - h * 0.8f, c.y - h * 0.8f), Pt(c.x + h * 0.8f, c.y - h * 0.8f),
                                   Pt(c.x + h * 0.8f, c.y + h * 0.8f), Pt(c.x - h * 0.8f, c.y + h * 0.8f)};
        for (int i = 0; i < 4; ++i) {
            const float sx = (i == 1 || i == 2) ? -1.0f : 1.0f, sy = i >= 2 ? -1.0f : 1.0f;
            dl.line(corners[i], Pt(corners[i].x + sx * g * 1.6f, corners[i].y), col, th);
            dl.line(corners[i], Pt(corners[i].x, corners[i].y + sy * g * 1.6f), col, th);
        }
        dl.rectFilled(Pt(c.x - h * 0.22f, c.y - h * 0.22f), Pt(c.x + h * 0.22f, c.y + h * 0.22f), col, 1.5f);
        break;
    }
    case Icon::Ease: { // an S-curve between two keyframes
        dl.bezier(Pt(c.x - h * 0.75f, c.y + h * 0.6f), Pt(c.x + h * 0.1f, c.y + h * 0.6f),
                           Pt(c.x - h * 0.1f, c.y - h * 0.6f), Pt(c.x + h * 0.75f, c.y - h * 0.6f), col, th * 1.2f);
        drawDiamond(dl, Pt(c.x - h * 0.75f, c.y + h * 0.6f), h * 0.22f, col, 0);
        drawDiamond(dl, Pt(c.x + h * 0.75f, c.y - h * 0.6f), h * 0.22f, col, 0);
        break;
    }
    case Icon::LayerUp:
    case Icon::LayerDown: { // two stacked layers and an arrow
        const float dir = icon == Icon::LayerUp ? -1.0f : 1.0f;
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.15f), Pt(c.x + h * 0.2f, c.y + h * 0.75f), col, 2.0f, th);
        dl.rectFilled(Pt(c.x - h * 0.55f, c.y - h * 0.4f + dir * h * 0.25f),
                          Pt(c.x + h * 0.0f, c.y + h * 0.1f + dir * h * 0.25f), col, 1.5f);
        const float ax = c.x + h * 0.6f;
        dl.line(Pt(ax, c.y - h * 0.7f), Pt(ax, c.y + h * 0.7f), col, th);
        const float tip = c.y + dir * h * 0.75f;
        dl.triangleFilled(Pt(ax, tip), Pt(ax - h * 0.28f, tip - dir * h * 0.38f),
                              Pt(ax + h * 0.28f, tip - dir * h * 0.38f), col);
        break;
    }
    case Icon::ZoomIn:
    case Icon::ZoomOut: {
        const Pt lens(c.x - h * 0.15f, c.y - h * 0.15f);
        dl.circle(lens, h * 0.55f, col, th);
        dl.line(Pt(c.x + h * 0.25f, c.y + h * 0.25f), Pt(c.x + h * 0.8f, c.y + h * 0.8f), col, th * 1.3f);
        dl.line(Pt(lens.x - h * 0.28f, lens.y), Pt(lens.x + h * 0.28f, lens.y), col, th);
        if (icon == Icon::ZoomIn)
            dl.line(Pt(lens.x, lens.y - h * 0.28f), Pt(lens.x, lens.y + h * 0.28f), col, th);
        break;
    }
    }
}

void drawDiamond(Draw& dl, Pt c, float r, Rgba fill, Rgba outline) {
    const Pt pts[4] = {Pt(c.x, c.y - r), Pt(c.x + r, c.y), Pt(c.x, c.y + r), Pt(c.x - r, c.y)};
    if (alpha(fill)) dl.polyFilled(pts, 4, fill);
    if (alpha(outline)) dl.polyline(pts, 4, outline, true, 1.3f);
}


QIcon makeIcon(Icon icon, Rgba color, int size) {
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    Draw d(p);
    drawIcon(d, icon, Pt(size * 0.5f, size * 0.5f), size * 0.62f, color);
    p.end();
    return QIcon(pm);
}

namespace {

Rgba toneColor(Tone t) {
    switch (t) {
    case Tone::Text: return theme::kText;
    case Tone::TextDim: return theme::kTextDim;
    case Tone::TextFaint: return theme::kTextFaint;
    case Tone::Accent: return theme::kAccent;
    case Tone::Danger: return theme::kDanger;
    case Tone::Border: return theme::kBorder;
    }
    return theme::kText;
}

void applyTone(QWidget* w) {
    const QVariant v = w->property("bemotionTone");
    if (v.isValid()) w->setStyleSheet(QString("color: %1;").arg(theme::q(toneColor(static_cast<Tone>(v.toInt()))).name()));
}

} // namespace

void setTone(QWidget* widget, Tone tone) {
    static bool hooked = false;
    if (!hooked) { // one listener recolors every toned widget
        hooked = true;
        QObject::connect(&AppSignals::get(), &AppSignals::appearanceChanged, [] {
            for (QWidget* w : QApplication::allWidgets()) applyTone(w);
        });
    }
    widget->setProperty("bemotionTone", static_cast<int>(tone));
    applyTone(widget);
}

QIcon menuIcon(Icon icon, Rgba color) {
    // Menus draw icons at 16 px: the glyph fills the square (makeIcon leaves room for a button's padding).
    constexpr int size = 16;
    QPixmap pm(size * 3, size * 3);
    pm.setDevicePixelRatio(3.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    Draw d(p);
    drawIcon(d, icon, Pt(size * 0.5f, size * 0.5f), size * 0.9f, color);
    p.end();
    return QIcon(pm);
}

QWidget* lengthLimitRow(double seconds, std::function<void(double)> changed) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* on = new QCheckBox("Limit the length to");
    on->setChecked(seconds > 0.0);
    h->addWidget(on);
    auto* value = new DragValue;
    value->setRange(1.0f, 24.0f * 3600.0f);
    value->setSpeed(0.5f);
    value->setFormat("%.1f s");
    value->setValue(static_cast<float>(seconds > 0.0 ? seconds : 60.0));
    value->setFixedWidth(110);
    value->setEnabled(seconds > 0.0);
    h->addWidget(value);
    h->addStretch();
    QObject::connect(on, &QCheckBox::toggled, row, [=](bool checked) {
        value->setEnabled(checked);
        changed(checked ? value->value() : 0.0);
    });
    QObject::connect(value, &DragValue::valueChanged, row, [=](float v) {
        if (on->isChecked()) changed(v);
    });
    return row;
}

std::string formatTimecode(double seconds, double fps) {
    if (seconds < 0) seconds = 0;
    const long long totalFrames = static_cast<long long>(std::floor(seconds * fps + 1e-6));
    const long long f = static_cast<long long>(fps + 0.5);
    const long long frames = f > 0 ? totalFrames % f : 0;
    const long long totalSec = f > 0 ? totalFrames / f : 0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld:%02lld", totalSec / 3600, (totalSec / 60) % 60, totalSec % 60, frames);
    return buf;
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text.toUpper(), parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.8);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    l->setFont(f);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

QPushButton* accentButton(const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setProperty("accent", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// --- IconButton -----------------------------------------------------------------------

IconButton::IconButton(Icon icon, const QString& tip, int size, QWidget* parent) : QAbstractButton(parent), icon_(icon), size_(size) {
    setToolTip(tip);
    setFixedSize(size, size);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void IconButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hovered = underMouse() && isEnabled();
    const bool danger = icon_ == Icon::Trash; // deleting stands out
    if (isDown()) d.rectFilled(Pt(0, 0), Pt(width(), height()), danger ? theme::withAlpha(theme::kDanger, 90) : theme::kBorder, 5.0f);
    else if (hovered) d.rectFilled(Pt(0, 0), Pt(width(), height()), danger ? theme::withAlpha(theme::kDanger, 45) : theme::kBgHover, 5.0f);
    if (active_ && activeColor_) d.rectFilled(Pt(0, 0), Pt(width(), height()), theme::withAlpha(activeColor_, 50), 5.0f);
    Rgba col = active_ ? (activeColor_ ? activeColor_ : theme::kAccent) : (hovered ? (danger ? theme::kDanger : theme::kText) : theme::kTextDim);
    if (!isEnabled()) col = theme::withAlpha(theme::kTextFaint, 140);
    drawIcon(d, icon_, Pt(width() * 0.5f, height() * 0.5f), std::min(width(), height()) * 0.58f, col);
}

// --- Tile ------------------------------------------------------------------------------

Tile::Tile(Icon icon, const QString& label, QWidget* parent) : QAbstractButton(parent), icon_(icon), label_(label) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void Tile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hovered = underMouse();
    const Pt a(0.5f, 0.5f), b(width() - 0.5f, height() - 0.5f);
    d.rectFilled(a, b, hovered || isDown() ? theme::kBgHover : theme::kBgRaised, 8.0f);
    if (hovered || highlighted_) d.rect(a, b, theme::kAccent, 8.0f, highlighted_ ? 1.5f : 1.0f);
    const float iconSize = std::min(width(), height()) * 0.34f;
    drawIcon(d, icon_, Pt(width() * 0.5f, height() * 0.42f), iconSize, hovered ? theme::kAccent : theme::kText);
    const QString text = p.fontMetrics().elidedText(label_, Qt::ElideRight, width() - 8);
    const float tw = d.textWidth(text);
    d.text(Pt((width() - tw) * 0.5f, height() - d.textHeight() - height() * 0.1f), hovered ? theme::kText : theme::kTextDim, text);
    if (marked_) d.circleFilled(Pt(width() - 9.0f, 9.0f), 3.0f, theme::kAccent);
    if (dimmed_) d.rectFilled(a, b, theme::withAlpha(theme::kBgPanel, 140), 8.0f);
}

void Tile::mousePressEvent(QMouseEvent* e) {
    pressPos_ = e->position().toPoint();
    QAbstractButton::mousePressEvent(e);
}

void Tile::mouseMoveEvent(QMouseEvent* e) {
    if (dragData_ && (e->buttons() & Qt::LeftButton) &&
        (e->position().toPoint() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
        setDown(false);
        auto* drag = new QDrag(this);
        drag->setMimeData(dragData_());
        QPixmap pm(56, 56);
        pm.setDevicePixelRatio(1.0);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            Draw d(p);
            d.rectFilled(Pt(2, 2), Pt(54, 54), theme::withAlpha(theme::kBgRaised, 230), 10.0f);
            drawIcon(d, icon_, Pt(28, 28), 26.0f, theme::kAccent);
        }
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(28, 28));
        drag->exec(Qt::CopyAction);
        update();
        return;
    }
    QAbstractButton::mouseMoveEvent(e);
}

// --- KeyframeButton -----------------------------------------------------------------------

KeyframeButton::KeyframeButton(QWidget* parent) : QAbstractButton(parent) {
    setFixedSize(20, 24);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void KeyframeButton::setState(bool keyHere, bool animated) {
    if (keyHere == keyHere_ && animated == animated_) return;
    keyHere_ = keyHere;
    animated_ = animated;
    update();
}

void KeyframeButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const Rgba outline = animated_ || keyHere_ ? theme::kAccent : (underMouse() ? theme::kText : theme::kTextFaint);
    drawDiamond(d, Pt(width() * 0.5f, height() * 0.5f), 5.5f, keyHere_ ? theme::kAccent : 0, outline);
}

// --- DragValue -------------------------------------------------------------------------------

DragValue::DragValue(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize DragValue::sizeHint() const { return {80, 24}; }

bool DragValue::editing() const { return dragging_ || (edit_ && edit_->isVisible()); }

void DragValue::setValue(float v) {
    if (editing() || v == value_) return;
    value_ = v;
    update();
}

QString DragValue::text() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, format_.c_str(), value_);
    return QString::fromUtf8(buf);
}

void DragValue::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const bool hot = underMouse() || dragging_;
    d.rectFilled(Pt(0, 0), Pt(width(), height()), hot ? theme::kBgHover : theme::kBgRaised, theme::current().rounding);
    if (edit_ && edit_->isVisible()) return;
    const QString t = text();
    const float tw = d.textWidth(t);
    d.text(Pt((width() - tw) * 0.5f, (height() - d.textHeight()) * 0.5f), highlighted_ ? theme::kAccent : theme::kText, t);
}

void DragValue::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    dragging_ = true;
    moved_ = false;
    pressPos_ = e->globalPosition();
    pressValue_ = value_;
}

void DragValue::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    const float dx = static_cast<float>(e->globalPosition().x() - pressPos_.x());
    if (!moved_ && std::fabs(dx) < 3.0f) return;
    if (!moved_) {
        moved_ = true;
        emit editStarted();
    }
    float step = speed_;
    if (e->modifiers() & Qt::ShiftModifier) step *= 10.0f;
    if (e->modifiers() & Qt::AltModifier) step *= 0.1f;
    commit(pressValue_ + dx * step);
}

void DragValue::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !dragging_) return;
    dragging_ = false;
    if (moved_) emit editFinished();
    else beginTyping(); // a click: type the value
    update();
}

void DragValue::commit(float v) {
    if (min_ != max_) v = std::clamp(v, min_, max_);
    if (v == value_) return;
    value_ = v;
    update();
    emit valueChanged(v);
}

void DragValue::beginTyping() {
    if (!edit_) {
        edit_ = new QLineEdit(this);
        edit_->setAlignment(Qt::AlignCenter);
        edit_->setFrame(false);
        connect(edit_, &QLineEdit::editingFinished, this, [this] {
            if (!edit_->isVisible()) return;
            bool ok = false;
            // Accept "12", "12.5", "12 px", "45°", "50%" ...
            QString s = edit_->text().trimmed();
            while (!s.isEmpty() && !(s.back().isDigit() || s.back() == '.')) s.chop(1);
            const float v = s.toFloat(&ok);
            edit_->hide();
            if (ok) {
                emit editStarted();
                commit(v);
                emit editFinished();
            }
            update();
        });
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", value_);
    edit_->setText(QString::fromUtf8(buf));
    edit_->setGeometry(rect());
    edit_->show();
    edit_->setFocus();
    edit_->selectAll();
}

// --- ColorButton --------------------------------------------------------------------------

ColorButton::ColorButton(QWidget* parent) : QAbstractButton(parent) {
    setFixedSize(28, 24);
    setCursor(Qt::PointingHandCursor);
    connect(this, &QAbstractButton::clicked, this, &ColorButton::pick);
}

void ColorButton::setColor(Color c) {
    if (c == color_) return;
    color_ = c;
    update();
}

void ColorButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const float r = theme::current().rounding;
    // Checkerboard under translucent colors.
    if (color_.a < 1.0f) {
        d.rectFilled(Pt(0, 0), Pt(width(), height()), rgba(200, 200, 200), r);
        for (int y = 0; y < height(); y += 6)
            for (int x = (y / 6 % 2) * 6; x < width(); x += 12) d.rectFilled(Pt(x, y), Pt(std::min(x + 6, width()), std::min(y + 6, height())), rgba(150, 150, 150));
    }
    d.rectFilled(Pt(0, 0), Pt(width(), height()),
                 rgba(static_cast<int>(color_.r * 255 + 0.5f), static_cast<int>(color_.g * 255 + 0.5f),
                      static_cast<int>(color_.b * 255 + 0.5f), static_cast<int>(color_.a * 255 + 0.5f)),
                 r);
    d.rect(Pt(0, 0), Pt(width(), height()), underMouse() ? theme::kAccent : theme::kBorder, r);
}

void ColorButton::pick() {
    QColorDialog dialog(QColor::fromRgbF(color_.r, color_.g, color_.b, color_.a), window());
    dialog.setOption(QColorDialog::ShowAlphaChannel, alphaEnabled_);
    const Color before = color_;
    emit editStarted();
    connect(&dialog, &QColorDialog::currentColorChanged, this, [this](const QColor& c) {
        color_ = {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), static_cast<float>(c.alphaF())};
        update();
        emit colorChanged(color_);
    });
    if (dialog.exec() != QDialog::Accepted) { // cancelled: back to the color before
        color_ = before;
        update();
        emit colorChanged(color_);
    }
    emit editFinished();
}

} // namespace bemotion::ui
