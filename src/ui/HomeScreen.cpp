#include "ui/HomeScreen.h"

#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Settings.h"
#include "ui/AppSignals.h"
#include "core/Serialization.h"
#include "render/SceneRenderer.h"
#include "ui/Dialogs.h"
#include "ui/PagedWindow.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/SkiaQt.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace bemotion {

using ui::Rgba;

namespace {

struct FormatPreset {
    const char* label;
    int width;
    int height;
};

constexpr FormatPreset kPresets[] = {
    {TR_NOOP("Full HD  ·  1920 × 1080"), 1920, 1080},
    {TR_NOOP("4K UHD  ·  3840 × 2160"), 3840, 2160},
    {TR_NOOP("HD  ·  1280 × 720"), 1280, 720},
    {TR_NOOP("Vertical  ·  1080 × 1920  (Stories, Reels, Shorts)"), 1080, 1920},
    {TR_NOOP("Square  ·  1080 × 1080"), 1080, 1080},
    {TR_NOOP("Portrait  ·  1080 × 1350"), 1080, 1350},
    {TR_NOOP("Custom"), 0, 0},
};
constexpr int kCustomPreset = static_cast<int>(std::size(kPresets)) - 1;
constexpr double kFpsOptions[] = {24, 25, 30, 50, 60};
constexpr int kCardMin = 240, kGap = 20;

constexpr const char* kSupportUrl = "https://buymeacoffee.com/eugeniobenfica";

std::string relativeTime(int64_t unixSeconds) {
    using namespace std::chrono;
    const int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const int64_t d = std::max<int64_t>(0, now - unixSeconds);
    char buf[64];
    if (d < 60) return tr("just now").toStdString();
    if (d < 3600) return tr("%1 min ago").arg(d / 60).toStdString();
    else if (d < 86400) return tr("%1 h ago").arg(d / 3600).toStdString();
    else if (d < 86400 * 30) {
        const long long days = d / 86400;
        return (days == 1 ? tr("1 day ago") : tr("%1 days ago").arg(days)).toStdString();
    } else {
        const std::time_t t = static_cast<std::time_t>(unixSeconds);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
    }
    return buf;
}

std::string sanitizeFileName(std::string name) {
    for (char& ch : name)
        if (std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos || static_cast<unsigned char>(ch) < 32) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name.empty() ? tr("Untitled").toStdString() : name;
}

std::filesystem::path uniqueProjectPath(const std::filesystem::path& folder, const std::string& name) {
    const std::string base = sanitizeFileName(name);
    std::error_code ec;
    auto candidate = folder / platform::fromUtf8(base + kProjectExtension);
    for (int i = 2; std::filesystem::exists(candidate, ec); ++i)
        candidate = folder / platform::fromUtf8(base + " " + std::to_string(i) + kProjectExtension);
    return candidate;
}

bool containsCaseInsensitive(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
}

// A still of the project, rendered once when the home screen shows.
QImage renderThumbnail(const Project& project, QSize size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(10, 10, 12));
    const SkImageInfo info = qtImageInfo(size.width(), size.height());
    if (sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, img.bits(), static_cast<size_t>(img.bytesPerLine()))) {
        const Composition& c = project.comp;
        const float pad = 12.0f;
        const float zoom = std::min((size.width() - pad * 2) / c.width, (size.height() - pad * 2) / c.height);
        const Vec2 origin{size.width() * 0.5f - c.width * zoom * 0.5f, size.height() * 0.5f - c.height * zoom * 0.5f};
        render::drawComposition(surface->getCanvas(), c, std::min(c.duration * 0.5, 2.0), render::View{origin, zoom, {}});
    }
    return img;
}

// A project: thumbnail, name and details; click to open, "..." (or right-click) for more.
class ProjectCard : public QWidget {
public:
    ProjectCard(QString name, QString meta, const QImage* thumbnail, std::function<void()> open, std::function<void(QPoint)> menu)
        : name_(std::move(name)), meta_(std::move(meta)), thumbnail_(thumbnail), open_(std::move(open)), menu_(std::move(menu)) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(name_);
    }
    void setCardWidth(int w) {
        const int thumbH = w * 9 / 16;
        setFixedSize(w, thumbH + 62);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        const bool hot = underMouse();
        const float w = static_cast<float>(width()), h = static_cast<float>(height());
        const float thumbH = w * 9.0f / 16.0f;
        d.rectFilled({0, 0}, {w, h}, hot ? theme::kBgHover : theme::kBgPanel, 8.0f);
        // Thumbnail with rounded top corners.
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, w, thumbH + 8), 8, 8);
        p.save();
        p.setClipPath(clip);
        p.setClipRect(QRectF(0, 0, w, thumbH), Qt::IntersectClip);
        if (thumbnail_ && !thumbnail_->isNull()) p.drawImage(QRectF(0, 0, w, thumbH), *thumbnail_);
        else {
            p.fillRect(QRectF(0, 0, w, thumbH), QColor(10, 10, 12));
            ui::drawLogoMark(p, {w * 0.5f, thumbH * 0.5f}, 18.0f, theme::kTextFaint);
        }
        p.restore();
        d.rect({0.5f, 0.5f}, {w - 0.5f, h - 0.5f}, hot ? theme::kAccent : theme::kBorder, 8.0f, hot ? 1.5f : 1.0f);
        if (hot) {
            const QRectF r = dotsRect();
            d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), r.contains(mapFromGlobal(QCursor::pos())) ? theme::kBgHover : theme::shade(160), 6.0f);
            for (int i = -1; i <= 1; ++i) d.circleFilled({static_cast<float>(r.center().x()) + i * 6.0f, static_cast<float>(r.center().y())}, 2.0f, theme::kText);
        }
        p.setClipRect(QRectF(12, 0, w - 24, h));
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        p.setFont(bold);
        // Too long for the card: an ellipsis, not a cut (a longer language, a long name).
        const int room = static_cast<int>(w) - 28;
        d.text({14, thumbH + 12}, theme::kText, QFontMetrics(bold).elidedText(name_, Qt::ElideRight, room));
        p.setFont(font());
        d.text({14, thumbH + 34}, theme::kTextFaint, fontMetrics().elidedText(meta_, Qt::ElideRight, room));
    }
    void mouseMoveEvent(QMouseEvent*) override { update(); }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!rect().contains(e->position().toPoint())) return;
        if (e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && dotsRect().contains(e->position())))
            menu_(e->globalPosition().toPoint());
        else if (e->button() == Qt::LeftButton) open_();
    }

private:
    using Pt = ui::Pt;
    QRectF dotsRect() const { return {width() - 28.0 - 8, 8, 28, 28}; }
    QString name_, meta_;
    const QImage* thumbnail_;
    std::function<void()> open_;
    std::function<void(QPoint)> menu_;
};

QLabel* styled(const QString& text, Rgba color, float scale = 1.0f, bool bold = false) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QString("color: %1;").arg(theme::q(color).name()));
    if (scale != 1.0f || bold) {
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() * scale);
        if (bold) f.setWeight(QFont::Bold);
        l->setFont(f);
    }
    return l;
}

// The bemotion mark as a widget.
class LogoMark : public QWidget {
public:
    explicit LogoMark(int size) { setFixedSize(size, size); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::drawLogoMark(p, {width() * 0.5f, height() * 0.5f}, width() * 0.48f, theme::kAccent);
    }
};

} // namespace

HomeScreen::HomeScreen(RecentProjects& recents, QWidget* parent) : QWidget(parent), recents_(recents) {
    setAutoFillBackground(true);
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    buildUi();
    // Colors are baked into the widgets: a new theme rebuilds them.
    connect(&ui::AppSignals::get(), &ui::AppSignals::appearanceChanged, this, [this] {
        const QString search = search_->text();
        buildUi();
        search_->setText(search);
        rebuildGrid();
    });
    connect(&ui::AppSignals::get(), &ui::AppSignals::projectsChanged, this, [this] { refresh(); });
    // Projects saved, copied or removed in the projects folder show up by themselves.
    watcher_ = new QFileSystemWatcher(this);
    if (const auto folder = projectsFolder(); !folder.empty()) watcher_->addPath(QString::fromStdString(platform::toUtf8(folder)));
    auto* later = new QTimer(this);
    later->setSingleShot(true);
    later->setInterval(400);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, later, qOverload<>(&QTimer::start));
    connect(later, &QTimer::timeout, this, [this] {
        if (isVisible()) refresh();
    });
}

void HomeScreen::buildUi() {
    if (content_) {
        layout()->removeWidget(content_);
        content_->deleteLater();
    }
    content_ = new QWidget;
    layout()->addWidget(content_);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::q(theme::kBgDeep));
    setPalette(pal);
    columns_ = 0;
    auto* row = new QHBoxLayout(content_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // --- sidebar -----------------------------------------------------------------------
    auto* sidebar = new QFrame;
    sidebar->setFixedWidth(270);
    sidebar->setAutoFillBackground(true);
    QPalette sp = sidebar->palette();
    sp.setColor(QPalette::Window, theme::q(theme::kBgPanel));
    sidebar->setPalette(sp);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(22, 26, 22, 22);
    side->setSpacing(8);
    auto* brand = new QHBoxLayout;
    brand->setSpacing(12);
    brand->addWidget(new LogoMark(40));
    auto* names = new QVBoxLayout;
    names->setSpacing(0);
    names->addWidget(styled("bemotion", theme::kText, 1.6f, true));
    names->addWidget(styled(QString("v%1").arg(BEMOTION_VERSION), theme::kTextFaint));
    brand->addLayout(names, 1);
    side->addLayout(brand);
    side->addSpacing(18);
    auto* create = ui::accentButton(tr("+  New Project"));
    create->setMinimumHeight(38);
    connect(create, &QPushButton::clicked, this, &HomeScreen::openNewProjectDialog);
    side->addWidget(create);
    auto* open = new QPushButton(tr("Open Project..."));
    open->setMinimumHeight(34);
    connect(open, &QPushButton::clicked, this, [this] {
        if (auto file = platform::openFileDialog(TR_NOOP("Open bemotion Project"), TR_NOOP("bemotion Project"), kProjectExtension))
            if (onOpen) onOpen(*file);
    });
    side->addWidget(open);
    side->addStretch(1);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    ui::setTone(line, ui::Tone::Border);
    side->addWidget(line);
    // The projects folder (a button of its own: it needs its words), then the small icons.
    auto* footer = new QHBoxLayout;
    footer->setSpacing(6);
    auto* folder = new QPushButton(ui::makeIcon(ui::Icon::Folder, theme::kTextDim, 16), tr("Projects Folder"));
    folder->setToolTip(projectsFolder().empty() ? tr("Choose where your projects live")
                                                : QString::fromStdString(platform::toUtf8(projectsFolder())));
    connect(folder, &QPushButton::clicked, this, [this, folder] {
        if (projectsFolder().empty()) {
            chooseProjectsFolder();
            return;
        }
        QMenu menu;
        menu.addAction(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("Show in File Manager"),
                       [] { platform::revealInFileManager(projectsFolder()); });
        menu.addAction(tr("Change Folder..."), [this] { chooseProjectsFolder(); });
        menu.exec(folder->mapToGlobal(QPoint(0, folder->height())));
    });
    side->addWidget(folder);
    side->addSpacing(2);
    footer->addStretch(1);
    auto* coffee = new ui::IconButton(ui::Icon::Coffee, tr("Support bemotion: buy a coffee (it stays free thanks to you)"), 30);
    connect(coffee, &QAbstractButton::clicked, this, [] { platform::openWithDefaultApp(kSupportUrl); });
    footer->addWidget(coffee);
    auto* gear = new ui::IconButton(ui::Icon::Gear, tr("Settings (Ctrl+,)"), 30);
    connect(gear, &QAbstractButton::clicked, this, [] { ui::openSettings(); });
    footer->addWidget(gear);
    auto* help = new ui::IconButton(ui::Icon::Help, tr("Help, shortcuts, credits and license (F1)"), 30);
    connect(help, &QPushButton::clicked, this, [] { ui::openHelp(ui::HelpTab::About); });
    footer->addWidget(help);
    side->addLayout(footer);
    count_ = nullptr;
    row->addWidget(sidebar);

    // --- projects ----------------------------------------------------------------------
    auto* main = new QVBoxLayout;
    main->setContentsMargins(36, 30, 36, 0);
    main->setSpacing(6);
    auto* head = new QHBoxLayout;
    auto* title = styled(tr("Projects"), theme::kText, 1.8f, true);
    if (!projectsFolder().empty())
        title->setToolTip(tr("Every project in ") + QString::fromStdString(platform::toUtf8(projectsFolder())) +
                          tr(", and the ones you opened from elsewhere"));
    head->addWidget(title);
    head->addStretch(1);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search projects"));
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(260);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildGrid(); });
    head->addWidget(search_);
    main->addLayout(head);

    errorRow_ = new QWidget;
    auto* er = new QHBoxLayout(errorRow_);
    er->setContentsMargins(0, 4, 0, 4);
    error_ = styled("", theme::kDanger);
    error_->setWordWrap(true);
    er->addWidget(error_, 1);
    auto* dismiss = new QPushButton(tr("Dismiss"));
    connect(dismiss, &QPushButton::clicked, errorRow_, &QWidget::hide);
    er->addWidget(dismiss);
    errorRow_->hide();
    main->addWidget(errorRow_);
    main->addSpacing(8);
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->setAutoFillBackground(false);
    scroll_->viewport()->installEventFilter(this); // its width decides the columns
    main->addWidget(scroll_, 1);
    row->addLayout(main, 1);
}

void HomeScreen::setError(const std::string& message) {
    error_->setText(QString::fromStdString(message));
    errorRow_->setVisible(!message.empty());
}

void HomeScreen::refresh() {
    // Every project in the projects folder (and its subfolders), plus the ones opened from elsewhere.
    namespace fs = std::filesystem;
    entries_.clear();
    auto modified = [](const fs::path& f) -> int64_t {
        std::error_code ec;
        const auto t = fs::last_write_time(f, ec);
        if (ec) return 0;
        using namespace std::chrono;
        // File clock -> system clock through "now" (std::clock_cast is missing from libc++).
        const auto sys = system_clock::now() + duration_cast<system_clock::duration>(t - fs::file_time_type::clock::now());
        return duration_cast<seconds>(sys.time_since_epoch()).count();
    };
    for (const RecentProject& r : recents_.items()) {
        RecentProject e = r;
        e.lastOpened = std::max(r.lastOpened, modified(r.path));
        entries_.push_back(std::move(e));
    }
    std::error_code ec;
    int scanned = 0;
    const fs::path folder = projectsFolder();
    if (folder.empty()) ec = std::make_error_code(std::errc::no_such_file_or_directory); // no folder chosen yet: recents only
    for (auto it = folder.empty() ? fs::recursive_directory_iterator()
                                  : fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator() && scanned < 500; it.increment(ec)) {
        if (it.depth() >= 3) it.disable_recursion_pending();
        std::error_code fe;
        if (!it->is_regular_file(fe) || it->path().extension() != kProjectExtension) continue;
        ++scanned;
        const fs::path path = it->path();
        if (std::any_of(entries_.begin(), entries_.end(), [&](const RecentProject& e) { return fs::equivalent(e.path, path, fe); }))
            continue;
        entries_.push_back({path, platform::toUtf8(path.stem()), modified(path)});
    }
    // The Welcome project is there whatever folder is chosen, until the user deletes it.
    if (const fs::path welcome = settings().welcomeRemoved ? fs::path() : platform::welcomeProject(); !welcome.empty() &&
        std::none_of(entries_.begin(), entries_.end(), [&](const RecentProject& e) { std::error_code fe; return fs::equivalent(e.path, welcome, fe); }))
        entries_.push_back({welcome, tr("Welcome to bemotion").toStdString(), modified(welcome)});
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const RecentProject& a, const RecentProject& b) { return a.lastOpened > b.lastOpened; });

    // Previews: only projects changed since the last time are loaded again.
    std::map<fs::path, Preview> previews;
    for (RecentProject& e : entries_) {
        const int64_t stamp = modified(e.path);
        if (auto old = previews_.find(e.path); old != previews_.end() && old->second.stamp == stamp) {
            previews[e.path] = std::move(old->second);
        } else {
            Preview pv;
            pv.stamp = stamp;
            Project p;
            if (loadProject(e.path, p)) {
                PluginHost::get().reconcile(p.comp); // plugin objects (charts) as the editor would show them
                const qreal dpr = devicePixelRatioF();
                pv.thumbnail = renderThumbnail(p, QSize(static_cast<int>(400 * dpr), static_cast<int>(225 * dpr)));
                pv.project = std::move(p);
            }
            previews[e.path] = std::move(pv);
        }
        if (const Preview& pv = previews[e.path]; pv.project && !pv.project->name.empty()) e.name = pv.project->name;
    }
    previews_ = std::move(previews);
    rebuildGrid();
}

void HomeScreen::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const int avail = scroll_->viewport()->width();
    const int cols = std::max(1, (avail + kGap) / (kCardMin + kGap));
    if (cols != columns_) rebuildGrid();
}

bool HomeScreen::eventFilter(QObject* watched, QEvent* e) {
    // A rebuilt screen (new theme) gets its final width after the grid was first laid out.
    if (scroll_ && watched == scroll_->viewport() && e->type() == QEvent::Resize) {
        const int cols = std::max(1, (scroll_->viewport()->width() + kGap) / (kCardMin + kGap));
        if (cols != columns_) QMetaObject::invokeMethod(this, [this] { rebuildGrid(); }, Qt::QueuedConnection);
    }
    return QWidget::eventFilter(watched, e);
}

void HomeScreen::rebuildGrid() {
    const auto& items = entries_;

    const int avail = std::max(kCardMin, scroll_->viewport()->width() - 2);
    columns_ = std::max(1, (avail + kGap) / (kCardMin + kGap));
    const int cardW = (avail - kGap * (columns_ - 1)) / columns_;

    grid_ = new QWidget;
    auto* col = new QVBoxLayout(grid_);
    col->setContentsMargins(0, 0, 0, 24);
    // No projects folder yet: a banner on top says what it is for, with the button that picks it;
    // it goes as soon as there is a folder.
    if (projectsFolder().empty()) {
        auto* banner = new ui::SettingsCard;
        auto* choose = ui::accentButton(tr("Choose Folder..."));
        connect(choose, &QPushButton::clicked, this, [this] { chooseProjectsFolder(); });
        banner->addRow(tr("Choose a folder for your projects"), tr("New projects are saved there, and every project in it shows here"), choose);
        col->addWidget(banner);
        col->addSpacing(14);
    }
    if (items.empty() && projectsFolder().empty()) {
        col->addStretch(1);
        scroll_->setWidget(grid_);
        return;
    }
    if (items.empty()) {
        col->addStretch(1);
        auto* t1 = styled(tr("No projects yet"), theme::kText, 1.5f, true);
        t1->setAlignment(Qt::AlignCenter);
        col->addWidget(t1);
        auto* t2 = styled(tr("Create your first animation in a few clicks."), theme::kTextDim);
        t2->setAlignment(Qt::AlignCenter);
        col->addWidget(t2);
        col->addSpacing(12);
        auto* b = ui::accentButton(tr("Create a project"));
        b->setFixedSize(200, 40);
        connect(b, &QPushButton::clicked, this, &HomeScreen::openNewProjectDialog);
        col->addWidget(b, 0, Qt::AlignHCenter);
        col->addStretch(2);
        scroll_->setWidget(grid_);
        return;
    }
    auto* grid = new QGridLayout;
    grid->setSpacing(kGap);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    col->addLayout(grid);
    const std::string search = search_->text().toStdString();
    int shown = 0;
    for (const RecentProject& item : items) {
        if (!containsCaseInsensitive(item.name, search)) continue;
        const auto it = previews_.find(item.path);
        const Preview* pv = it != previews_.end() ? &it->second : nullptr;
        std::string meta;
        if (pv && pv->project) {
            char buf[128];
            // When first: what the list is about; the size goes first if the card is narrow.
            std::snprintf(buf, sizeof(buf), "%s  ·  %d × %d  ·  %.4g fps", relativeTime(item.lastOpened).c_str(), pv->project->comp.width,
                          pv->project->comp.height, pv->project->comp.fps);
            meta = buf;
        } else {
            meta = tr("Unreadable file").toStdString() + "  ·  " + relativeTime(item.lastOpened);
        }
        const std::filesystem::path path = item.path;
        const RecentProject copy = item;
        auto* card = new ProjectCard(
            QString::fromStdString(item.name), QString::fromStdString(meta), pv ? &pv->thumbnail : nullptr,
            [this, path] {
                if (onOpen) onOpen(path);
            },
            [this, copy](QPoint at) {
                QMenu menu;
                menu.addAction(ui::menuIcon(ui::Icon::Play, theme::kTextDim), tr("Open"), [this, copy] {
                    if (onOpen) onOpen(copy.path);
                });
                menu.addAction(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("Show in Folder"),
                               [copy] { platform::revealInFileManager(copy.path); });
                menu.addSeparator();
                menu.addAction(ui::menuIcon(ui::Icon::Trash, theme::kDanger), tr("Delete Project..."), [this, copy] { confirmDelete(copy); });
                menu.exec(at);
            });
        card->setCardWidth(cardW);
        grid->addWidget(card, shown / columns_, shown % columns_);
        ++shown;
    }
    if (shown == 0) col->addWidget(styled(tr("No projects match \"%1\".").arg(search_->text()), theme::kTextDim));
    col->addStretch(1);
    scroll_->setWidget(grid_);
}

bool HomeScreen::chooseProjectsFolder() {
    const auto current = projectsFolder();
    const auto folder = platform::chooseFolderDialog(TR_NOOP("Choose a Folder for Your Projects"), current.empty() ? platform::documentsDir() : current);
    if (!folder) return false;
    settings().projectsFolder = platform::toUtf8(*folder);
    saveSettings();
    if (!watcher_->directories().isEmpty()) watcher_->removePaths(watcher_->directories());
    watcher_->addPath(QString::fromStdString(platform::toUtf8(*folder)));
    // The sidebar's tooltips name the folder: rebuilt with it.
    const QString search = search_->text();
    buildUi();
    search_->setText(search);
    refresh();
    return true;
}

void HomeScreen::confirmDelete(const RecentProject& item) {
#ifdef _WIN32
    const char* bin = "Recycle Bin";
#else
    const char* bin = "Trash";
#endif
    std::error_code fe;
    const bool welcome = std::filesystem::equivalent(item.path, platform::welcomeProject(), fe);
    // Which project (as its card shows it), what happens to it, and the two answers: keeping it is
    // the default, deleting is red.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Delete Project"));
    dialog.setMinimumWidth(480);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(24, 22, 24, 18);
    col->setSpacing(12);
    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    auto* icon = new QLabel;
    icon->setPixmap(ui::makeIcon(ui::Icon::Trash, theme::kDanger, 26).pixmap(26, 26));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon, 0, Qt::AlignTop);
    auto* title = styled(tr("Delete \"%1\"?").arg(QString::fromStdString(item.name)), theme::kText, 1.25f, true);
    title->setWordWrap(true);
    head->addWidget(title, 1);
    col->addLayout(head);

    auto* card = new ui::SettingsCard;
    auto* project = new QWidget;
    auto* row = new QHBoxLayout(project);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);
    auto* thumb = new QLabel;
    const QSize thumbSize(128, 72);
    thumb->setFixedSize(thumbSize);
    if (auto it = previews_.find(item.path); it != previews_.end() && !it->second.thumbnail.isNull())
        thumb->setPixmap(QPixmap::fromImage(it->second.thumbnail.scaled(thumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    else thumb->setPixmap(ui::makeIcon(ui::Icon::Film, theme::kTextFaint, 32).pixmap(32, 32));
    thumb->setAlignment(Qt::AlignCenter);
    row->addWidget(thumb);
    auto* about = new QVBoxLayout;
    about->setSpacing(3);
    about->addWidget(styled(QString::fromStdString(item.name), theme::kText, 1.0f, true));
    auto* where = styled(QString::fromStdString(platform::toUtf8(item.path)), theme::kTextFaint, 0.92f);
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    about->addWidget(where);
    about->addStretch();
    row->addLayout(about, 1);
    card->addContent(project);
    col->addWidget(card);

    auto* what = styled(tr("It goes to the %1, where you can still restore it. The media files it uses are not touched.").arg(bin),
                        theme::kTextDim);
    what->setWordWrap(true);
    col->addWidget(what);
    if (welcome) {
        auto* back = styled(tr("The Welcome project can come back later: Settings › Interface › Welcome project."), theme::kTextFaint, 0.92f);
        back->setWordWrap(true);
        col->addWidget(back);
    }
    col->addSpacing(4);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* keep = new QPushButton(tr("Cancel"));
    keep->setMinimumWidth(96);
    keep->setDefault(true);
    keep->setAutoDefault(true);
    auto* del = new QPushButton(tr("Delete Project"));
    del->setProperty("danger", true);
    del->setAutoDefault(false);
    del->setMinimumWidth(120);
    connect(keep, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(del, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(keep);
    buttons->addWidget(del);
    col->addLayout(buttons);
    keep->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;
    std::string err;
    std::error_code ec;
    // A file that is already gone just leaves the list.
    if (!std::filesystem::exists(item.path, ec) || platform::moveToTrash(item.path, &err)) {
        if (welcome) { // not made again at the next start
            settings().welcomeRemoved = true;
            saveSettings();
        }
        recents_.remove(item.path);
        previews_.erase(item.path);
        std::erase_if(entries_, [&](const RecentProject& e) { return e.path == item.path; });
    } else {
        setError(tr("Could not delete \"%1\": %2").arg(QString::fromStdString(item.name), QString::fromStdString(err)).toStdString());
    }
    QMetaObject::invokeMethod(this, [this] { rebuildGrid(); }, Qt::QueuedConnection);
}

void HomeScreen::openNewProjectDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Project"));
    dialog.setMinimumWidth(520);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    auto* name = new QLineEdit(tr("Untitled"));
    name->selectAll();
    form->addRow(tr("Name"), name);
    auto* format = new QComboBox;
    for (const FormatPreset& p : kPresets) format->addItem(tr(p.label));
    form->addRow(tr("Format"), format);
    auto* sizeRow = new QHBoxLayout;
    auto* w = new ui::DragValue;
    auto* h = new ui::DragValue;
    for (ui::DragValue* v : {w, h}) {
        v->setRange(16.0f, 16384.0f);
        v->setSpeed(1.0f);
        v->setFormat("%.0f px");
        v->setMinimumWidth(110);
    }
    w->setValue(1920);
    h->setValue(1080);
    sizeRow->addWidget(w);
    sizeRow->addWidget(new QLabel("×"));
    sizeRow->addWidget(h);
    form->addRow(tr("Size"), sizeRow);
    auto* fps = new QComboBox;
    for (double f : kFpsOptions) fps->addItem(tr("%1 fps").arg(f));
    fps->setCurrentIndex(2);
    form->addRow(tr("Frame rate"), fps);
    auto* bg = new ui::ColorButton;
    bg->setAlphaEnabled(false);
    bg->setColor(Color::fromHex(0x101012));
    form->addRow(tr("Background"), bg);
    double limit = 0.0;
    form->addRow(tr("Length"), ui::lengthLimitRow(limit, [&limit](double v) { limit = v; }));
    // New projects go to the projects folder: asked for first if there is none yet.
    if (projectsFolder().empty() && !chooseProjectsFolder()) return;
    std::filesystem::path folderPath = projectsFolder();
    auto* folderRow = new QHBoxLayout;
    auto* folder = new QLineEdit(QString::fromStdString(platform::toUtf8(folderPath)));
    folder->setReadOnly(true);
    folderRow->addWidget(folder, 1);
    auto* change = new QPushButton(tr("Change..."));
    folderRow->addWidget(change);
    form->addRow(tr("Location"), folderRow);

    bool syncing = false;
    connect(format, &QComboBox::currentIndexChanged, &dialog, [&](int i) {
        if (syncing || i < 0 || kPresets[i].width <= 0) return;
        syncing = true;
        w->setValue(kPresets[i].width);
        h->setValue(kPresets[i].height);
        syncing = false;
    });
    auto sizeEdited = [&] {
        if (syncing) return;
        syncing = true;
        int preset = kCustomPreset;
        for (int i = 0; i < kCustomPreset; ++i)
            if (kPresets[i].width == std::lround(w->value()) && kPresets[i].height == std::lround(h->value())) preset = i;
        format->setCurrentIndex(preset);
        syncing = false;
    };
    connect(w, &ui::DragValue::valueChanged, &dialog, sizeEdited);
    connect(h, &ui::DragValue::valueChanged, &dialog, sizeEdited);
    connect(change, &QPushButton::clicked, &dialog, [&] {
        if (auto file = platform::saveFileDialog(TR_NOOP("Choose where to save the project"), uniqueProjectPath(folderPath, name->text().toStdString()),
                                                 TR_NOOP("bemotion Project"), kProjectExtension)) {
            folderPath = file->parent_path();
            folder->setText(QString::fromStdString(platform::toUtf8(folderPath)));
            name->setText(QString::fromStdString(platform::toUtf8(file->stem())));
        }
    });

    auto* buttons = new QDialogButtonBox;
    QPushButton* createButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    createButton->setProperty("accent", true);
    createButton->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(24, 22, 24, 18);
    col->addLayout(form);
    col->addSpacing(10);
    col->addWidget(buttons);
    name->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;

    Project p;
    p.name = name->text().trimmed().isEmpty() ? tr("Untitled").toStdString() : name->text().trimmed().toStdString();
    p.comp.width = static_cast<int>(std::lround(w->value()));
    p.comp.height = static_cast<int>(std::lround(h->value()));
    p.comp.fps = kFpsOptions[std::clamp(fps->currentIndex(), 0, static_cast<int>(std::size(kFpsOptions)) - 1)];
    p.comp.maxDuration = limit;
    p.comp.fitDuration(); // grows as objects are added (up to the limit)
    p.comp.background = bg->color();
    if (onCreate) onCreate(uniqueProjectPath(folderPath, p.name), std::move(p));
}

} // namespace bemotion
