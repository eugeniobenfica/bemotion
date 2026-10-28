#pragma once

// Sound processing behind the audio effects that change a clip's samples as a whole: EQ and
// Noise Reduction run once over the clip's decoded audio (in the background, see
// media/MediaManager), never per playback block, so playback and export hear the same thing.
// Ducking only needs how loud the other clips are: loudness() every 10 ms.

#include "core/Effects.h"

#include <string>
#include <utility>
#include <vector>

namespace bemotion::dsp {

// Three bands on interleaved stereo: a low shelf at 100 Hz, a bell at 1 kHz and a high shelf
// at 8 kHz, each `dB` up or down (0: untouched).
void equalize(std::vector<float>& stereo, double rate, float lowDb, float midDb, float highDb);

// Lowers steady background noise (hum, hiss, fans): the noise's level in each frequency is
// learned from a sample of only the noise ([sampleFrom, sampleTo) seconds of the buffer) or,
// without one, from the quietest moments, and what stays near it is turned down by up to
// `reductionDb`. `sensitivity` (0..100) is how far above the noise still counts as noise.
void reduceNoise(std::vector<float>& stereo, double rate, float reductionDb, float sensitivity, double sampleFrom = -1.0,
                 double sampleTo = -1.0);

// A room around the sound (Schroeder / Freeverb: eight combs and four all-passes per side).
// `mix` (0..100) of the wet sound, `size` (0..100) how long it rings, `damping` (0..100) how
// soon the highs die away.
void reverb(std::vector<float>& stereo, double rate, float mix, float size, float damping);

// Repeats of the sound `delaySeconds` apart, each `feedback` (0..90 %) of the last, mixed in at
// `mix` (0..100 %).
void echo(std::vector<float>& stereo, double rate, float delaySeconds, float feedback, float mix);

// Evens out loud and quiet moments: above `thresholdDb` the level rises `ratio` times slower
// (quick attack, slower release), then `makeupDb` brings the whole up again.
void compress(std::vector<float>& stereo, double rate, float thresholdDb, float ratio, float makeupDb);

// Cuts what is below `lowCutHz` (rumble, wind) and above `highCutHz` (hiss): 12 dB per octave
// each; at the ends of the range (20 Hz, 20 kHz) they do nothing.
void bandLimit(std::vector<float>& stereo, double rate, float lowCutHz, float highCutHz);

// Automatic levels: the clip brought to `targetDb` (RMS of its sound, silence left out), and
// with `evenOut` (0..100 %) its quiet and loud passages brought toward that level too (a slow
// gain rider). A limiter keeps peaks under -1 dBFS. Clips leveled to the same target sound
// equally loud.
void autoLevel(std::vector<float>& stereo, double rate, float targetDb, float evenOut);

// RMS loudness (0..1) of every block of `blockFrames` stereo frames.
std::vector<float> loudness(const std::vector<float>& stereo, size_t blockFrames);

// Silence detection on a loudness envelope (one value every `blockSeconds`): the stretches
// quieter than `thresholdDb` for at least `minSeconds`, as [start, end) seconds from the
// envelope's start, each shrunk by `padSeconds` at both ends (what is left of short ones goes).
std::vector<std::pair<double, double>> quietStretches(const std::vector<float>& loudness, double blockSeconds, float thresholdDb,
                                                      double minSeconds, double padSeconds);
// A threshold learned from a sample of the silence (its envelope values between [from, to)
// seconds): its loudest moment, a few dB up. -100 when the sample is empty.
float thresholdFromSample(const std::vector<float>& loudness, double blockSeconds, double from, double to);

// The stack's effects that change the samples as a whole (EQ, Noise Reduction, Reverb, Echo,
// Compressor, Low / High Cut, Auto Level), applied in order. processKey names that
// processing (empty: none), so clips with the same settings share one result.
bool changesSamples(const std::vector<AudioEffect>& effects);
std::string processKey(const std::vector<AudioEffect>& effects);
void process(std::vector<float>& stereo, double rate, const std::vector<AudioEffect>& effects);

} // namespace bemotion::dsp
