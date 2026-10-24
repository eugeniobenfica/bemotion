#include "render/Text.h"

#include "render/Fonts.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTextBlob.h"
#include "modules/skshaper/include/SkShaper.h"
#include "modules/skshaper/include/SkShaper_harfbuzz.h"
#include "modules/skshaper/include/SkShaper_skunicode.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace bemotion::render {

namespace {

constexpr float kRef = 100.0f; // shaping size; positions scale linearly with the font size

struct Run {
    SkFont font; // at kRef
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> pos;   // at kRef, x from the line start, before tracking
    std::vector<uint32_t> clusters; // byte offsets in the line
    std::vector<int> slot;      // visual cluster index in the line (for tracking)
    std::vector<int> charIndex; // logical character index in the whole text (for the reveal)
};

struct Line {
    std::vector<Run> runs;
    float width = 0.0f; // at kRef, no tracking
    int clusters = 0;   // visual clusters (tracking gaps = clusters - 1)
};

struct Shaped {
    std::vector<Line> lines;
    float ascent = -80.0f, descent = 20.0f; // primary font metrics at kRef
    int charCount = 0;
};

// Collects SkShaper's output.
class Collector final : public SkShaper::RunHandler {
public:
    explicit Collector(Line& line) : line_(line) {}
    void beginLine() override {}
    void runInfo(const RunInfo&) override {}
    void commitRunInfo() override {}
    Buffer runBuffer(const RunInfo& info) override {
        line_.runs.emplace_back();
        Run& r = line_.runs.back();
        r.font = info.fFont;
        const size_t n = info.glyphCount;
        r.glyphs.resize(n);
        r.pos.resize(n);
        r.clusters.resize(n);
        return {r.glyphs.data(), r.pos.data(), nullptr, r.clusters.data(), {x_, 0.0f}};
    }
    void commitRunBuffer(const RunInfo& info) override { x_ += info.fAdvance.fX; }
    void commitLine() override {}
    float width() const { return x_; }

private:
    Line& line_;
    float x_ = 0.0f;
};

struct Shaper {
    sk_sp<SkUnicode> unicode = SkUnicodes::ICU::Make();
    std::unique_ptr<SkShaper> shaper;
    Shaper() {
        if (unicode) shaper = SkShapers::HB::ShaperDrivenWrapper(unicode, Fonts::manager());
        if (!shaper) shaper = SkShapers::Primitive::PrimitiveText(); // no ICU / HarfBuzz: simple shaping
    }
};

Shaper& shaper() {
    static Shaper s;
    return s;
}

int utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

std::shared_ptr<const Shaped> shapeUncached(const std::string& text, const sk_sp<SkTypeface>& typeface) {
    auto out = std::make_shared<Shaped>();
    SkFont font(typeface, kRef);
    font.setSubpixel(true);
    font.setLinearMetrics(true);
    font.setHinting(SkFontHinting::kNone);
    font.setEdging(SkFont::Edging::kAntiAlias);
    SkFontMetrics m;
    font.getMetrics(&m);
    out->ascent = m.fAscent;
    out->descent = m.fDescent;

    Shaper& sh = shaper();
    int charBase = 0;
    size_t lineStart = 0;
    for (;;) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string s = text.substr(lineStart, lineEnd - lineStart);
        if (!s.empty() && s.back() == '\r') s.pop_back();
        // Logical character index of every byte.
        std::vector<int> charOfByte(s.size() + 1, 0);
        int chars = 0;
        for (size_t i = 0; i < s.size();) {
            const int len = std::min<int>(utf8Length(static_cast<unsigned char>(s[i])), static_cast<int>(s.size() - i));
            for (int k = 0; k < len; ++k) charOfByte[i + k] = chars;
            i += len;
            ++chars;
        }
        charOfByte[s.size()] = chars;

        out->lines.emplace_back();
        Line& line = out->lines.back();
        if (!s.empty()) {
            Collector collector(line);
            const char* utf8 = s.c_str();
            const size_t n = s.size();
            std::unique_ptr<SkShaper::FontRunIterator> fontIt = SkShaper::MakeFontMgrRunIterator(utf8, n, font, Fonts::manager());
            std::unique_ptr<SkShaper::BiDiRunIterator> bidi =
                sh.unicode ? SkShapers::unicode::BidiRunIterator(sh.unicode, utf8, n, 0) : nullptr;
            if (!bidi) bidi = std::make_unique<SkShaper::TrivialBiDiRunIterator>(0, n);
            std::unique_ptr<SkShaper::ScriptRunIterator> script = SkShapers::HB::ScriptRunIterator(utf8, n);
            if (!script) script = std::make_unique<SkShaper::TrivialScriptRunIterator>(SkSetFourByteTag('Z', 'y', 'y', 'y'), n);
            std::unique_ptr<SkShaper::LanguageRunIterator> lang = SkShaper::MakeStdLanguageRunIterator(utf8, n);
            sh.shaper->shape(utf8, n, *fontIt, *bidi, *script, *lang, nullptr, 0, FLT_MAX, &collector);
            line.width = collector.width();
            // Visual cluster slots, for even letter spacing.
            int slot = -1;
            uint32_t last = UINT32_MAX;
            for (Run& r : line.runs) {
                r.slot.resize(r.glyphs.size());
                r.charIndex.resize(r.glyphs.size());
                for (size_t i = 0; i < r.glyphs.size(); ++i) {
                    if (r.clusters[i] != last) {
                        ++slot;
                        last = r.clusters[i];
                    }
                    r.slot[i] = slot;
                    r.charIndex[i] = charBase + charOfByte[std::min<size_t>(r.clusters[i], s.size())];
                }
            }
            line.clusters = slot + 1;
        }
        charBase += chars;
        if (lineEnd >= text.size()) break;
        lineStart = lineEnd + 1;
    }
    out->charCount = charBase;
    return out;
}

// Small LRU cache of shaped texts.
std::shared_ptr<const Shaped> shape(const std::string& text, const std::string& family, const std::string& style) {
    static std::mutex mutex;
    static std::list<std::pair<std::string, std::shared_ptr<const Shaped>>> lru;
    static std::unordered_map<std::string, decltype(lru)::iterator> index;
    const std::string key = family + '\x1f' + style + '\x1f' + text;
    {
        std::lock_guard lock(mutex);
        if (auto it = index.find(key); it != index.end()) {
            lru.splice(lru.begin(), lru, it->second);
            return it->second->second;
        }
    }
    auto shaped = shapeUncached(text, Fonts::typeface(family, style));
    std::lock_guard lock(mutex);
    lru.emplace_front(key, shaped);
    index[key] = lru.begin();
    if (lru.size() > 512) {
        index.erase(lru.back().first);
        lru.pop_back();
    }
    return shaped;
}

// Where everything goes at a given moment.
struct Placement {
    float scale = 1.0f;   // font size / kRef
    float tracking = 0.0f;
    float advance = 0.0f; // line to line
    float top = 0.0f;
    float height = 0.0f;
    float width = 0.0f;   // widest line
    std::vector<float> lineX;
    std::vector<float> baseline;
};

float lineWidth(const Line& line, const Placement& p) {
    return line.width * p.scale + p.tracking * static_cast<float>(std::max(0, line.clusters - 1));
}

Placement place(const Shaped& s, float size, float tracking, float lineHeightPercent, TextAlign align) {
    Placement p;
    p.scale = std::max(1.0f, size) / kRef;
    p.tracking = tracking;
    p.advance = std::max(1.0f, size) * std::max(0.1f, lineHeightPercent * 0.01f);
    p.height = p.advance * static_cast<float>(s.lines.size());
    p.top = -p.height * 0.5f;
    const float box = (s.descent - s.ascent) * p.scale; // the font's line box, centered in the line
    for (size_t i = 0; i < s.lines.size(); ++i) {
        const float w = lineWidth(s.lines[i], p);
        p.width = std::max(p.width, w);
        p.lineX.push_back(align == TextAlign::Left ? 0.0f : align == TextAlign::Center ? -w * 0.5f : -w);
        p.baseline.push_back(p.top + p.advance * static_cast<float>(i) + (p.advance - box) * 0.5f - s.ascent * p.scale);
    }
    return p;
}

void drawShaped(SkCanvas* canvas, const Shaped& s, const Placement& p, const SkPaint& paint, int visibleChars) {
    for (size_t li = 0; li < s.lines.size(); ++li) {
        for (const Run& r : s.lines[li].runs) {
            int count = 0;
            for (int ci : r.charIndex) count += ci < visibleChars;
            if (count == 0) continue;
            SkFont font = r.font;
            font.setSize(kRef * p.scale);
            SkTextBlobBuilder builder;
            const auto& buf = builder.allocRunPos(font, count);
            int k = 0;
            for (size_t i = 0; i < r.glyphs.size(); ++i) {
                if (r.charIndex[i] >= visibleChars) continue;
                buf.glyphs[k] = r.glyphs[i];
                buf.points()[k] = {p.lineX[li] + r.pos[i].fX * p.scale + p.tracking * static_cast<float>(r.slot[i]),
                                   p.baseline[li] + r.pos[i].fY * p.scale};
                ++k;
            }
            canvas->drawTextBlob(builder.make(), 0, 0, paint);
        }
    }
}

SkColor4f color4(Color c, float opacity) { return {c.r, c.g, c.b, std::clamp(c.a * opacity, 0.0f, 1.0f)}; }

// `text` with line breaks put at spaces so no line is wider than `width` (a word wider than
// that keeps its own line). Spaces become breaks one for one, so the reveal still counts right.
std::string wrapped(const std::string& text, const std::string& family, const std::string& style, float size, float tracking,
                    float width) {
    if (width <= 0.0f || text.find(' ') == std::string::npos) return text;
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> cache;
    char key[96];
    std::snprintf(key, sizeof(key), "%.2f|%.2f|%.1f|", size, tracking, width);
    const std::string k = std::string(key) + family + '\x1f' + style + '\x1f' + text;
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(k); it != cache.end()) return it->second;
    }
    const float scale = std::max(1.0f, size) / kRef;
    auto measure = [&](const std::string& s) {
        const auto sh = shape(s, family, style);
        if (sh->lines.empty()) return 0.0f;
        return sh->lines.front().width * scale + tracking * static_cast<float>(std::max(0, sh->lines.front().clusters - 1));
    };
    std::string out = text;
    size_t lineStart = 0, lastSpace = std::string::npos;
    for (size_t i = 0; i <= out.size(); ++i) {
        if (i < out.size() && out[i] == '\n') {
            lineStart = i + 1;
            lastSpace = std::string::npos;
            continue;
        }
        if (i < out.size() && out[i] != ' ') continue;
        // A word ends at i: break at the space before it when the line got too wide.
        if (lastSpace != std::string::npos && measure(out.substr(lineStart, i - lineStart)) > width) {
            out[lastSpace] = '\n';
            lineStart = lastSpace + 1;
        }
        lastSpace = i < out.size() ? i : lastSpace;
    }
    std::lock_guard lock(mutex);
    if (cache.size() > 2048) cache.clear();
    cache[k] = out;
    return out;
}

struct Laid {
    std::shared_ptr<const Shaped> shaped;
    Placement place;
};

Laid layOut(const TextProps& text, double t) {
    const float size = text.fontSize.valueAt(t), tracking = text.tracking.valueAt(t);
    const std::string shown = wrapped(text.textAt(t), text.fontFamily, text.fontStyle, size, tracking, text.wrapWidth.valueAt(t));
    Laid l;
    l.shaped = shape(shown, text.fontFamily, text.fontStyle);
    l.place = place(*l.shaped, size, tracking, text.lineHeight.valueAt(t), text.align);
    if (text.subtitle) { // the last line stays put at the position: more lines grow upwards
        const float shift = -l.place.height * 0.5f;
        l.place.top += shift;
        for (float& b : l.place.baseline) b += shift;
    }
    return l;
}

// The box the lines occupy (no padding).
void linesBox(const TextProps& text, const Placement& p, float size, Vec2& mn, Vec2& mx) {
    const float left = text.align == TextAlign::Left ? 0.0f : text.align == TextAlign::Center ? -p.width * 0.5f : -p.width;
    mn = {left, p.top};
    mx = {left + std::max(p.width, std::max(1.0f, size) * 0.25f), p.top + p.height};
}

} // namespace

void textBounds(const TextProps& text, double t, Vec2& outMin, Vec2& outMax) {
    const Laid l = layOut(text, t);
    linesBox(text, l.place, text.fontSize.valueAt(t), outMin, outMax);
    float grow = text.outlineEnabled ? std::max(0.0f, text.outlineWidth.valueAt(t)) : 0.0f;
    if (text.boxEnabled) grow = std::max(grow, std::max(0.0f, text.boxPadding.valueAt(t)));
    outMin = outMin - Vec2{grow, grow};
    outMax = outMax + Vec2{grow, grow};
}

void drawText(SkCanvas* canvas, const TextProps& text, double t, float opacity) {
    const std::string& shownText = text.textAt(t);
    if (shownText.empty()) return;
    const Laid l = layOut(text, t);
    const Shaped& s = *l.shaped;
    const Placement& p = l.place;
    const float reveal = std::clamp(text.reveal.valueAt(t), 0.0f, 100.0f) * 0.01f;
    const int visible = static_cast<int>(std::floor(reveal * static_cast<float>(s.charCount) + 1e-4f));
    if (text.boxEnabled) { // behind everything, around the lines
        const SkColor4f bc = color4(text.boxColor.valueAt(t), opacity);
        if (bc.fA > 0.0f && visible > 0) {
            Vec2 mn, mx;
            linesBox(text, p, text.fontSize.valueAt(t), mn, mx);
            const float pad = std::max(0.0f, text.boxPadding.valueAt(t));
            const SkRect r = SkRect::MakeLTRB(mn.x - pad, mn.y - pad, mx.x + pad, mx.y + pad);
            const float round = std::clamp(text.boxRoundness.valueAt(t), 0.0f, std::min(r.width(), r.height()) * 0.5f);
            SkPaint box(bc);
            box.setAntiAlias(true);
            canvas->drawRoundRect(r, round, round, box);
        }
    }
    if (text.outlineEnabled) { // under the letters, so it grows outwards
        const float w = std::max(0.0f, text.outlineWidth.valueAt(t));
        const SkColor4f oc = color4(text.outlineColor.valueAt(t), opacity);
        if (w > 0.0f && oc.fA > 0.0f) {
            SkPaint stroke(oc);
            stroke.setAntiAlias(true);
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(w * 2.0f);
            stroke.setStrokeJoin(SkPaint::kRound_Join);
            drawShaped(canvas, s, p, stroke, visible);
        }
    }
    const SkColor4f c = color4(text.color.valueAt(t), opacity);
    if (c.fA <= 0.0f) return;
    SkPaint paint(c);
    paint.setAntiAlias(true);
    drawShaped(canvas, s, p, paint, visible);
}

float measurePlainText(const std::string& str, float size, bool bold) {
    if (str.empty() || size <= 0.0f) return 0.0f;
    const auto s = shape(str, Fonts::kDefaultFamily, bold ? "Bold" : "Regular");
    return s->lines.empty() ? 0.0f : s->lines.front().width * size / kRef;
}

void drawPlainText(SkCanvas* canvas, const std::string& str, Vec2 at, float size, Color color, TextAnchor anchor, bool bold) {
    if (str.empty() || size <= 0.0f || color.a <= 0.0f) return;
    const auto s = shape(str, Fonts::kDefaultFamily, bold ? "Bold" : "Regular");
    const TextAlign align = anchor == TextAnchor::Left ? TextAlign::Left : anchor == TextAnchor::Center ? TextAlign::Center : TextAlign::Right;
    Placement p = place(*s, size, 0.0f, 100.0f, align);
    for (float& x : p.lineX) x += at.x;
    for (float& y : p.baseline) y += at.y;
    SkPaint paint(color4(color, 1.0f));
    paint.setAntiAlias(true);
    drawShaped(canvas, *s, p, paint, s->charCount);
}

} // namespace bemotion::render
