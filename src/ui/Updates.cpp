#include "ui/Updates.h"

#include "app/Downloads.h"
#include "app/Platform.h"
#include "app/Settings.h"
#include "ui/AppSignals.h"
#include "ui/Language.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QUrl>

#include <algorithm>

namespace bemotion::ui {

namespace {

constexpr int64_t kCheckEvery = 24 * 60 * 60; // seconds

#if defined(Q_OS_WIN)
constexpr UpdatePlatform kPlatform = UpdatePlatform::Windows;
#elif defined(Q_OS_MACOS)
constexpr UpdatePlatform kPlatform = UpdatePlatform::Mac;
#else
constexpr UpdatePlatform kPlatform = UpdatePlatform::Linux;
#endif

QString qstr(const std::string& s) { return QString::fromStdString(s); }

// Whether this copy can replace itself: the installed one on Windows (the installer's folder),
// an AppImage on Linux. Not on macOS (the app is not signed) nor a portable folder or a build.
bool canReplaceItself() {
#if defined(Q_OS_WIN)
    const QSettings reg("HKEY_CURRENT_USER\\Software\\bemotion", QSettings::NativeFormat);
    const QString dir = reg.value("InstallDir").toString();
    if (dir.isEmpty()) return false;
    return QFileInfo(dir).canonicalFilePath().compare(QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath(),
                                                      Qt::CaseInsensitive) == 0;
#elif defined(Q_OS_MACOS)
    return false;
#else
    return !platform::env("APPIMAGE").empty();
#endif
}

void openPage(const Release& r) { QDesktopServices::openUrl(QUrl(qstr(r.page))); }

// The new version in place of this one, once the window closed (it asks about unsaved work;
// staying open cancels). Windows: the installer, quietly, waits for bemotion to close and opens
// it again. Linux: the AppImage is swapped (a running file can be replaced there).
bool replaceItself(const std::filesystem::path& file, QWidget* window, QString& error) {
    const QString package = qstr(platform::toUtf8(file));
#if defined(Q_OS_WIN)
    if (window && !window->close()) return true; // stayed open: not now
    if (!QProcess::startDetached(package, {"/S", "/RELAUNCH"})) error = tr("Could not start the installer.");
#else
    const QString target = qstr(platform::env("APPIMAGE"));
    const QString fresh = target + ".new";
    QFile::remove(fresh);
    if (!QFile::copy(package, fresh)) {
        error = tr("Could not write %1.").arg(fresh);
        return false;
    }
    QFile(fresh).setPermissions(QFile(target).permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    if (window && !window->close()) {
        QFile::remove(fresh);
        return true;
    }
    if (!QFile::remove(target) || !QFile::rename(fresh, target) || !QProcess::startDetached(target, {}))
        error = tr("Could not replace %1.").arg(target);
#endif
    QFile::remove(package);
    return error.isEmpty();
}

// The notice: "bemotion 0.2.0 is available", What's New, Update, Later, Skip This Version.
class UpdateNotice : public QWidget {
public:
    explicit UpdateNotice(QWidget* window) : QWidget(window) {
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 10, 10, 10);
        row->setSpacing(8);
        text_ = new QLabel;
        setTone(text_, Tone::Text);
        row->addWidget(text_, 1);
        notes_ = new QPushButton;
        later_ = new QPushButton;
        skip_ = new QPushButton;
        update_ = accentButton(QString());
        for (QPushButton* b : {notes_, later_, skip_}) b->setProperty("flat", true);
        for (QPushButton* b : {notes_, skip_, later_, update_}) row->addWidget(b);
        connect(notes_, &QPushButton::clicked, this, [] {
            if (const auto& r = Updates::get().available()) openPage(*r);
        });
        connect(later_, &QPushButton::clicked, this, [] { Updates::get().later(); });
        connect(skip_, &QPushButton::clicked, this, [] { Updates::get().skip(); });
        connect(update_, &QPushButton::clicked, this, [] { Updates::get().install(); });
        connect(&Updates::get(), &Updates::changed, this, [this] { refresh(); });
        connect(&AppSignals::get(), &AppSignals::languageChanged, this, [this] { refresh(); });
        window->installEventFilter(this);
        refresh();
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize) place();
        return false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(theme::q(theme::kBorder), 1));
        p.setBrush(theme::q(theme::kBgRaised));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }

private:
    void refresh() {
        const auto& r = Updates::get().available();
        if (r) text_->setText(tr("bemotion %1 is available").arg(qstr(r->version)));
        notes_->setText(tr("What's New"));
        later_->setText(tr("Later"));
        skip_->setText(tr("Skip This Version"));
        update_->setText(tr("Update"));
        setVisible(Updates::get().offered());
        if (isVisible()) {
            adjustSize();
            place();
            raise();
        }
    }
    void place() {
        const QWidget* w = parentWidget();
        const QSize s = sizeHint().boundedTo(QSize(w->width() - 32, 200));
        setGeometry(w->width() - s.width() - 16, w->height() - s.height() - 16, s.width(), s.height());
    }
    QLabel* text_;
    QPushButton *notes_, *later_, *skip_, *update_;
};

} // namespace

Updates& Updates::get() {
    static auto* updates = new Updates; // lives as long as the app (the notice listens to it)
    return *updates;
}

bool Updates::betas() const {
    const std::string& channel = settings().updateChannel;
    return channel.empty() ? isPrerelease(BEMOTION_VERSION) : channel == "beta";
}

void Updates::checkIfDue() {
    if (!settings().checkUpdates || checking_) return;
    if (QDateTime::currentSecsSinceEpoch() - settings().lastUpdateCheck < kCheckEvery) return;
    check();
}

void Updates::checkNow() {
    if (!checking_) check();
}

void Updates::check() {
    checking_ = true;
    error_.clear();
    emit changed();
    net::fetchReleases(this, [this](bool ok, std::vector<Release> releases, QString error) {
        checking_ = false;
        settings().lastUpdateCheck = QDateTime::currentSecsSinceEpoch();
        saveSettings();
        if (!ok) {
            error_ = error;
            platform::log("update check failed: " + error.toStdString());
        } else {
            available_ = newestRelease(releases, BEMOTION_VERSION, betas());
            dismissed_ = false;
            platform::log(available_ ? "update available: " + available_->version : std::string("no update"));
        }
        emit changed();
    });
}

QString Updates::status() const {
    if (checking_) return tr("Looking for a new version...");
    if (!error_.isEmpty()) return tr("Could not check: %1").arg(error_);
    if (available_) return tr("bemotion %1 is available").arg(qstr(available_->version));
    if (settings().lastUpdateCheck == 0) return tr("Not checked yet");
    const QDateTime when = QDateTime::fromSecsSinceEpoch(settings().lastUpdateCheck);
    return tr("Up to date (checked %1)").arg(QLocale().toString(when, QLocale::ShortFormat));
}

bool Updates::offered() const {
    return available_ && !dismissed_ && available_->version != settings().skippedVersion;
}

void Updates::later() {
    dismissed_ = true;
    emit changed();
}

void Updates::skip() {
    if (!available_) return;
    settings().skippedVersion = available_->version;
    saveSettings();
    emit changed();
}

void Updates::install() {
    if (!available_) return;
    const Release release = *available_;
    const ReleaseAsset* package = packageFor(release, kPlatform);
    if (!package || !canReplaceItself()) return openPage(release);

    QWidget* parent = window_ ? window_.data() : nullptr;
    auto* dialog = new QProgressDialog(tr("Downloading bemotion %1...").arg(qstr(release.version)), tr("Cancel"), 0, 1000, parent);
    dialog->setWindowTitle(tr("Update bemotion"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setValue(0);
    const int64_t size = package->size;
    auto cancel = net::downloadUpdate(
        release, *package, dialog,
        [this, dialog, release](bool ok, std::filesystem::path file, QString error) {
            dialog->deleteLater();
            QWidget* w = window_ ? window_.data() : nullptr;
            if (!ok) {
                QMessageBox::warning(w, tr("Update bemotion"), tr("The update could not be downloaded: %1").arg(error));
                return;
            }
            const auto answer = QMessageBox::question(
                w, tr("Update bemotion"), tr("bemotion %1 is ready. bemotion closes to install it, then opens again.").arg(qstr(release.version)),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
            if (answer != QMessageBox::Ok) return; // the package stays for next time... until the next check replaces it
            platform::log("installing update " + release.version);
            QString why;
            if (!replaceItself(file, w, why)) QMessageBox::warning(w, tr("Update bemotion"), why);
        },
        [dialog, size](int64_t received, int64_t total) {
            if (total <= 0) total = size;
            if (total > 0) dialog->setValue(static_cast<int>(std::min<int64_t>(1000, received * 1000 / total)));
        });
    connect(dialog, &QProgressDialog::canceled, dialog, [dialog, cancel] {
        cancel();
        dialog->deleteLater();
    });
}

void attachUpdateNotice(QWidget* window) {
    Updates::get().setMainWindow(window);
    new UpdateNotice(window);
}

} // namespace bemotion::ui
