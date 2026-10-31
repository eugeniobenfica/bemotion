#pragma once

// New versions of bemotion (core/Updates.h, app/Downloads): looked for only once the user turned
// updates on (Settings › Interface › Updates), at most once a day, or with Check Now. A quiet
// notice in the window's corner offers the new one; Update downloads it, checks its SHA-256 and
// installs it (Windows: the installer, quietly, then bemotion opens again; Linux: the AppImage is
// replaced). Where bemotion cannot replace itself (macOS, the portable folder, a build from the
// sources) Update opens the release's page.

#include "core/Updates.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

class QWidget;

namespace bemotion::ui {

class Updates : public QObject {
    Q_OBJECT
public:
    static Updates& get();

    void checkIfDue(); // at start: only with updates on, once a day
    void checkNow();   // Settings' Check Now
    bool checking() const { return checking_; }
    const std::optional<Release>& available() const { return available_; }
    QString status() const; // for Settings: up to date, available, could not check...
    bool betas() const;     // the channel: betas too (as this build, unless the user chose)

    bool offered() const; // the notice shows: a newer version, not skipped nor put off
    void later();         // not until the next start
    void skip();          // not this version again (a newer one is offered)
    void install();       // downloads and installs it, or opens its page

    // The window that closes for the installer (the app's main window).
    void setMainWindow(QWidget* window) { window_ = window; }

signals:
    void changed();

private:
    void check();
    std::optional<Release> available_;
    bool checking_ = false, dismissed_ = false;
    QString error_;
    QPointer<QWidget> window_;
};

// The notice, floating in `window`'s bottom right corner while an update is offered; also makes
// `window` the one that closes for the installer.
void attachUpdateNotice(QWidget* window);

} // namespace bemotion::ui
