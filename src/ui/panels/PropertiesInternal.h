#pragma once

// What the Properties panel's source files share (PropertiesPanel.cpp: the panel, masks, the
// composition and a layer's main rows; PropertiesSections.cpp: media, sound, shapes, time,
// blending, mask rows; PropertiesData.cpp: captions and plugin objects' data). Internal to them.

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Automation.h"
#include "app/PresetLibrary.h"
#include "core/Transitions.h"
#include "app/Settings.h"
#include "core/Data.h"
#include "core/Editing.h"
#include "core/Geometry.h"
#include "core/Ops.h"
#include "core/Presets.h"
#include "media/MediaManager.h"
#include "render/Fonts.h"
#include "ui/EditorContext.h"
#include "ui/LayerMenus.h"
#include "ui/Modules.h"
#include "ui/PluginInstall.h"
#include "ui/PropertyRow.h"
#include "ui/Transcribe.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bemotion::properties_internal {

inline QLabel* faint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextFaint);
    return l;
}

inline QLabel* dim(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    ui::setTone(l, ui::Tone::TextDim);
    return l;
}

// Combo boxes size to the panel, not to their longest entry (font names can be long).
inline QComboBox* newCombo() {
    auto* c = new QComboBox;
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(6);
    return c;
}

inline bool hasAnimatedParams(const Layer& layer, const std::string& group) {
    return std::any_of(layer.plugin.params.begin(), layer.plugin.params.end(), [&](const PluginParam& p) {
        return p.group == group && (p.kind == PluginParamKind::Number || p.kind == PluginParamKind::Color);
    });
}

inline std::string colorHex(Color c) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

// "label, value[, #RRGGBB]" per line; tabs (spreadsheet paste) and semicolons work too.
inline std::vector<DataRow> parseRows(const std::string& text) {
    std::vector<DataRow> rows;
    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        for (char& c : line)
            if (c == '\t' || c == ';') c = ',';
        std::vector<std::string> cells;
        std::stringstream ls(line);
        std::string cell;
        while (std::getline(ls, cell, ',')) {
            const size_t a = cell.find_first_not_of(" \r\"");
            const size_t b = cell.find_last_not_of(" \r\"");
            cells.push_back(a == std::string::npos ? "" : cell.substr(a, b - a + 1));
        }
        if (cells.size() < 2) continue;
        char* end = nullptr;
        const float v = std::strtof(cells[1].c_str(), &end);
        if (end == cells[1].c_str()) continue; // header line
        DataRow r;
        r.label = cells[0];
        r.value = v;
        r.color = labelColor(static_cast<int>(rows.size()));
        if (cells.size() > 2 && cells[2].size() == 7 && cells[2][0] == '#')
            r.color = Color::fromHex(static_cast<unsigned>(std::strtoul(cells[2].c_str() + 1, nullptr, 16)));
        rows.push_back(std::move(r));
    }
    return rows;
}

// A combo box of `labels`, at `current`; picking one runs `pick` with its index.
template <size_t N>
inline QComboBox* choiceCombo(const char* const (&labels)[N], int current, std::function<void(int)> pick) {
    auto* combo = newCombo();
    for (const char* label : labels) combo->addItem(tr(label));
    combo->setCurrentIndex(current);
    QObject::connect(combo, &QComboBox::activated, combo, [pick = std::move(pick)](int i) {
        if (i >= 0) pick(i);
    });
    return combo;
}

} // namespace bemotion::properties_internal
