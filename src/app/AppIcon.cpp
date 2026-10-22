#include "app/AppIcon.h"

#include "render/LogoMark.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include <stb_image_write.h>

namespace bemotion {

namespace {

float roundedBoxDistance(float px, float py, float half, float radius) {
    const float qx = std::fabs(px) - half + radius, qy = std::fabs(py) - half + radius;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

} // namespace

std::vector<uint8_t> renderAppIcon(int size) {
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4);
    const float s = static_cast<float>(size);
    const float markR = s * 0.335f; // the mark's disc radius

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float fx = x + 0.5f - s * 0.5f, fy = y + 0.5f - s * 0.5f;
            const float box = roundedBoxDistance(fx, fy, s * 0.47f, s * 0.22f);
            const float boxA = std::clamp(0.5f - box, 0.0f, 1.0f);
            const float d = logo::distance({fx / markR, fy / markR}) * markR; // in pixels
            const float markA = std::clamp(0.5f - d, 0.0f, 1.0f);

            // Background #141417, mark #E8B931 (the accent).
            const float r = 20 + (232 - 20) * markA, g = 20 + (185 - 20) * markA, b = 23 + (49 - 23) * markA;
            uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 4];
            p[0] = static_cast<uint8_t>(r);
            p[1] = static_cast<uint8_t>(g);
            p[2] = static_cast<uint8_t>(b);
            p[3] = static_cast<uint8_t>(boxA * 255.0f);
        }
    return px;
}

namespace {

std::vector<uint8_t> encodePng(const std::vector<uint8_t>& rgba, int size) {
    std::vector<uint8_t> png;
    stbi_write_png_to_func(
        [](void* context, void* data, int n) {
            auto* out = static_cast<std::vector<uint8_t>*>(context);
            out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + n);
        },
        &png, size, size, 4, rgba.data(), size * 4);
    return png;
}

} // namespace

bool writeAppIcon(const std::filesystem::path& file, bool markOnly) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (file.extension() == ".svg") { // same geometry as the raster icon (512 units)
        const float s = 512.0f, r = markOnly ? s * 0.48f : s * 0.335f;
        char buf[160];
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\" width=\"512\" height=\"512\">\n";
        if (!markOnly) out << "  <rect x=\"15.36\" y=\"15.36\" width=\"481.28\" height=\"481.28\" rx=\"112.64\" fill=\"#141417\"/>\n";
        out << "  <path fill=\"#E8B931\" d=\"";
        for (const auto& shape : logo::outlines(192)) {
            for (size_t i = 0; i < shape.size(); ++i) {
                std::snprintf(buf, sizeof buf, "%s%.2f %.2f ", i == 0 ? "M" : "L", s * 0.5f + shape[i].x * r, s * 0.5f + shape[i].y * r);
                out << buf;
            }
            out << "Z ";
        }
        out << "\"/>\n</svg>\n";
        return static_cast<bool>(out);
    }
    if (file.extension() != ".ico") {
        const auto png = encodePng(renderAppIcon(1024), 1024);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return static_cast<bool>(out);
    }
    // ICO with PNG-compressed entries (Windows Vista and later).
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::vector<std::vector<uint8_t>> images;
    for (int s : sizes) images.push_back(encodePng(renderAppIcon(s), s));
    auto u16 = [&](uint16_t v) { out.put(static_cast<char>(v & 255)).put(static_cast<char>(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>(v >> 16)); };
    const int count = static_cast<int>(std::size(sizes));
    u16(0);
    u16(1); // icon
    u16(static_cast<uint16_t>(count));
    uint32_t offset = 6 + 16 * count;
    for (int i = 0; i < count; ++i) {
        out.put(static_cast<char>(sizes[i] >= 256 ? 0 : sizes[i])).put(static_cast<char>(sizes[i] >= 256 ? 0 : sizes[i]));
        out.put(0).put(0); // no palette, reserved
        u16(1);  // planes
        u16(32); // bits per pixel
        u32(static_cast<uint32_t>(images[i].size()));
        u32(offset);
        offset += static_cast<uint32_t>(images[i].size());
    }
    for (const auto& img : images) out.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    return static_cast<bool>(out);
}

} // namespace bemotion
