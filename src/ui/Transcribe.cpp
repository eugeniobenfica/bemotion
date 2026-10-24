#include "ui/Transcribe.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "media/Transcriber.h"
#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace bemotion::ui {

namespace {

// Languages offered first (whisper knows ~100; "auto" detects it).
constexpr std::pair<const char*, const char*> kLanguages[] = {
    {"auto", "Detect automatically"}, {"en", "English"}, {"pt", "Português"}, {"es", "Español"},  {"fr", "Français"},
    {"de", "Deutsch"},                {"it", "Italiano"}, {"nl", "Nederlands"}, {"ja", "日本語"}, {"ko", "한국어"},
    {"zh", "中文"},                    {"ru", "Русский"},  {"ar", "العربية"},    {"hi", "हिन्दी"}, {"tr", "Türkçe"},
    {"pl", "Polski"},
};

const MediaItem* soundOf(const EditorContext& constCtx, uint64_t clipId, const Layer** clip) {
    auto& ctx = const_cast<EditorContext&>(constCtx); // comp() is not const; nothing changes here
    const Layer* l = ctx.comp().findLayer(clipId);
    if (!l || l->type != LayerType::Media || !l->media.hasAudio) return nullptr;
    const MediaItem* item = ctx.comp().findMedia(l->media.mediaId);
    if (!item || item->path.empty()) return nullptr;
    if (clip) *clip = l;
    return item;
}

// What the worker shares with the interface (it may outlive the dialog).
struct Job {
    std::atomic<float> progress{0.0f};
    std::atomic<bool> stop{false}, done{false};
    std::mutex mutex;
    bool ok = false;
    std::string error;
    std::vector<Caption> captions;
};

} // namespace

bool canTranscribe(const EditorContext& ctx, uint64_t clipId) { return soundOf(ctx, clipId, nullptr) != nullptr; }

void transcribeClip(EditorContext& ctx, uint64_t clipId, uint64_t subtitlesId, QWidget* parent) {
    const Layer* clip = nullptr;
    const MediaItem* item = soundOf(ctx, clipId, &clip);
    if (!item) return;
    media::TranscribeOptions options;
    if (!media::speechToTextAvailable() || !media::findSpeechModels(platform::speechModelFolders(), options)) {
        QMessageBox::information(parent, "Transcribe", media::speechToTextAvailable()
                                                           ? "The speech model is missing from this installation (assets/models)."
                                                           : "This build of bemotion has no speech recognition.");
        return;
    }

    // The language: detected, or said (more accurate when known). Remembered.
    QDialog ask(parent);
    ask.setWindowTitle("Transcribe Speech");
    auto* col = new QVBoxLayout(&ask);
    auto* about = new QLabel(QString("The words spoken in \"%1\" become subtitles, at their time. It runs on this computer.")
                                 .arg(QString::fromStdString(clip->name)));
    about->setWordWrap(true);
    col->addWidget(about);
    auto* language = new QComboBox;
    for (const auto& [code, name] : kLanguages) language->addItem(QString::fromUtf8(name), QString(code));
    language->setCurrentIndex(std::max(0, language->findData(QString::fromStdString(settings().speechLanguage))));
    col->addWidget(new QLabel("Language"));
    col->addWidget(language);
    auto* buttons = new QDialogButtonBox;
    QPushButton* go = buttons->addButton("Transcribe", QDialogButtonBox::AcceptRole);
    go->setProperty("accent", true);
    buttons->addButton(QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    col->addWidget(buttons);
    ask.setMinimumWidth(380);
    if (ask.exec() != QDialog::Accepted) return;
    options.language = language->currentData().toString().toStdString();
    settings().speechLanguage = options.language;
    saveSettings();

    // The part of the source the clip plays, and where it sits in the composition.
    const double sourceIn = std::max(0.0, clip->inPoint - clip->media.start), sourceOut = clip->outPoint - clip->media.start;
    const double offset = clip->media.start, clipIn = clip->inPoint, clipOut = clip->outPoint;
    const std::filesystem::path file = platform::fromUtf8(item->path);

    auto job = std::make_shared<Job>();
    std::thread([job, file, sourceIn, sourceOut, options] {
        std::vector<Caption> caps;
        std::string error;
        const bool ok = media::transcribe(file, sourceIn, sourceOut, options, caps,
                                          [job](float f) {
                                              job->progress = f;
                                              return !job->stop.load();
                                          },
                                          &error);
        std::lock_guard lock(job->mutex);
        job->ok = ok;
        job->error = error;
        job->captions = std::move(caps);
        job->done = true;
    }).detach();

    auto* wait = new QProgressDialog("Listening...", "Stop", 0, 100, parent);
    wait->setWindowTitle("Transcribe Speech");
    wait->setWindowModality(Qt::WindowModal);
    wait->setMinimumDuration(0);
    wait->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(wait, &QProgressDialog::canceled, wait, [job] { job->stop = true; });
    auto* timer = new QTimer(wait);
    QObject::connect(timer, &QTimer::timeout, wait, [&ctx, job, wait, timer, subtitlesId, offset, clipIn, clipOut, parent] {
        if (!job->done) {
            const int pct = static_cast<int>(job->progress * 100.0f);
            wait->setValue(std::min(pct, 99));
            wait->setLabelText(pct <= 0 ? "Loading the speech model..." : "Listening...");
            return;
        }
        timer->stop();
        std::lock_guard lock(job->mutex);
        const bool stopped = job->stop;
        QObject::disconnect(wait, &QProgressDialog::canceled, nullptr, nullptr); // closing it says "canceled" too
        wait->close();
        if (stopped) return;
        if (!job->ok) {
            QMessageBox::warning(parent, "Transcribe Speech", QString::fromStdString(job->error));
            return;
        }
        // Source time -> composition time, inside the clip.
        std::vector<Caption> caps;
        for (Caption c : job->captions) {
            c.start = std::max(clipIn, c.start + offset);
            c.end = std::min(clipOut, c.end + offset);
            if (c.end > c.start) caps.push_back(std::move(c));
        }
        if (caps.empty()) {
            QMessageBox::information(parent, "Transcribe Speech", "No speech was heard in this clip.");
            return;
        }
        Layer* target = subtitlesId ? ctx.comp().findLayer(subtitlesId) : nullptr;
        if (!target || target->type != LayerType::Text || !target->text.subtitle) target = &ctx.addSubtitles();
        // The layer lasts at least as long as its captions.
        const uint64_t id = target->id;
        const double first = caps.front().start, last = caps.back().end;
        target->text.captions = std::move(caps);
        if (target->outPoint < last) ctx.comp().setOutPoint(id, last);
        if (Layer* l = ctx.comp().findLayer(id); l && l->inPoint > first) ctx.comp().setInPoint(id, first);
        ctx.select(id);
        ctx.touch();
    });
    timer->start(100);
    wait->show();
}

} // namespace bemotion::ui
