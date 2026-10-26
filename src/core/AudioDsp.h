#pragma once

// Sound processing behind the audio effects that change a clip's samples as a whole: EQ and
// Noise Reduction run once over the clip's decoded audio (in the background, see
// media/MediaManager), never per playback block, so playback and export hear the same thing.
// Ducking only needs how loud the other clips are: loudness() every 10 ms.

#include "core/Effects.h"

#include <string>
#include <vector>

namespace bemotion::dsp {

// Three bands on interleaved stereo: a low shelf at 100 Hz, a bell at 1 kHz and a high shelf
// at 8 kHz, each `dB` up or down (0: untouched).
void equalize(std::vector<float>& stereo, double rate, float lowDb, float midDb, float highDb);

// Lowers steady background noise (hum, hiss, fans): the noise's level in each frequency is
// learned from the quietest moments, and what stays near it is turned down by up to
// `reductionDb`. `sensitivity` (0..100) is how far above the noise still counts as noise.
void reduceNoise(std::vector<float>& stereo, double rate, float reductionDb, float sensitivity);

// RMS loudness (0..1) of every block of `blockFrames` stereo frames.
std::vector<float> loudness(const std::vector<float>& stereo, size_t blockFrames);

// The stack's EQ and Noise Reduction effects, applied in order. processKey names that
// processing (empty: none), so clips with the same settings share one result.
bool changesSamples(const std::vector<AudioEffect>& effects);
std::string processKey(const std::vector<AudioEffect>& effects);
void process(std::vector<float>& stereo, double rate, const std::vector<AudioEffect>& effects);

} // namespace bemotion::dsp
