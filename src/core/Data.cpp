#include "core/Data.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace bemotion {

namespace {

char detectDelimiter(const std::string& text) {
    // The candidate seen most often outside quotes on the first line.
    int comma = 0, semicolon = 0, tab = 0;
    bool quoted = false;
    for (char c : text) {
        if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '\n' || c == '\r')) break;
        else if (!quoted && c == ',') ++comma;
        else if (!quoted && c == ';') ++semicolon;
        else if (!quoted && c == '\t') ++tab;
    }
    if (tab >= comma && tab >= semicolon && tab > 0) return '\t';
    return semicolon > comma ? ';' : ',';
}

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool isHexColor(const std::string& s) {
    return (s.size() == 7 || s.size() == 9) && s[0] == '#' &&
           std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isxdigit(c); });
}

} // namespace

Cells parseCsv(const std::string& source) {
    std::string text = source;
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3); // UTF-8 BOM (spreadsheet exports)
    const char delim = detectDelimiter(text);
    Cells out;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, wasQuoted = false;
    auto endCell = [&] {
        row.push_back(wasQuoted ? cell : trimmed(cell));
        cell.clear();
        wasQuoted = false;
    };
    auto endRow = [&] {
        endCell();
        if (!(row.size() == 1 && row[0].empty())) out.push_back(std::move(row));
        row.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') cell += '"', ++i;
            else if (c == '"') quoted = false;
            else cell += c;
        } else if (c == '"' && trimmed(cell).empty()) {
            cell.clear();
            quoted = wasQuoted = true;
        } else if (c == delim) {
            endCell();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            endRow();
        } else {
            cell += c;
        }
    }
    if (!cell.empty() || !row.empty()) endRow();
    return out;
}

std::string toCsv(const Cells& cells) {
    std::string out;
    for (const auto& row : cells) {
        for (size_t i = 0; i < row.size(); ++i) {
            const std::string& c = row[i];
            if (i) out += ',';
            if (c.find_first_of(",\"\n\r") != std::string::npos) {
                out += '"';
                for (char ch : c) out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
                out += '"';
            } else {
                out += c;
            }
        }
        out += '\n';
    }
    return out;
}

bool parseNumber(const std::string& cell, float& out) {
    // Keep digits, signs and separators; anything else (currency, %, spaces) is decoration.
    std::string s;
    bool digit = false;
    // Letters only count as decoration next to a currency sign ("R$ 12", "US$ 5"): "Q1" is a label.
    const std::string t = trimmed(cell);
    const bool currency = t.find('$') != std::string::npos ||
                          std::any_of(t.begin(), t.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
    for (unsigned char c : t) {
        if (std::isdigit(c)) digit = true, s += static_cast<char>(c);
        else if (c == '-' || c == '+' || c == '.' || c == ',') s += static_cast<char>(c);
        else if (c == 'e' || c == 'E') {
            if (!digit) return false;
            s += 'e';
        } else if (std::isalpha(c) && !currency) {
            return false; // a word, not a decorated number
        }
    }
    if (!digit) return false;
    // The last of '.' / ',' is the decimal mark when both appear; a lone ',' is decimal unless it groups thousands.
    const size_t dot = s.rfind('.'), comma = s.rfind(',');
    if (dot != std::string::npos && comma != std::string::npos) {
        const char thousands = dot > comma ? ',' : '.';
        s.erase(std::remove(s.begin(), s.end(), thousands), s.end());
        std::replace(s.begin(), s.end(), ',', '.');
    } else if (comma != std::string::npos) {
        const bool grouping = std::count(s.begin(), s.end(), ',') > 1 || s.size() - comma == 4;
        if (grouping) s.erase(std::remove(s.begin(), s.end(), ','), s.end());
        else std::replace(s.begin(), s.end(), ',', '.');
    } else if (dot != std::string::npos && std::count(s.begin(), s.end(), '.') > 1) {
        s.erase(std::remove(s.begin(), s.end(), '.'), s.end()); // 1.234.567
    }
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str()) return false;
    out = v;
    return true;
}

std::vector<DataRow> rowsFromCells(const Cells& cells) {
    std::vector<DataRow> rows;
    if (cells.empty()) return rows;
    // Columns: the value is the first column that is numeric in most lines; the label the first other one.
    size_t width = 0;
    for (const auto& r : cells) width = std::max(width, r.size());
    int valueCol = -1, labelCol = -1, colorCol = -1;
    for (size_t c = 0; c < width; ++c) {
        int numeric = 0, colors = 0, filled = 0;
        for (const auto& r : cells) {
            if (c >= r.size() || r[c].empty()) continue;
            ++filled;
            float v;
            if (isHexColor(r[c])) ++colors;
            else if (parseNumber(r[c], v)) ++numeric;
        }
        if (filled == 0) continue;
        if (colors * 2 > filled && colorCol < 0) colorCol = static_cast<int>(c);
        else if (numeric * 2 >= filled && valueCol < 0 && static_cast<int>(c) != labelCol) valueCol = static_cast<int>(c);
        else if (labelCol < 0) labelCol = static_cast<int>(c);
    }
    if (valueCol < 0) return rows;
    for (const auto& r : cells) {
        float v;
        if (valueCol >= static_cast<int>(r.size()) || !parseNumber(r[valueCol], v)) continue; // the header, notes
        DataRow row;
        row.label = labelCol >= 0 && labelCol < static_cast<int>(r.size()) ? r[labelCol] : "Item " + std::to_string(rows.size() + 1);
        row.value = v;
        row.color = labelColor(static_cast<int>(rows.size()));
        if (colorCol >= 0 && colorCol < static_cast<int>(r.size()) && isHexColor(r[colorCol])) {
            const unsigned hex = static_cast<unsigned>(std::strtoul(r[colorCol].c_str() + 1, nullptr, 16));
            row.color = r[colorCol].size() == 7 ? Color::fromHex(hex) : Color::fromHex(hex >> 8, (hex & 0xFF) / 255.0f);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

DataTable tableFromCells(const Cells& cells) {
    DataTable t;
    if (cells.empty()) return t;
    size_t width = 0;
    for (const auto& r : cells) width = std::max(width, r.size());
    t.header = cells.front();
    t.header.resize(width);
    for (size_t i = 1; i < cells.size(); ++i) {
        std::vector<std::string> row = cells[i];
        row.resize(width);
        t.rows.push_back(std::move(row));
    }
    return t;
}

Cells cellsOf(const DataTable& table) {
    Cells out;
    out.push_back(table.header);
    for (const auto& r : table.rows) out.push_back(r);
    return out;
}

Cells cellsOf(const std::vector<DataRow>& rows) {
    Cells out;
    out.push_back({"label", "value", "color"});
    for (const DataRow& r : rows) {
        char value[32], color[16];
        std::snprintf(value, sizeof value, "%g", r.value);
        auto byte = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        std::snprintf(color, sizeof color, "#%02X%02X%02X", byte(r.color.r), byte(r.color.g), byte(r.color.b));
        out.push_back({r.label, value, color});
    }
    return out;
}

} // namespace bemotion
