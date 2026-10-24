#pragma once

#include "ui/EditorContext.h"
#include "media/RecordingBuffer.h"

#include <QPointF>
#include <QWidget>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QHBoxLayout;

namespace bemotion {

class PropertyRow;
class TimelineRuler;
class TimelineRows;
class ToolPill;
namespace ui {
class Draw;
class IconButton;
}

// Bottom panel: toolbar (the user's tools, snapping, transport), time ruler,
// track rows with their clips and keyframes, and the graph editor.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(EditorContext& ctx, QWidget* parent = nullptr);
    void sync(); // from the editor's tick
    void setRecordingPreview(double start, uint64_t scope, media::RecordingPreview preview);
    void clearRecordingPreview();

    enum class Ease { Linear, EasyEase, EaseIn, EaseOut, Hold };
    // Applies an easing to the selected keyframes (F9 = Easy Ease, like After Effects).
    static void applyEase(EditorContext& ctx, Ease ease);

signals:
    void toolsPanelRequested(); // the toolbar's "+" or Customize...
    // The toolbar was dragged (or sent) to the viewport (at `at`, viewport coordinates; (-1, -1):
    // its default place) or back to the timeline.
    void toolbarMoveRequested(bool toViewport, QPoint at);
    void toolbarDragActive(bool active);
    void toolbarDockRequested(QPointF position);
    // A plugin command's button (API 5) was clicked; `pressed`: its state for toggle buttons.
    void pluginButtonClicked(const std::string& commandId, bool pressed);

public:
    QWidget* toolPill() const;
    void setToolPillVertical(bool vertical);
    void setPluginButtonPressed(const std::string& commandId, bool pressed);
    void rebuildPluginButtons();
    void releaseToolPill(); // out of the timeline's bar (it floats elsewhere)
    void dockToolPill();    // back into it
    bool deleteSelectedTransition();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    friend class TimelineRuler;
    friend class TimelineRows;
    friend class ToolPill;

    // Each timeline row is a track that may hold several clips (layers).
    //   LayerName  drag the row name: reorders whole tracks
    //   LayerMove  drag a clip: moves it in time and, vertically, onto another
    //              track (drop in a row) or onto a new track (drop between rows)
    //   LayerIn/Out  trim a clip edge
    enum class Drag { None, Keys, LayerName, LayerMove, LayerIn, LayerOut, TransitionIn, TransitionOut, Playhead };

    // One laid out row of the body.
    struct Item {
        enum Kind { Track, Group, Prop, Recording } kind = Track;
        float top = 0, h = 0;
        int rowIndex = 0;           // Track: position among the visible tracks
        uint64_t track = 0;
        int first = 0, count = 0;   // Track: its clips in Composition::layers
        uint64_t layer = 0;         // Track: the focus clip; Prop: the layer
        std::string prop;           // Prop: property id
        int effectIndex = -1;
        std::string groupKey;       // Group: "track/group"
        std::string groupLabel;
        float indent = 0;
    };
    struct ClipRect {
        uint64_t id;
        float top, bottom, x1, x2;
    };
    struct TransitionHit {
        uint64_t layer = 0;
        bool incoming = true;
        bool resize = false;
        explicit operator bool() const { return layer != 0; }
    };
    // Where a transition dragged in from the Transitions panel would go.
    struct TransitionDrop {
        enum class Spot { In, Out, Cut };
        uint64_t layer = 0; // the clip (for a cut: the one before it)
        Spot spot = Spot::In;
        bool ok = false;    // the clip (or both clips of the cut) can take it
    };
    // Where a vertical drag would land.
    struct DropTarget {
        bool valid = false;
        bool newTrack = true; // true: new track at `gap`; false: join `track`
        int gap = 0;
        uint64_t track = 0;
        bool fits = true;     // joining is only possible without overlaps
    };

    void buildToolbar(QHBoxLayout* bar);
    void syncBreadcrumb();
    void clampView();
    void computeItems();
    void placeRowWidgets();
    void relayout(); // after the document or the view changed: items, row widgets, repaint

    QWidget* rowsWidget() const;
    float trackX0() const { return leftWidth_; }
    float trackW() const;
    float timeToX(double t) const { return trackX0() + static_cast<float>((t - scroll_) * pxPerSec_); }
    double xToTime(float x) const { return scroll_ + (x - trackX0()) / pxPerSec_; }
    void zoomAround(float x, double factor);

    // Painting.
    void paintRuler(QPainter& p, int width);
    void paintRows(QPainter& p, QSize size);
    void paintTrack(QPainter& p, const Item& item, float width);
    void paintRecording(QPainter& p, const Item& item, float width);
    bool hasRecordingPreview() const { return recordingPreviewActive_ && recordingScope_ == ctx_.scope; }
    double visibleEnd() const;
    bool recordingPreviewActive_ = false;
    double recordingStart_ = 0.0;
    uint64_t recordingScope_ = 0;
    media::RecordingPreview recordingPreview_;
    void paintGroup(QPainter& p, const Item& item, float width);
    void paintPropKeys(QPainter& p, const Item& item, float width);
    void paintWaveform(QPainter& p, const Layer& layer, QPointF a, QPointF b, uint32_t color) const;
    void paintGraph(QPainter& p, QSize size);

    // Input on the body.
    void rowsPress(QMouseEvent* e);
    void rowsMove(QMouseEvent* e);
    void rowsRelease(QMouseEvent* e);
    void rowsDoubleClick(QMouseEvent* e);
    void wheel(QWheelEvent* e);
    const Item* itemAt(float y) const;
    int clipHit(const Item& track, float x, Drag& mode) const;
    TransitionDrop transitionDropAt(QPointF pos, const std::string& transition) const;
    void paintTransitionDrop(ui::Draw& d);
    TransitionHit transitionHit(const Item& track, float x) const;
    void clickLayer(uint64_t id);
    void clickTrack(const Item& track);
    void showTrackMenu(QPoint global);
    void showKeyMenu(QPoint global);
    void startRename(const Item& track);

    void beginLayerDrag(Drag mode, uint64_t id, QPointF pos);
    void updateLayerDrag(QPointF pos);
    void endLayerDrag(QPointF pos);
    int dropGapAt(float y) const; // insertion gap (0..rowCount) under y
    DropTarget dropTargetAt(float y, Drag mode) const;
    void beginMarquee(QPointF pos);
    void updateMarquee(QPointF pos);
    void beginKeyDrag(QPointF pos);
    void updateKeyDrag(QPointF pos);
    void endKeyDrag();

    // Snapping: targets are gathered when a drag starts; snapLineTime_ marks
    // the time that snapped (drawn as a pink line), or -1.
    void collectTimeSnapTargets(const std::vector<uint64_t>& movingClips, bool movingKeys);
    // Moves `base` times by `rawDelta`, snapped to a target if one is close, else to whole frames.
    double snappedDelta(const std::vector<double>& base, double rawDelta, bool force = false);

    // Object / media drops from the other panels.
    bool dropTimeAndGap(QPointF pos, double& t, int& gap);

    // Graph editor (TimelineGraph.cpp): value curves of the primary layer's animated
    // properties, like After Effects' Graph Editor: drag keyframes (time / value) and
    // the easing handles of each segment (they edit its cubic-bezier timing curve).
    struct GraphHit {
        enum Kind { None, Key, Handle } kind = None;
        std::string prop;
        int comp = 0;     // component (x / y)
        uint32_t uid = 0; // key (Key) or segment start key (Handle)
        int which = 0;    // Handle: 1 = outgoing (x1, y1), 2 = incoming (x2, y2)
        QPointF pos;
        std::string label;
    };
    std::vector<GraphHit> graphHits(); // also fits the value range
    const GraphHit* graphHitAt(const std::vector<GraphHit>& hits, QPointF m) const;
    void graphPress(QMouseEvent* e);
    void graphMove(QMouseEvent* e);
    void graphRelease(QMouseEvent* e);
    void showGraphMenu(QPoint global, QPointF at); // right-click on the graph
    std::optional<std::pair<std::string, int>> curveNear(QPointF m);
    void addGraphKeys(const std::vector<std::string>& propIds, double t); // new keys keep the curve's value
    void addKeysAtPlayhead();
    float graphY(float v) const;
    float graphValue(float y) const;
    bool graphMode_ = false;
    std::set<std::string> graphHidden_; // property ids switched off in the graph
    float graphMin_ = 0.0f, graphMax_ = 1.0f, graphTop_ = 0.0f, graphBottom_ = 1.0f;
    struct GraphDrag {
        GraphHit::Kind kind = GraphHit::None;
        std::string prop;
        int comp = 0;
        uint32_t uid = 0;
        int which = 0;
        QPointF startMouse;
        double startTime = 0.0;
        float startValue = 0.0f;
        bool moved = false;
    } graphDrag_;

    EditorContext& ctx_;
    QLabel* timecode_ = nullptr;
    QLabel* frameInfo_ = nullptr;
    ui::IconButton* snap_ = nullptr;
    ui::IconButton* play_ = nullptr;
    ui::IconButton* loop_ = nullptr;
    ui::IconButton* graph_ = nullptr;
    ui::IconButton* animOnly_ = nullptr;
    ui::IconButton* addKeys_ = nullptr; // graph mode: keyframes at the playhead
    QHBoxLayout* pluginBar_ = nullptr;  // plugin command buttons
    std::vector<std::pair<std::string, ui::IconButton*>> pluginButtons_;
    std::vector<QPushButton*> easeButtons_;
    ToolPill* pill_ = nullptr;
    QHBoxLayout* bar_ = nullptr;
    QWidget* divider_ = nullptr;
    QWidget* breadcrumb_ = nullptr;
    std::string breadcrumbKey_;
    TimelineRuler* ruler_ = nullptr;
    TimelineRows* rows_ = nullptr;

    float leftWidth_ = 300.0f;
    double pxPerSec_ = 0.0; // 0 = fit on next layout
    double scroll_ = 0.0;   // time at the left edge of the track area
    float scrollY_ = 0.0f;  // body scrolled down by this many pixels
    float contentHeight_ = 0.0f;
    bool animatedOnly_ = false;

    std::vector<Item> items_;
    std::vector<ClipRect> clips_;
    float rowsBottom_ = 0.0f;
    std::set<uint64_t> expanded_;           // track ids showing their property rows
    std::set<std::string> collapsedGroups_; // "track/group"
    std::map<std::string, std::unique_ptr<PropertyRow>> rowWidgets_; // "layer/effect/prop"
    QLineEdit* rename_ = nullptr;
    uint64_t renaming_ = 0;
    std::string signature_;
    int idleSyncs_ = 0;

    Drag drag_ = Drag::None;
    QPointF dragStart_;
    uint64_t dragLayer_ = 0;
    double dragOrigIn_ = 0.0, dragOrigOut_ = 0.0, dragApplied_ = 0.0;
    float dragOrigTransitionDuration_ = 0.0f;
    bool restacking_ = false; // vertical drag past the threshold: show the drop gap
    float dragY_ = 0.0f;
    std::vector<uint64_t> dragMoving_; // clips moving in time (a compound brings its contents)
    struct ClipStart {
        uint64_t id;
        double in, out;
    };
    std::vector<ClipStart> dragClips_;
    std::vector<std::pair<KeySelection, double>> dragKeys_;
    bool keysMoved_ = false;
    double playheadGrab_ = 0.0;
    double followedTime_ = -1.0; // playhead time the view last followed (see sync)
    std::vector<double> timeTargets_;
    double snapLineTime_ = -1.0;
    TransitionDrop transitionDrop_; // while a transition is dragged over the rows
    uint64_t selectedTransitionLayer_ = 0;
    bool selectedTransitionIncoming_ = true;
    // Box selection by dragging on empty timeline space.
    bool marquee_ = false;
    QPointF marqueeStart_, marqueeEnd_;
    std::vector<uint64_t> marqueeBase_;
    float hoverX_ = -1.0f; // razor preview
    bool dropActive_ = false;
    double dropTime_ = 0.0;
    int dropGap_ = 0;
    double dropLength_ = 0.0;
};

} // namespace bemotion
