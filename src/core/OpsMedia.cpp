// Operations on plugin values and data, media and media folders (core/OpsInternal.h).

#include "core/OpsInternal.h"

#include "core/Arrange.h"
#include "core/AudioDsp.h"
#include "core/Data.h"
#include "core/Editing.h"
#include "core/Expressions.h"
#include "core/Geometry.h"
#include "core/Serialization.h"
#include "core/Suggest.h"
#include "core/Svg.h"
#include "core/Transitions.h"
#include "core/Validate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace bemotion::opsimpl {

// A plugin object's toggle, choice, text or file parameter (numbers and colors are
// properties: property.set with "p.<id>").
json Applier::paramSet(const json& op) {
    Layer& l = layer(op);
    if (l.type != LayerType::Plugin) fail("layer \"" + l.name + "\" is not a plugin object (" + typeName(l) + ")");
    const std::string id = text(field(op, "param"), "param");
    PluginParam* p = l.plugin.find(id);
    if (!p) {
        std::vector<std::string> ids;
        for (const PluginParam& q : l.plugin.params)
            ids.push_back(q.id);
        fail("\"" + l.name + "\" has no parameter \"" + id + "\"" + suggest(id, ids));
    }
    const json& v = field(op, "value");
    switch (p->kind) {
    case PluginParamKind::Number:
    case PluginParamKind::Color: fail("\"" + id + "\" is animatable: use property.set with \"prop\": \"p." + id + "\"");
    case PluginParamKind::Toggle: p->toggle = boolean(v, "value"); break;
    case PluginParamKind::Text: p->text = text(v, "value"); break;
    case PluginParamKind::Choice: {
        const std::string c = text(v, "value");
        const auto it = std::find(p->choices.begin(), p->choices.end(), c);
        if (it == p->choices.end()) fail("\"" + c + "\" is not a choice of \"" + id + "\"" + suggest(c, p->choices));
        p->choice = static_cast<int>(it - p->choices.begin());
        break;
    }
    case PluginParamKind::File: {
        const std::string path = text(v, "value");
        if (!path.empty()) {
            std::string ext =
                path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.') + 1);
            for (char& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!p->accept.empty() && std::find(p->accept.begin(), p->accept.end(), ext) == p->accept.end())
                fail("\"" + id + "\" takes " + [&] {
                    std::string list;
                    for (const auto& a : p->accept)
                        list += (list.empty() ? "." : ", .") + a;
                    return list;
                }() + " files");
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), ec))
                fail("there is no file \"" + path + "\"");
        }
        p->text = path;
        p->relative.clear();
        break;
    }
    }
    return json::object();
}

// The data of a plugin object (charts: rows; tables: a table), from JSON or CSV text.
json Applier::dataSet(const json& op) {
    Layer& l = layer(op);
    PluginProps& p = l.plugin;
    if (l.type != LayerType::Plugin || (!p.hasRows && !p.hasTable))
        fail("layer \"" + l.name + "\" holds no data (" + typeName(l) + ")");
    if (has(op, "csv")) {
        const Cells cells = parseCsv(text(op["csv"], "csv"));
        if (p.hasRows) {
            std::vector<DataRow> rows = rowsFromCells(cells);
            if (rows.empty()) fail("no \"label, value\" lines found in the CSV");
            p.rows = std::move(rows);
        } else {
            if (cells.empty()) fail("the CSV is empty");
            p.table = tableFromCells(cells);
        }
        return json::object();
    }
    if (has(op, "rows")) {
        if (!p.hasRows) fail("this object takes a \"table\", not \"rows\"");
        if (!op["rows"].is_array())
            fail("\"rows\" must be a list of {\"label\", \"value\", \"color\"} (or [label, value, color])");
        std::vector<DataRow> rows;
        for (const json& r : op["rows"]) {
            DataRow row;
            row.color = labelColor(static_cast<int>(rows.size()));
            if (r.is_array() && r.size() >= 2) {
                row.label = r[0].is_string() ? r[0].get<std::string>() : r[0].dump();
                row.value = static_cast<float>(number(r[1], "value"));
                if (r.size() > 2) row.color = colorOf(r[2], "color");
            } else if (r.is_object()) {
                row.label = has(r, "label") ? text(r["label"], "label") : "";
                row.value = static_cast<float>(number(field(r, "value"), "value"));
                if (has(r, "color")) row.color = colorOf(r["color"], "color");
            } else {
                fail("each row must be {\"label\", \"value\"(, \"color\")} or [label, value(, color)]");
            }
            rows.push_back(std::move(row));
        }
        p.rows = std::move(rows);
        return json::object();
    }
    if (has(op, "table")) {
        if (!p.hasTable) fail("this object takes \"rows\", not a \"table\"");
        const json& t = op["table"];
        if (!t.is_object() || !has(t, "rows") || !t["rows"].is_array())
            fail("\"table\" must be {\"header\": [...], \"rows\": [[...], ...]}");
        auto cell = [](const json& c) { return c.is_string() ? c.get<std::string>() : c.dump(); };
        Cells cells;
        std::vector<std::string> header;
        if (has(t, "header") && t["header"].is_array())
            for (const json& h : t["header"])
                header.push_back(cell(h));
        cells.push_back(header);
        for (const json& r : t["rows"]) {
            if (!r.is_array()) fail("every table row must be a list of cells");
            std::vector<std::string> row;
            for (const json& c : r)
                row.push_back(cell(c));
            cells.push_back(std::move(row));
        }
        p.table = tableFromCells(cells);
        return json::object();
    }
    fail("give \"rows\", \"table\" or \"csv\"");
}

json Applier::mediaImport(const json& op) {
    const std::string path = text(field(op, "path"), "path");
    if (!env_.probeMedia) fail("importing media is not available here");
    MediaItem item;
    std::string error;
    if (!env_.probeMedia(path, item, error)) fail("could not import \"" + path + "\": " + error);
    const uint64_t id = c_.addMedia(item);
    const MediaItem& m = *c_.findMedia(id);
    return {{"media", id},      {"name", m.name},     {"kind", mediaKindLabel(m.kind)},
            {"width", m.width}, {"height", m.height}, {"duration", m.duration}};
}

// Media folders: names only, to keep the Media panel tidy (nothing moves on disk).
MediaItem& Applier::mediaItem(const json& op) {
    const json& ref = field(op, "media");
    if (!ref.is_number_integer()) fail("\"media\" must be a media id (from media.import or query.composition)");
    if (MediaItem* m = c_.findMedia(ref.get<uint64_t>())) return *m;
    fail("there is no media item with id " + std::to_string(ref.get<uint64_t>()));
}

std::string Applier::folderName(const json& op, const char* key, bool mustExist) {
    const std::string name = text(field(op, key), key);
    const bool exists = std::find(c_.mediaFolders.begin(), c_.mediaFolders.end(), name) != c_.mediaFolders.end();
    if (mustExist && !exists) fail("there is no media folder named \"" + name + "\"" + suggest(name, c_.mediaFolders));
    return name;
}

std::string Applier::newFolderName(const json& op, const char* key) {
    std::string name = text(field(op, key), key);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.pop_back();
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.erase(name.begin());
    if (name.empty()) fail("a folder needs a name");
    if (std::find(c_.mediaFolders.begin(), c_.mediaFolders.end(), name) != c_.mediaFolders.end())
        fail("there is already a media folder named \"" + name + "\"");
    return name;
}

json Applier::folderAdd(const json& op) {
    const std::string name = newFolderName(op, "name");
    c_.mediaFolders.push_back(name);
    return {{"folder", name}};
}

json Applier::folderRename(const json& op) {
    const std::string from = folderName(op, "folder", true);
    const std::string to = newFolderName(op, "name");
    std::replace(c_.mediaFolders.begin(), c_.mediaFolders.end(), from, to);
    for (MediaItem& m : c_.media)
        if (m.folder == from) m.folder = to;
    return {{"folder", to}};
}

json Applier::mediaMove(const json& op) {
    MediaItem& m = mediaItem(op);
    const std::string folder =
        has(op, "folder") && !(op["folder"].is_string() && op["folder"].get<std::string>().empty())
            ? folderName(op, "folder", true)
            : std::string();
    m.folder = folder;
    return json::object();
}

} // namespace bemotion::opsimpl
