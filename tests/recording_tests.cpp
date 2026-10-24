#include "media/RecordingBuffer.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using bemotion::media::RecordingBuffer;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        RecordingBuffer buffer;
        check(buffer.preview().peaks.empty(), "empty recording has no waveform");
        std::vector<float> input(479, 0.0f);
        input[17] = -0.75f;
        buffer.append(input);
        auto preview = buffer.preview();
        check(preview.peaks.size() == 1 && preview.peaks[0] == 0.75f, "partial bins preserve negative impulses");
        check(std::abs(preview.seconds - 479.0 / 48000) < 1e-9, "duration follows captured samples");
        buffer.append(std::vector<float>{0.5f, 0.25f});
        preview = buffer.preview();
        check(preview.peaks.size() == 2 && preview.peaks[0] == 0.75f && preview.peaks[1] == 0.25f,
              "callback boundaries keep the same waveform bins");
        buffer.append(std::vector<float>{2.0f, std::numeric_limits<float>::quiet_NaN()});
        check(buffer.preview().peaks[1] == 1.0f, "invalid and clipped capture samples stay bounded");
        check(buffer.samples().size() == 483, "preview does not consume captured audio");
        buffer.clear();
        check(buffer.seconds() == 0.0 && buffer.preview().peaks.empty(), "a new take clears waveform and duration");
        std::vector<float> tone(48000);
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f);
        buffer.append(tone);
        preview = buffer.preview();
        check(preview.seconds == 1.0 && preview.peaks.size() == 100, "one second produces 100 envelope bins");
        for (float peak : preview.peaks) check(peak > 0.49f && peak <= 0.5f, "tone envelope remains visible");
        auto captured = buffer.takeSamples();
        check(captured.size() == 48000 && buffer.seconds() == 0.0 && buffer.preview().peaks.empty(),
              "finalizing a take preserves the audio and clears its live preview");
        std::cout << "recording buffer tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
