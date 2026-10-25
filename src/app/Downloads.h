#pragma once

// The few things bemotion fetches from the internet, and only when the user asks for them
// (never at start or in the background): the official plugins a release leaves out (from
// bemotion's repository on GitHub, core/PluginCatalog.h) and the speech-to-text model (from
// whisper.cpp's models on Hugging Face) when it did not come with the app.

#include "core/PluginCatalog.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

class QObject;
class QNetworkReply;

namespace bemotion::net {

using Done = std::function<void(bool ok, const QByteArray& data, const QString& error)>;
using Progress = std::function<void(int64_t received, int64_t total)>; // total -1: unknown

// A file into memory, or (with `to`) onto disk: written as <to>.part, renamed when complete.
// Abort the reply to stop (then `done` gets ok = false). Nothing happens after `context` goes.
QNetworkReply* fetch(const QUrl& url, QObject* context, Done done, Progress progress = {}, const std::filesystem::path& to = {});

QString sha256Hex(const QByteArray& data);

// Where the official plugin catalog is: index.json, and each plugin's files under <base>/<id>/.
// BEMOTION_PLUGIN_CATALOG replaces it (a mirror, or a local folder as file:///.../plugins/).
QString pluginCatalogBase();
// The catalog, read (plugins of a newer API than this bemotion's are left out).
void fetchPluginCatalog(QObject* context, std::function<void(bool ok, std::vector<CatalogPlugin> plugins, QString error)> done);
// Downloads a catalog plugin's files, checks each against the catalog, and installs it
// (PluginHost::install: into the user plugins folder; it replaces a bundled one of that id).
void installCatalogPlugin(const CatalogPlugin& plugin, QObject* context, std::function<void(bool ok, QString error)> done,
                          Progress progress = {});

// The speech models (what tools/fetch-speech-model.sh fetches) into the app data's models
// folder, where transcription looks. About 550 MB.
constexpr int64_t kSpeechModelBytes = 574000000;
// Returns what cancels it.
std::function<void()> downloadSpeechModels(QObject* context, std::function<void(bool ok, QString error)> done, Progress progress = {});

} // namespace bemotion::net
