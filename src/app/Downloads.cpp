#include "app/Downloads.h"

#include "app/Platform.h"
#include "app/PluginHost.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <memory>

namespace bemotion::net {

namespace {

QString qpath(const std::filesystem::path& p) { return QString::fromStdString(platform::toUtf8(p)); }

QNetworkAccessManager& manager() {
    static auto* m = new QNetworkAccessManager(QCoreApplication::instance());
    return *m;
}

struct SpeechModel {
    const char* url;
    const char* file;
    int64_t minBytes; // smaller: an incomplete download
};
constexpr SpeechModel kSpeechModels[] = {
    {"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin", "ggml-large-v3-turbo-q5_0.bin", 400000000},
    {"https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin", "ggml-silero-v5.1.2.bin", 500000},
};

} // namespace

QNetworkReply* fetch(const QUrl& url, QObject* context, Done done, Progress progress, const std::filesystem::path& to) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString("bemotion/%1").arg(BEMOTION_VERSION));
    request.setTransferTimeout(60000); // stalled for a minute: failed
    QNetworkReply* reply = manager().get(request);
    std::shared_ptr<QFile> file;
    if (!to.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(to.parent_path(), ec);
        file = std::make_shared<QFile>(qpath(to) + ".part");
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            reply->abort();
            reply->deleteLater();
            const QString why = "Could not write " + qpath(to);
            QMetaObject::invokeMethod(context, [done, why] { done(false, {}, why); }, Qt::QueuedConnection);
            return nullptr;
        }
        QObject::connect(reply, &QIODevice::readyRead, context, [reply, file] { file->write(reply->readAll()); });
    }
    QObject::connect(context, &QObject::destroyed, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    if (progress)
        QObject::connect(reply, &QNetworkReply::downloadProgress, context, [progress](qint64 received, qint64 total) { progress(received, total); });
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, file, to, done] {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString error = ok ? QString() : reply->errorString();
        QByteArray data;
        if (file) {
            file->write(reply->readAll());
            file->close();
            if (ok) {
                QFile::remove(qpath(to)); // a damaged older copy
                if (!file->rename(qpath(to))) return done(false, {}, "Could not save " + qpath(to));
            } else {
                file->remove();
            }
        } else {
            data = reply->readAll();
        }
        done(ok, data, error);
    });
    return reply;
}

QString sha256Hex(const QByteArray& data) { return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()); }

QString pluginCatalogBase() {
    QString base = QString::fromStdString(platform::env("BEMOTION_PLUGIN_CATALOG"));
    if (base.isEmpty()) base = "https://raw.githubusercontent.com/eugeniobenfica/bemotion/main/assets/plugins/";
    if (!base.endsWith('/')) base += '/';
    return base;
}

void fetchPluginCatalog(QObject* context, std::function<void(bool, std::vector<CatalogPlugin>, QString)> done) {
    fetch(QUrl(pluginCatalogBase() + "index.json"), context, [done](bool ok, const QByteArray& data, const QString& error) {
        if (!ok) return done(false, {}, error);
        std::vector<CatalogPlugin> plugins;
        std::string why;
        if (!parsePluginCatalog(data.toStdString(), plugins, &why)) return done(false, {}, QString::fromStdString(why));
        std::erase_if(plugins, [](const CatalogPlugin& p) { return p.api > PluginHost::kApiVersion; }); // needs a newer bemotion
        done(true, std::move(plugins), {});
    });
}

void installCatalogPlugin(const CatalogPlugin& plugin, QObject* context, std::function<void(bool, QString)> done, Progress progress) {
    // Into a folder of our own first; installed only when every file arrived as the catalog says.
    const std::filesystem::path staging = platform::appDataDir() / "downloads" / platform::fromUtf8(plugin.id);
    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    auto state = std::make_shared<std::pair<size_t, int64_t>>(0, 0); // next file, bytes so far
    auto step = std::make_shared<std::function<void()>>();
    QPointer<QObject> guard(context);
    *step = [plugin, staging, state, step, done, progress, guard] {
        if (!guard) return;
        if (state->first == plugin.files.size()) {
            std::string error;
            const bool ok = PluginHost::get().install(staging / "plugin.json", &error);
            std::error_code rec;
            std::filesystem::remove_all(staging, rec);
            *step = nullptr; // (breaks the cycle)
            return done(ok, QString::fromStdString(error));
        }
        const CatalogFile file = plugin.files[state->first];
        const QUrl url(pluginCatalogBase() + QString::fromStdString(plugin.id + "/" + file.path));
        fetch(url, guard, [plugin, staging, state, step, done, progress, file](bool ok, const QByteArray& data, const QString& error) {
            auto fail = [&](const QString& why) {
                std::error_code rec;
                std::filesystem::remove_all(staging, rec);
                *step = nullptr;
                done(false, why);
            };
            if (!ok) return fail(error);
            if (sha256Hex(data) != QString::fromStdString(file.sha256)) return fail(QString::fromStdString(file.path) + " did not arrive intact");
            const std::filesystem::path target = staging / platform::fromUtf8(file.path);
            std::error_code dec;
            std::filesystem::create_directories(target.parent_path(), dec);
            QFile out(qpath(target));
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size()) return fail("Could not write " + qpath(target));
            out.close();
            ++state->first;
            state->second += data.size();
            if (progress) progress(state->second, plugin.size());
            (*step)();
        });
    };
    (*step)();
}

std::function<void()> downloadSpeechModels(QObject* context, std::function<void(bool, QString)> done, Progress progress) {
    const std::filesystem::path folder = platform::appDataDir() / "models";
    auto current = std::make_shared<QPointer<QNetworkReply>>();
    auto index = std::make_shared<size_t>(0);
    auto step = std::make_shared<std::function<void()>>();
    QPointer<QObject> guard(context);
    *step = [folder, current, index, step, done, progress, guard] {
        if (!guard) return;
        // Skips what is already there, whole.
        while (*index < std::size(kSpeechModels)) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(folder / kSpeechModels[*index].file, ec);
            if (ec || static_cast<int64_t>(size) < kSpeechModels[*index].minBytes) break;
            ++*index;
        }
        if (*index == std::size(kSpeechModels)) {
            *step = nullptr;
            return done(true, {});
        }
        const SpeechModel& model = kSpeechModels[*index];
        *current = fetch(QUrl(model.url), guard,
                         [index, step, done, folder, model](bool ok, const QByteArray&, const QString& error) {
                             std::error_code ec;
                             const auto size = std::filesystem::file_size(folder / model.file, ec);
                             if (!ok || ec || static_cast<int64_t>(size) < model.minBytes) {
                                 if (ok) std::filesystem::remove(folder / model.file, ec); // cut short
                                 *step = nullptr;
                                 return done(false, ok ? QString("The download was incomplete.") : error);
                             }
                             ++*index;
                             (*step)();
                         },
                         *index == 0 ? progress : Progress{}, folder / model.file);
    };
    (*step)();
    return [current] {
        if (*current) (*current)->abort();
    };
}

} // namespace bemotion::net
