#include "bestyle/Icons.h"

#include "bestyle/Theme.h"

#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace bestyle {

namespace {
constexpr float kPi = 3.14159265358979323846f;
std::function<void(QPainter&, Pt, float, Rgba)>& logoPainter() {
    static std::function<void(QPainter&, Pt, float, Rgba)> f;
    return f;
}
} // namespace

void setLogoPainter(std::function<void(QPainter& p, Pt center, float radius, Rgba color)> paint) { logoPainter() = std::move(paint); }

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
    case Icon::ExitFullscreen: {
        const float a = h * 0.78f, b = h * 0.3f;
        for (const float sx : {-1.0f, 1.0f})
            for (const float sy : {-1.0f, 1.0f}) {
                const Pt corner(c.x + sx * b, c.y + sy * b);
                dl.line(corner, Pt(c.x + sx * b, c.y + sy * a), col, th);
                dl.line(corner, Pt(c.x + sx * a, c.y + sy * b), col, th);
            }
        break;
    }
    case Icon::Close: {
        const float a = h * 0.55f;
        dl.line(Pt(c.x - a, c.y - a), Pt(c.x + a, c.y + a), col, th * 1.15f);
        dl.line(Pt(c.x - a, c.y + a), Pt(c.x + a, c.y - a), col, th * 1.15f);
        break;
    }
    case Icon::Maximize:
        dl.rect(Pt(c.x - h * 0.6f, c.y - h * 0.6f), Pt(c.x + h * 0.6f, c.y + h * 0.6f), col, 1.5f, th);
        break;
    case Icon::Restore:
        dl.rect(Pt(c.x - h * 0.65f, c.y - h * 0.3f), Pt(c.x + h * 0.3f, c.y + h * 0.65f), col, 1.5f, th);
        dl.line(Pt(c.x - h * 0.3f, c.y - h * 0.65f), Pt(c.x + h * 0.65f, c.y - h * 0.65f), col, th);
        dl.line(Pt(c.x + h * 0.65f, c.y - h * 0.65f), Pt(c.x + h * 0.65f, c.y + h * 0.3f), col, th);
        break;
    case Icon::Eyedropper: { // a pipette, tip at the bottom left
        dl.line(Pt(c.x - h * 0.7f, c.y + h * 0.7f), Pt(c.x + h * 0.15f, c.y - h * 0.15f), col, th * 1.3f);
        dl.line(Pt(c.x - h * 0.05f, c.y - h * 0.55f), Pt(c.x + h * 0.55f, c.y + h * 0.05f), col, th * 1.3f);
        dl.circleFilled(Pt(c.x + h * 0.45f, c.y - h * 0.45f), h * 0.3f, col);
        break;
    }
    case Icon::Snapshot: { // a photo camera: body, lens, the bump on top
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.4f), Pt(c.x + h * 0.8f, c.y + h * 0.65f), col, 2.5f, th);
        dl.line(Pt(c.x - h * 0.3f, c.y - h * 0.4f), Pt(c.x - h * 0.15f, c.y - h * 0.65f), col, th);
        dl.line(Pt(c.x - h * 0.15f, c.y - h * 0.65f), Pt(c.x + h * 0.25f, c.y - h * 0.65f), col, th);
        dl.line(Pt(c.x + h * 0.25f, c.y - h * 0.65f), Pt(c.x + h * 0.4f, c.y - h * 0.4f), col, th);
        dl.circle(Pt(c.x, c.y + h * 0.12f), h * 0.3f, col, th);
        break;
    }
    case Icon::Pen: { // a nib: a pointed shield with a slit and a hole
        const Pt nib[5] = {Pt(c.x, c.y + h * 0.9f), Pt(c.x - h * 0.55f, c.y + h * 0.05f), Pt(c.x - h * 0.3f, c.y - h * 0.55f),
                           Pt(c.x + h * 0.3f, c.y - h * 0.55f), Pt(c.x + h * 0.55f, c.y + h * 0.05f)};
        dl.polyline(nib, 5, col, true, th);
        dl.line(Pt(c.x, c.y + h * 0.9f), Pt(c.x, c.y + h * 0.05f), col, th);
        dl.circleFilled(Pt(c.x, c.y - h * 0.05f), h * 0.12f, col);
        dl.line(Pt(c.x - h * 0.3f, c.y - h * 0.8f), Pt(c.x + h * 0.3f, c.y - h * 0.8f), col, th);
        break;
    }
    case Icon::PathShape: { // a curve between two vertices, with a handle
        const Pt a(c.x - h * 0.75f, c.y + h * 0.55f), b(c.x + h * 0.75f, c.y - h * 0.55f);
        dl.bezier(a, Pt(c.x - h * 0.2f, c.y - h * 0.9f), Pt(c.x + h * 0.2f, c.y + h * 0.9f), b, col, th);
        dl.rectFilled(Pt(a.x - h * 0.17f, a.y - h * 0.17f), Pt(a.x + h * 0.17f, a.y + h * 0.17f), col);
        dl.rectFilled(Pt(b.x - h * 0.17f, b.y - h * 0.17f), Pt(b.x + h * 0.17f, b.y + h * 0.17f), col);
        break;
    }
    case Icon::Mask: { // a square with a round hole marked by a dashed outline
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.8f), Pt(c.x + h * 0.8f, c.y + h * 0.8f), col, 2.0f, th);
        for (int i = 0; i < 8; i += 2)
            dl.arc(c, h * 0.45f, kPi * 0.25f * i, kPi * 0.25f * (i + 1), col, th);
        break;
    }
    case Icon::RippleEdit: { // a clip's edge and an arrow pushing what follows
        dl.line(Pt(c.x - h * 0.3f, c.y - h * 0.8f), Pt(c.x - h * 0.3f, c.y + h * 0.8f), col, th);
        dl.line(Pt(c.x - h * 0.3f, c.y - h * 0.8f), Pt(c.x - h * 0.75f, c.y - h * 0.8f), col, th);
        dl.line(Pt(c.x - h * 0.3f, c.y + h * 0.8f), Pt(c.x - h * 0.75f, c.y + h * 0.8f), col, th);
        dl.line(Pt(c.x, c.y), Pt(c.x + h * 0.8f, c.y), col, th);
        dl.line(Pt(c.x + h * 0.8f, c.y), Pt(c.x + h * 0.5f, c.y - h * 0.3f), col, th);
        dl.line(Pt(c.x + h * 0.8f, c.y), Pt(c.x + h * 0.5f, c.y + h * 0.3f), col, th);
        break;
    }
    case Icon::RollEdit: { // the cut between two clips, arrows both ways
        dl.line(Pt(c.x, c.y - h * 0.85f), Pt(c.x, c.y + h * 0.85f), col, th);
        for (float s : {-1.0f, 1.0f}) {
            dl.line(Pt(c.x + s * h * 0.2f, c.y), Pt(c.x + s * h * 0.85f, c.y), col, th);
            dl.line(Pt(c.x + s * h * 0.85f, c.y), Pt(c.x + s * h * 0.55f, c.y - h * 0.3f), col, th);
            dl.line(Pt(c.x + s * h * 0.85f, c.y), Pt(c.x + s * h * 0.55f, c.y + h * 0.3f), col, th);
        }
        break;
    }
    case Icon::SlipEdit: { // a clip, its content sliding inside it
        dl.rect(Pt(c.x - h * 0.85f, c.y - h * 0.55f), Pt(c.x + h * 0.85f, c.y + h * 0.55f), col, 2.0f, th);
        dl.line(Pt(c.x - h * 0.45f, c.y), Pt(c.x + h * 0.45f, c.y), col, th);
        for (float s : {-1.0f, 1.0f}) {
            dl.line(Pt(c.x + s * h * 0.45f, c.y), Pt(c.x + s * h * 0.2f, c.y - h * 0.22f), col, th);
            dl.line(Pt(c.x + s * h * 0.45f, c.y), Pt(c.x + s * h * 0.2f, c.y + h * 0.22f), col, th);
        }
        break;
    }
    case Icon::Marker: { // a flag on its pole
        dl.line(Pt(c.x - h * 0.55f, c.y - h * 0.85f), Pt(c.x - h * 0.55f, c.y + h * 0.85f), col, th);
        const Pt flag[4] = {Pt(c.x - h * 0.55f, c.y - h * 0.85f), Pt(c.x + h * 0.7f, c.y - h * 0.55f), Pt(c.x - h * 0.55f, c.y - h * 0.15f),
                            Pt(c.x - h * 0.55f, c.y - h * 0.85f)};
        dl.polyFilled(flag, 4, col);
        break;
    }
    case Icon::Speaker:
    case Icon::SpeakerMuted: { // a speaker, with waves or a cross
        const Pt body[6] = {Pt(c.x - h * 0.85f, c.y - h * 0.3f), Pt(c.x - h * 0.45f, c.y - h * 0.3f), Pt(c.x + h * 0.05f, c.y - h * 0.75f),
                            Pt(c.x + h * 0.05f, c.y + h * 0.75f), Pt(c.x - h * 0.45f, c.y + h * 0.3f), Pt(c.x - h * 0.85f, c.y + h * 0.3f)};
        dl.polyFilled(body, 6, col);
        if (icon == Icon::Speaker) {
            dl.arc(Pt(c.x + h * 0.05f, c.y), h * 0.42f, -kPi * 0.3f, kPi * 0.3f, col, th);
            dl.arc(Pt(c.x + h * 0.05f, c.y), h * 0.78f, -kPi * 0.3f, kPi * 0.3f, col, th);
        } else {
            dl.line(Pt(c.x + h * 0.35f, c.y - h * 0.35f), Pt(c.x + h * 0.85f, c.y + h * 0.35f), col, th);
            dl.line(Pt(c.x + h * 0.35f, c.y + h * 0.35f), Pt(c.x + h * 0.85f, c.y - h * 0.35f), col, th);
        }
        break;
    }
    case Icon::Solo: { // headphones
        dl.arc(Pt(c.x, c.y + h * 0.1f), h * 0.7f, kPi, kPi * 2.0f, col, th);
        dl.rectFilled(Pt(c.x - h * 0.85f, c.y + h * 0.05f), Pt(c.x - h * 0.45f, c.y + h * 0.75f), col, 2.0f);
        dl.rectFilled(Pt(c.x + h * 0.45f, c.y + h * 0.05f), Pt(c.x + h * 0.85f, c.y + h * 0.75f), col, 2.0f);
        break;
    }
    case Icon::FxKeying:
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.8f), Pt(c.x + h * 0.8f, c.y + h * 0.8f), col, 2.0f, th);
        dl.circleFilled(c, h * 0.42f, col);
        dl.line(Pt(c.x - h * 0.8f, c.y + h * 0.8f), Pt(c.x + h * 0.8f, c.y - h * 0.8f), col, th);
        break;
    case Icon::Scopes: {
        dl.rect(Pt(c.x - h * 0.85f, c.y - h * 0.75f), Pt(c.x + h * 0.85f, c.y + h * 0.75f), col, 2.0f, th);
        Pt pts[9];
        for (int i = 0; i <= 8; ++i) {
            const float x = -0.65f + 1.3f * i / 8.0f;
            pts[i] = Pt(c.x + x * h, c.y + std::sin(x * kPi * 1.4f + 0.6f) * h * 0.35f);
        }
        dl.polyline(pts, 9, col, false, th);
        break;
    }
    case Icon::Proxy:
        dl.rect(Pt(c.x - h * 0.8f, c.y - h * 0.55f), Pt(c.x + h * 0.3f, c.y + h * 0.55f), col, 1.5f, th);
        dl.rectFilled(Pt(c.x + h * 0.05f, c.y + h * 0.05f), Pt(c.x + h * 0.8f, c.y + h * 0.75f), col, 1.0f);
        break;
    case Icon::Speed: { // a gauge and its needle
        dl.arc(Pt(c.x, c.y + h * 0.3f), h * 0.8f, kPi, kPi * 2.0f, col, th);
        dl.line(Pt(c.x - h * 0.8f, c.y + h * 0.3f), Pt(c.x + h * 0.8f, c.y + h * 0.3f), col, th);
        dl.line(Pt(c.x, c.y + h * 0.3f), Pt(c.x + h * 0.45f, c.y - h * 0.3f), col, th);
        dl.circleFilled(Pt(c.x, c.y + h * 0.3f), h * 0.13f, col);
        break;
    }
    case Icon::AlignLeft:
    case Icon::AlignCenter:
    case Icon::AlignRight: // three lines of text, long and short, against an edge or centered
        for (int i = 0; i < 3; ++i) {
            const float w = h * (i == 1 ? 1.0f : 1.6f), y = c.y + h * (-0.6f + 0.6f * i);
            const float x0 = icon == Icon::AlignLeft ? c.x - h * 0.8f : icon == Icon::AlignRight ? c.x + h * 0.8f - w : c.x - w * 0.5f;
            dl.line(Pt(x0, y), Pt(x0 + w, y), col, th);
        }
        break;
    case Icon::FitView: { // corner marks around a smaller frame
        const float a = h * 0.85f, b = h * 0.45f;
        for (const float sx : {-1.0f, 1.0f})
            for (const float sy : {-1.0f, 1.0f}) {
                dl.line(Pt(c.x + sx * a, c.y + sy * a), Pt(c.x + sx * a, c.y + sy * b), col, th);
                dl.line(Pt(c.x + sx * a, c.y + sy * a), Pt(c.x + sx * b, c.y + sy * a), col, th);
            }
        dl.rect(Pt(c.x - h * 0.42f, c.y - h * 0.28f), Pt(c.x + h * 0.42f, c.y + h * 0.28f), col, 1.0f, th);
        break;
    }
    case Icon::KeyPrev:
    case Icon::KeyNext: { // a keyframe diamond with an arrowhead beside it
        const float s = icon == Icon::KeyNext ? 1.0f : -1.0f;
        const Pt k(c.x - s * h * 0.3f, c.y);
        const Pt diamond[4] = {Pt(k.x, k.y - h * 0.55f), Pt(k.x + h * 0.55f, k.y), Pt(k.x, k.y + h * 0.55f), Pt(k.x - h * 0.55f, k.y)};
        dl.polyFilled(diamond, 4, col);
        dl.triangleFilled(Pt(c.x + s * h * 0.45f, c.y - h * 0.5f), Pt(c.x + s * h * 0.95f, c.y), Pt(c.x + s * h * 0.45f, c.y + h * 0.5f), col);
        break;
    }
    case Icon::Brush: { // a handle and a round tip, tilted
        dl.line(Pt(c.x + h * 0.75f, c.y - h * 0.75f), Pt(c.x - h * 0.05f, c.y + h * 0.05f), col, th * 1.3f);
        dl.circleFilled(Pt(c.x - h * 0.35f, c.y + h * 0.35f), h * 0.32f, col);
        dl.triangleFilled(Pt(c.x - h * 0.62f, c.y + h * 0.45f), Pt(c.x - h * 0.45f, c.y + h * 0.62f), Pt(c.x - h * 0.85f, c.y + h * 0.85f), col);
        break;
    }
    case Icon::Eraser: { // a tilted block, its tip filled
        const Pt q[4] = {Pt(c.x - h * 0.85f, c.y + h * 0.2f), Pt(c.x + h * 0.15f, c.y - h * 0.8f), Pt(c.x + h * 0.8f, c.y - h * 0.15f), Pt(c.x - h * 0.2f, c.y + h * 0.85f)};
        dl.polyline(q, 4, col, true, th);
        const Pt tip[4] = {q[0], Pt(c.x - h * 0.35f, c.y - h * 0.3f), Pt(c.x + h * 0.3f, c.y + h * 0.35f), q[3]};
        dl.polyFilled(tip, 4, col);
        dl.line(Pt(c.x - h * 0.2f, c.y + h * 0.85f), Pt(c.x + h * 0.85f, c.y + h * 0.85f), col, th);
        break;
    }
    case Icon::Puppet: { // pins joined by bones
        const Pt pins[4] = {Pt(c.x, c.y - h * 0.7f), Pt(c.x, c.y + h * 0.05f), Pt(c.x - h * 0.7f, c.y + h * 0.7f), Pt(c.x + h * 0.7f, c.y + h * 0.7f)};
        dl.line(pins[0], pins[1], col, th);
        dl.line(pins[1], pins[2], col, th);
        dl.line(pins[1], pins[3], col, th);
        for (const Pt& p : pins) dl.circleFilled(p, h * 0.2f, col);
        break;
    }
    case Icon::Warp: { // a grid bent by a curve
        for (int i = 0; i < 3; ++i) {
            const float y = c.y + h * (-0.6f + 0.6f * i);
            dl.bezier(Pt(c.x - h * 0.8f, y), Pt(c.x - h * 0.2f, y - h * 0.45f), Pt(c.x + h * 0.2f, y + h * 0.45f), Pt(c.x + h * 0.8f, y), col, th);
        }
        dl.line(Pt(c.x - h * 0.8f, c.y - h * 0.6f), Pt(c.x - h * 0.8f, c.y + h * 0.6f), col, th);
        dl.line(Pt(c.x + h * 0.8f, c.y - h * 0.6f), Pt(c.x + h * 0.8f, c.y + h * 0.6f), col, th);
        break;
    }
    case Icon::Track: { // a target and the path it followed
        dl.circle(Pt(c.x + h * 0.25f, c.y - h * 0.25f), h * 0.45f, col, th);
        dl.circleFilled(Pt(c.x + h * 0.25f, c.y - h * 0.25f), h * 0.12f, col);
        dl.bezier(Pt(c.x - h * 0.85f, c.y + h * 0.8f), Pt(c.x - h * 0.5f, c.y + h * 0.1f), Pt(c.x - h * 0.3f, c.y + h * 0.5f), Pt(c.x - h * 0.05f, c.y + h * 0.05f), col, th);
        break;
    }
    case Icon::Gradient: { // a square fading from full to clear
        const Pt a(c.x - h * 0.75f, c.y - h * 0.75f), b(c.x + h * 0.75f, c.y + h * 0.75f);
        for (int i = 0; i < 4; ++i) {
            const float x0 = a.x + (b.x - a.x) * i / 4.0f, x1 = a.x + (b.x - a.x) * (i + 1) / 4.0f;
            dl.rectFilled(Pt(x0, a.y), Pt(x1, b.y), theme::withAlpha(col, 255 - i * 60));
        }
        dl.rect(a, b, col, 1.5f, th * 0.8f);
        break;
    }
    case Icon::Levels: { // uneven bars brought to one line
        const float heights[4] = {0.5f, 1.1f, 0.75f, 1.3f};
        for (int i = 0; i < 4; ++i) {
            const float x = c.x - h * 0.75f + h * 0.5f * i;
            dl.rectFilled(Pt(x - h * 0.15f, c.y + h * 0.8f - h * heights[i]), Pt(x + h * 0.15f, c.y + h * 0.8f), theme::withAlpha(col, 150));
        }
        dl.line(Pt(c.x - h * 0.95f, c.y - h * 0.05f), Pt(c.x + h * 0.95f, c.y - h * 0.05f), col, th);
        break;
    }
    case Icon::Meter:
        dl.rectFilled(Pt(c.x - h * 0.6f, c.y - h * 0.2f), Pt(c.x - h * 0.1f, c.y + h * 0.75f), col, 1.0f);
        dl.rectFilled(Pt(c.x + h * 0.1f, c.y - h * 0.7f), Pt(c.x + h * 0.6f, c.y + h * 0.75f), col, 1.0f);
        break;
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
    case Icon::Map: { // a folded map: three panels
        const Pt pts[8] = {Pt(c.x - h * 0.85f, c.y - h * 0.55f), Pt(c.x - h * 0.3f, c.y - h * 0.75f), Pt(c.x + h * 0.3f, c.y - h * 0.55f),
                           Pt(c.x + h * 0.85f, c.y - h * 0.75f), Pt(c.x + h * 0.85f, c.y + h * 0.55f), Pt(c.x + h * 0.3f, c.y + h * 0.75f),
                           Pt(c.x - h * 0.3f, c.y + h * 0.55f), Pt(c.x - h * 0.85f, c.y + h * 0.75f)};
        dl.polyline(pts, 8, col, true, th);
        dl.line(Pt(c.x - h * 0.3f, c.y - h * 0.75f), Pt(c.x - h * 0.3f, c.y + h * 0.55f), col, th);
        dl.line(Pt(c.x + h * 0.3f, c.y - h * 0.55f), Pt(c.x + h * 0.3f, c.y + h * 0.75f), col, th);
        break;
    }
    case Icon::Counter: { // a number sign
        dl.line(Pt(c.x - h * 0.15f, c.y - h * 0.75f), Pt(c.x - h * 0.4f, c.y + h * 0.75f), col, th);
        dl.line(Pt(c.x + h * 0.4f, c.y - h * 0.75f), Pt(c.x + h * 0.15f, c.y + h * 0.75f), col, th);
        dl.line(Pt(c.x - h * 0.75f, c.y - h * 0.28f), Pt(c.x + h * 0.8f, c.y - h * 0.28f), col, th);
        dl.line(Pt(c.x - h * 0.8f, c.y + h * 0.28f), Pt(c.x + h * 0.75f, c.y + h * 0.28f), col, th);
        break;
    }
    case Icon::ArrowUp:
    case Icon::ArrowDown:
    case Icon::ArrowLeft:
    case Icon::ArrowRight: { // a shaft and an open head, drawn pointing right then turned
        const float ang = icon == Icon::ArrowRight ? 0.0f : icon == Icon::ArrowDown ? kPi * 0.5f : icon == Icon::ArrowLeft ? kPi : -kPi * 0.5f;
        const float cs = std::cos(ang), sn = std::sin(ang);
        auto at = [&](float x, float y) { return Pt(c.x + (x * cs - y * sn) * h, c.y + (x * sn + y * cs) * h); };
        dl.line(at(-0.75f, 0.0f), at(0.7f, 0.0f), col, th);
        const Pt head[3] = {at(0.2f, -0.5f), at(0.72f, 0.0f), at(0.2f, 0.5f)};
        dl.polyline(head, 3, col, false, th);
        break;
    }
    case Icon::Burst: { // a dot with eight rays
        dl.circleFilled(c, h * 0.22f, col);
        for (int i = 0; i < 8; ++i) {
            const float a = kPi * 0.25f * i, cs = std::cos(a), sn = std::sin(a);
            dl.line(Pt(c.x + cs * h * 0.48f, c.y + sn * h * 0.48f), Pt(c.x + cs * h * 0.82f, c.y + sn * h * 0.82f), col, th);
        }
        break;
    }
    case Icon::Ripple: // a dot and two rings
        dl.circleFilled(c, h * 0.2f, col);
        dl.circle(c, h * 0.5f, col, th);
        dl.circle(c, h * 0.82f, theme::withAlpha(col, 140), th);
        break;
    case Icon::Heartbeat: { // a heart monitor line
        const Pt pts[7] = {Pt(c.x - h * 0.85f, c.y), Pt(c.x - h * 0.35f, c.y), Pt(c.x - h * 0.15f, c.y - h * 0.7f),
                           Pt(c.x + h * 0.12f, c.y + h * 0.7f), Pt(c.x + h * 0.32f, c.y - h * 0.25f), Pt(c.x + h * 0.45f, c.y),
                           Pt(c.x + h * 0.85f, c.y)};
        dl.polyline(pts, 7, col, false, th);
        break;
    }
    case Icon::Zigzag: { // shaking side to side
        const Pt pts[6] = {Pt(c.x - h * 0.85f, c.y), Pt(c.x - h * 0.55f, c.y - h * 0.5f), Pt(c.x - h * 0.2f, c.y + h * 0.5f),
                           Pt(c.x + h * 0.2f, c.y - h * 0.5f), Pt(c.x + h * 0.55f, c.y + h * 0.5f), Pt(c.x + h * 0.85f, c.y)};
        dl.polyline(pts, 6, col, false, th);
        break;
    }
    case Icon::Wave: { // floating: one slow wave
        Pt pts[17];
        for (int i = 0; i < 17; ++i) {
            const float u = i / 16.0f;
            pts[i] = Pt(c.x + (u * 1.7f - 0.85f) * h, c.y - std::sin(u * 2.0f * kPi) * h * 0.4f);
        }
        dl.polyline(pts, 17, col, false, th);
        break;
    }
    case Icon::Bounce: // a ball above the floor, with its path
        dl.circleFilled(Pt(c.x, c.y - h * 0.4f), h * 0.28f, col);
        dl.line(Pt(c.x - h * 0.8f, c.y + h * 0.7f), Pt(c.x + h * 0.8f, c.y + h * 0.7f), col, th);
        dl.line(Pt(c.x, c.y + h * 0.02f), Pt(c.x, c.y + h * 0.4f), theme::withAlpha(col, 120), th);
        break;
    case Icon::Fade: { // four squares, fainter and fainter
        const float s = h * 0.36f;
        for (int i = 0; i < 4; ++i) {
            const float x = c.x - h * 0.85f + i * h * 0.45f;
            dl.rectFilled(Pt(x, c.y - s), Pt(x + s, c.y + s), theme::withAlpha(col, 255 - i * 65), 1.5f);
        }
        break;
    }
    case Icon::Help: { // a question mark in a circle
        dl.circle(c, h * 0.92f, col, th);
        dl.arc(Pt(c.x, c.y - h * 0.2f), h * 0.3f, kPi * 1.0f, kPi * 2.35f, col, th);
        dl.line(Pt(c.x + h * 0.04f, c.y + h * 0.06f), Pt(c.x, c.y + h * 0.22f), col, th);
        dl.circleFilled(Pt(c.x, c.y + h * 0.5f), th * 0.8f, col);
        break;
    }
    case Icon::Keyboard: { // a keyboard: keys and a space bar
        dl.rect(Pt(c.x - h * 0.95f, c.y - h * 0.6f), Pt(c.x + h * 0.95f, c.y + h * 0.6f), col, size * 0.1f, th);
        for (int i = 0; i < 4; ++i) {
            const float x = c.x - h * 0.55f + i * h * 0.37f;
            dl.circleFilled(Pt(x, c.y - h * 0.22f), th * 0.75f, col);
        }
        dl.line(Pt(c.x - h * 0.45f, c.y + h * 0.25f), Pt(c.x + h * 0.45f, c.y + h * 0.25f), col, th);
        break;
    }
    case Icon::Edit: { // a pencil, tip down left
        const Pt a(c.x + h * 0.55f, c.y - h * 0.85f), b(c.x + h * 0.85f, c.y - h * 0.55f);
        const Pt tip(c.x - h * 0.85f, c.y + h * 0.85f);
        const Pt e1(c.x - h * 0.75f, c.y + h * 0.35f), e2(c.x - h * 0.35f, c.y + h * 0.75f);
        const Pt body[5] = {a, b, e2, tip, e1};
        dl.polyline(body, 5, col, true, th);
        dl.line(Pt(c.x + h * 0.3f, c.y - h * 0.6f), Pt(c.x + h * 0.6f, c.y - h * 0.3f), col, th);
        break;
    }
    case Icon::Crop: { // two L shapes crossing, as every crop tool draws it
        const Pt a[3] = {Pt(c.x - h * 0.45f, c.y - h * 0.9f), Pt(c.x - h * 0.45f, c.y + h * 0.45f), Pt(c.x + h * 0.9f, c.y + h * 0.45f)};
        const Pt b[3] = {Pt(c.x - h * 0.9f, c.y - h * 0.45f), Pt(c.x + h * 0.45f, c.y - h * 0.45f), Pt(c.x + h * 0.45f, c.y + h * 0.9f)};
        dl.polyline(a, 3, col, false, th);
        dl.polyline(b, 3, col, false, th);
        break;
    }
    case Icon::Expression: { // an italic f, as After Effects marks expressions
        dl.bezier(Pt(c.x + h * 0.62f, c.y - h * 0.7f), Pt(c.x + h * 0.05f, c.y - h * 1.05f), Pt(c.x + h * 0.05f, c.y + h * 1.05f),
                  Pt(c.x - h * 0.6f, c.y + h * 0.72f), col, th);
        dl.line(Pt(c.x - h * 0.42f, c.y - h * 0.18f), Pt(c.x + h * 0.48f, c.y - h * 0.18f), col, th);
        break;
    }
    case Icon::Subtitles: { // a frame with two lines of text at its bottom
        dl.rect(Pt(c.x - h * 0.85f, c.y - h * 0.65f), Pt(c.x + h * 0.85f, c.y + h * 0.65f), col, size * 0.1f, th);
        dl.line(Pt(c.x - h * 0.5f, c.y + h * 0.05f), Pt(c.x + h * 0.5f, c.y + h * 0.05f), col, th);
        dl.line(Pt(c.x - h * 0.3f, c.y + h * 0.35f), Pt(c.x + h * 0.3f, c.y + h * 0.35f), col, th);
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
    case Icon::Logo: // the app's mark (setLogoPainter), else a plain disc
        if (logoPainter()) logoPainter()(dl.painter(), c, h * 0.95f, col);
        else dl.circleFilled(c, h * 0.95f, col);
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

} // namespace bestyle
