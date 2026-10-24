#pragma once

#include "core/Layer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bemotion {

// A ruler guide in composition pixels (vertical: x = position; else y).
// How far past the end of the composition a clip may run (seconds).
inline constexpr double kMaxOverhang = 3600.0;
// Length of a new object (like Premiere's default still duration). The composition
// has no fixed length: it ends where its last clip ends (see fitDuration).
inline constexpr double kDefaultLayerLength = 5.0;
inline constexpr double kMinCompDuration = 1.0;

struct Guide {
    bool vertical = true;
    float position = 0.0f;
};

// A timeline row: a contiguous run of `layers` sharing one track id.
struct TrackRow {
    uint64_t track = 0;
    int first = 0; // index of the first layer of the track in Composition::layers
    int count = 0;
};

struct Composition {
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    double duration = 10.0; // seconds; follows the content (fitDuration)
    double maxDuration = 0.0; // seconds; 0: no limit, else the composition never grows past it
    Color background = Color::fromHex(0x101012);

    // Rendering order: index 0 is the top-most layer. Layers of the same
    // track are always kept next to each other (see normalizeTracks).
    std::vector<Layer> layers;
    uint64_t nextLayerId = 1;
    uint64_t nextTrackId = 1;
    std::vector<Guide> guides;
    std::vector<MediaItem> media; // imported footage (see core/Media.h)
    uint64_t nextMediaId = 1;
    MediaItem* findMedia(uint64_t id);
    const MediaItem* findMedia(uint64_t id) const;
    uint64_t addMedia(MediaItem item); // reuses an item already imported from the same path

    // Makes the composition end where its last clip ends (at least kMinCompDuration, at most
    // maxDuration when set).
    void fitDuration();

    double frameDuration() const { return 1.0 / fps; }
    int frameCount() const;
    int frameAt(double t) const;
    double timeOfFrame(int frame) const { return frame / fps; }
    double snapToFrame(double t) const { return timeOfFrame(frameAt(t)); }
    Vec2 center() const { return {width * 0.5f, height * 0.5f}; }

    // Scene -> composition transform of the top-most active camera (identity without one).
    Affine cameraAt(double t) const;
    // Scene -> composition transform for `layer` as the camera sees it: cameraAt for 2D
    // layers; for 3D layers (and what is inside a 3D compound) the camera's perspective on the
    // layer's plane: at its Z position, turned by its X / Y rotation around its position
    // (`tilted` = false: the plane at that Z, unturned, where its position moves). `out` keeps
    // w as the distance from the camera in near-plane units: w < 1 is too close or behind.
    // False when the whole layer is behind the camera. Without a camera, 3D layers are seen by
    // a default one (center, Z 0, zoom 100%). throughCamera = false: the free view, from the
    // front without perspective (2D layers as they are, 3D layers only turned).
    bool viewFor(const Layer& layer, double t, Projective& out, bool throughCamera = true, bool tilted = true) const;
    // How far the camera stands from the layers at its own Z position: a 50 mm lens on the
    // composition width (like After Effects' default camera).
    float focalLength() const { return static_cast<float>(width) * (50.0f / 36.0f); }
    // Top-level active layers in drawing order, bottom first: the stacking order, except that
    // neighboring 3D layers are drawn by distance from the camera, farthest first. Cameras only
    // with `cameras`.
    std::vector<const Layer*> drawOrder(double t, bool cameras = false) const;
    // Gives every camera the current composition size as its frame.
    void syncCameras();

    // --- compounds -----------------------------------------------------------------
    // A compound groups layers (its children, Layer::parent): they move, scale,
    // rotate, fade and take effects together, and stay editable one by one.
    // Layer -> scene transform, through every enclosing compound.
    Affine worldMatrix(const Layer& layer, double t) const;
    Affine parentMatrix(const Layer& layer, double t) const; // identity for top-level layers
    // Visible at `t`: active, and so is every enclosing compound.
    bool visibleAt(const Layer& layer, double t) const;
    bool isInside(const Layer& layer, uint64_t compoundId) const; // directly or nested
    std::vector<uint64_t> descendantsOf(uint64_t compoundId) const;
    // Groups layers into a new compound centered on them; returns its id (0 if none).
    // Their tracks move right below the compound's.
    uint64_t createCompound(const std::vector<uint64_t>& layerIds, double t);
    // Puts the children back where the compound was, keeping how they look (its
    // transform, clip and effects are baked into them), and removes the compound.
    void releaseCompound(uint64_t compoundId, double t);
    // Removes a layer and, for a compound, everything inside it.
    void removeLayerTree(uint64_t id);

    // Inserts a layer, assigning it a fresh id (and a fresh track when its
    // track is 0). `index` is rounded to a track boundary so tracks stay
    // contiguous; the default puts it on top.
    Layer& addLayer(Layer layer, int index = 0);
    Layer* findLayer(uint64_t id);
    const Layer* findLayer(uint64_t id) const;
    int indexOf(uint64_t id) const;
    void removeLayer(uint64_t id);
    std::string uniqueLayerName(const std::string& base) const;

    // --- tracks (timeline rows) ---------------------------------------------------
    std::vector<TrackRow> trackRows() const;
    int trackRowOf(uint64_t layerId) const; // row index of the layer's track, or -1
    // Groups layers of the same track together, ordered by first appearance.
    void normalizeTracks();

    // Moves whole tracks, keeping their order, so they start at row
    // `targetRow` of the list *without* them.
    void moveTracks(const std::vector<uint64_t>& trackIds, int targetRow);
    // Moves each of the tracks `delta` rows (negative = towards the top).
    void shiftTracks(const std::vector<uint64_t>& trackIds, int delta);
    // Takes the given clips out of their tracks and puts them on new tracks
    // inserted at row gap `gapRow` (0 = above the first row). Clips that came
    // from the same track stay together. Empty tracks disappear.
    void moveClipsToNewTracks(const std::vector<uint64_t>& layerIds, int gapRow);
    // True if the clips can join `trackId` without overlapping its other clips (or each other).
    bool clipsFitTrack(const std::vector<uint64_t>& layerIds, uint64_t trackId) const;
    // Moves the clips onto an existing track. Returns false (and changes nothing) if they don't fit.
    bool moveClipsToTrack(const std::vector<uint64_t>& layerIds, uint64_t trackId);
    // How far the clips may be shifted in time without leaving the
    // composition or overlapping other clips on their tracks.
    void timeShiftLimits(const std::vector<uint64_t>& layerIds, double& minDelta, double& maxDelta) const;
    // End of the previous clip and start of the next clip on the same track.
    void neighborBounds(uint64_t layerId, double& prevOut, double& nextIn) const;
    // Where a clip may start/end: inside the composition and between its neighbors.
    void clipLimits(uint64_t layerId, double& minIn, double& maxOut) const;
    // Trim a clip edge to `t` within clipLimits; a clip shorter than a frame never grows over a neighbor.
    void setInPoint(uint64_t layerId, double t);
    void setOutPoint(uint64_t layerId, double t);

    // Cuts a layer in two at time `t`. The original keeps [in, t), a copy on
    // the same track gets [t, out). Returns the copy's id, or 0 when `t` is
    // not strictly inside the layer.
    uint64_t splitLayer(uint64_t id, double t);

private:
    void rebuild(std::vector<std::vector<Layer>> rows);
};

struct Project {
    static constexpr int kFormatVersion = 1;

    std::string name = "Untitled";
    Composition comp;
};

} // namespace bemotion
