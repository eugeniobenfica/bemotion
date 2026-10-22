// PNG image sequence: <name>_0000.png, <name>_0001.png, ... next to the chosen file.

#include "export/Export.h"

#include "app/Platform.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <fstream>

namespace bemotion::exporting {

namespace {

class PngSequenceEncoder final : public Encoder {
public:
    bool open(const EncoderParams& params, std::string& error) override {
        params_ = params;
        folder_ = params.output.parent_path();
        stem_ = platform::toUtf8(params.output.stem());
        std::error_code ec;
        if (!folder_.empty()) std::filesystem::create_directories(folder_, ec);
        if (ec) {
            error = "Could not create the output folder: " + ec.message();
            return false;
        }
        // Image sequences carry no sound: the audio goes next to the frames.
        if (params.audio && !writeWav(folder_ / platform::fromUtf8(stem_ + ".wav"), *params.audio, kExportAudioRate, error))
            return false;
        return true;
    }

    bool write(const uint8_t* rgba, std::string& error) override {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%04d.png", index_++);
        const auto file = folder_ / platform::fromUtf8(stem_ + suffix);
        // stb writes through a callback so paths with any characters work on Windows.
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Could not write " + platform::toUtf8(file);
            return false;
        }
        const int channels = params_.alpha ? 4 : 3;
        const uint8_t* data = rgba;
        if (channels == 3) { // drop the (opaque) alpha channel: smaller files
            rgb_.resize(static_cast<size_t>(params_.width) * params_.height * 3);
            for (size_t i = 0, j = 0; j < rgb_.size(); i += 4, j += 3) {
                rgb_[j] = rgba[i];
                rgb_[j + 1] = rgba[i + 1];
                rgb_[j + 2] = rgba[i + 2];
            }
            data = rgb_.data();
        }
        auto sink = [](void* context, void* bytes, int size) {
            static_cast<std::ofstream*>(context)->write(static_cast<const char*>(bytes), size);
        };
        const int ok = stbi_write_png_to_func(sink, &out, params_.width, params_.height, channels, data,
                                              params_.width * channels);
        if (!ok || !out) {
            error = "Could not encode " + platform::toUtf8(file);
            return false;
        }
        return true;
    }

    bool finish(std::string&) override { return true; }

private:
    EncoderParams params_{};
    std::filesystem::path folder_;
    std::string stem_;
    std::vector<uint8_t> rgb_;
    int index_ = 0;
};

} // namespace

std::unique_ptr<Encoder> makePngSequenceEncoder() { return std::make_unique<PngSequenceEncoder>(); }

} // namespace bemotion::exporting
