#include "ui/Tracking.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "core/Tracking.h"
#include "media/MediaIO.h"
#include "ui/EditorContext.h"
#include "ui/Language.h"

#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bemotion::ui {

void trackPoint(EditorContext& ctx, uint64_t layerId, Vec2 point, float radius, QWidget* parent) {
    const Layer* clip = ctx.comp().findLayer(layerId);
    if (!clip || clip->type != LayerType::Media || !clip->media.hasVideo) return;
    const MediaItem* item = ctx.comp().findMedia(clip->media.mediaId);
    if (!item) return;
    std::string error;
    auto reader = media::VideoReader::open(platform::fromUtf8(item->path), &error);
    if (!reader) {
        QMessageBox::warning(parent, tr("Track Point"), tr("Could not read the video: %1").arg(QString::fromStdString(error)));
        return;
    }
    const ToolSettings& opts = settings().tools;
    // Tracked on a smaller picture (at most 960 px wide): as precise for following a detail, and quick.
    const Vec2 size = clip->media.size;
    const double scale = size.x > 960.0f ? 960.0 / size.x : 1.0;
    reader->setOutputScale(scale);
    const float s = static_cast<float>(reader->width()) / std::max(1.0f, size.x);
    const int patch = std::clamp(static_cast<int>(std::lround(radius * s)), 4, 60);
    const int search = std::clamp(static_cast<int>(std::lround(opts.trackSearch * s)), 4, 200);
    const float match = std::clamp(opts.trackMatch * 0.01f, 0.1f, 0.99f);
    // From the playhead toward the clip's end (or its start), frame by frame.
    const double frame = ctx.comp().frameDuration();
    const bool back = opts.trackBackward;
    const double from = std::clamp(ctx.comp().snapToFrame(ctx.time), clip->inPoint, clip->outPoint - frame * 0.5);
    const double to = back ? clip->inPoint : clip->outPoint - frame * 0.5;
    const int frames = std::max(1, static_cast<int>(std::floor(std::fabs(to - from) / frame)) + 1);
    QProgressDialog progress(tr("Following the point..."), tr("Stop"), 0, frames, parent);
    progress.setWindowTitle(tr("Track Point"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    std::vector<std::pair<double, Vec2>> path; // composition time, the point in the clip's own space
    std::vector<uint8_t> rgba;
    // The detail is looked for as it was in the last frame where it matched well (a template that
    // follows slow changes of light and angle without drifting on every frame's small errors).
    GrayImage ref;
    Vec2 refAt{}, at{(point.x + size.x * 0.5f) * s, (point.y + size.y * 0.5f) * s}; // the tracked picture's pixels
    Vec2 velocity{};
    bool lost = false;
    for (int i = 0; i < frames; ++i) {
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled()) break;
        const double t = from + (back ? -frame : frame) * i;
        const Layer* l = ctx.comp().findLayer(layerId);
        if (!l || !reader->frameAt(l->media.sourceTime(t), rgba)) break;
        GrayImage gray = grayFromRgba(rgba.data(), reader->width(), reader->height());
        if (i == 0) {
            ref = gray;
            refAt = at;
        } else {
            const TrackResult r = trackPyramid(ref, gray, refAt, at + velocity, patch, search);
            if (r.confidence < match) { // the detail went (covered, out of the picture, blurred)
                lost = true;
                break;
            }
            velocity = (r.point - at) * 0.7f + velocity * 0.3f; // its motion, smoothed: where to look next
            at = r.point;
            if (r.confidence > std::max(0.85f, match)) {
                ref = gray;
                refAt = at;
            }
        }
        path.emplace_back(t, Vec2{at.x / s - size.x * 0.5f, at.y / s - size.y * 0.5f});
    }
    progress.setValue(frames);
    if (path.size() < 2) {
        QMessageBox::information(parent, tr("Track Point"), tr("The point could not be followed: pick a detail with contrast (a corner, a spot)."));
        return;
    }
    std::sort(path.begin(), path.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const Layer* l = ctx.comp().findLayer(layerId);
    if (!l) return;
    // Onto the layer chosen in Tool Settings (it keeps its place and moves as the point does), or a
    // small ring made to follow it.
    Layer* target = ctx.trackTarget && ctx.trackTarget != layerId ? ctx.comp().findLayer(ctx.trackTarget) : nullptr;
    const Vec2 start = l->matrixAt(path.front().first).apply(path.front().second);
    Vec2 offset{};
    if (target) {
        offset = target->transform.position.valueAt(path.front().first) - start;
    } else {
        Layer& marker = ctx.addShape(ShapeKind::Ellipse, start);
        marker.name = tr("Track Point").toStdString();
        marker.shape.size = AnimVec2{Vec2{28.0f, 28.0f}};
        marker.shape.fillEnabled = false;
        marker.shape.strokeEnabled = true;
        marker.shape.strokeWidth = AnimFloat{3.0f};
        marker.shape.strokeColor = AnimColor{Color::fromHex(0xE8B931)};
        marker.inPoint = l->inPoint;
        marker.outPoint = l->outPoint;
        target = &marker;
    }
    auto props = target->properties();
    if (PropertyRef* pos = target->findProperty(props, "position"))
        for (const auto& [t, local] : path) {
            const Vec2 c = l->matrixAt(t).apply(local) + offset;
            pos->anim->addKeyAt(t);
            setPropertyValue(*pos, t, {c.x, c.y});
        }
    const QString name = QString::fromStdString(target->name);
    ctx.select(target->id);
    ctx.touch();
    const QString done = tr("Followed for %1 frames.").arg(path.size()) +
                         (lost ? " " + tr("It was lost after that: try a detail with more contrast, or track again from there.") : QString());
    QMessageBox::information(parent, tr("Track Point"), done + "\n\n" + tr("\"%1\" now moves with it (Position keyframes).").arg(name));
}

} // namespace bemotion::ui
