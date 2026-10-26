#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <cstdint>
#include <vector>

class QComboBox;
class QTimer;

namespace bemotion {

class EditorContext;

// Lumetri-style scopes of the frame at the playhead: the luma waveform, the RGB parade, the
// vectorscope (hue and saturation, with the color bar targets and the skin tone line) and the
// histogram. The frame is rendered small for them (as the composition is, through its camera),
// at most a few times a second while it plays.
class ScopesPanel : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Waveform, Parade, Vectorscope, Histogram };
    explicit ScopesPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void renderFrame();
    QRectF scopeRect() const;

    EditorContext& ctx_;
    QComboBox* mode_ = nullptr;
    QTimer* later_ = nullptr; // the last frame, once playback or a drag stops
    QElapsedTimer sinceRender_;
    uint64_t revision_ = 0;
    double time_ = -1.0;
    uint64_t arrivals_ = 0; // a scrubbed video frame decoded since
    bool valid_ = false;
    int width_ = 0, height_ = 0;
    std::vector<uint8_t> rgba_; // the frame, straight RGBA
};

} // namespace bemotion
