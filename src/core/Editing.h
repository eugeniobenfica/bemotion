#pragma once

// Timeline edits the editor and the operations share: Premiere's ripple delete and ripple
// trim, rolling edits, slips, a clip's speed and direction, time remapping, frozen frames
// and markers. Each keeps the composition valid (tracks never overlap); compounds bring
// their contents along when they move.

#include "core/Project.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bemotion {

enum class ClipEdge { In, Out };

// Removes the clips and closes the gaps they leave: the later clips on each one's track move
// back by its length. Returns how many clips went.
int rippleDelete(Composition& comp, const std::vector<uint64_t>& ids);

// Closes the empty space on `track` around `t`: the clips after it move back to where the clip
// before it ends (or to 0). False when `t` is not in a gap with something after it.
bool closeGap(Composition& comp, uint64_t track, double t);

// Ripple trim: moves one edge of the clip to `t` and shifts the later clips on its track by as
// much, so no gap opens or closes. A trimmed in point keeps the clip starting where it did
// (what was before `t` is cut away). False when nothing changed.
bool rippleTrim(Composition& comp, uint64_t id, ClipEdge edge, double t);

// Rolling edit: moves the cut between `left` and the clip right after it on its track to `t`,
// both edges together (the total length stays). False without such a neighbor.
bool rollEdit(Composition& comp, uint64_t left, double t);
// The clip that starts where `left` ends, on its track (0: none).
uint64_t clipAfterCut(const Composition& comp, uint64_t left);

// Slip: the clip stays where it is and shows other moments of its content: the source (a
// video's frames) and its keyframes move by `delta` seconds (positive: later), within the
// source. Returns the delta used.
double slipClip(Composition& comp, uint64_t id, double delta);

// A video or audio clip's speed (1 = 100%, from 0.01 to 100) and direction. The source frame at
// the clip's start stays (when reversing: the frame at its end comes first), and the clip
// gets longer or shorter for the same footage; `ripple` moves the later clips on its track
// along, else it stops at the next one. Time Remap goes off. False for other layers.
bool setClipSpeed(Composition& comp, uint64_t id, double speed, bool reversed, bool ripple = false);
// Time Remap on: keyframes at the clip's ends with the source seconds shown there (linear),
// to reshape freely; off: back to its speed and direction.
bool setTimeRemap(Composition& comp, uint64_t id, bool on);
// Freezes the clip on the frame it shows at `t` (Premiere's Frame Hold): Time Remap with one value.
bool freezeFrame(Composition& comp, uint64_t id, double t);
// Separate Audio (Premiere's Unlink): a video clip with sound keeps its picture, and its sound
// becomes a clip of its own on a new track right below (the same media, time, speed, volume
// and audio effects), to move, trim or delete apart. Returns the audio clip's id (0: the
// layer is not a video with sound).
uint64_t separateAudio(Composition& comp, uint64_t id);
// Link Audio, the other way: a video clip without sound takes the sound of a sound-only clip of
// the same media (its volume, audio effects) and that clip goes. The video's timing decides;
// `inSync` says whether the two played the same moment. False when they do not fit.
bool linkAudio(Composition& comp, uint64_t video, uint64_t audio, bool* inSync = nullptr);
// A sound-only clip of `video`'s media to link it with: a selected one in `preferred` first,
// else the one overlapping it most in time (0: none).
uint64_t audioToLink(const Composition& comp, uint64_t video, const std::vector<uint64_t>& preferred = {});
// Cuts quiet stretches out of a video or audio clip (silence or near silence between words):
// `quiet` are [from, to) source seconds (dsp::quietStretches over its sound). The clip is split
// around each one it shows and those pieces go; with `ripple` the pieces after them (and the
// clips after it on its track) move back to close the gaps. Clips playing reversed or with
// Time Remap are left alone. Returns the seconds removed (0: none).
double removeQuietParts(Composition& comp, uint64_t id, const std::vector<std::pair<double, double>>& quiet, bool ripple = true);
// True for clips these apply to: video and audio (not stills).
bool hasTimeline(const Composition& comp, const Layer& layer);

// Markers (kept sorted): one at `t` (snapped to a frame), or the one there renamed. Returns its index.
int addMarker(Composition& comp, double t, const std::string& name, int color = -1);
int markerAt(const Composition& comp, double t); // the marker within half a frame of `t`, or -1
// Moves marker `index` to `t` (merging with one already there); returns its new index.
int moveMarker(Composition& comp, int index, double t);
// The next / previous marker time after / before `t` (false: none).
bool nextMarker(const Composition& comp, double t, bool forward, double& out);

} // namespace bemotion
