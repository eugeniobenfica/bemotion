#pragma once

// Full screen preview: only the picture, on black, as it will be exported (no handles, guides
// or panels). It plays from the playhead; Space plays / pauses, the arrows step, Home / End, L loops,
// Esc (or a double-click) goes back to the editor. The bar at the bottom has the same as
// buttons (step, play, the progress to click or drag, loop, exit); it and the cursor show
// while the mouse moves or the playback is paused.

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <cstdint>

namespace bemotion {

class EditorContext;

namespace ui {

class PreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWindow(EditorContext& ctx, QWidget* parent = nullptr);

    void sync(); // the editor's tick: redraws when the frame or the bar changed

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    enum class Part { None, StepBack, Play, StepForward, Track, Loop, Exit };
    bool barShown() const;
    QRectF barTrack() const;        // the progress line, where a click seeks
    QPointF partCenter(Part) const; // the round buttons
    Part partAt(QPointF pos) const;
    void seek(double x);
    void wake(); // the mouse moved: the bar and the cursor show for a while

    EditorContext& ctx_;
    QImage frame_;
    uint64_t drawnRevision_ = ~0ull;
    double drawnTime_ = -1.0;
    bool drawnBar_ = false;
    QElapsedTimer sinceMouse_;
    Part hover_ = Part::None;
    bool seeking_ = false; // dragging along the progress line
};

} // namespace ui
} // namespace bemotion
