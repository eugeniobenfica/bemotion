#include "ui/Widgets.h"

#include "app/PluginHost.h"
#include "core/Effects.h"
#include "core/Math.h"
#include "render/LogoMark.h"
#include "ui/AppSignals.h"
#include "ui/ColorPicker.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QButtonGroup>
#include <QPixmap>
#include <QPointer>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bemotion::ui {

Icon iconForPlugin(const std::string& name) {
    if (name == "bars" || name == "chart") return Icon::BarChart;
    if (name == "line_chart") return Icon::LineChart;
    if (name == "pie" || name == "donut") return Icon::PieChart;
    if (name == "table") return Icon::Table;
    if (name == "map") return Icon::Map;
    if (name == "cube" || name == "model") return Icon::Cube;
    if (name == "mic" || name == "microphone") return Icon::Microphone;
    if (name == "text") return Icon::Text;
    if (name == "counter" || name == "number") return Icon::Counter;
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
    case ShapeKind::Path: return Icon::PathShape;
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
    if (layer.type == LayerType::Text) return layer.text.subtitle ? Icon::Subtitles : layer.text.counter ? Icon::Counter : Icon::Text;
    return iconForShape(layer.shape.kind);
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

// --- AudioMeter --------------------------------------------------------------------------

namespace {
constexpr float kMeterFloor = -60.0f; // dB at the bottom
float meterU(float db) { return std::clamp((db - kMeterFloor) / -kMeterFloor, 0.0f, 1.0f); }
} // namespace

AudioMeter::AudioMeter(QWidget* parent) : QWidget(parent) {
    setToolTip(tr("Audio levels, left and right (dB). Red at the top: the sound clipped; click to clear it."));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void AudioMeter::feed(float left, float right, bool clipped, float seconds) {
    bool changed = clipped && !clipped_;
    clipped_ = clipped_ || clipped;
    const float in[2] = {left, right};
    for (int c = 0; c < 2; ++c) {
        const float db = in[c] > 1e-5f ? 20.0f * std::log10(in[c]) : -90.0f;
        // Up at once, down at 24 dB a second (like a hardware meter).
        const float next = std::max(db, level_[c] - 24.0f * seconds);
        if (std::fabs(next - level_[c]) > 0.05f) changed = true;
        level_[c] = std::max(next, -90.0f);
        if (db >= hold_[c]) {
            hold_[c] = db;
            holdFor_[c] = 1.5f;
        } else if ((holdFor_[c] -= seconds) <= 0.0f && hold_[c] > -90.0f) {
            hold_[c] = std::max(-90.0f, hold_[c] - 30.0f * seconds);
            changed = true;
        }
    }
    if (changed) update();
}

void AudioMeter::mousePressEvent(QMouseEvent*) {
    clipped_ = false;
    hold_[0] = hold_[1] = -90.0f;
    update();
}

void AudioMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.78);
    p.setFont(small);
    const float th = d.textHeight();
    const float top = 14.0f, bottom = static_cast<float>(height()) - th - 6.0f;
    const float barW = 8.0f, gap = 3.0f, x0 = 9.0f;
    d.line(Pt(0.5f, 4.0f), Pt(0.5f, static_cast<float>(height()) - 4.0f), theme::kBorder); // apart from the timeline
    const float h = std::max(10.0f, bottom - top);
    auto yOf = [&](float db) { return bottom - meterU(db) * h; };
    // The clip light.
    d.circleFilled(Pt(x0 + barW + gap * 0.5f, 6.0f), 3.5f, clipped_ ? theme::kDanger : theme::shade(60));
    for (int c = 0; c < 2; ++c) {
        const float x = x0 + c * (barW + gap);
        d.rectFilled(Pt(x, top), Pt(x + barW, bottom), theme::kBgDeep, 2.0f);
        // Lit up to the level, in three zones.
        const float lit = yOf(level_[c]);
        struct Zone {
            float from, to;
            Rgba color;
        } zones[] = {{kMeterFloor, -12.0f, theme::kSuccess}, {-12.0f, -3.0f, theme::kWarning}, {-3.0f, 0.0f, theme::kDanger}};
        for (const Zone& z : zones) {
            const float y0 = std::max(lit, yOf(z.to)), y1 = yOf(z.from);
            if (y0 < y1) d.rectFilled(Pt(x, y0), Pt(x + barW, y1), z.color, 1.0f);
        }
        if (hold_[c] > kMeterFloor) {
            const float hy = yOf(hold_[c]);
            d.line(Pt(x, hy), Pt(x + barW, hy), hold_[c] > -3.0f ? theme::kDanger : theme::kText, 1.5f);
        }
        d.text(Pt(x + barW * 0.5f - d.textWidth(c ? "R" : "L") * 0.5f, bottom + 3.0f), theme::kTextFaint, QString(c ? "R" : "L"));
    }
    // The scale.
    const float sx = x0 + 2 * barW + gap + 4.0f;
    for (const float db : {0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f}) {
        const float y = yOf(db);
        if (h < 120 && (db == -6.0f || db == -36.0f)) continue; // short: fewer marks
        d.line(Pt(sx - 2.0f, y), Pt(sx, y), theme::kTextFaint);
        d.text(Pt(sx + 2.0f, y - th * 0.5f), theme::kTextFaint, QString::number(static_cast<int>(-db)));
    }
}

void toggleFullScreen(QWidget* window) {
    if (window->isFullScreen()) {
        if (window->property("bemotionWasMaximized").toBool()) window->showMaximized();
        else window->showNormal();
        return;
    }
    window->setProperty("bemotionWasMaximized", window->isMaximized());
    window->showFullScreen();
}

QWidget* lengthLimitRow(double seconds, std::function<void(double)> changed) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* on = new QCheckBox(tr("Limit the length to"));
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
    const Rgba outline = animated_ || keyHere_ ? theme::kKeyframe : (underMouse() ? theme::kText : theme::kTextFaint);
    drawDiamond(d, Pt(width() * 0.5f, height() * 0.5f), 5.5f, keyHere_ ? theme::kKeyframe : 0, outline);
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
    QPointer<ColorButton> self(this); // a panel may rebuild while the picker is open
    Color c = color_;
    emit editStarted();
    pickColor(this, c, alphaEnabled_, [self](Color live) {
        if (!self) return;
        self->color_ = live;
        self->update();
        emit self->colorChanged(live);
    });
    if (self) emit editFinished();
}

Icon iconForSection(const QString& title) {
    static const std::pair<const char*, Icon> kIcons[] = {
        {TR_NOOP("Transform"), Icon::Pointer},     {TR_NOOP("Fill"), Icon::Gradient},         {TR_NOOP("Stroke"), Icon::Pen},
        {TR_NOOP("Shape"), Icon::Polygon},         {TR_NOOP("Text"), Icon::Text},             {TR_NOOP("Outline"), Icon::Text},
        {TR_NOOP("Box"), Icon::Rectangle},         {TR_NOOP("Crop"), Icon::Crop},             {TR_NOOP("Frame"), Icon::FitView},
        {TR_NOOP("Audio"), Icon::Speaker},         {TR_NOOP("Time"), Icon::Speed},            {TR_NOOP("Camera"), Icon::Camera},
        {TR_NOOP("Background"), Icon::Background}, {TR_NOOP("Repeater"), Icon::Duplicate},   {TR_NOOP("Object"), Icon::Plugin},
        {TR_NOOP("Counter"), Icon::Counter},       {TR_NOOP("Composition Settings"), Icon::Gear},
        {TR_NOOP("Basic Correction"), Icon::FxColor}, {TR_NOOP("Look"), Icon::Palette},   {TR_NOOP("Curves"), Icon::Graph},
        {TR_NOOP("Letters"), Icon::Text},          {TR_NOOP("Data"), Icon::Table},            {TR_NOOP("Masks"), Icon::Mask},
        {TR_NOOP("Blending"), Icon::Transition},   {TR_NOOP("Puppet"), Icon::Puppet},         {TR_NOOP("Warp"), Icon::Warp},
        {TR_NOOP("Tracking"), Icon::Track},
    };
    for (const auto& [english, icon] : kIcons)
        if (title == english || title == tr(english)) return icon;
    if (title.startsWith(tr("Mask")) || title.startsWith("Mask")) return Icon::Mask;
    // An effect ("Gaussian Blur 2"): its category's icon, as the Effects browser shows it.
    static const std::pair<const char*, Icon> kCategories[] = {
        {"Blur & Sharpen", Icon::FxBlur}, {"Color Correction", Icon::FxColor},   {"Channel", Icon::FxChannel},
        {"Distort", Icon::FxDistort},     {"Generate", Icon::FxGenerate},        {"Perspective", Icon::FxPerspective},
        {"Stylize", Icon::FxStylize},     {"Time", Icon::FxTime},                {"Keying", Icon::FxKeying},
    };
    for (int i = 0; i < static_cast<int>(EffectType::Count); ++i) {
        const auto type = static_cast<EffectType>(i);
        const QString label = tr(effectLabel(type));
        if (title == label || title.startsWith(label + " ") || title == effectLabel(type))
            for (const auto& [category, icon] : kCategories)
                if (std::strcmp(effectCategory(type), category) == 0) return icon;
    }
    for (int i = 0; i < static_cast<int>(AudioEffectType::Count); ++i)
        if (title == tr(audioEffectLabel(static_cast<AudioEffectType>(i)))) return Icon::Audio;
    return Icon::Sliders;
}

} // namespace bemotion::ui
