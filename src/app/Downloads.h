#pragma once

// The few things bemotion fetches from the internet, and only when the user asks for them:
// the official plugins (packages leave them out; from bemotion's repository on GitHub,
// core/PluginCatalog.h), the speech-to-text model (from whisper.cpp's models on Hugging Face)
// when it did not come with the app, and new versions (bemotion's releases on GitHub,
// core/Updates.h) once the user turned updates on — the one thing that may be looked for
// without a click, at most once a day (app/Updater).

#include "core/PluginCatalog.h"
#include "core/Updates.h"

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

// Where the official plugin catalog is: index.json, and each plugin's files under <base>/<id>/,
// in bemotion's repository at this version's tag (v<version>), never the main branch.
// BEMOTION_PLUGIN_CATALOG replaces it (a mirror, or a local folder as file:///.../plugins/).
QString pluginCatalogBase();
// The catalog, read (plugins of a newer API than this bemotion's are left out).
void fetchPluginCatalog(QObject* context, std::function<void(bool ok, std::vector<CatalogPlugin> plugins, QString error)> done);
// Downloads a catalog plugin's files, checks each against the catalog, and installs it
// (PluginHost::install: into the user plugins folder; it replaces a bundled one of that id).
void installCatalogPlugin(const CatalogPlugin& plugin, QObject* context, std::function<void(bool ok, QString error)> done,
                          Progress progress = {});

// bemotion's releases (the GitHub API's list). BEMOTION_UPDATE_FEED replaces the address (a
// mirror, or a file:///.../releases.json for tests).
QString updateFeed();
void fetchReleases(QObject* context, std::function<void(bool ok, std::vector<Release> releases, QString error)> done);
// A release's package into <app data>/updates, checked against the release's SHA256SUMS.txt (a
// package without its sum is refused). `done` gets the file. Returns what cancels it.
std::function<void()> downloadUpdate(const Release& release, const ReleaseAsset& package, QObject* context,
                                     std::function<void(bool ok, std::filesystem::path file, QString error)> done,
                                     Progress progress = {});

// The speech models (what tools/fetch-speech-model.sh fetches) into the app data's models
// folder, where transcription looks. About 550 MB.
constexpr int64_t kSpeechModelBytes = 574000000;
// Returns what cancels it.
std::function<void()> downloadSpeechModels(QObject* context, std::function<void(bool ok, QString error)> done, Progress progress = {});

} // namespace bemotion::net
