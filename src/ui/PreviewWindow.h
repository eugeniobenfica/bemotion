#pragma once

// Full screen preview: only the picture, on black, as it will be exported (no handles, guides
// or panels). It plays from the playhead; Space plays / pauses, the arrows step, Home / End,
// a click on the bar seeks, Esc (or a double-click) goes back to the editor. The bar and the
// cursor show while the mouse moves or the playback is paused.

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
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    bool barShown() const;
    QRectF barTrack() const; // the progress line, where a click seeks
    void seek(double x);
    void wake(); // the mouse moved: the bar and the cursor show for a while

    EditorContext& ctx_;
    QImage frame_;
    uint64_t drawnRevision_ = ~0ull;
    double drawnTime_ = -1.0;
    bool drawnBar_ = false;
    QElapsedTimer sinceMouse_;
};

} // namespace ui
} // namespace bemotion
