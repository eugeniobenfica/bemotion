#pragma once

// Data for plugin objects: CSV text in and out, and the two shapes plugin
// objects keep it in: chart rows (label, value, color) and plain tables.

#include "core/Layer.h"

#include <string>
#include <vector>

namespace bemotion {

using Cells = std::vector<std::vector<std::string>>;

// CSV / TSV text: the delimiter (comma, semicolon or tab) is detected; quoted
// cells may hold delimiters, quotes ("") and line breaks. Empty lines are skipped.
Cells parseCsv(const std::string& text);
std::string toCsv(const Cells& cells); // comma separated, quoted where needed

// A number written by people: "1,234.5", "1.234,5", "R$ 12", "45%", "-3".
bool parseNumber(const std::string& cell, float& out);

// Chart rows: the first text column is the label, the first numeric column the value,
// a "#RRGGBB" cell the color (else label colors in turn). A header line is skipped.
std::vector<DataRow> rowsFromCells(const Cells& cells);
// A table: the first line is the header; every row gets as many cells as the widest line.
DataTable tableFromCells(const Cells& cells);
Cells cellsOf(const DataTable& table); // header first
Cells cellsOf(const std::vector<DataRow>& rows);

// Subtitles in SubRip (.srt) text: numbered blocks of "00:00:01,500 --> 00:00:03,000" and
// text lines (also "." separators and WebVTT-style headers). Out of order input is kept as read.
std::vector<Caption> parseSrt(const std::string& text);
// A time as people write it: "1.5", "0:01.5", "00:00:01,500" (seconds; false if not a time).
bool parseCaptionTime(const std::string& text, double& seconds);
std::string toSrt(const std::vector<Caption>& captions);

} // namespace bemotion
