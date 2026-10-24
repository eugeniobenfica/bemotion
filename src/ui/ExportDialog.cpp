// Video and portable project export: settings, progress and result.

#include "ui/Dialogs.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "app/ProjectPackage.h"
#include "export/Export.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "ui/EditorContext.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <cmath>
#include <cstdio>

namespace bemotion::ui {

namespace {

struct ScaleOption {
    const char* label;
    float scale;
};
constexpr ScaleOption kScales[] = {{"Full size", 1.0f}, {"Half size", 0.5f}, {"Quarter size", 0.25f}, {"Double (2x)", 2.0f}};
constexpr const char* kQualities[] = {"Low (smaller file)", "Medium", "High"};

std::string formatDuration(double seconds) {
    const int s = static_cast<int>(std::lround(std::max(0.0, seconds)));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

QLabel* colored(const QString& text, Rgba color) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet(QString("color: %1;").arg(theme::q(color).name()));
    return l;
}

// Remembered between exports in this session, like the old dialog.
struct Remembered {
    exporting::Settings settings;
    int scaleIndex = 0;
    bool customRange = false;
    bool package = false;
    double rangeStart = 0.0, rangeEnd = 0.0;
};
Remembered& remembered() {
    static Remembered r;
    return r;
}

class ExportDialog : public QDialog {
public:
    ExportDialog(EditorContext& ctx, QWidget* parent) : QDialog(parent), ctx_(ctx) {
        setWindowTitle("Export");
        setMinimumWidth(560);
        Remembered& r = remembered();
        exporting::Settings& s = r.settings;
        const Composition& comp = ctx.comp();
        if (s.output.empty()) {
            const auto folder = ctx.path.empty() ? projectsFolderOrDocuments() : ctx.path.parent_path();
            s.output = folder / platform::fromUtf8(ctx.project.name);
            s.format = exporting::formatAvailable(s.format) ? s.format : exporting::Format::PngSequence;
            s.output.replace_extension(exporting::formatExtension(s.format));
        }
        if (!r.customRange || r.rangeEnd > comp.duration) {
            r.rangeStart = 0.0;
            r.rangeEnd = comp.duration;
        }
        pages_ = new QStackedWidget;
        pages_->addWidget(buildSettings());
        pages_->addWidget(buildProgress());
        pages_->addWidget(buildResult());
        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(24, 22, 24, 18);
        col->addWidget(pages_);
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, [this] { stepExport(); });
        refreshSettings();
    }
    ~ExportDialog() override {
        timer_->stop();
        if (packageFuture_.valid()) {
            packageCancel_->store(true);
            packageFuture_.wait();
        }
    }

protected:
    void reject() override {
        if (packageRunning_) {
            packageCancel_->store(true);
            progressInfo_->setText("Cancelling...");
            return;
        }
        if (job_.running()) { // Esc / close while exporting cancels it
            job_.cancel();
            showResult();
            return;
        }
        QDialog::reject();
    }

private:
    QWidget* buildSettings() {
        using namespace exporting;
        Remembered& r = remembered();
        auto* page = new QWidget;
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        form_ = new QFormLayout;
        auto* form = form_;
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(8);

        format_ = new QComboBox;
        for (int i = 0; i < static_cast<int>(Format::Count); ++i) {
            const auto f = static_cast<Format>(i);
            format_->addItem(QString(formatLabel(f)) + (formatAvailable(f) ? "" : "  (not in this FFmpeg build)"));
            if (!formatAvailable(f))
                if (auto* model = qobject_cast<QStandardItemModel*>(format_->model())) model->item(i)->setEnabled(false);
        }
        format_->addItem("Project package (.zip)");
        format_->setCurrentIndex(r.package ? static_cast<int>(Format::Count) : static_cast<int>(r.settings.format));
        connect(format_, &QComboBox::currentIndexChanged, this, [this](int i) {
            Remembered& r = remembered();
            exporting::Settings& s = r.settings;
            r.package = i == static_cast<int>(Format::Count);
            if (!r.package) s.format = static_cast<Format>(i);
            s.output = platform::fromUtf8(output_->text().toStdString());
            s.output.replace_extension(r.package ? ".zip" : formatExtension(s.format));
            if (!r.package && !formatSupportsAlpha(s.format)) s.transparent = false;
            refreshSettings();
        });
        form->addRow("Format", format_);

        scale_ = new QComboBox;
        for (const ScaleOption& o : kScales) scale_->addItem(o.label);
        scale_->setCurrentIndex(r.scaleIndex);
        connect(scale_, &QComboBox::currentIndexChanged, this, [this](int i) {
            remembered().scaleIndex = std::max(0, i);
            refreshSettings();
        });
        form->addRow("Size", scale_);
        sizeInfo_ = colored("", theme::kTextFaint);
        form->addRow("", sizeInfo_);

        quality_ = new QComboBox;
        for (const char* q : kQualities) quality_->addItem(q);
        quality_->setCurrentIndex(static_cast<int>(r.settings.quality));
        connect(quality_, &QComboBox::currentIndexChanged, this,
                [](int i) { remembered().settings.quality = static_cast<Quality>(std::max(0, i)); });
        qualityLabel_ = new QLabel("Quality");
        form->addRow(qualityLabel_, quality_);

        rangeOptions_ = new QWidget;
        auto* rangeRow = new QHBoxLayout(rangeOptions_);
        rangeRow->setContentsMargins(0, 0, 0, 0);
        auto* entire = new QRadioButton("Entire composition");
        auto* custom = new QRadioButton("Custom");
        (r.customRange ? custom : entire)->setChecked(true);
        rangeRow->addWidget(entire);
        rangeRow->addWidget(custom);
        rangeRow->addStretch();
        form->addRow("Range", rangeOptions_);
        rangeBox_ = new QWidget;
        auto* rb = new QHBoxLayout(rangeBox_);
        rb->setContentsMargins(0, 0, 0, 0);
        from_ = new DragValue;
        to_ = new DragValue;
        const float dur = static_cast<float>(ctx_.comp().duration);
        for (DragValue* d : {from_, to_}) {
            d->setRange(0.0f, dur);
            d->setSpeed(0.02f);
            d->setFormat("%.2f s");
            rb->addWidget(d);
        }
        from_->setValue(static_cast<float>(r.rangeStart));
        to_->setValue(static_cast<float>(r.rangeEnd));
        connect(from_, &DragValue::valueChanged, this, [this](float v) {
            remembered().rangeStart = v;
            if (remembered().rangeEnd < v) to_->setValue(v), remembered().rangeEnd = v;
        });
        connect(to_, &DragValue::valueChanged, this, [this](float v) {
            remembered().rangeEnd = v;
            if (remembered().rangeStart > v) from_->setValue(v), remembered().rangeStart = v;
        });
        form->addRow("", rangeBox_);
        connect(custom, &QRadioButton::toggled, this, [this](bool on) {
            remembered().customRange = on;
            refreshSettings();
        });

        transparent_ = new QCheckBox("Transparent (no composition background)");
        transparent_->setChecked(r.settings.transparent);
        connect(transparent_, &QCheckBox::toggled, this, [](bool on) { remembered().settings.transparent = on; });
        transparentLabel_ = new QLabel("Background");
        form->addRow(transparentLabel_, transparent_);

        auto* outRow = new QHBoxLayout;
        output_ = new QLineEdit(QString::fromStdString(platform::toUtf8(r.settings.output)));
        connect(output_, &QLineEdit::textEdited, this,
                [](const QString& t) { remembered().settings.output = platform::fromUtf8(t.toStdString()); });
        outRow->addWidget(output_, 1);
        auto* browse = new QPushButton("Browse...");
        connect(browse, &QPushButton::clicked, this, [this] {
            exporting::Settings& s = remembered().settings;
            const bool package = remembered().package;
            if (auto file = platform::saveFileDialog("Export to", s.output, package ? "Project package" : formatLabel(s.format),
                                                     package ? ".zip" : formatExtension(s.format))) {
                s.output = *file;
                output_->setText(QString::fromStdString(platform::toUtf8(s.output)));
                refreshSettings();
            }
        });
        outRow->addWidget(browse);
        form->addRow("Save to", outRow);
        sequenceNote_ = colored("", theme::kTextFaint);
        form->addRow("", sequenceNote_);
        col->addLayout(form);

        error_ = colored("", theme::kDanger);
        error_->hide();
        col->addWidget(error_);
        col->addSpacing(10);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        auto* cancel = new QPushButton("Cancel");
        cancel->setMinimumSize(120, 34);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        auto* go = accentButton("Export");
        go->setMinimumSize(120, 34);
        go->setDefault(true);
        connect(go, &QPushButton::clicked, this, [this] { startExport(); });
        buttons->addWidget(cancel);
        buttons->addWidget(go);
        col->addLayout(buttons);
        return page;
    }

    QWidget* buildProgress() {
        auto* page = new QWidget;
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        progressTitle_ = new QLabel("Exporting...");
        QFont f = progressTitle_->font();
        f.setPointSizeF(f.pointSizeF() * 1.1);
        f.setWeight(QFont::DemiBold);
        progressTitle_->setFont(f);
        col->addWidget(progressTitle_);
        progressFile_ = colored("", theme::kTextDim);
        col->addWidget(progressFile_);
        col->addSpacing(6);
        bar_ = new QProgressBar;
        bar_->setRange(0, 1000);
        bar_->setTextVisible(true);
        bar_->setFixedHeight(22);
        col->addWidget(bar_);
        progressInfo_ = colored("", theme::kTextFaint);
        col->addWidget(progressInfo_);
        col->addSpacing(8);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        auto* cancel = new QPushButton("Cancel");
        cancel->setMinimumSize(120, 34);
        connect(cancel, &QPushButton::clicked, this, [this] {
            if (packageRunning_) {
                packageCancel_->store(true);
                progressInfo_->setText("Cancelling...");
            } else {
                job_.cancel();
                showResult();
            }
        });
        buttons->addWidget(cancel);
        col->addLayout(buttons);
        return page;
    }

    QWidget* buildResult() {
        auto* page = new QWidget;
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        resultTitle_ = new QLabel;
        QFont f = resultTitle_->font();
        f.setPointSizeF(f.pointSizeF() * 1.1);
        f.setWeight(QFont::DemiBold);
        resultTitle_->setFont(f);
        col->addWidget(resultTitle_);
        resultInfo_ = colored("", theme::kTextDim);
        col->addWidget(resultInfo_);
        resultPath_ = new QLabel;
        resultPath_->setWordWrap(true);
        resultPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        col->addWidget(resultPath_);
        col->addSpacing(10);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        reveal_ = new QPushButton("Show in Folder");
        play_ = new QPushButton("Play");
        auto* done = accentButton("Done");
        for (QPushButton* b : {reveal_, play_, done}) {
            b->setMinimumSize(130, 34);
            buttons->addWidget(b);
        }
        connect(reveal_, &QPushButton::clicked, this, [this] {
            const auto& out = packageResultMode_ ? packageOutput_ : job_.settings().output;
            const bool seq = !packageResultMode_ && job_.settings().format == exporting::Format::PngSequence;
            platform::revealInFileManager(seq ? out.parent_path() / platform::fromUtf8(platform::toUtf8(out.stem()) + "_0000.png") : out);
        });
        connect(play_, &QPushButton::clicked, this, [this] { platform::openWithDefaultApp(job_.settings().output); });
        connect(done, &QPushButton::clicked, this, &QDialog::accept);
        col->addLayout(buttons);
        return page;
    }

    void refreshSettings() {
        using namespace exporting;
        Remembered& r = remembered();
        exporting::Settings& s = r.settings;
        s.scale = kScales[std::clamp(r.scaleIndex, 0, static_cast<int>(std::size(kScales)) - 1)].scale;
        int w = 0, h = 0;
        outputSize(ctx_.comp(), s, w, h);
        sizeInfo_->setText(QString("%1 × %2 px  ·  %3 fps").arg(w).arg(h).arg(ctx_.comp().fps, 0, 'g', 4));
        const bool sequence = s.format == Format::PngSequence;
        form_->setRowVisible(scale_, !r.package);
        form_->setRowVisible(sizeInfo_, !r.package);
        form_->setRowVisible(quality_, !r.package && !sequence);
        form_->setRowVisible(rangeOptions_, !r.package);
        form_->setRowVisible(rangeBox_, !r.package && r.customRange);
        const bool alpha = !r.package && formatSupportsAlpha(s.format);
        form_->setRowVisible(transparent_, alpha);
        {
            const QSignalBlocker b(transparent_);
            transparent_->setChecked(s.transparent);
        }
        output_->setText(QString::fromStdString(platform::toUtf8(s.output)));
        form_->setRowVisible(sequenceNote_, r.package || sequence);
        sequenceNote_->setText(r.package ? QString("Includes the project and its linked media and plugin files. Extract the ZIP to open it on another computer.")
                                        : QString("Frames are saved as %1_0000.png, _0001.png, ...")
                                              .arg(QString::fromStdString(platform::toUtf8(s.output.stem()))));
        adjustSize();
    }

    void startExport() {
        Remembered& r = remembered();
        exporting::Settings& s = r.settings;
        ctx_.playing = false;
        s.output = platform::fromUtf8(output_->text().toStdString());
        s.start = r.customRange ? r.rangeStart : 0.0;
        s.end = r.customRange ? r.rangeEnd : ctx_.comp().duration;
        if (s.output.empty() || s.output.filename().empty()) {
            showError("Choose where to save the export.");
            return;
        }
        if (r.package) {
            s.output.replace_extension(".zip");
            startPackage();
            return;
        }
        packageResultMode_ = false;
        if (s.output.extension() != exporting::formatExtension(s.format)) s.output += exporting::formatExtension(s.format);
        s.mixAudio = media::AudioEngine::mix;
        showError("");
        pages_->setCurrentIndex(1);
        progressTitle_->setText("Preparing...");
        progressFile_->setText(QString::fromStdString(platform::toUtf8(s.output.filename())));
        bar_->setRange(0, 1000);
        bar_->setValue(0);
        waiting_ = true;
        timer_->start(0);
    }

    void startPackage() {
        packageOutput_ = remembered().settings.output;
        packageResultMode_ = true;
        packageCancel_ = std::make_shared<std::atomic_bool>(false);
        try {
            packageFuture_ = std::async(std::launch::async,
                [project = ctx_.project, file = ctx_.path, output = packageOutput_, cancel = packageCancel_] {
                    PackageResult result;
                    result.ok = packageProject(project, file, output, &result.error, cancel.get());
                    return result;
                });
        } catch (const std::exception& e) {
            showError(std::string("Could not start project packaging: ") + e.what());
            return;
        }
        packageRunning_ = true;
        showError("");
        pages_->setCurrentIndex(1);
        progressTitle_->setText("Packaging project...");
        progressFile_->setText(QString::fromStdString(platform::toUtf8(packageOutput_.filename())));
        progressInfo_->setText("Collecting linked files...");
        bar_->setRange(0, 0);
        timer_->start(30);
    }

    void stepExport() {
        if (packageRunning_) {
            if (packageFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            PackageResult result;
            try {
                result = packageFuture_.get();
            } catch (const std::exception& e) {
                result.error = e.what();
            }
            packageRunning_ = false;
            timer_->stop();
            pages_->setCurrentIndex(2);
            bar_->setRange(0, 1000);
            bar_->setValue(result.ok ? 1000 : 0);
            const bool cancelled = !result.ok && packageCancel_->load();
            resultTitle_->setText(result.ok ? "Project package ready" : cancelled ? "Export cancelled" : "Export failed");
            ui::setTone(resultTitle_, result.ok ? ui::Tone::Accent : cancelled ? ui::Tone::Text : ui::Tone::Danger);
            resultInfo_->setText(result.ok ? "Project and linked files are ready to share." : "");
            resultPath_->setText(result.ok ? QString::fromStdString(platform::toUtf8(packageOutput_))
                                          : cancelled ? QString() : QString::fromStdString(result.error));
            reveal_->setVisible(result.ok);
            play_->hide();
            return;
        }
        if (waiting_) { // audio of the composition still decoding
            media::MediaManager::get().update(ctx_.comp());
            if (!media::MediaManager::get().audioReady(ctx_.comp())) {
                progressInfo_->setText("Reading audio...");
                timer_->setInterval(30);
                return;
            }
            waiting_ = false;
            timer_->setInterval(0);
            std::string error;
            if (!job_.start(ctx_.project, remembered().settings, error)) {
                timer_->stop();
                pages_->setCurrentIndex(0);
                showError(error);
                return;
            }
            progressTitle_->setText("Exporting...");
        }
        job_.step(1.0 / 30.0); // keep the UI responsive
        if (!job_.running()) {
            showResult();
            return;
        }
        const int done = job_.framesDone(), total = std::max(1, job_.framesTotal());
        bar_->setValue(done * 1000 / total);
        const double elapsed = job_.elapsedSeconds();
        const double remaining = done > 0 ? elapsed / done * (total - done) : 0.0;
        progressInfo_->setText(QString("Frame %1 of %2  ·  %3 elapsed  ·  about %4 left")
                                   .arg(done)
                                   .arg(total)
                                   .arg(QString::fromStdString(formatDuration(elapsed)))
                                   .arg(done > 0 ? QString::fromStdString(formatDuration(remaining)) : QString("--:--")));
    }

    void showResult() {
        timer_->stop();
        waiting_ = false;
        pages_->setCurrentIndex(2);
        const bool ok = job_.succeeded();
        const bool sequence = job_.settings().format == exporting::Format::PngSequence;
        if (ok) {
            resultTitle_->setText("Export complete");
            ui::setTone(resultTitle_, ui::Tone::Accent);
            resultInfo_->setText(QString("%1 frames in %2").arg(job_.framesTotal()).arg(QString::fromStdString(formatDuration(job_.elapsedSeconds()))));
            resultPath_->setText(QString::fromStdString(platform::toUtf8(job_.settings().output)));
        } else if (job_.cancelled()) {
            resultTitle_->setText("Export cancelled.");
            resultTitle_->setStyleSheet("");
            resultInfo_->clear();
            resultPath_->clear();
        } else {
            resultTitle_->setText("Export failed");
            ui::setTone(resultTitle_, ui::Tone::Danger);
            resultInfo_->clear();
            resultPath_->setText(QString::fromStdString(job_.error()));
        }
        reveal_->setVisible(ok);
        play_->setVisible(ok && !sequence);
    }

    void showError(const std::string& message) {
        error_->setText(QString::fromStdString(message));
        error_->setVisible(!message.empty());
    }

    struct PackageResult {
        bool ok = false;
        std::string error;
    };
    EditorContext& ctx_;
    exporting::ExportJob job_;
    std::future<PackageResult> packageFuture_;
    std::shared_ptr<std::atomic_bool> packageCancel_;
    std::filesystem::path packageOutput_;
    bool packageRunning_ = false, packageResultMode_ = false;
    QStackedWidget* pages_;
    QTimer* timer_;
    bool waiting_ = false;
    QComboBox *format_ = nullptr, *scale_ = nullptr, *quality_ = nullptr;
    QLabel *sizeInfo_ = nullptr, *qualityLabel_ = nullptr, *transparentLabel_ = nullptr, *sequenceNote_ = nullptr, *error_ = nullptr;
    QFormLayout* form_ = nullptr;
    QWidget *rangeBox_ = nullptr, *rangeOptions_ = nullptr;
    DragValue *from_ = nullptr, *to_ = nullptr;
    QCheckBox* transparent_ = nullptr;
    QLineEdit* output_ = nullptr;
    QLabel *progressTitle_ = nullptr, *progressFile_ = nullptr, *progressInfo_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel *resultTitle_ = nullptr, *resultInfo_ = nullptr, *resultPath_ = nullptr;
    QPushButton *reveal_ = nullptr, *play_ = nullptr;
};

} // namespace

void showExportDialog(EditorContext& ctx, QWidget* parent) {
    ExportDialog dialog(ctx, parent);
    dialog.exec();
}

} // namespace bemotion::ui
