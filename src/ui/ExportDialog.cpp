// Video and portable project export: settings, progress and result.

#include "ui/Dialogs.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "app/ProjectPackage.h"
#include "export/Export.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "ui/EditorContext.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
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
constexpr ScaleOption kScales[] = {{TR_NOOP("Full size"), 1.0f}, {TR_NOOP("Half size"), 0.5f}, {TR_NOOP("Quarter size"), 0.25f}, {TR_NOOP("Double (2x)"), 2.0f}};
constexpr const char* kQualities[] = {TR_NOOP("Smaller file"), TR_NOOP("Balanced"), TR_NOOP("Best")};

// The formats as tiles, in the order people reach for them: what it is, what it is for.
struct FormatChoice {
    int index; // an exporting::Format, or Format::Count for the project package
    const char* title;
    const char* use;
    Icon icon;
};
constexpr FormatChoice kFormatChoices[] = {
    {static_cast<int>(exporting::Format::Mp4), TR_NOOP("MP4"), TR_NOOP("Plays everywhere"), Icon::Film},
    {static_cast<int>(exporting::Format::WebM), TR_NOOP("WebM"), TR_NOOP("Web, transparent"), Icon::Film},
    {static_cast<int>(exporting::Format::MovProRes), TR_NOOP("ProRes MOV"), TR_NOOP("Editing, transparent"), Icon::Film},
    {static_cast<int>(exporting::Format::Gif), TR_NOOP("GIF"), TR_NOOP("Loops, no sound"), Icon::Loop},
    {static_cast<int>(exporting::Format::PngSequence), TR_NOOP("PNG Sequence"), TR_NOOP("An image per frame"), Icon::Image},
    {static_cast<int>(exporting::Format::Count), TR_NOOP("Project Package"), TR_NOOP("Project and files"), Icon::Folder},
};

// One format: its icon, name and use; outlined in the accent when chosen.
class FormatTile : public QAbstractButton {
public:
    FormatTile(const FormatChoice& c, bool available) : choice_(c), available_(available) {
        setCheckable(true);
        setEnabled(available);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(58);
        setMinimumWidth(150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (!available) setToolTip(tr("Not in this build of FFmpeg"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const float w = static_cast<float>(width()), h = static_cast<float>(height()), r = theme::current().rounding + 3.0f;
        const bool on = isChecked(), hot = underMouse() && isEnabled();
        d.rectFilled(Pt(0.5f, 0.5f), Pt(w - 0.5f, h - 0.5f), on ? theme::accent(34) : hot ? theme::kBgHover : theme::kBgRaised, r);
        d.rect(Pt(0.5f, 0.5f), Pt(w - 0.5f, h - 0.5f), on ? theme::kAccent : theme::kBorder, r, on ? 1.6f : 1.0f);
        const Rgba ink = !available_ ? theme::kTextFaint : on ? theme::kAccent : theme::kTextDim;
        drawIcon(d, choice_.icon, Pt(24, h * 0.5f), 20, ink);
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        p.setFont(bold);
        d.text(Pt(46, h * 0.5f - d.textHeight() + 1), available_ ? theme::kText : theme::kTextFaint, tr(choice_.title));
        p.setFont(font());
        d.text(Pt(46, h * 0.5f + 2), theme::kTextFaint, available_ ? tr(choice_.use) : tr("Not available"));
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    FormatChoice choice_;
    bool available_;
};

std::string formatDuration(double seconds) {
    const int s = static_cast<int>(std::lround(std::max(0.0, seconds)));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

QLabel* colored(const QString& text, Tone tone) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    setTone(l, tone); // follows the theme
    return l;
}

// The frame being exported (then the last one), fitted in a rounded box.
class FramePreview : public QWidget {
public:
    FramePreview() {
        setMinimumHeight(150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(QImage image) {
        image_ = std::move(image);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QPainterPath box;
        box.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        p.fillPath(box, theme::q(theme::kField));
        if (!image_.isNull()) {
            const QSize s = image_.size().scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
            const QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
            QPainterPath frame;
            frame.addRoundedRect(QRectF(r), 4, 4);
            p.setClipPath(frame);
            p.drawImage(r, image_);
            p.setClipping(false);
        }
        p.setPen(QPen(theme::q(theme::kBorder), 1.0));
        p.drawPath(box);
    }

private:
    QImage image_;
};

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
        setWindowTitle(tr("Export"));
        setMinimumWidth(620);
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
        if (packageRunning_ || job_.running() || waiting_) { // Esc / close while exporting cancels it
            cancelExport();
            return;
        }
        QDialog::reject();
    }

private:
    QWidget* buildSettings() {
        using namespace exporting;
        Remembered& r = remembered();
        const Composition& comp = ctx_.comp();
        auto* page = new QWidget;
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);

        // What is exported: the composition, its size, rate and length.
        auto* title = pageTitle(tr("Export"));
        col->addWidget(title);
        auto* about = new QLabel(tr("%1  ·  %2 × %3  ·  %4 fps  ·  %5")
                                     .arg(QString::fromStdString(ctx_.project.name))
                                     .arg(comp.width)
                                     .arg(comp.height)
                                     .arg(comp.fps, 0, 'g', 4)
                                     .arg(QString::fromStdString(formatDuration(comp.duration))));
        setTone(about, Tone::TextDim);
        col->addWidget(about);

        col->addSpacing(8);
        col->addWidget(sectionLabel(tr("Format")));
        auto* tiles = new QGridLayout;
        tiles->setSpacing(8);
        auto* group = new QButtonGroup(page);
        const int chosen = r.package ? static_cast<int>(Format::Count) : static_cast<int>(r.settings.format);
        int n = 0;
        for (const FormatChoice& c : kFormatChoices) {
            const bool available = c.index == static_cast<int>(Format::Count) || formatAvailable(static_cast<Format>(c.index));
            auto* tile = new FormatTile(c, available);
            tile->setChecked(c.index == chosen);
            group->addButton(tile, c.index);
            tiles->addWidget(tile, n / 3, n % 3);
            ++n;
        }
        col->addLayout(tiles);
        connect(group, &QButtonGroup::idClicked, this, [this](int i) {
            Remembered& r = remembered();
            exporting::Settings& s = r.settings;
            r.package = i == static_cast<int>(Format::Count);
            if (!r.package) s.format = static_cast<Format>(i);
            s.output = platform::fromUtf8(output_->text().toStdString());
            s.output.replace_extension(r.package ? ".zip" : formatExtension(s.format));
            if (!r.package && !formatSupportsAlpha(s.format)) s.transparent = false;
            refreshSettings();
        });

        col->addSpacing(8);
        videoTitle_ = sectionLabel(tr("Video"));
        col->addWidget(videoTitle_);
        video_ = new SettingsCard;
        col->addWidget(video_);
        QStringList scales;
        for (const ScaleOption& o : kScales) scales << tr(o.label);
        sizeRow_ = video_->addRow(tr("Size"), " ", segmented(scales, r.scaleIndex, [this](int i) {
                                      remembered().scaleIndex = std::max(0, i);
                                      refreshSettings();
                                  }));
        sizeInfo_ = sizeRow_->findChildren<QLabel*>().size() > 1 ? sizeRow_->findChildren<QLabel*>()[1] : nullptr;
        QStringList qualities;
        for (const char* q : kQualities) qualities << tr(q);
        qualityRow_ = video_->addRow("Quality", "", segmented(qualities, static_cast<int>(r.settings.quality), [](int i) {
                                         remembered().settings.quality = static_cast<Quality>(std::max(0, i));
                                     }));
        auto* rangeBox = new QWidget;
        auto* rangeCol = new QHBoxLayout(rangeBox);
        rangeCol->setContentsMargins(0, 0, 0, 0);
        rangeCol->setSpacing(8);
        rangeBox_ = new QWidget;
        auto* rb = new QHBoxLayout(rangeBox_);
        rb->setContentsMargins(0, 0, 0, 0);
        rb->setSpacing(6);
        from_ = new DragValue;
        to_ = new DragValue;
        const float dur = static_cast<float>(comp.duration);
        auto* toLabel = new QLabel("to");
        setTone(toLabel, Tone::TextDim);
        for (DragValue* d : {from_, to_}) {
            d->setRange(0.0f, dur);
            d->setSpeed(0.02f);
            d->setFormat("%.2f s");
            d->setFixedSize(76, 28);
        }
        rb->addWidget(from_);
        rb->addWidget(toLabel);
        rb->addWidget(to_);
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
        rangeCol->addWidget(rangeBox_);
        rangeCol->addWidget(segmented({tr("Whole"), tr("Part")}, r.customRange ? 1 : 0, [this](int i) {
            remembered().customRange = i == 1;
            refreshSettings();
        }));
        rangeRow_ = video_->addRow(tr("Range"), "", rangeBox);
        transparent_ = new QCheckBox;
        transparent_->setChecked(r.settings.transparent);
        connect(transparent_, &QCheckBox::toggled, this, [](bool on) { remembered().settings.transparent = on; });
        transparentRow_ = video_->addRow(tr("Transparent background"), tr("Without the composition's background color"), transparent_);

        col->addSpacing(8);
        col->addWidget(sectionLabel(tr("Save to")));
        auto* where = new SettingsCard;
        col->addWidget(where);
        auto* outBox = new QWidget;
        auto* outRow = new QHBoxLayout(outBox);
        outRow->setContentsMargins(0, 0, 0, 0);
        outRow->setSpacing(8);
        output_ = new QLineEdit(QString::fromStdString(platform::toUtf8(r.settings.output)));
        connect(output_, &QLineEdit::textEdited, this,
                [](const QString& t) { remembered().settings.output = platform::fromUtf8(t.toStdString()); });
        outRow->addWidget(output_, 1);
        auto* browse = new QPushButton(tr("Change..."));
        connect(browse, &QPushButton::clicked, this, [this] {
            exporting::Settings& s = remembered().settings;
            const bool package = remembered().package;
            if (auto file = platform::saveFileDialog(TR_NOOP("Export to"), s.output, package ? TR_NOOP("Project package") : formatLabel(s.format),
                                                     package ? ".zip" : formatExtension(s.format))) {
                s.output = *file;
                output_->setText(QString::fromStdString(platform::toUtf8(s.output)));
                refreshSettings();
            }
        });
        outRow->addWidget(browse);
        where->addContent(outBox);

        error_ = new QLabel;
        error_->setWordWrap(true);
        setTone(error_, Tone::Danger);
        error_->hide();
        col->addWidget(error_);
        col->addSpacing(12);
        auto* buttons = new QHBoxLayout;
        sequenceNote_ = new QLabel;
        sequenceNote_->setWordWrap(true);
        setTone(sequenceNote_, Tone::TextFaint);
        buttons->addWidget(sequenceNote_, 1);
        auto* cancel = new QPushButton(tr("Cancel"));
        cancel->setMinimumWidth(110);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        auto* go = accentButton(tr("Export"));
        go->setMinimumWidth(130);
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
        col->setSpacing(6);
        // What, where to, how far: the title and file on the left, the percentage on the right.
        auto* head = new QHBoxLayout;
        auto* names = new QVBoxLayout;
        names->setSpacing(2);
        progressTitle_ = pageTitle(tr("Exporting..."));
        names->addWidget(progressTitle_);
        progressFile_ = colored("", Tone::TextDim);
        names->addWidget(progressFile_);
        head->addLayout(names, 1);
        percent_ = new QLabel;
        QFont big = percent_->font();
        big.setPointSizeF(big.pointSizeF() * 2.2);
        big.setWeight(QFont::DemiBold);
        percent_->setFont(big);
        setTone(percent_, Tone::Accent);
        head->addWidget(percent_, 0, Qt::AlignRight | Qt::AlignVCenter);
        col->addLayout(head);
        col->addSpacing(8);
        preview_ = new FramePreview; // the frame being written
        col->addWidget(preview_, 1);
        col->addSpacing(8);
        bar_ = new QProgressBar;
        bar_->setRange(0, 1000);
        bar_->setTextVisible(false);
        bar_->setFixedHeight(8);
        col->addWidget(bar_);
        auto* facts = new QHBoxLayout;
        progressInfo_ = colored("", Tone::TextDim);
        progressInfo_->setWordWrap(false);
        facts->addWidget(progressInfo_, 1);
        progressLeft_ = colored("", Tone::TextDim);
        progressLeft_->setWordWrap(false);
        facts->addWidget(progressLeft_);
        col->addLayout(facts);
        col->addSpacing(10);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        auto* cancel = new QPushButton(tr("Cancel"));
        cancel->setMinimumWidth(110);
        connect(cancel, &QPushButton::clicked, this, [this] { cancelExport(); });
        buttons->addWidget(cancel);
        col->addLayout(buttons);
        return page;
    }

    QWidget* buildResult() {
        auto* page = new QWidget;
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);
        resultTitle_ = pageTitle("");
        col->addWidget(resultTitle_);
        resultInfo_ = colored("", Tone::TextDim);
        col->addWidget(resultInfo_);
        col->addSpacing(8);
        resultPreview_ = new FramePreview; // the last frame written
        col->addWidget(resultPreview_, 1);
        col->addSpacing(4);
        resultPath_ = colored("", Tone::TextDim);
        resultPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        col->addWidget(resultPath_);
        col->addStretch(); // the buttons stay at the bottom without a preview
        col->addSpacing(10);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        reveal_ = new QPushButton(tr("Show in Folder"));
        play_ = new QPushButton(tr("Play"));
        auto* done = accentButton(tr("Done"));
        for (QPushButton* b : {reveal_, play_, done}) {
            b->setMinimumWidth(110);
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
        const bool sequence = s.format == Format::PngSequence;
        // A package has no picture settings at all.
        videoTitle_->setVisible(!r.package);
        video_->setVisible(!r.package);
        if (sizeInfo_) sizeInfo_->setText(tr("%1 × %2 px").arg(w).arg(h)); // what the choice gives
        qualityRow_->setVisible(!sequence);
        rangeBox_->setVisible(r.customRange);
        transparentRow_->setVisible(!r.package && formatSupportsAlpha(s.format));
        {
            const QSignalBlocker b(transparent_);
            transparent_->setChecked(s.transparent);
        }
        output_->setText(QString::fromStdString(platform::toUtf8(s.output)));
        const double length = r.customRange ? std::max(0.0, r.rangeEnd - r.rangeStart) : ctx_.comp().duration;
        const int frames = static_cast<int>(std::lround(length * ctx_.comp().fps));
        sequenceNote_->setText(r.package ? tr("The project, its media and its plugins' files in one ZIP: extract it to open the project elsewhere.")
                               : sequence ? tr("%1 images: %2_0000.png, _0001.png, ...")
                                                .arg(frames)
                                                .arg(QString::fromStdString(platform::toUtf8(s.output.stem())))
                                          : tr("%1 frames  ·  %2").arg(frames).arg(QString::fromStdString(formatDuration(length))));
        video_->update();
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
            showError(tr("Choose where to save the export.").toStdString());
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
        s.decodeScale = [](const Composition* comp, double scale) { media::MediaManager::get().setOutputScale(comp, scale); };
        showError("");
        pages_->setCurrentIndex(1);
        progressTitle_->setText(tr("Preparing..."));
        progressFile_->setText(QString::fromStdString(platform::toUtf8(s.output.filename())));
        percent_->setText("0%");
        progressInfo_->clear();
        progressLeft_->clear();
        preview_->setImage({});
        previewClock_.invalidate();
        cancelledEarly_ = false;
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
            showError(tr("Could not start project packaging: %1").arg(e.what()).toStdString());
            return;
        }
        packageRunning_ = true;
        showError("");
        pages_->setCurrentIndex(1);
        progressTitle_->setText(tr("Packaging project..."));
        progressFile_->setText(QString::fromStdString(platform::toUtf8(packageOutput_.filename())));
        progressInfo_->setText(tr("Collecting linked files..."));
        percent_->clear();
        progressLeft_->clear();
        preview_->setImage({});
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
            resultTitle_->setText(result.ok ? tr("Project package ready") : cancelled ? tr("Export cancelled") : tr("Export failed"));
            ui::setTone(resultTitle_, result.ok ? ui::Tone::Accent : cancelled ? ui::Tone::Text : ui::Tone::Danger);
            resultInfo_->setText(result.ok ? tr("Project and linked files are ready to share.") : "");
            resultPath_->setText(result.ok ? QString::fromStdString(platform::toUtf8(packageOutput_))
                                          : cancelled ? QString() : QString::fromStdString(result.error));
            reveal_->setVisible(result.ok);
            play_->hide();
            resultPreview_->hide();
            return;
        }
        if (waiting_) { // audio of the composition still decoding
            media::MediaManager::get().update(ctx_.comp());
            if (!media::MediaManager::get().audioReady(ctx_.comp())) {
                progressInfo_->setText(tr("Reading audio..."));
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
            progressTitle_->setText(tr("Exporting..."));
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
        progressTitle_->setText(tr("Exporting..."));
        percent_->setText(QString("%1%").arg(done * 100 / total));
        progressInfo_->setText(tr("Frame %1 of %2  ·  %3 elapsed").arg(done).arg(total).arg(QString::fromStdString(formatDuration(elapsed))));
        progressLeft_->setText(done > 0 ? tr("About %1 left").arg(QString::fromStdString(formatDuration(remaining))) : QString());
        if (!previewClock_.isValid() || previewClock_.elapsed() > 250) { // a few times a second is plenty
            previewClock_.restart();
            preview_->setImage(frameImage());
        }
    }

    // Stops at once, whatever the stage (reading audio, exporting, packaging).
    void cancelExport() {
        if (packageRunning_) {
            packageCancel_->store(true);
            progressInfo_->setText(tr("Cancelling..."));
            return;
        }
        cancelledEarly_ = waiting_; // before the job started
        job_.cancel();
        showResult();
    }

    // The last frame, small (a preview, not the file).
    QImage frameImage() const {
        const std::vector<uint8_t>& px = job_.lastFrame();
        const int w = job_.frameWidth(), h = job_.frameHeight();
        if (w <= 0 || h <= 0 || px.size() < static_cast<size_t>(w) * h * 4) return {};
        return QImage(px.data(), w, h, QImage::Format_RGBA8888).scaled(640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    void showResult() {
        timer_->stop();
        waiting_ = false;
        const QImage last = job_.succeeded() ? frameImage() : QImage();
        resultPreview_->setImage(last);
        resultPreview_->setVisible(!last.isNull()); // nothing to show: no empty box
        pages_->setCurrentIndex(2);
        const bool ok = job_.succeeded();
        const bool sequence = job_.settings().format == exporting::Format::PngSequence;
        if (ok) {
            resultTitle_->setText(tr("Export complete"));
            ui::setTone(resultTitle_, ui::Tone::Accent);
            resultInfo_->setText(tr("%1 frames in %2").arg(job_.framesTotal()).arg(QString::fromStdString(formatDuration(job_.elapsedSeconds()))));
            resultPath_->setText(QString::fromStdString(platform::toUtf8(job_.settings().output)));
        } else if (job_.cancelled() || cancelledEarly_) {
            resultTitle_->setText(tr("Export cancelled"));
            ui::setTone(resultTitle_, ui::Tone::Text);
            resultInfo_->setText(tr("Nothing was saved: the unfinished file was removed."));
            resultPath_->clear();
        } else {
            resultTitle_->setText(tr("Export failed"));
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
    QLabel *sizeInfo_ = nullptr, *videoTitle_ = nullptr, *sequenceNote_ = nullptr, *error_ = nullptr;
    SettingsCard* video_ = nullptr;
    QWidget *sizeRow_ = nullptr, *qualityRow_ = nullptr, *rangeRow_ = nullptr, *transparentRow_ = nullptr;
    QWidget* rangeBox_ = nullptr;
    DragValue *from_ = nullptr, *to_ = nullptr;
    QCheckBox* transparent_ = nullptr;
    QLineEdit* output_ = nullptr;
    QLabel *progressTitle_ = nullptr, *progressFile_ = nullptr, *progressInfo_ = nullptr, *progressLeft_ = nullptr, *percent_ = nullptr;
    FramePreview *preview_ = nullptr, *resultPreview_ = nullptr;
    QElapsedTimer previewClock_;
    bool cancelledEarly_ = false; // cancelled while reading the audio, before the job began
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
