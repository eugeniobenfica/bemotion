#include "ui/panels/MediaPanel.h"

#include "app/Platform.h"
#include "media/MediaIO.h"
#include "media/MediaManager.h"
#include "ui/EditorContext.h"
#include "ui/Mime.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <map>

namespace bemotion {

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool matches(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(text).find(lower(filter)) != std::string::npos;
}

int layersUsing(const Composition& comp, uint64_t mediaId) {
    int n = 0;
    for (const Layer& l : comp.layers) n += l.type == LayerType::Media && l.media.mediaId == mediaId;
    return n;
}

// "Video · 1920×1080 · 00:00:12:05 · 30 fps"
std::string describe(const MediaItem& item, double fps) {
    std::string s = mediaKindLabel(item.kind);
    char buf[64];
    if (item.kind != MediaKind::Audio && item.width > 0) {
        std::snprintf(buf, sizeof buf, "  ·  %d×%d", item.width, item.height);
        s += buf;
    }
    if (item.kind != MediaKind::Image) s += "  ·  " + ui::formatTimecode(item.duration, fps);
    if (item.kind == MediaKind::Video && item.fps > 0) {
        std::snprintf(buf, sizeof buf, "  ·  %.4g fps", item.fps);
        s += buf;
    }
    return s;
}

QImage toQImage(const sk_sp<SkImage>& img) {
    SkPixmap pm;
    if (!img || !img->peekPixels(&pm)) return {};
    const QImage::Format f = pm.alphaType() == kUnpremul_SkAlphaType ? QImage::Format_RGBA8888 : QImage::Format_RGBA8888_Premultiplied;
    if (pm.colorType() != kRGBA_8888_SkColorType) return {};
    return QImage(static_cast<const uchar*>(pm.addr()), pm.width(), pm.height(), static_cast<qsizetype>(pm.rowBytes()), f).copy();
}

} // namespace

// The list of items: painted rows (thumbnail, name, details), selectable, draggable.
class MediaList : public QWidget {
public:
    MediaList(EditorContext& ctx, MediaPanel& panel) : QWidget(&panel), ctx_(ctx), panel_(panel) {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    std::string filter;
    uint64_t selected = 0;

    void refresh() {
        std::vector<uint64_t> ids;
        for (const MediaItem& item : ctx_.comp().media)
            if (matches(item.name, filter)) ids.push_back(item.id);
        if (ids != ids_) {
            ids_ = std::move(ids);
            setFixedHeight(std::max(1, static_cast<int>(ids_.size()) * kRow));
        }
        update();
    }

protected:
    static constexpr int kRow = 46;

    const MediaItem* itemAt(int y) const {
        const int i = y / kRow;
        return i >= 0 && i < static_cast<int>(ids_.size()) ? ctx_.comp().findMedia(ids_[i]) : nullptr;
    }

    QImage thumbnail(const MediaItem& item) {
        auto& entry = thumbs_[item.id];
        if (entry.first != item.path) {
            entry.first = item.path;
            entry.second = toQImage(media::MediaManager::get().thumbnail(item));
        }
        return entry.second;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        media::MediaManager& mm = media::MediaManager::get();
        for (size_t i = 0; i < ids_.size(); ++i) {
            const MediaItem* item = ctx_.comp().findMedia(ids_[i]);
            if (!item) continue;
            const float y = static_cast<float>(i * kRow);
            const bool missing = mm.fileMissing(*item);
            const std::string error = missing ? "File not found: " + item->path : mm.error(item->id);
            if (item->id == selected) d.rectFilled(ui::Pt(2, y + 1), ui::Pt(width() - 2, y + kRow - 1), theme::accent(45), 6.0f);
            else if (static_cast<int>(i) == hover_) d.rectFilled(ui::Pt(2, y + 1), ui::Pt(width() - 2, y + kRow - 1), theme::kBgHover, 6.0f);

            // Thumbnail box: the picture fitted inside, or the kind's icon.
            const float th = kRow - 10.0f, tw = th * 16.0f / 9.0f;
            const ui::Pt a(6, y + 5), b(6 + tw, y + 5 + th);
            d.rectFilled(a, b, theme::kBgDeep, 4.0f);
            const QImage img = missing || item->kind == MediaKind::Audio ? QImage() : thumbnail(*item);
            if (!img.isNull()) {
                const float s = std::min(tw / img.width(), th / img.height());
                const float w = img.width() * s, h = img.height() * s;
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawImage(QRectF(a.x + (tw - w) * 0.5f, a.y + (th - h) * 0.5f, w, h), img);
            } else {
                ui::drawIcon(d, ui::iconForMedia(item->kind), ui::Pt((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), th * 0.42f,
                             missing ? theme::kDanger : theme::kTextDim);
            }
            d.rect(a, b, missing ? theme::kDanger : theme::kBorder, 4.0f);

            const float tx = b.x + 8.0f;
            d.pushClip(ui::Pt(tx, y), ui::Pt(width() - 14, y + kRow));
            const float lh = d.textHeight();
            const float ty = y + (kRow - lh * 2.0f - 2.0f) * 0.5f;
            d.text(ui::Pt(tx, ty), missing ? theme::kDanger : theme::kText, item->name);
            std::string info = missing ? "Missing — right-click to relink" : describe(*item, ctx_.comp().fps);
            if (!missing && item->hasAudio && mm.audioPending(item->id)) info += "  ·  reading audio...";
            else if (!missing && !error.empty()) info = "Error — " + error;
            d.text(ui::Pt(tx, ty + lh + 2.0f), !error.empty() ? theme::kDanger : theme::kTextFaint, info);
            d.popClip();
            if (layersUsing(ctx_.comp(), item->id) > 0) d.circleFilled(ui::Pt(width() - 9.0f, y + 10.0f), 3.0f, theme::kAccent);
        }
    }

    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(e);
            if (const MediaItem* item = itemAt(he->pos().y())) {
                const bool missing = media::MediaManager::get().fileMissing(*item);
                const std::string error = missing ? "File not found: " + item->path : media::MediaManager::get().error(item->id);
                QToolTip::showText(he->globalPos(),
                                   QString::fromStdString(item->name + "\n" + item->path + "\n" +
                                                          (error.empty() ? "Drag onto the canvas or the timeline, or double-click to add at the playhead." : error)),
                                   this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QWidget::event(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const int h = static_cast<int>(e->position().y()) / kRow;
        if (h != hover_) {
            hover_ = h;
            update();
        }
        if ((e->buttons() & Qt::LeftButton) && pressed_ && (e->position().toPoint() - pressPos_).manhattanLength() > QApplication::startDragDistance()) {
            const MediaItem* item = ctx_.comp().findMedia(pressed_);
            pressed_ = 0;
            if (!item || media::MediaManager::get().fileMissing(*item)) return;
            auto* drag = new QDrag(this);
            drag->setMimeData(ui::makeMime(ui::kMediaMime, item->id));
            QPixmap pm(56, 56);
            pm.fill(Qt::transparent);
            {
                QPainter p(&pm);
                ui::Draw d(p);
                d.rectFilled(ui::Pt(2, 2), ui::Pt(54, 54), theme::withAlpha(theme::kBgRaised, 230), 10.0f);
                ui::drawIcon(d, ui::iconForMedia(item->kind), ui::Pt(28, 28), 26.0f, theme::kAccent);
            }
            drag->setPixmap(pm);
            drag->setHotSpot(QPoint(28, 28));
            drag->exec(Qt::CopyAction);
        }
    }

    void leaveEvent(QEvent*) override {
        hover_ = -1;
        update();
    }

    void mousePressEvent(QMouseEvent* e) override {
        const MediaItem* item = itemAt(static_cast<int>(e->position().y()));
        selected = item ? item->id : 0;
        pressed_ = e->button() == Qt::LeftButton && item ? item->id : 0;
        pressPos_ = e->position().toPoint();
        update();
        if (e->button() == Qt::RightButton && item) showMenu(item->id, e->globalPosition().toPoint());
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        const MediaItem* item = itemAt(static_cast<int>(e->position().y()));
        if (item && !media::MediaManager::get().fileMissing(*item)) ctx_.addMedia(item->id);
    }

    void showMenu(uint64_t id, QPoint at) {
        const MediaItem* item = ctx_.comp().findMedia(id);
        if (!item) return;
        const bool missing = media::MediaManager::get().fileMissing(*item);
        QMenu menu(this);
        QAction* add = menu.addAction("Add to Composition", [this, id] { ctx_.addMedia(id); });
        add->setEnabled(!missing);
        QAction* reveal = menu.addAction("Show in Folder", [item] { platform::revealInFileManager(pathOf(item->path)); });
        reveal->setEnabled(!missing);
        menu.addAction(missing ? "Relink..." : "Replace Footage...", [this, id] { relink(id); });
        menu.addSeparator();
        menu.addAction("Remove", [this, id] { remove(id); });
        menu.exec(at);
    }

    void relink(uint64_t id) {
        const auto files = platform::openFilesDialog("Relink Media", "Media files", media::supportedExtensions());
        MediaItem* item = ctx_.comp().findMedia(id);
        if (files.empty() || !item) return;
        media::ProbeResult r = media::probe(files.front());
        if (!r.ok) {
            QMessageBox::warning(window(), "Relink", QString::fromStdString(r.item.name + ": " + r.error));
            return;
        }
        // Same item (its layers keep pointing at it), new file.
        r.item.id = item->id;
        *item = std::move(r.item);
        for (Layer& l : ctx_.comp().layers)
            if (l.type == LayerType::Media && l.media.mediaId == item->id) {
                l.media.hasVideo = item->hasVideo;
                l.media.hasAudio = item->hasAudio;
                if (item->width > 0) l.media.size = {static_cast<float>(item->width), static_cast<float>(item->height)};
            }
        ctx_.touch();
    }

    void remove(uint64_t id) {
        Composition& comp = ctx_.comp();
        const MediaItem* item = comp.findMedia(id);
        if (!item) return;
        // Unused items go right away; used ones ask first (their layers go too).
        if (const int uses = layersUsing(comp, id); uses > 0) {
            QMessageBox box(window());
            box.setWindowTitle("Remove Media?");
            box.setText(QString("\"%1\" is used by %2 layer%3.").arg(QString::fromStdString(item->name)).arg(uses).arg(uses == 1 ? "" : "s"));
            box.setInformativeText(uses == 1 ? "Removing it also deletes that layer." : "Removing it also deletes those layers.");
            QPushButton* ok = box.addButton("Remove", QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != ok) return;
            std::vector<uint64_t> doomed;
            for (const Layer& l : comp.layers)
                if (l.type == LayerType::Media && l.media.mediaId == id) doomed.push_back(l.id);
            for (uint64_t lid : doomed) comp.removeLayerTree(lid);
            ctx_.select(0);
        }
        std::erase_if(comp.media, [&](const MediaItem& m) { return m.id == id; });
        thumbs_.erase(id);
        ctx_.touch();
    }

private:
    EditorContext& ctx_;
    MediaPanel& panel_;
    std::vector<uint64_t> ids_;
    std::map<uint64_t, std::pair<std::string, QImage>> thumbs_;
    int hover_ = -1;
    uint64_t pressed_ = 0;
    QPoint pressPos_;
};

MediaPanel::MediaPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setAcceptDrops(true);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);

    auto* top = new QHBoxLayout;
    auto* import = new ui::IconButton(ui::Icon::Import, "Import images, videos or audio (Ctrl+I)", 28);
    connect(import, &QAbstractButton::clicked, this, &MediaPanel::openImportDialog);
    top->addWidget(import);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search media");
    search_->setClearButtonEnabled(true);
    top->addWidget(search_);
    col->addLayout(top);

    messageRow_ = new QWidget;
    auto* mrow = new QHBoxLayout(messageRow_);
    mrow->setContentsMargins(0, 0, 0, 0);
    message_ = new QLabel;
    message_->setWordWrap(true);
    message_->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kDanger).name()));
    mrow->addWidget(message_, 1);
    auto* dismiss = new QPushButton("Dismiss");
    connect(dismiss, &QPushButton::clicked, messageRow_, &QWidget::hide);
    mrow->addWidget(dismiss, 0, Qt::AlignTop);
    messageRow_->hide();
    col->addWidget(messageRow_);

    empty_ = new QWidget;
    auto* ecol = new QVBoxLayout(empty_);
    ecol->setContentsMargins(0, 12, 0, 0);
    auto* hint = new QLabel("No media yet. Import images, videos or audio, or drop files onto the window.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color: %1;").arg(theme::q(theme::kTextFaint).name()));
    ecol->addWidget(hint);
    auto* importBig = ui::accentButton("Import...");
    connect(importBig, &QPushButton::clicked, this, &MediaPanel::openImportDialog);
    ecol->addWidget(importBig);
    ecol->addStretch();
    col->addWidget(empty_, 1);

    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    auto* hcol = new QVBoxLayout(holder);
    hcol->setContentsMargins(0, 0, 0, 0);
    list_ = new MediaList(ctx_, *this);
    hcol->addWidget(list_);
    hcol->addStretch();
    scroll_->setWidget(holder);
    col->addWidget(scroll_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        list_->filter = t.toStdString();
        list_->refresh();
    });
}

void MediaPanel::sync() {
    if (!isVisible()) return;
    const bool none = ctx_.comp().media.empty();
    empty_->setVisible(none);
    scroll_->setVisible(!none);
    list_->refresh();
}

void MediaPanel::showMessage(const std::string& text) {
    message_->setText(QString::fromStdString(text));
    messageRow_->setVisible(!text.empty());
}

void MediaPanel::importFiles(const std::vector<std::filesystem::path>& files, bool addLayers) {
    if (files.empty()) return;
    std::vector<std::string> errors;
    const std::vector<uint64_t> ids = media::MediaManager::get().import(ctx_.comp(), files, &errors);
    if (addLayers)
        for (uint64_t id : ids) ctx_.addMedia(id);
    if (!ids.empty()) {
        list_->selected = ids.back();
        ctx_.touch();
    }
    std::string msg;
    for (const std::string& e : errors) msg += (msg.empty() ? "" : "\n") + e;
    showMessage(msg);
}

void MediaPanel::openImportDialog() {
    importFiles(platform::openFilesDialog("Import Media", "Media files", media::supportedExtensions()), false);
}

void MediaPanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MediaPanel::dropEvent(QDropEvent* e) {
    // Files dropped here are only imported (like After Effects' Project panel).
    std::vector<std::filesystem::path> files;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile()) files.push_back(platform::fromUtf8(url.toLocalFile().toStdString()));
    importFiles(files, false);
    e->acceptProposedAction();
}

} // namespace bemotion
