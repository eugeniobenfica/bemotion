#pragma once

// Drag-and-drop payloads between panels.

#include <QByteArray>
#include <QMimeData>

#include <cstdint>
#include <cstring>
#include <string>

namespace bemotion::ui {

// An object of the Objects panel: -1 text, -2 camera, kDropPlugin - i plugin type i, else a ShapeKind.
inline constexpr const char* kObjectMime = "application/x-bemotion-object";
inline constexpr int kDropCamera = -2;
inline constexpr int kDropBackground = -3;
inline constexpr int kDropSubtitles = -4;
inline constexpr int kDropCounter = -5;
inline constexpr int kDropPlugin = -100;
// A Media panel item: its media id.
inline constexpr const char* kMediaMime = "application/x-bemotion-media";
// A tool of the Tools popover: its index in toolCatalog().
inline constexpr const char* kToolMime = "application/x-bemotion-tool";
// The toolbar itself, dragged by its grip (to the viewport, or back to the timeline).
inline constexpr const char* kToolbarMime = "application/x-bemotion-toolbar";
inline constexpr const char* kTransitionMime = "application/x-bemotion-transition";
// A Quick Animation: the preset as JSON (core/Presets.h format).
inline constexpr const char* kAnimationMime = "application/x-bemotion-animation";

template <typename T>
QMimeData* makeMime(const char* type, T value) {
    auto* m = new QMimeData;
    m->setData(type, QByteArray(reinterpret_cast<const char*>(&value), sizeof(T)));
    return m;
}

inline QMimeData* makeTextMime(const char* type, const std::string& value) {
    auto* m = new QMimeData;
    m->setData(type, QByteArray::fromStdString(value));
    return m;
}

inline bool readTextMime(const QMimeData* m, const char* type, std::string& out) {
    if (!m || !m->hasFormat(type)) return false;
    out = m->data(type).toStdString();
    return !out.empty();
}

template <typename T>
bool readMime(const QMimeData* m, const char* type, T& out) {
    if (!m || !m->hasFormat(type)) return false;
    const QByteArray data = m->data(type);
    if (data.size() != static_cast<qsizetype>(sizeof(T))) return false;
    std::memcpy(&out, data.constData(), sizeof(T));
    return true;
}

} // namespace bemotion::ui
