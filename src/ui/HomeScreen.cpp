#include "ui/HomeScreen.h"

#include "app/Platform.h"
#include "core/Serialization.h"
#include "render/SceneRenderer.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
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
    {"Full HD  ·  1920 × 1080", 1920, 1080},
    {"4K UHD  ·  3840 × 2160", 3840, 2160},
    {"HD  ·  1280 × 720", 1280, 720},
    {"Vertical  ·  1080 × 1920  (Stories, Reels, Shorts)", 1080, 1920},
    {"Square  ·  1080 × 1080", 1080, 1080},
    {"Portrait  ·  1080 × 1350", 1080, 1350},
    {"Custom", 0, 0},
};
constexpr int kCustomPreset = static_cast<int>(std::size(kPresets)) - 1;
constexpr double kFpsOptions[] = {24, 25, 30, 50, 60};
constexpr int kCardMin = 240, kGap = 20;

std::string relativeTime(int64_t unixSeconds) {
    using namespace std::chrono;
    const int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const int64_t d = std::max<int64_t>(0, now - unixSeconds);
    char buf[64];
    if (d < 60) return "just now";
    if (d < 3600) std::snprintf(buf, sizeof(buf), "%lld min ago", static_cast<long long>(d / 60));
    else if (d < 86400) std::snprintf(buf, sizeof(buf), "%lld h ago", static_cast<long long>(d / 3600));
    else if (d < 86400 * 30) {
        const long long days = d / 86400;
        std::snprintf(buf, sizeof(buf), "%lld day%s ago", days, days == 1 ? "" : "s");
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
    return name.empty() ? "Untitled" : name;
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
    const SkImageInfo info = SkImageInfo::MakeN32Premul(size.width(), size.height());
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
        d.text({14, thumbH + 12}, theme::kText, name_);
        p.setFont(font());
        d.text({14, thumbH + 34}, theme::kTextFaint, meta_);
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
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::q(theme::kBgDeep));
    setPalette(pal);
    auto* row = new QHBoxLayout(this);
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
    auto* create = ui::accentButton("+  New Project");
    create->setMinimumHeight(38);
    connect(create, &QPushButton::clicked, this, &HomeScreen::openNewProjectDialog);
    side->addWidget(create);
    auto* open = new QPushButton("Open Project...");
    open->setMinimumHeight(34);
    connect(open, &QPushButton::clicked, this, [this] {
        if (auto file = platform::openFileDialog("Open bemotion Project", "bemotion Project", kProjectExtension))
            if (onOpen) onOpen(*file);
    });
    side->addWidget(open);
    side->addStretch(1);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kBorder).name()));
    side->addWidget(line);
    auto* footer = new QHBoxLayout;
    footer->setSpacing(6);
    auto* folder = new QPushButton("Projects Folder");
    connect(folder, &QPushButton::clicked, this, [] { platform::revealInFileManager(platform::defaultProjectsDir()); });
    footer->addWidget(folder, 1);
    auto* gear = new ui::IconButton(ui::Icon::Gear, "Settings (Ctrl+,)", 30);
    connect(gear, &QAbstractButton::clicked, this, [] { ui::openSettings(); });
    footer->addWidget(gear);
    auto* help = new QPushButton("?");
    help->setFixedSize(30, 30);
    help->setStyleSheet("padding: 0;");
    help->setToolTip("Help, shortcuts, credits and license (F1)");
    connect(help, &QPushButton::clicked, this, [] { ui::openHelp(ui::HelpTab::About); });
    footer->addWidget(help);
    side->addLayout(footer);
    count_ = styled("", theme::kTextFaint);
    side->addWidget(count_);
    row->addWidget(sidebar);

    // --- projects ----------------------------------------------------------------------
    auto* main = new QVBoxLayout;
    main->setContentsMargins(36, 30, 36, 0);
    main->setSpacing(6);
    auto* head = new QHBoxLayout;
    head->addWidget(styled("Projects", theme::kText, 1.8f, true));
    head->addStretch(1);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search projects");
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(260);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildGrid(); });
    head->addWidget(search_);
    main->addLayout(head);
    main->addWidget(styled("Recently opened", theme::kTextFaint));
    errorRow_ = new QWidget;
    auto* er = new QHBoxLayout(errorRow_);
    er->setContentsMargins(0, 4, 0, 4);
    error_ = styled("", theme::kDanger);
    error_->setWordWrap(true);
    er->addWidget(error_, 1);
    auto* dismiss = new QPushButton("Dismiss");
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
    main->addWidget(scroll_, 1);
    row->addLayout(main, 1);
}

void HomeScreen::setError(const std::string& message) {
    error_->setText(QString::fromStdString(message));
    errorRow_->setVisible(!message.empty());
}

void HomeScreen::refresh() {
    previews_.clear();
    for (const auto& r : recents_.items()) {
        Preview pv;
        Project p;
        if (loadProject(r.path, p)) {
            const qreal dpr = devicePixelRatioF();
            pv.thumbnail = renderThumbnail(p, QSize(static_cast<int>(400 * dpr), static_cast<int>(225 * dpr)));
            pv.project = std::move(p);
        }
        previews_[r.path] = std::move(pv);
    }
    rebuildGrid();
}

void HomeScreen::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const int avail = scroll_->viewport()->width();
    const int cols = std::max(1, (avail + kGap) / (kCardMin + kGap));
    if (cols != columns_) rebuildGrid();
}

void HomeScreen::rebuildGrid() {
    const auto& items = recents_.items();
    count_->setText(QString("%1 recent project%2").arg(items.size()).arg(items.size() == 1 ? "" : "s"));
    const int avail = std::max(kCardMin, scroll_->viewport()->width() - 2);
    columns_ = std::max(1, (avail + kGap) / (kCardMin + kGap));
    const int cardW = (avail - kGap * (columns_ - 1)) / columns_;

    grid_ = new QWidget;
    auto* col = new QVBoxLayout(grid_);
    col->setContentsMargins(0, 0, 0, 24);
    if (items.empty()) {
        col->addStretch(1);
        auto* t1 = styled("No projects yet", theme::kText, 1.5f, true);
        t1->setAlignment(Qt::AlignCenter);
        col->addWidget(t1);
        auto* t2 = styled("Create your first animation in a few clicks.", theme::kTextDim);
        t2->setAlignment(Qt::AlignCenter);
        col->addWidget(t2);
        col->addSpacing(12);
        auto* b = ui::accentButton("Create a project");
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
            std::snprintf(buf, sizeof(buf), "%d × %d  ·  %.4g fps  ·  %s", pv->project->comp.width, pv->project->comp.height,
                          pv->project->comp.fps, relativeTime(item.lastOpened).c_str());
            meta = buf;
        } else {
            meta = "Unreadable file  ·  " + relativeTime(item.lastOpened);
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
                menu.addAction("Open", [this, copy] {
                    if (onOpen) onOpen(copy.path);
                });
                menu.addAction("Show in Folder", [copy] { platform::revealInFileManager(copy.path); });
                menu.addSeparator();
                menu.addAction("Remove from List", [this, copy] {
                    recents_.remove(copy.path);
                    previews_.erase(copy.path);
                    QMetaObject::invokeMethod(this, [this] { rebuildGrid(); }, Qt::QueuedConnection);
                });
                menu.addAction(ui::makeIcon(ui::Icon::Trash, theme::kDanger, 16), "Delete Project...",
                               [this, copy] { confirmDelete(copy); });
                menu.exec(at);
            });
        card->setCardWidth(cardW);
        grid->addWidget(card, shown / columns_, shown % columns_);
        ++shown;
    }
    if (shown == 0) col->addWidget(styled(QString("No projects match \"%1\".").arg(search_->text()), theme::kTextDim));
    col->addStretch(1);
    scroll_->setWidget(grid_);
}

void HomeScreen::confirmDelete(const RecentProject& item) {
#ifdef _WIN32
    const char* bin = "Recycle Bin";
#else
    const char* bin = "Trash";
#endif
    QMessageBox box(this);
    box.setWindowTitle("Delete Project?");
    box.setText(QString("Delete \"%1\"?").arg(QString::fromStdString(item.name)));
    box.setInformativeText(QString("The project file goes to the %1. Media files it uses are not touched.").arg(bin));
    QPushButton* del = box.addButton("Delete", QMessageBox::DestructiveRole);
    del->setProperty("danger", true);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != del) return;
    std::string err;
    std::error_code ec;
    // A file that is already gone just leaves the list.
    if (!std::filesystem::exists(item.path, ec) || platform::moveToTrash(item.path, &err)) {
        recents_.remove(item.path);
        previews_.erase(item.path);
    } else {
        setError("Could not delete \"" + item.name + "\": " + err);
    }
    QMetaObject::invokeMethod(this, [this] { rebuildGrid(); }, Qt::QueuedConnection);
}

void HomeScreen::openNewProjectDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("New Project");
    dialog.setMinimumWidth(520);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    auto* name = new QLineEdit("Untitled");
    name->selectAll();
    form->addRow("Name", name);
    auto* format = new QComboBox;
    for (const FormatPreset& p : kPresets) format->addItem(QString::fromUtf8(p.label));
    form->addRow("Format", format);
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
    form->addRow("Size", sizeRow);
    auto* fps = new QComboBox;
    for (double f : kFpsOptions) fps->addItem(QString("%1 fps").arg(f));
    fps->setCurrentIndex(2);
    form->addRow("Frame rate", fps);
    auto* bg = new ui::ColorButton;
    bg->setAlphaEnabled(false);
    bg->setColor(Color::fromHex(0x101012));
    form->addRow("Background", bg);
    std::filesystem::path folderPath = platform::defaultProjectsDir();
    auto* folderRow = new QHBoxLayout;
    auto* folder = new QLineEdit(QString::fromStdString(platform::toUtf8(folderPath)));
    folder->setReadOnly(true);
    folderRow->addWidget(folder, 1);
    auto* change = new QPushButton("Change...");
    folderRow->addWidget(change);
    form->addRow("Location", folderRow);

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
        if (auto file = platform::saveFileDialog("Choose where to save the project", uniqueProjectPath(folderPath, name->text().toStdString()),
                                                 "bemotion Project", kProjectExtension)) {
            folderPath = file->parent_path();
            folder->setText(QString::fromStdString(platform::toUtf8(folderPath)));
            name->setText(QString::fromStdString(platform::toUtf8(file->stem())));
        }
    });

    auto* buttons = new QDialogButtonBox;
    QPushButton* createButton = buttons->addButton("Create", QDialogButtonBox::AcceptRole);
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
    p.name = name->text().trimmed().isEmpty() ? "Untitled" : name->text().trimmed().toStdString();
    p.comp.width = static_cast<int>(std::lround(w->value()));
    p.comp.height = static_cast<int>(std::lround(h->value()));
    p.comp.fps = kFpsOptions[std::clamp(fps->currentIndex(), 0, static_cast<int>(std::size(kFpsOptions)) - 1)];
    p.comp.fitDuration(); // grows as objects are added
    p.comp.background = bg->color();
    if (onCreate) onCreate(uniqueProjectPath(folderPath, p.name), std::move(p));
}

} // namespace bemotion
