#include "core/Svg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>

namespace bemotion {

namespace {

// --- a small XML reader (what SVG files need) -------------------------------------------------

struct Element {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::vector<std::unique_ptr<Element>> children;
    std::string text; // <style> contents
    std::string attr(const std::string& key, const std::string& fallback = {}) const {
        const auto it = attrs.find(key);
        return it == attrs.end() ? fallback : it->second;
    }
};

std::string decodeEntities(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const size_t end = s.find(';', i);
        if (end == std::string::npos) {
            out += s[i];
            continue;
        }
        const std::string e = s.substr(i + 1, end - i - 1);
        if (e == "amp") out += '&';
        else if (e == "lt") out += '<';
        else if (e == "gt") out += '>';
        else if (e == "quot") out += '"';
        else if (e == "apos") out += '\'';
        else if (!e.empty() && e[0] == '#') {
            const long code = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtol(e.c_str() + 2, nullptr, 16) : std::strtol(e.c_str() + 1, nullptr, 10);
            if (code > 0 && code < 128) out += static_cast<char>(code); // (names and ids: ASCII is enough)
        } else {
            out += s.substr(i, end - i + 1);
        }
        i = end;
    }
    return out;
}

std::string localName(const std::string& n) {
    const size_t colon = n.find(':');
    return colon == std::string::npos ? n : n.substr(colon + 1);
}

class XmlReader {
public:
    explicit XmlReader(const std::string& s) : s_(s) {}
    std::unique_ptr<Element> read() {
        auto root = std::make_unique<Element>();
        std::vector<Element*> stack{root.get()};
        while (i_ < s_.size()) {
            const size_t lt = s_.find('<', i_);
            if (lt == std::string::npos) break;
            if (stack.size() > 1 && stack.back()->name == "style") stack.back()->text += s_.substr(i_, lt - i_);
            i_ = lt;
            if (starts("<!--")) {
                skipPast("-->");
            } else if (starts("<![CDATA[")) {
                const size_t end = s_.find("]]>", i_);
                if (stack.size() > 1 && stack.back()->name == "style") stack.back()->text += s_.substr(i_ + 9, end == std::string::npos ? std::string::npos : end - i_ - 9);
                i_ = end == std::string::npos ? s_.size() : end + 3;
            } else if (starts("<?")) {
                skipPast("?>");
            } else if (starts("<!")) { // <!DOCTYPE ...> with perhaps [ ... ]
                int depth = 0;
                for (; i_ < s_.size(); ++i_) {
                    if (s_[i_] == '[') ++depth;
                    else if (s_[i_] == ']') --depth;
                    else if (s_[i_] == '>' && depth <= 0) break;
                }
                ++i_;
            } else if (starts("</")) {
                const size_t end = s_.find('>', i_);
                const std::string name = localName(trim(s_.substr(i_ + 2, end == std::string::npos ? std::string::npos : end - i_ - 2)));
                i_ = end == std::string::npos ? s_.size() : end + 1;
                for (size_t k = stack.size(); k-- > 1;)
                    if (stack[k]->name == name) {
                        stack.resize(k);
                        break;
                    }
            } else {
                ++i_;
                auto e = std::make_unique<Element>();
                e->name = localName(word());
                bool selfClosing = false;
                for (;;) {
                    skipSpace();
                    if (i_ >= s_.size()) break;
                    if (s_[i_] == '/') {
                        selfClosing = true;
                        ++i_;
                        continue;
                    }
                    if (s_[i_] == '>') {
                        ++i_;
                        break;
                    }
                    const std::string key = word();
                    if (key.empty()) {
                        ++i_;
                        continue;
                    }
                    skipSpace();
                    std::string value;
                    if (i_ < s_.size() && s_[i_] == '=') {
                        ++i_;
                        skipSpace();
                        if (i_ < s_.size() && (s_[i_] == '"' || s_[i_] == '\'')) {
                            const char q = s_[i_++];
                            const size_t end = s_.find(q, i_);
                            value = s_.substr(i_, end == std::string::npos ? std::string::npos : end - i_);
                            i_ = end == std::string::npos ? s_.size() : end + 1;
                        } else {
                            value = word();
                        }
                    }
                    e->attrs[key] = decodeEntities(value);
                }
                Element* raw = e.get();
                stack.back()->children.push_back(std::move(e));
                if (!selfClosing) stack.push_back(raw);
            }
        }
        return root;
    }

private:
    bool starts(const char* p) const { return s_.compare(i_, std::strlen(p), p) == 0; }
    void skipPast(const char* p) {
        const size_t end = s_.find(p, i_);
        i_ = end == std::string::npos ? s_.size() : end + std::strlen(p);
    }
    void skipSpace() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    std::string word() {
        const size_t from = i_;
        while (i_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[i_])) && s_[i_] != '=' && s_[i_] != '>' && s_[i_] != '/' &&
               s_[i_] != '<')
            ++i_;
        return s_.substr(from, i_ - from);
    }
    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }
    const std::string& s_;
    size_t i_ = 0;
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// --- numbers, lengths, colors, transforms -----------------------------------------------------

// Reads numbers the way SVG writes them ("1.5.5" is two, "10-2" too, commas or spaces between).
// It keeps a reference: the text must outlive it.
class Numbers {
public:
    explicit Numbers(const std::string& s, size_t from = 0) : s_(s), i_(from) {}
    void skip() {
        while (i_ < s_.size() && (std::isspace(static_cast<unsigned char>(s_[i_])) || s_[i_] == ',')) ++i_;
    }
    bool more() {
        skip();
        return i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.');
    }
    std::optional<float> next() {
        skip();
        const size_t from = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        bool digits = false, dot = false;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (std::isdigit(static_cast<unsigned char>(c))) digits = true;
            else if (c == '.' && !dot) dot = true;
            else break;
            ++i_;
        }
        if (digits && i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            size_t j = i_ + 1;
            if (j < s_.size() && (s_[j] == '-' || s_[j] == '+')) ++j;
            if (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
                i_ = j;
                while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
            }
        }
        if (!digits) {
            i_ = from;
            return std::nullopt;
        }
        return std::strtof(s_.substr(from, i_ - from).c_str(), nullptr);
    }
    // An arc's flag: one character, "0" or "1", maybe glued to what follows.
    std::optional<bool> flag() {
        skip();
        if (i_ < s_.size() && (s_[i_] == '0' || s_[i_] == '1')) return s_[i_++] == '1';
        return std::nullopt;
    }
    size_t at() const { return i_; }
    void seek(size_t i) { i_ = i; }

private:
    const std::string& s_;
    size_t i_;
};

float number(const std::string& s, float fallback = 0.0f) {
    Numbers n(s);
    const std::optional<float> v = n.next();
    if (!v) return fallback;
    const std::string unit = lower(trim(s.substr(n.at())));
    if (unit == "pt") return *v * 4.0f / 3.0f;
    if (unit == "mm") return *v * 96.0f / 25.4f;
    if (unit == "cm") return *v * 96.0f / 2.54f;
    if (unit == "in") return *v * 96.0f;
    if (unit == "pc") return *v * 16.0f;
    if (unit == "em") return *v * 16.0f;
    return *v; // px, or none
}

std::optional<Color> parseColor(std::string s) {
    s = lower(trim(s));
    if (s.empty() || s == "none" || s == "transparent") return std::nullopt;
    if (s == "currentcolor") return Color(0, 0, 0, 1);
    if (s[0] == '#') {
        const std::string hex = s.substr(1);
        auto nib = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0; };
        if (hex.size() == 3 || hex.size() == 4) {
            Color c{nib(hex[0]) * 17 / 255.0f, nib(hex[1]) * 17 / 255.0f, nib(hex[2]) * 17 / 255.0f, 1.0f};
            if (hex.size() == 4) c.a = nib(hex[3]) * 17 / 255.0f;
            return c;
        }
        if (hex.size() == 6 || hex.size() == 8) {
            auto byte = [&](int i) { return (nib(hex[i]) * 16 + nib(hex[i + 1])) / 255.0f; };
            return Color{byte(0), byte(2), byte(4), hex.size() == 8 ? byte(6) : 1.0f};
        }
        return std::nullopt;
    }
    if (s.rfind("rgb", 0) == 0) {
        const size_t open = s.find('(');
        if (open == std::string::npos) return std::nullopt;
        float v[4] = {0, 0, 0, 1};
        size_t at = open + 1;
        for (int k = 0; k < 4; ++k) {
            Numbers n(s, at);
            const std::optional<float> f = n.next();
            if (!f) break;
            at = n.at();
            const bool percent = at < s.size() && s[at] == '%';
            if (percent) ++at;
            v[k] = k < 3 ? (percent ? *f / 100.0f : *f / 255.0f) : (percent ? *f / 100.0f : *f);
        }
        return Color{std::clamp(v[0], 0.0f, 1.0f), std::clamp(v[1], 0.0f, 1.0f), std::clamp(v[2], 0.0f, 1.0f), std::clamp(v[3], 0.0f, 1.0f)};
    }
    static const std::map<std::string, unsigned> named = {
        {"black", 0x000000},  {"white", 0xFFFFFF},   {"red", 0xFF0000},     {"green", 0x008000},  {"blue", 0x0000FF},
        {"yellow", 0xFFFF00}, {"cyan", 0x00FFFF},    {"aqua", 0x00FFFF},    {"magenta", 0xFF00FF}, {"fuchsia", 0xFF00FF},
        {"gray", 0x808080},   {"grey", 0x808080},    {"silver", 0xC0C0C0},  {"maroon", 0x800000}, {"olive", 0x808000},
        {"lime", 0x00FF00},   {"teal", 0x008080},    {"navy", 0x000080},    {"purple", 0x800080}, {"orange", 0xFFA500},
        {"pink", 0xFFC0CB},   {"brown", 0xA52A2A},   {"gold", 0xFFD700},    {"indigo", 0x4B0082}, {"violet", 0xEE82EE},
        {"coral", 0xFF7F50},  {"salmon", 0xFA8072},  {"tomato", 0xFF6347},  {"crimson", 0xDC143C}, {"khaki", 0xF0E68C},
        {"beige", 0xF5F5DC},  {"ivory", 0xFFFFF0},   {"lavender", 0xE6E6FA}, {"turquoise", 0x40E0D0}, {"tan", 0xD2B48C},
        {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"dimgray", 0x696969},
        {"darkred", 0x8B0000}, {"darkgreen", 0x006400}, {"darkblue", 0x00008B}, {"skyblue", 0x87CEEB}, {"steelblue", 0x4682B4},
        {"royalblue", 0x4169E1}, {"orangered", 0xFF4500}, {"hotpink", 0xFF69B4}, {"chocolate", 0xD2691E}, {"forestgreen", 0x228B22},
        {"limegreen", 0x32CD32}, {"seagreen", 0x2E8B57}, {"slategray", 0x708090}, {"whitesmoke", 0xF5F5F5}, {"gainsboro", 0xDCDCDC}};
    if (const auto it = named.find(s); it != named.end()) return Color::fromHex(it->second);
    return std::nullopt;
}

Affine parseTransform(const std::string& s) {
    Affine m;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
        const size_t nameStart = i;
        while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
        const std::string name = s.substr(nameStart, i - nameStart);
        const size_t open = s.find('(', i), close = s.find(')', i);
        if (name.empty() || open == std::string::npos || close == std::string::npos) break;
        std::vector<float> v;
        const std::string args = s.substr(open + 1, close - open - 1);
        Numbers n(args);
        while (const std::optional<float> f = n.next()) v.push_back(*f);
        i = close + 1;
        Affine t;
        if (name == "matrix" && v.size() >= 6) t = Affine{v[0], v[1], v[2], v[3], v[4], v[5]};
        else if (name == "translate" && !v.empty()) t = Affine::translate({v[0], v.size() > 1 ? v[1] : 0.0f});
        else if (name == "scale" && !v.empty()) t = Affine::scale({v[0], v.size() > 1 ? v[1] : v[0]});
        else if (name == "rotate" && !v.empty()) {
            t = Affine::rotateDeg(v[0]);
            if (v.size() >= 3) t = Affine::translate({v[1], v[2]}) * t * Affine::translate({-v[1], -v[2]});
        } else if (name == "skewX" && !v.empty()) {
            t.c = std::tan(v[0] * 3.14159265f / 180.0f);
        } else if (name == "skewY" && !v.empty()) {
            t.b = std::tan(v[0] * 3.14159265f / 180.0f);
        }
        m = m * t;
    }
    return m;
}

// --- outlines -------------------------------------------------------------------------------

constexpr float kKappa = 0.5522847f; // a quarter circle's handles, as a fraction of its radius

class Outlines {
public:
    void moveTo(Vec2 p) {
        finish(false);
        current_.vertices.push_back({p, {}, {}});
        start_ = p;
    }
    void lineTo(Vec2 p) {
        if (current_.vertices.empty()) current_.vertices.push_back({start_, {}, {}});
        current_.vertices.push_back({p, {}, {}});
    }
    void cubicTo(Vec2 c1, Vec2 c2, Vec2 p) {
        if (current_.vertices.empty()) current_.vertices.push_back({start_, {}, {}});
        PathVertex& last = current_.vertices.back();
        last.out = c1 - last.point;
        current_.vertices.push_back({p, c2 - p, {}});
    }
    void close() {
        Vec2 at = start_;
        if (!current_.vertices.empty()) at = current_.vertices.front().point;
        finish(true);
        start_ = at;
    }
    Vec2 last() const { return current_.vertices.empty() ? start_ : current_.vertices.back().point; }
    std::vector<BezierPath> take() {
        finish(false);
        return std::move(done_);
    }

private:
    void finish(bool closed) {
        auto& v = current_.vertices;
        if (closed && v.size() > 1) {
            // Ending where it started: one vertex there, its handles from both.
            const Vec2 d = v.back().point - v.front().point;
            if (std::fabs(d.x) < 1e-3f && std::fabs(d.y) < 1e-3f) {
                v.front().in = v.back().in;
                v.pop_back();
            }
        }
        current_.closed = closed;
        if (v.size() >= 2) done_.push_back(std::move(current_));
        current_ = {};
    }
    BezierPath current_;
    Vec2 start_{0, 0};
    std::vector<BezierPath> done_;
};

// An elliptical arc (SVG's endpoint form) as cubic curves.
void arcTo(Outlines& o, Vec2 p0, float rx, float ry, float angleDeg, bool large, bool sweep, Vec2 p1) {
    if (rx == 0 || ry == 0 || (p0.x == p1.x && p0.y == p1.y)) {
        o.lineTo(p1);
        return;
    }
    rx = std::fabs(rx), ry = std::fabs(ry);
    const float phi = angleDeg * 3.14159265f / 180.0f, cs = std::cos(phi), sn = std::sin(phi);
    const float dx = (p0.x - p1.x) * 0.5f, dy = (p0.y - p1.y) * 0.5f;
    const float x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
    const float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) rx *= std::sqrt(lambda), ry *= std::sqrt(lambda);
    const float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1, den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    float coef = den > 0 ? std::sqrt(std::max(0.0f, num / den)) : 0.0f;
    if (large == sweep) coef = -coef;
    const float cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
    const float cx = cs * cx1 - sn * cy1 + (p0.x + p1.x) * 0.5f, cy = sn * cx1 + cs * cy1 + (p0.y + p1.y) * 0.5f;
    auto angle = [](float ux, float uy, float vx, float vy) {
        const float a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        return a;
    };
    const float t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    float dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && dt > 0) dt -= 2 * 3.14159265f;
    else if (sweep && dt < 0) dt += 2 * 3.14159265f;
    const int segments = std::max(1, static_cast<int>(std::ceil(std::fabs(dt) / (3.14159265f / 2) - 1e-4f)));
    const float step = dt / segments, k = 4.0f / 3.0f * std::tan(step / 4.0f);
    auto point = [&](float t) { return Vec2{cx + rx * std::cos(t) * cs - ry * std::sin(t) * sn, cy + rx * std::cos(t) * sn + ry * std::sin(t) * cs}; };
    auto deriv = [&](float t) { return Vec2{-rx * std::sin(t) * cs - ry * std::cos(t) * sn, -rx * std::sin(t) * sn + ry * std::cos(t) * cs}; };
    float t = t1;
    for (int i = 0; i < segments; ++i) {
        const float u = t + step;
        const Vec2 a = point(t), b = point(u);
        o.cubicTo(a + deriv(t) * k, b - deriv(u) * k, i == segments - 1 ? p1 : b);
        t = u;
    }
}

void parsePathData(const std::string& d, Outlines& o) {
    Numbers n(d);
    char cmd = 0;
    Vec2 cur{0, 0}, lastCtrl{0, 0};
    char lastCmd = 0;
    size_t i = 0;
    while (true) {
        n.skip();
        i = n.at();
        if (i >= d.size()) break;
        if (std::isalpha(static_cast<unsigned char>(d[i]))) {
            cmd = d[i];
            n.seek(i + 1);
            if (cmd == 'Z' || cmd == 'z') {
                o.close();
                cur = o.last();
                lastCmd = cmd;
                continue;
            }
        } else if (!cmd) {
            break;
        }
        const bool rel = std::islower(static_cast<unsigned char>(cmd));
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        auto pt = [&](std::optional<float> x, std::optional<float> y) { return rel ? Vec2{cur.x + *x, cur.y + *y} : Vec2{*x, *y}; };
        const size_t before = n.at();
        switch (up) {
        case 'M': {
            const auto x = n.next(), y = n.next();
            if (!x || !y) return;
            cur = pt(x, y);
            o.moveTo(cur);
            cmd = rel ? 'l' : 'L'; // more pairs after a move are lines
            break;
        }
        case 'L': {
            const auto x = n.next(), y = n.next();
            if (!x || !y) return;
            cur = pt(x, y);
            o.lineTo(cur);
            break;
        }
        case 'H': {
            const auto x = n.next();
            if (!x) return;
            cur.x = rel ? cur.x + *x : *x;
            o.lineTo(cur);
            break;
        }
        case 'V': {
            const auto y = n.next();
            if (!y) return;
            cur.y = rel ? cur.y + *y : *y;
            o.lineTo(cur);
            break;
        }
        case 'C': {
            const auto x1 = n.next(), y1 = n.next(), x2 = n.next(), y2 = n.next(), x = n.next(), y = n.next();
            if (!y) return;
            const Vec2 c1 = pt(x1, y1), c2 = pt(x2, y2), p = pt(x, y);
            o.cubicTo(c1, c2, p);
            lastCtrl = c2, cur = p;
            break;
        }
        case 'S': {
            const auto x2 = n.next(), y2 = n.next(), x = n.next(), y = n.next();
            if (!y) return;
            const bool smooth = lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' || lastCmd == 's';
            const Vec2 c1 = smooth ? cur * 2.0f - lastCtrl : cur, c2 = pt(x2, y2), p = pt(x, y);
            o.cubicTo(c1, c2, p);
            lastCtrl = c2, cur = p;
            break;
        }
        case 'Q': {
            const auto x1 = n.next(), y1 = n.next(), x = n.next(), y = n.next();
            if (!y) return;
            const Vec2 q = pt(x1, y1), p = pt(x, y);
            o.cubicTo(cur + (q - cur) * (2.0f / 3.0f), p + (q - p) * (2.0f / 3.0f), p);
            lastCtrl = q, cur = p;
            break;
        }
        case 'T': {
            const auto x = n.next(), y = n.next();
            if (!y) return;
            const bool smooth = lastCmd == 'Q' || lastCmd == 'q' || lastCmd == 'T' || lastCmd == 't';
            const Vec2 q = smooth ? cur * 2.0f - lastCtrl : cur, p = pt(x, y);
            o.cubicTo(cur + (q - cur) * (2.0f / 3.0f), p + (q - p) * (2.0f / 3.0f), p);
            lastCtrl = q, cur = p;
            break;
        }
        case 'A': {
            const auto rx = n.next(), ry = n.next(), angle = n.next();
            const auto large = n.flag(), sweep = n.flag();
            const auto x = n.next(), y = n.next();
            if (!rx || !ry || !angle || !large || !sweep || !y) return;
            const Vec2 p = pt(x, y);
            arcTo(o, cur, *rx, *ry, *angle, *large, *sweep, p);
            cur = p;
            break;
        }
        default: return;
        }
        if (n.at() == before) return; // nothing read: stop rather than loop
        lastCmd = cmd;
        if (up != 'C' && up != 'S' && up != 'Q' && up != 'T') lastCtrl = cur;
    }
}

void ellipse(Outlines& o, float cx, float cy, float rx, float ry) {
    const float kx = rx * kKappa, ky = ry * kKappa;
    o.moveTo({cx + rx, cy});
    o.cubicTo({cx + rx, cy + ky}, {cx + kx, cy + ry}, {cx, cy + ry});
    o.cubicTo({cx - kx, cy + ry}, {cx - rx, cy + ky}, {cx - rx, cy});
    o.cubicTo({cx - rx, cy - ky}, {cx - kx, cy - ry}, {cx, cy - ry});
    o.cubicTo({cx + kx, cy - ry}, {cx + rx, cy - ky}, {cx + rx, cy});
    o.close();
}

void rect(Outlines& o, float x, float y, float w, float h, float rx, float ry) {
    rx = std::min(rx, w * 0.5f), ry = std::min(ry, h * 0.5f);
    if (rx <= 0 || ry <= 0) {
        o.moveTo({x, y});
        o.lineTo({x + w, y});
        o.lineTo({x + w, y + h});
        o.lineTo({x, y + h});
        o.close();
        return;
    }
    const float kx = rx * (1 - kKappa), ky = ry * (1 - kKappa);
    o.moveTo({x + rx, y});
    o.lineTo({x + w - rx, y});
    o.cubicTo({x + w - kx, y}, {x + w, y + ky}, {x + w, y + ry});
    o.lineTo({x + w, y + h - ry});
    o.cubicTo({x + w, y + h - ky}, {x + w - kx, y + h}, {x + w - rx, y + h});
    o.lineTo({x + rx, y + h});
    o.cubicTo({x + kx, y + h}, {x, y + h - ky}, {x, y + h - ry});
    o.lineTo({x, y + ry});
    o.cubicTo({x, y + ky}, {x + kx, y}, {x + rx, y});
    o.close();
}

BezierPath transformed(const BezierPath& path, const Affine& m) {
    BezierPath out = path;
    for (PathVertex& v : out.vertices) {
        const Vec2 p = m.apply(v.point);
        v.in = m.apply(v.point + v.in) - p;
        v.out = m.apply(v.point + v.out) - p;
        v.point = p;
    }
    return out;
}

// A few points along the outline (its curves sampled): for "is it inside that one" and its box.
std::vector<Vec2> polygonOf(const BezierPath& path) {
    std::vector<Vec2> pts;
    const auto& v = path.vertices;
    const size_t n = v.size(), segments = path.closed ? n : n - 1;
    for (size_t i = 0; i < segments; ++i) {
        const PathVertex& a = v[i];
        const PathVertex& b = v[(i + 1) % n];
        const Vec2 p0 = a.point, p1 = a.point + a.out, p2 = b.point + b.in, p3 = b.point;
        for (int k = 0; k < 8; ++k) {
            const float t = k / 8.0f, u = 1 - t;
            pts.push_back(p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t));
        }
    }
    if (!path.closed && n) pts.push_back(v.back().point);
    return pts;
}

bool inside(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}

float boxArea(const std::vector<Vec2>& poly) {
    if (poly.empty()) return 0;
    Vec2 mn = poly[0], mx = poly[0];
    for (const Vec2& p : poly) mn = {std::min(mn.x, p.x), std::min(mn.y, p.y)}, mx = {std::max(mx.x, p.x), std::max(mx.y, p.y)};
    return (mx.x - mn.x) * (mx.y - mn.y);
}

// --- painting: what each element inherits ------------------------------------------------------

struct Paint {
    enum Kind { None, Solid, Url } kind = Solid;
    Color color{0, 0, 0, 1};
    std::string url;
};

struct Style {
    Paint fill;                          // black by default
    Paint stroke{Paint::None, {}, {}};
    float strokeWidth = 1.0f;
    float fillOpacity = 1.0f, strokeOpacity = 1.0f;
    float opacity = 1.0f; // the element's, times its groups'
    bool hidden = false;
};

Paint parsePaint(const std::string& value, const Paint& inherited) {
    const std::string v = trim(value);
    if (v.empty() || v == "inherit") return inherited;
    if (lower(v) == "none") return {Paint::None, {}, {}};
    if (v.rfind("url(", 0) == 0) {
        const size_t hash = v.find('#'), close = v.find(')');
        Paint p{Paint::Url, {}, {}};
        if (hash != std::string::npos && close != std::string::npos && close > hash) p.url = v.substr(hash + 1, close - hash - 1);
        // A fallback color after the url.
        if (close != std::string::npos)
            if (const auto c = parseColor(v.substr(close + 1))) p.color = *c;
        return p;
    }
    if (const auto c = parseColor(v)) return {Paint::Solid, *c, {}};
    return inherited;
}

using Declarations = std::vector<std::pair<std::string, std::string>>;

Declarations parseDeclarations(const std::string& text) {
    Declarations out;
    size_t i = 0;
    while (i < text.size()) {
        const size_t semi = std::min(text.find(';', i), text.size());
        const std::string part = text.substr(i, semi - i);
        if (const size_t colon = part.find(':'); colon != std::string::npos)
            out.emplace_back(lower(trim(part.substr(0, colon))), trim(part.substr(colon + 1)));
        i = semi + 1;
    }
    return out;
}

void applyDeclaration(Style& s, const Style& parent, const std::string& key, const std::string& value) {
    if (key == "fill") s.fill = parsePaint(value, parent.fill);
    else if (key == "stroke") s.stroke = parsePaint(value, parent.stroke);
    else if (key == "stroke-width") s.strokeWidth = number(value, parent.strokeWidth);
    else if (key == "fill-opacity") s.fillOpacity = std::clamp(number(value, 1.0f), 0.0f, 1.0f);
    else if (key == "stroke-opacity") s.strokeOpacity = std::clamp(number(value, 1.0f), 0.0f, 1.0f);
    else if (key == "opacity") s.opacity = parent.opacity * std::clamp(number(value, 1.0f), 0.0f, 1.0f);
    else if (key == "display" && trim(value) == "none") s.hidden = true;
    else if (key == "visibility" && (trim(value) == "hidden" || trim(value) == "collapse")) s.hidden = true;
}

struct Gradient {
    std::vector<std::pair<float, Color>> stops;
    bool linear = true;
    float x1 = 0, y1 = 0, x2 = 1, y2 = 0;
    std::string href;
};

class Importer {
public:
    explicit Importer(SvgDrawing& out) : out_(out) {}

    void run(const Element& root) {
        index(root);
        const Element* svg = find(root, "svg");
        if (!svg) return;
        // The view box (else the width and height) is the drawing's frame.
        const std::string vb = svg->attr("viewBox");
        Numbers n(vb);
        const auto x = n.next(), y = n.next(), w = n.next(), h = n.next();
        if (w && h && *w > 0 && *h > 0) out_.origin = {*x, *y}, out_.size = {*w, *h};
        else out_.size = {number(svg->attr("width"), 0.0f), number(svg->attr("height"), 0.0f)};
        Style style;
        walk(*svg, Affine{}, style, 0);
    }

private:
    void index(const Element& e) {
        if (const std::string id = e.attr("id"); !id.empty()) ids_[id] = &e;
        if (e.name == "style") parseCss(e.text);
        if (e.name == "linearGradient" || e.name == "radialGradient") {
            Gradient g;
            g.linear = e.name == "linearGradient";
            g.x1 = fraction(e.attr("x1", "0")), g.y1 = fraction(e.attr("y1", "0"));
            g.x2 = fraction(e.attr("x2", "1")), g.y2 = fraction(e.attr("y2", "0"));
            g.href = e.attr("xlink:href", e.attr("href"));
            if (!g.href.empty() && g.href[0] == '#') g.href.erase(0, 1);
            for (const auto& c : e.children) {
                if (c->name != "stop") continue;
                Color color{0, 0, 0, 1};
                float opacity = 1.0f;
                Declarations decl{{"stop-color", c->attr("stop-color")}, {"stop-opacity", c->attr("stop-opacity")}};
                for (const auto& d : parseDeclarations(c->attr("style"))) decl.push_back(d);
                for (const auto& [k, v] : decl) {
                    if (k == "stop-color")
                        if (const auto col = parseColor(v)) color = *col;
                    if (k == "stop-opacity" && !v.empty()) opacity = std::clamp(number(v, 1.0f), 0.0f, 1.0f);
                }
                color.a *= opacity;
                g.stops.emplace_back(fraction(c->attr("offset", "0")), color);
            }
            if (const std::string id = e.attr("id"); !id.empty()) gradients_[id] = g;
        }
        for (const auto& c : e.children) index(*c);
    }

    static float fraction(const std::string& s) {
        const float v = number(s, 0.0f);
        return s.find('%') != std::string::npos ? v / 100.0f : v;
    }

    static const Element* find(const Element& e, const std::string& name) {
        if (e.name == name) return &e;
        for (const auto& c : e.children)
            if (const Element* f = find(*c, name)) return f;
        return nullptr;
    }

    // Simple rules only: .class, tag and #id selectors (lists of them).
    void parseCss(const std::string& css) {
        size_t i = 0;
        while (i < css.size()) {
            const size_t open = css.find('{', i);
            if (open == std::string::npos) break;
            const size_t close = css.find('}', open);
            if (close == std::string::npos) break;
            const Declarations decl = parseDeclarations(css.substr(open + 1, close - open - 1));
            std::string selectors = css.substr(i, open - i);
            size_t s = 0;
            while (s <= selectors.size()) {
                const size_t comma = std::min(selectors.find(',', s), selectors.size());
                const std::string sel = trim(selectors.substr(s, comma - s));
                if (!sel.empty() && sel.find_first_of(" >+~:[") == std::string::npos) css_[sel].insert(css_[sel].end(), decl.begin(), decl.end());
                s = comma + 1;
            }
            i = close + 1;
        }
    }

    Style styleOf(const Element& e, const Style& parent) {
        Style s = parent;
        s.opacity = parent.opacity;
        s.hidden = false;
        // Presentation attributes, then style sheet rules, then the style attribute.
        Declarations decl;
        for (const char* key : {"fill", "stroke", "stroke-width", "fill-opacity", "stroke-opacity", "opacity", "display", "visibility"})
            if (const auto it = e.attrs.find(key); it != e.attrs.end()) decl.emplace_back(key, it->second);
        auto rule = [&](const std::string& sel) {
            if (const auto it = css_.find(sel); it != css_.end()) decl.insert(decl.end(), it->second.begin(), it->second.end());
        };
        rule(e.name);
        std::string classes = e.attr("class");
        size_t i = 0;
        while (i < classes.size()) {
            while (i < classes.size() && std::isspace(static_cast<unsigned char>(classes[i]))) ++i;
            const size_t from = i;
            while (i < classes.size() && !std::isspace(static_cast<unsigned char>(classes[i]))) ++i;
            if (i > from) rule("." + classes.substr(from, i - from));
        }
        if (const std::string id = e.attr("id"); !id.empty()) rule("#" + id);
        for (const auto& d : parseDeclarations(e.attr("style"))) decl.push_back(d);
        for (const auto& [k, v] : decl) applyDeclaration(s, parent, k, v);
        return s;
    }

    void walk(const Element& e, const Affine& parentMatrix, const Style& parentStyle, int depth) {
        if (depth > 64) return;
        static const char* skipped[] = {"defs", "symbol", "clipPath", "mask", "pattern", "marker", "title", "desc", "metadata", "style",
                                        "linearGradient", "radialGradient", "filter", "text", "image", "foreignObject", "script"};
        for (const char* s : skipped)
            if (e.name == s) return;
        const Style style = styleOf(e, parentStyle);
        if (style.hidden) return;
        Affine m = parentMatrix * parseTransform(e.attr("transform"));
        if (e.name == "svg" && depth > 0) m = m * Affine::translate({number(e.attr("x")), number(e.attr("y"))});
        if (e.name == "use") {
            std::string href = e.attr("xlink:href", e.attr("href"));
            if (!href.empty() && href[0] == '#') href.erase(0, 1);
            const auto it = ids_.find(href);
            if (it == ids_.end() || it->second == &e) return;
            const Affine at = m * Affine::translate({number(e.attr("x")), number(e.attr("y"))});
            const Element& target = *it->second;
            // A symbol is drawn like a group where it is used.
            if (target.name == "symbol") {
                for (const auto& c : target.children) walk(*c, at, style, depth + 1);
            } else {
                walk(target, at, style, depth + 1);
            }
            return;
        }
        Outlines o;
        const std::string& n = e.name;
        if (n == "path") parsePathData(e.attr("d"), o);
        else if (n == "rect") {
            const float w = number(e.attr("width")), h = number(e.attr("height"));
            const std::string rxs = e.attr("rx"), rys = e.attr("ry");
            const float rx = number(rxs.empty() ? rys : rxs), ry = number(rys.empty() ? rxs : rys);
            if (w > 0 && h > 0) rect(o, number(e.attr("x")), number(e.attr("y")), w, h, rx, ry);
        } else if (n == "circle") {
            const float r = number(e.attr("r"));
            if (r > 0) ellipse(o, number(e.attr("cx")), number(e.attr("cy")), r, r);
        } else if (n == "ellipse") {
            const float rx = number(e.attr("rx")), ry = number(e.attr("ry"));
            if (rx > 0 && ry > 0) ellipse(o, number(e.attr("cx")), number(e.attr("cy")), rx, ry);
        } else if (n == "line") {
            o.moveTo({number(e.attr("x1")), number(e.attr("y1"))});
            o.lineTo({number(e.attr("x2")), number(e.attr("y2"))});
        } else if (n == "polyline" || n == "polygon") {
            const std::string points = e.attr("points");
            Numbers pts(points);
            bool first = true;
            while (pts.more()) {
                const auto x = pts.next(), y = pts.next();
                if (!x || !y) break;
                if (first) o.moveTo({*x, *y});
                else o.lineTo({*x, *y});
                first = false;
            }
            if (n == "polygon") o.close();
        } else {
            for (const auto& c : e.children) walk(*c, m, style, depth + 1); // a group (g, a, svg, switch...)
            return;
        }
        add(e, o.take(), m, style);
    }

    // One element's outlines: each on its own, those inside another one cutting it.
    void add(const Element& e, std::vector<BezierPath> outlines, const Affine& m, const Style& style) {
        if (outlines.empty()) return;
        const bool fills = style.fill.kind != Paint::None, strokes = style.stroke.kind != Paint::None;
        if (!fills && !strokes) return;
        for (BezierPath& p : outlines) p = transformed(p, m);
        std::vector<std::vector<Vec2>> polys;
        for (const BezierPath& p : outlines) polys.push_back(polygonOf(p));
        // How many others each one is inside: odd, a hole of the smallest one around it.
        const size_t count = outlines.size();
        std::vector<int> container(count, -1), depth(count, 0);
        for (size_t i = 0; i < count; ++i) {
            if (!fills || polys[i].empty()) continue;
            float best = 0;
            for (size_t j = 0; j < count; ++j) {
                if (i == j || !outlines[j].closed || polys[j].size() < 3 || boxArea(polys[j]) <= boxArea(polys[i])) continue;
                if (!inside(polys[j], polys[i].front())) continue;
                ++depth[i];
                if (container[i] < 0 || boxArea(polys[j]) < best) container[i] = static_cast<int>(j), best = boxArea(polys[j]);
            }
        }
        std::map<size_t, size_t> shapeOf; // outline -> its shape in out_.shapes
        const float scale = std::sqrt(std::fabs(m.determinant()));
        for (size_t i = 0; i < count; ++i) {
            if (depth[i] % 2 == 1 && container[i] >= 0) continue; // a hole: below
            SvgShape s;
            std::string id = e.attr("id");
            s.name = id.empty() ? e.name : id;
            s.outline = outlines[i];
            s.fill = fills;
            s.opacity = style.opacity;
            if (fills) paintFill(s, style);
            s.stroke = strokes;
            if (strokes) {
                s.strokeColor = style.stroke.kind == Paint::Url ? gradientColor(style.stroke.url, style.stroke.color, false) : style.stroke.color;
                s.strokeColor.a *= style.strokeOpacity;
                s.strokeWidth = std::max(0.0f, style.strokeWidth * scale);
            }
            shapeOf[i] = out_.shapes.size();
            out_.shapes.push_back(std::move(s));
        }
        for (size_t i = 0; i < count; ++i) {
            if (!(depth[i] % 2 == 1 && container[i] >= 0)) continue;
            // The shape it cuts: the container's (or, nested deeper, the one that container is a hole of).
            int c = container[i];
            while (c >= 0 && !shapeOf.count(static_cast<size_t>(c))) c = container[static_cast<size_t>(c)];
            if (c >= 0) out_.shapes[shapeOf[static_cast<size_t>(c)]].holes.push_back(outlines[i]);
        }
    }

    const Gradient* gradient(const std::string& id) const {
        const auto it = gradients_.find(id);
        return it == gradients_.end() ? nullptr : &it->second;
    }
    // A gradient's stops, from the one it refers to when it has none of its own.
    std::vector<std::pair<float, Color>> stopsOf(const Gradient& g) const {
        const Gradient* at = &g;
        for (int guard = 0; guard < 8 && at && at->stops.empty() && !at->href.empty(); ++guard) at = gradient(at->href);
        return at ? at->stops : std::vector<std::pair<float, Color>>{};
    }
    Color gradientColor(const std::string& id, Color fallback, bool last) const {
        const Gradient* g = gradient(id);
        const auto stops = g ? stopsOf(*g) : std::vector<std::pair<float, Color>>{};
        if (stops.empty()) return fallback;
        return last ? stops.back().second : stops.front().second;
    }
    void paintFill(SvgShape& s, const Style& style) const {
        s.fillColor = style.fill.color;
        if (style.fill.kind == Paint::Url) {
            const Gradient* g = gradient(style.fill.url);
            const auto stops = g ? stopsOf(*g) : std::vector<std::pair<float, Color>>{};
            if (stops.size() >= 2) {
                s.fillKind = g->linear ? ShapeFill::Linear : ShapeFill::Radial;
                s.fillColor = stops.front().second;
                s.fillColor2 = stops.back().second;
                if (g->linear) s.fillAngle = std::atan2(g->y2 - g->y1, g->x2 - g->x1) * 180.0f / 3.14159265f;
            } else if (stops.size() == 1) {
                s.fillColor = stops.front().second;
            }
        }
        s.fillColor.a *= style.fillOpacity;
        s.fillColor2.a *= style.fillOpacity;
    }

    SvgDrawing& out_;
    std::map<std::string, const Element*> ids_;
    std::map<std::string, Gradient> gradients_;
    std::map<std::string, Declarations> css_;
};

} // namespace

bool parseSvg(const std::string& text, SvgDrawing& out, std::string* error) {
    out = {};
    std::unique_ptr<Element> root = XmlReader(text).read();
    const bool hasSvg = [&] {
        for (const auto& c : root->children)
            if (c->name == "svg") return true;
        return false;
    }();
    if (!hasSvg) {
        if (error) *error = "This is not an SVG file.";
        return false;
    }
    Importer(out).run(*root);
    if (out.shapes.empty()) {
        if (error) *error = "The SVG has no shapes bemotion can draw (text, images and filters are left out).";
        return false;
    }
    // No frame given: the shapes' own extent.
    if (out.size.x <= 0 || out.size.y <= 0) {
        Vec2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
        for (const SvgShape& s : out.shapes)
            for (const PathVertex& v : s.outline.vertices) mn = {std::min(mn.x, v.point.x), std::min(mn.y, v.point.y)}, mx = {std::max(mx.x, v.point.x), std::max(mx.y, v.point.y)};
        out.origin = mn;
        out.size = {std::max(1.0f, mx.x - mn.x), std::max(1.0f, mx.y - mn.y)};
    }
    return true;
}

uint64_t addSvg(Composition& comp, const std::string& text, const std::string& name, double time, std::string* error) {
    SvgDrawing drawing;
    if (!parseSvg(text, drawing, error)) return 0;
    // Fitted in the middle of the composition, 80% of it.
    const float k = std::min(comp.width * 0.8f / drawing.size.x, comp.height * 0.8f / drawing.size.y);
    const Vec2 center = drawing.origin + drawing.size * 0.5f, compCenter = comp.center();
    const Affine place = Affine::translate(compCenter) * Affine::scale(k) * Affine::translate(Vec2{0, 0} - center);
    const double in = std::max(0.0, time), out = comp.duration > in + 1e-3 ? comp.duration : in + 5.0;
    std::vector<uint64_t> ids;
    for (const SvgShape& s : drawing.shapes) {
        const BezierPath outline = transformed(s.outline, place);
        // The layer sits at the middle of its outline; its points are around it.
        Vec2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
        for (const PathVertex& v : outline.vertices) mn = {std::min(mn.x, v.point.x), std::min(mn.y, v.point.y)}, mx = {std::max(mx.x, v.point.x), std::max(mx.y, v.point.y)};
        const Vec2 at = (mn + mx) * 0.5f;
        const Affine local = Affine::translate(Vec2{0, 0} - at);
        Layer l = makeShapeLayer(ShapeKind::Path, at, out);
        l.name = comp.uniqueLayerName(s.name);
        l.inPoint = in;
        l.outPoint = out;
        l.shape.path = transformed(outline, local);
        l.shape.fillEnabled = s.fill && s.outline.closed;
        l.shape.fill = s.fillKind;
        l.shape.fillColor.base = s.fillColor;
        l.shape.fillColor2.base = s.fillColor2;
        l.shape.fillAngle.base = s.fillAngle;
        l.shape.strokeEnabled = s.stroke || (s.fill && !s.outline.closed); // an open outline is a line
        if (s.stroke) {
            l.shape.strokeColor.base = s.strokeColor;
            l.shape.strokeWidth.base = s.strokeWidth * k;
        } else if (!s.outline.closed) {
            l.shape.strokeColor.base = s.fillColor;
            l.shape.strokeWidth.base = std::max(1.0f, k);
        }
        l.transform.opacity.base = std::clamp(s.opacity, 0.0f, 1.0f) * 100.0f;
        int hole = 0;
        for (const BezierPath& h : s.holes) {
            Mask mask;
            mask.name = "Hole " + std::to_string(++hole);
            mask.mode = MaskMode::Subtract; // (a first Subtract mask cuts from the whole layer)
            mask.path = transformed(transformed(h, place), local);
            l.masks.push_back(mask);
        }
        ids.push_back(comp.addLayer(std::move(l), 0).id);
    }
    if (ids.size() == 1) return ids.front();
    const uint64_t group = comp.createCompound(ids, in);
    if (!group) return ids.front();
    Layer& g = *comp.findLayer(group);
    g.name = comp.uniqueLayerName(name.empty() ? std::string("SVG") : name);
    return group;
}

} // namespace bemotion
