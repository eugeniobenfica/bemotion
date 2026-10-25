#include "ui/Transcribe.h"

#include "app/Downloads.h"
#include "app/Platform.h"
#include "app/Settings.h"
#include "media/Transcriber.h"
#include "ui/EditorContext.h"
#include "ui/PagedWindow.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPointer>
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QProgressBar>
#include <QStackedWidget>
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

// The speech model did not come with this installation (Linux and macOS builds may leave it out):
// offered for download, once; `then` runs when it is there.
void offerSpeechModel(QWidget* parent, std::function<void()> then) {
    QMessageBox ask(parent);
    ask.setWindowTitle("Transcribe Speech");
    ask.setText("Speech recognition needs its model, which is not on this computer yet.");
    ask.setInformativeText(QString("Download it now? About %1 MB, from whisper.cpp's models on Hugging Face. It is kept in "
                                   "bemotion's folder and works offline from then on.")
                               .arg(net::kSpeechModelBytes / 1000000));
    QPushButton* download = ask.addButton("Download", QMessageBox::AcceptRole);
    download->setProperty("accent", true);
    ask.addButton("Not Now", QMessageBox::RejectRole);
    ask.setDefaultButton(download);
    ask.exec();
    if (ask.clickedButton() != download) return;

    auto* dialog = new QDialog(parent);
    dialog->setWindowTitle("Downloading the Speech Model");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMinimumWidth(420);
    auto* col = new QVBoxLayout(dialog);
    col->setContentsMargins(24, 20, 24, 18);
    col->setSpacing(8);
    auto* stage = new QLabel("Starting...");
    col->addWidget(stage);
    auto* bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    col->addWidget(bar);
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* cancel = new QPushButton("Cancel");
    row->addWidget(cancel);
    col->addLayout(row);
    auto finished = std::make_shared<bool>(false);
    const auto stop = net::downloadSpeechModels(
        dialog,
        [dialog, parent, then, finished](bool ok, QString error) {
            *finished = true;
            dialog->close();
            if (!ok) {
                if (!error.isEmpty()) QMessageBox::warning(parent, "Transcribe Speech", "The speech model could not be downloaded: " + error);
                return;
            }
            then();
        },
        [stage, bar](int64_t received, int64_t total) {
            if (total <= 0) total = net::kSpeechModelBytes;
            bar->setValue(static_cast<int>(received * 1000 / std::max<int64_t>(1, total)));
            stage->setText(QString("%1 of %2 MB").arg(received / 1000000).arg(total / 1000000));
        });
    QObject::connect(cancel, &QPushButton::clicked, dialog, [stop, dialog] {
        stop();
        dialog->close();
    });
    QObject::connect(dialog, &QDialog::rejected, dialog, [stop, finished] {
        if (!*finished) stop();
    });
    dialog->show();
}

bool canTranscribe(const EditorContext& ctx, uint64_t clipId) { return soundOf(ctx, clipId, nullptr) != nullptr; }

void transcribeClip(EditorContext& ctx, uint64_t clipId, uint64_t subtitlesId, QWidget* parent) {
    const Layer* clip = nullptr;
    const MediaItem* item = soundOf(ctx, clipId, &clip);
    if (!item) return;
    media::TranscribeOptions options;
    if (!media::speechToTextAvailable()) {
        QMessageBox::information(parent, "Transcribe", "This build of bemotion has no speech recognition (it was built without whisper.cpp).");
        return;
    }
    if (!media::findSpeechModels(platform::speechModelFolders(), options)) {
        QPointer<QWidget> guard(parent);
        offerSpeechModel(parent, [&ctx, clipId, subtitlesId, guard] {
            if (guard) transcribeClip(ctx, clipId, subtitlesId, guard);
        });
        return;
    }

    // One window, two steps: the language (detected, or said: more accurate), then the progress.
    // It does not block the editor: the captions land when it is done.
    auto* dialog = new QDialog(parent);
    dialog->setWindowTitle("Transcribe Speech");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMinimumWidth(440);
    auto* col = new QVBoxLayout(dialog);
    col->setContentsMargins(24, 20, 24, 18);
    col->setSpacing(8);
    auto* title = pageTitle("Subtitles from speech");
    col->addWidget(title);
    auto* about = new QLabel(QString("The words spoken in \"%1\" become captions, each at its time. It all runs on this "
                                     "computer: nothing is sent anywhere.")
                                 .arg(QString::fromStdString(clip->name)));
    about->setWordWrap(true);
    setTone(about, Tone::TextDim);
    col->addWidget(about);
    col->addSpacing(6);

    auto* pages = new QStackedWidget;
    col->addWidget(pages);
    // Step 1: the language.
    auto* setup = new QWidget;
    auto* setupCol = new QVBoxLayout(setup);
    setupCol->setContentsMargins(0, 0, 0, 0);
    auto* card = new SettingsCard;
    auto* language = new QComboBox;
    for (const auto& [code, name] : kLanguages) language->addItem(QString::fromUtf8(name), QString(code));
    language->setCurrentIndex(std::max(0, language->findData(QString::fromStdString(settings().speechLanguage))));
    language->setMinimumWidth(190);
    card->addRow("Language", "Choosing it is more accurate than detecting it", language);
    setupCol->addWidget(card);
    pages->addWidget(setup);
    // Step 2: what it is doing, how far, how long.
    auto* progress = new QWidget;
    auto* progressCol = new QVBoxLayout(progress);
    progressCol->setContentsMargins(0, 4, 0, 0);
    progressCol->setSpacing(8);
    auto* stage = new QLabel("Loading the speech model...");
    progressCol->addWidget(stage);
    auto* bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    progressCol->addWidget(bar);
    auto* timing = new QLabel(" ");
    setTone(timing, Tone::TextFaint);
    progressCol->addWidget(timing);
    auto* note = new QLabel("You can keep editing meanwhile.");
    setTone(note, Tone::TextFaint);
    progressCol->addWidget(note);
    pages->addWidget(progress);

    col->addSpacing(10);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton("Cancel");
    cancel->setMinimumWidth(100);
    auto* go = accentButton("Transcribe");
    go->setMinimumWidth(120);
    go->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(go);
    col->addLayout(buttons);

    auto job = std::make_shared<Job>();
    auto started = std::make_shared<QElapsedTimer>();
    QObject::connect(cancel, &QPushButton::clicked, dialog, [dialog, job] {
        job->stop = true; // (nothing to stop before it starts)
        dialog->close();
    });
    QObject::connect(dialog, &QDialog::rejected, dialog, [job] { job->stop = true; });
    // The part of the source the clip plays, and where it sits in the composition.
    const double sourceIn = std::max(0.0, clip->inPoint - clip->media.start), sourceOut = clip->outPoint - clip->media.start;
    const double offset = clip->media.start, clipIn = clip->inPoint, clipOut = clip->outPoint;
    const std::filesystem::path file = platform::fromUtf8(item->path);

    QObject::connect(go, &QPushButton::clicked, dialog, [=, &ctx]() mutable {
        options.language = language->currentData().toString().toStdString();
        settings().speechLanguage = options.language;
        saveSettings();
        pages->setCurrentIndex(1);
        go->hide();
        cancel->setText("Stop");
        started->start();
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

        auto* timer = new QTimer(dialog);
        QObject::connect(timer, &QTimer::timeout, dialog, [&ctx, job, dialog, timer, stage, bar, timing, started, subtitlesId, offset,
                                                           clipIn, clipOut, parent] {
            if (!job->done) {
                const float f = job->progress;
                bar->setValue(std::min(990, static_cast<int>(f * 1000.0f)));
                stage->setText(f <= 0.0f ? "Loading the speech model..." : QString("Listening...  %1%").arg(static_cast<int>(f * 100.0f)));
                const double elapsed = started->elapsed() / 1000.0;
                auto clock = [](double sec) {
                    const int t = static_cast<int>(std::lround(std::max(0.0, sec)));
                    return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
                };
                timing->setText(f > 0.02f ? QString("%1 elapsed  ·  about %2 left").arg(clock(elapsed)).arg(clock(elapsed / f * (1.0f - f)))
                                          : QString("%1 elapsed").arg(clock(elapsed)));
                return;
            }
            timer->stop();
            std::lock_guard lock(job->mutex);
            const bool stopped = job->stop;
            if (stopped) return;
            dialog->hide();
            if (!job->ok) {
                QMessageBox::warning(parent, "Transcribe Speech", QString::fromStdString(job->error));
                dialog->close();
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
                dialog->close();
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
            dialog->close();
        });
        timer->start(100);
    });
    dialog->show();
}

} // namespace bemotion::ui
