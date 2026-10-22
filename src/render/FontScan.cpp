// Reads family/style names and weight from TrueType/OpenType files.

#include "render/Fonts.h"

#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace bemotion {

namespace {

std::string utf16beToUtf8(const char* s, int len) {
    std::string out;
    for (int i = 0; i + 1 < len; i += 2) {
        unsigned cp = (static_cast<unsigned char>(s[i]) << 8) | static_cast<unsigned char>(s[i + 1]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < len) { // surrogate pair
            const unsigned lo = (static_cast<unsigned char>(s[i + 2]) << 8) | static_cast<unsigned char>(s[i + 3]);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) out += {static_cast<char>(0xC0 | (cp >> 6)), static_cast<char>(0x80 | (cp & 0x3F))};
        else if (cp < 0x10000)
            out += {static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                    static_cast<char>(0x80 | (cp & 0x3F))};
        else
            out += {static_cast<char>(0xF0 | (cp >> 18)), static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                    static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F))};
    }
    return out;
}

std::string nameString(const stbtt_fontinfo& info, int nameId) {
    int len = 0;
    if (const char* s = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                STBTT_MS_LANG_ENGLISH, nameId))
        return utf16beToUtf8(s, len);
    if (const char* s =
            stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, STBTT_MAC_LANG_ENGLISH, nameId))
        return std::string(s, len);
    return {};
}

} // namespace

std::vector<FontFace> scanFontFile(const std::filesystem::path& file) {
    std::vector<FontFace> faces;
    std::ifstream in(file, std::ios::binary);
    if (!in) return faces;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 12) return faces;
    const int count = std::max(1, stbtt_GetNumberOfFonts(data.data()));
    for (int i = 0; i < count; ++i) {
        const int offset = stbtt_GetFontOffsetForIndex(data.data(), i);
        stbtt_fontinfo info;
        if (offset < 0 || !stbtt_InitFont(&info, data.data(), offset)) continue;
        FontFace face;
        face.path = file;
        face.index = i;
        // Typographic names (16/17) group styles better than the legacy ones (1/2).
        face.family = nameString(info, 16);
        if (face.family.empty()) face.family = nameString(info, 1);
        face.style = nameString(info, 17);
        if (face.style.empty()) face.style = nameString(info, 2);
        if (face.style.empty()) face.style = "Regular";
        if (const stbtt_uint32 os2 = stbtt__find_table(data.data(), offset, "OS/2")) {
            face.weight = ttUSHORT(data.data() + os2 + 4);
            face.italic = ttUSHORT(data.data() + os2 + 62) & 1;
        }
        if (face.family.empty() || face.family[0] == '.') continue; // hidden system faces
        faces.push_back(std::move(face));
    }
    return faces;
}

} // namespace bemotion
