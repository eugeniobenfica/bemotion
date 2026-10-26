#include "ui/panels/MediaPanel.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "core/Ops.h"
#include "core/Svg.h"
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
#include <QInputDialog>
#include <QKeyEvent>
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
#include <fstream>
#include <map>
#include <set>
#include <sstream>

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

// The list of items: folders (open or closed), then what sits at the top; painted rows
// (thumbnail, name, details), selectable, draggable onto the canvas, the timeline or a folder.
class MediaList : public QWidget {
public:
    MediaList(EditorContext& ctx, MediaPanel& panel) : QWidget(&panel), ctx_(ctx), panel_(panel) {
        setMouseTracking(true);
        setAcceptDrops(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFocusPolicy(Qt::ClickFocus); // Delete removes the item clicked (in a folder too)
    }

    std::string filter;
    uint64_t selected = 0;

    void refresh() {
        const Composition& comp = ctx_.comp();
        std::vector<Row> rows;
        auto itemsIn = [&](const std::string& folder, int indent) {
            for (const MediaItem& item : comp.media)
                if (item.folder == folder && matches(item.name, filter)) rows.push_back({Row::Item, folder, item.id, indent});
        };
        for (const std::string& f : comp.mediaFolders) {
            const size_t at = rows.size();
            const bool open = !closed_.count(f) || !filter.empty(); // searching opens them
            rows.push_back({Row::Folder, f, 0, 0});
            if (open) itemsIn(f, 16);
            // Searching: only the folders with something that matches (or whose name does).
            if (!filter.empty() && rows.size() == at + 1 && !matches(f, filter)) rows.pop_back();
        }
        itemsIn({}, 0);
        if (rows != rows_) {
            rows_ = std::move(rows);
            int h = 0;
            for (const Row& r : rows_) h += height(r);
            rowsHeight_ = h;
            setMinimumHeight(h + kRow); // room below the rows: media dropped there leaves its folder
        }
        update();
    }

    // Folders are names in the project: changed through operations (core/Ops.cpp), one undo step each.
    bool apply(const nlohmann::json& op) {
        const OpsResult r = applyOps(ctx_.project, op);
        if (!r.ok) {
            QMessageBox::warning(window(), tr("Media"), QString::fromStdString(r.error.substr(r.error.find(": ") + 2)));
            return false;
        }
        ctx_.touch();
        refresh();
        return true;
    }
    void newFolder(uint64_t moveHere = 0) {
        bool ok = false;
        std::string name = tr("Folder").toStdString();
        for (int n = 2; std::find(ctx_.comp().mediaFolders.begin(), ctx_.comp().mediaFolders.end(), name) != ctx_.comp().mediaFolders.end(); ++n)
            name = tr("Folder %1").arg(n).toStdString();
        const QString typed = QInputDialog::getText(window(), tr("New Folder"), tr("Name"), QLineEdit::Normal, QString::fromStdString(name), &ok);
        if (!ok || typed.trimmed().isEmpty()) return;
        nlohmann::json ops = nlohmann::json::array({{{"op", "folder.add"}, {"name", typed.trimmed().toStdString()}}});
        if (moveHere) ops.push_back({{"op", "media.move"}, {"media", moveHere}, {"folder", typed.trimmed().toStdString()}});
        apply(ops);
    }

protected:
    struct Row {
        enum Kind { Folder, Item } kind;
        std::string folder; // its own name (a folder) or the one it is in (an item)
        uint64_t id = 0;    // an item's media id
        int indent = 0;
        bool operator==(const Row&) const = default;
    };
    static constexpr int kRow = 46, kFolderRow = 30;
    static int height(const Row& r) { return r.kind == Row::Folder ? kFolderRow : kRow; }

    const Row* rowAt(int y, float* top = nullptr) const {
        int t = 0;
        for (const Row& r : rows_) {
            if (y >= t && y < t + height(r)) {
                if (top) *top = static_cast<float>(t);
                return &r;
            }
            t += height(r);
        }
        return nullptr;
    }
    const MediaItem* itemAt(int y) const {
        const Row* r = rowAt(y);
        return r && r->kind == Row::Item ? ctx_.comp().findMedia(r->id) : nullptr;
    }

    QImage thumbnail(const MediaItem& item) {
        auto& entry = thumbs_[item.id];
        if (entry.first != item.path) {
            entry.first = item.path;
            entry.second = toQImage(media::MediaManager::get().thumbnail(item));
        }
        return entry.second;
    }

    void paintFolder(QPainter& p, ui::Draw& d, const Row& r, float y, bool hot) {
        const bool target = r.folder == dropFolder_;
        if (target) d.rectFilled(ui::Pt(2, y + 1), ui::Pt(width() - 2, y + kFolderRow - 1), theme::accent(45), 6.0f);
        else if (hot) d.rectFilled(ui::Pt(2, y + 1), ui::Pt(width() - 2, y + kFolderRow - 1), theme::kBgHover, 6.0f);
        const bool open = !closed_.count(r.folder) || !filter.empty();
        const float cy = y + kFolderRow * 0.5f;
        ui::drawIcon(d, open ? ui::Icon::ChevronDown : ui::Icon::ChevronRight, ui::Pt(13, cy), 14.0f, theme::kTextDim);
        ui::drawIcon(d, ui::Icon::Folder, ui::Pt(30, cy), 14.0f, target ? theme::kAccent : theme::kTextDim);
        int count = 0;
        for (const MediaItem& m : ctx_.comp().media) count += m.folder == r.folder;
        const QString n = QString::number(count);
        const float nw = static_cast<float>(p.fontMetrics().horizontalAdvance(n));
        d.pushClip(ui::Pt(44, y), ui::Pt(width() - nw - 18, y + kFolderRow));
        d.text(ui::Pt(44, cy - d.textHeight() * 0.5f), theme::kText, QString::fromStdString(r.folder));
        d.popClip();
        d.text(ui::Pt(width() - nw - 10, cy - d.textHeight() * 0.5f), theme::kTextFaint, n);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        media::MediaManager& mm = media::MediaManager::get();
        float y = 0;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& row = rows_[i];
            const float rowTop = y;
            y += height(row);
            if (row.kind == Row::Folder) {
                paintFolder(p, d, row, rowTop, static_cast<int>(i) == hover_);
                continue;
            }
            const MediaItem* item = ctx_.comp().findMedia(row.id);
            if (!item) continue;
            const float x0 = static_cast<float>(row.indent);
            const bool missing = mm.fileMissing(*item);
            const std::string error = missing ? tr("File not found: %1").arg(QString::fromStdString(item->path)).toStdString() : mm.error(item->id);
            if (item->id == selected) d.rectFilled(ui::Pt(x0 + 2, rowTop + 1), ui::Pt(width() - 2, rowTop + kRow - 1), theme::accent(45), 6.0f);
            else if (static_cast<int>(i) == hover_) d.rectFilled(ui::Pt(x0 + 2, rowTop + 1), ui::Pt(width() - 2, rowTop + kRow - 1), theme::kBgHover, 6.0f);
            if (row.indent) d.line(ui::Pt(12, rowTop), ui::Pt(12, rowTop + kRow), theme::kBorder, 1.0f); // inside a folder

            // Thumbnail box: the picture fitted inside, or the kind's icon.
            const float th = kRow - 10.0f, tw = th * 16.0f / 9.0f;
            const ui::Pt a(x0 + 6, rowTop + 5), b(x0 + 6 + tw, rowTop + 5 + th);
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
            d.pushClip(ui::Pt(tx, rowTop), ui::Pt(width() - 14, rowTop + kRow));
            const float lh = d.textHeight();
            const float ty = rowTop + (kRow - lh * 2.0f - 2.0f) * 0.5f;
            d.text(ui::Pt(tx, ty), missing ? theme::kDanger : theme::kText, item->name);
            QString info = missing ? tr("Missing — right-click to relink") : QString::fromStdString(describe(*item, ctx_.comp().fps));
            if (!missing && item->hasAudio && mm.audioPending(item->id)) info += tr("  ·  reading audio...");
            else if (!missing && !error.empty()) info = tr("Error — %1").arg(QString::fromStdString(error));
            else if (const float made = mm.proxyProgress(item->id); !missing && made >= 0.0f)
                info += tr("  ·  making proxy %1%").arg(static_cast<int>(made * 100.0f));
            else if (!missing && mm.hasProxy(*item)) info += mm.useProxies() ? tr("  ·  proxy") : tr("  ·  proxy (off)");
            else if (const std::string failed = mm.proxyError(item->id); !missing && !failed.empty())
                info += tr("  ·  no proxy: %1").arg(QString::fromStdString(failed));
            d.text(ui::Pt(tx, ty + lh + 2.0f), !error.empty() ? theme::kDanger : theme::kTextFaint, info);
            d.popClip();
            if (layersUsing(ctx_.comp(), item->id) > 0) d.circleFilled(ui::Pt(width() - 9.0f, rowTop + 10.0f), 3.0f, theme::kAccent);
        }
        // Media of a folder dragged off it: the room below says where it lands.
        if (dragOut_) {
            const float top = static_cast<float>(rowsHeight_) + 2.0f, bottom = std::max(top + kRow - 4.0f, static_cast<float>(QWidget::height()) - 2.0f);
            d.rectFilled(ui::Pt(2, top), ui::Pt(width() - 2, bottom), theme::accent(30), 6.0f);
            d.rect(ui::Pt(2, top), ui::Pt(width() - 2, bottom), theme::kAccent, 6.0f, 1.0f);
            const QString text = tr("Drop here: out of its folder");
            d.text(ui::Pt((width() - d.textWidth(text)) * 0.5f, top + (kRow - 4.0f - d.textHeight()) * 0.5f), theme::kAccent, text);
        }
    }

    bool event(QEvent* e) override {
        // Delete / Backspace on a selected item: it, not the timeline's layers (the editor's own
        // Delete shortcut would take the key first).
        if (e->type() == QEvent::ShortcutOverride && selected) {
            const int key = static_cast<QKeyEvent*>(e)->key();
            if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
                e->accept();
                return true;
            }
        }
        if (e->type() == QEvent::KeyPress && selected) {
            const int key = static_cast<QKeyEvent*>(e)->key();
            if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
                const uint64_t id = selected;
                selected = 0;
                remove(id);
                return true;
            }
        }
        if (e->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(e);
            const Row* row = rowAt(he->pos().y());
            if (const MediaItem* item = itemAt(he->pos().y())) {
                const bool missing = media::MediaManager::get().fileMissing(*item);
                const std::string error = missing ? tr("File not found: %1").arg(QString::fromStdString(item->path)).toStdString() : media::MediaManager::get().error(item->id);
                QToolTip::showText(he->globalPos(),
                                   QString::fromStdString(item->name + "\n" + item->path + "\n" +
                                                          (error.empty() ? tr("Drag onto the canvas or the timeline, or double-click to add at the playhead.").toStdString() : error)),
                                   this);
            } else if (row && row->kind == Row::Folder) {
                QToolTip::showText(he->globalPos(), tr("A folder to keep media tidy (nothing moves on disk). Drag media onto it."), this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QWidget::event(e);
    }

    int indexAt(int y) const {
        const Row* r = rowAt(y);
        return r ? static_cast<int>(r - rows_.data()) : -1;
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (const int h = indexAt(static_cast<int>(e->position().y())); h != hover_) {
            hover_ = h;
            update();
        }
        if ((e->buttons() & Qt::LeftButton) && pressed_ && (e->position().toPoint() - pressPos_).manhattanLength() > QApplication::startDragDistance()) {
            const MediaItem* item = ctx_.comp().findMedia(pressed_);
            pressed_ = 0;
            if (!item) return;
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
            drag->exec(Qt::CopyAction | Qt::MoveAction);
        }
    }

    void leaveEvent(QEvent*) override {
        hover_ = -1;
        update();
    }

    void mousePressEvent(QMouseEvent* e) override {
        const Row* row = rowAt(static_cast<int>(e->position().y()));
        if (row && row->kind == Row::Folder) {
            const std::string folder = row->folder;
            if (e->button() == Qt::LeftButton && filter.empty()) { // open / close
                if (!closed_.erase(folder)) closed_.insert(folder);
                refresh();
            } else if (e->button() == Qt::RightButton) {
                showFolderMenu(folder, e->globalPosition().toPoint());
            }
            return;
        }
        const MediaItem* item = itemAt(static_cast<int>(e->position().y()));
        selected = item ? item->id : 0;
        pressed_ = e->button() == Qt::LeftButton && item ? item->id : 0;
        pressPos_ = e->position().toPoint();
        update();
        if (e->button() == Qt::RightButton) {
            if (item) showMenu(item->id, e->globalPosition().toPoint());
            else showEmptyMenu(e->globalPosition().toPoint());
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        const Row* row = rowAt(static_cast<int>(e->position().y()));
        if (row && row->kind == Row::Folder) return mousePressEvent(e); // a second click opens / closes again
        const MediaItem* item = itemAt(static_cast<int>(e->position().y()));
        if (item && !media::MediaManager::get().fileMissing(*item)) ctx_.addMedia(item->id);
    }

    // Media dragged onto a folder (or what is in it) goes into it; anywhere else, out of its folder.
    std::string dropTarget(int y) const {
        const Row* r = rowAt(y);
        if (!r) return {};
        return r->folder;
    }
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(ui::kMediaMime)) e->acceptProposedAction();
        else e->ignore(); // files: the panel imports them
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (!e->mimeData()->hasFormat(ui::kMediaMime)) return e->ignore();
        const std::string target = dropTarget(static_cast<int>(e->position().y()));
        uint64_t id = 0;
        const MediaItem* item = ui::readMime(e->mimeData(), ui::kMediaMime, id) ? ctx_.comp().findMedia(id) : nullptr;
        const bool out = target.empty() && item && !item->folder.empty();
        if (target != dropFolder_ || out != dragOut_) dropFolder_ = target, dragOut_ = out, update();
        e->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override {
        dropFolder_.clear();
        dragOut_ = false;
        update();
    }
    void dropEvent(QDropEvent* e) override {
        uint64_t id = 0;
        const std::string target = dropTarget(static_cast<int>(e->position().y()));
        dropFolder_.clear();
        dragOut_ = false;
        update();
        if (!ui::readMime(e->mimeData(), ui::kMediaMime, id)) return e->ignore();
        const MediaItem* item = ctx_.comp().findMedia(id);
        if (item && item->folder != target) apply({{"op", "media.move"}, {"media", id}, {"folder", target}});
        e->acceptProposedAction();
    }

    void addMoveMenu(QMenu& menu, uint64_t id) {
        const MediaItem* item = ctx_.comp().findMedia(id);
        QMenu* move = menu.addMenu(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("Move to Folder"));
        for (const std::string& f : ctx_.comp().mediaFolders) {
            QAction* a = move->addAction(QString::fromStdString(f), [this, id, f] { apply({{"op", "media.move"}, {"media", id}, {"folder", f}}); });
            a->setCheckable(true);
            a->setChecked(item && item->folder == f);
        }
        if (!ctx_.comp().mediaFolders.empty()) move->addSeparator();
        QAction* top = move->addAction(tr("No Folder"), [this, id] { apply({{"op", "media.move"}, {"media", id}, {"folder", ""}}); });
        top->setEnabled(item && !item->folder.empty());
        move->addAction(tr("New Folder..."), [this, id] { newFolder(id); });
    }

    void showMenu(uint64_t id, QPoint at) {
        const MediaItem* item = ctx_.comp().findMedia(id);
        if (!item) return;
        const bool missing = media::MediaManager::get().fileMissing(*item);
        QMenu menu(this);
        QAction* add = menu.addAction(tr("Add to Composition"), [this, id] { ctx_.addMedia(id); });
        add->setEnabled(!missing);
        QAction* reveal = menu.addAction(tr("Show in Folder"), [item] { platform::revealInFileManager(pathOf(item->path)); });
        reveal->setEnabled(!missing);
        menu.addAction(missing ? tr("Relink...") : tr("Replace Footage..."), [this, id] { relink(id); });
        if (item->kind == MediaKind::Video) { // a small copy to edit with: smooth playback of big videos
            media::MediaManager& mm = media::MediaManager::get();
            const MediaItem copy = *item;
            menu.addSeparator();
            if (mm.proxyProgress(id) >= 0.0f) {
                menu.addAction(ui::menuIcon(ui::Icon::Proxy, theme::kTextDim), tr("Stop Making Proxy"), [copy] { media::MediaManager::get().deleteProxy(copy); });
            } else if (mm.hasProxy(*item)) {
                menu.addAction(ui::menuIcon(ui::Icon::Proxy, theme::kTextDim), tr("Delete Proxy"), [copy] { media::MediaManager::get().deleteProxy(copy); });
            } else {
                QAction* make = menu.addAction(ui::menuIcon(ui::Icon::Proxy, theme::kTextDim), tr("Create Proxy"), [copy] {
                    media::MediaManager::get().makeProxies({copy});
                });
                make->setEnabled(!missing);
                make->setToolTip(tr("A small copy of the video the editor plays instead: smooth even for 4K. Exports use the original."));
            }
        }
        menu.addSeparator();
        addMoveMenu(menu, id);
        menu.addSeparator();
        menu.addAction(tr("Remove"), [this, id] { remove(id); });
        menu.exec(at);
    }

    void showFolderMenu(const std::string& folder, QPoint at) {
        QMenu menu(this);
        menu.addAction(tr("Rename..."), [this, folder] {
            bool ok = false;
            const QString typed = QInputDialog::getText(window(), tr("Rename Folder"), tr("Name"), QLineEdit::Normal, QString::fromStdString(folder), &ok);
            if (ok && typed.trimmed().toStdString() != folder)
                if (apply({{"op", "folder.rename"}, {"folder", folder}, {"name", typed.trimmed().toStdString()}}) && closed_.erase(folder))
                    closed_.insert(typed.trimmed().toStdString());
        });
        menu.addAction(tr("New Folder..."), [this] { newFolder(); });
        menu.addSeparator();
        menu.addAction(tr("Delete Folder"), [this, folder] { apply({{"op", "folder.remove"}, {"folder", folder}}); })
            ->setToolTip(tr("The media inside stay, at the top"));
        menu.setToolTipsVisible(true);
        menu.exec(at);
    }

    void showEmptyMenu(QPoint at) {
        QMenu menu(this);
        menu.addAction(tr("Import..."), [this] { panel_.openImportDialog(); });
        menu.addAction(ui::menuIcon(ui::Icon::Folder, theme::kTextDim), tr("New Folder..."), [this] { newFolder(); });
        std::vector<MediaItem> videos; // big videos without a proxy yet
        for (const MediaItem& item : ctx_.comp().media)
            if (media::proxyWanted(item) && !media::MediaManager::get().hasProxy(item) && !media::MediaManager::get().fileMissing(item)) videos.push_back(item);
        QAction* all = menu.addAction(ui::menuIcon(ui::Icon::Proxy, theme::kTextDim), tr("Create Proxies for Big Videos"),
                                      [videos] { media::MediaManager::get().makeProxies(videos); });
        all->setEnabled(!videos.empty());
        menu.exec(at);
    }

    void relink(uint64_t id) {
        const auto files = platform::openFilesDialog(TR_NOOP("Relink Media"), TR_NOOP("Media files"), media::supportedExtensions());
        MediaItem* item = ctx_.comp().findMedia(id);
        if (files.empty() || !item) return;
        media::ProbeResult r = media::probe(files.front());
        if (!r.ok) {
            QMessageBox::warning(window(), tr("Relink"), QString::fromStdString(r.item.name + ": " + r.error));
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
            box.setWindowTitle(tr("Remove Media?"));
            box.setText(tr("\"%1\" is used by %2 layer%3.").arg(QString::fromStdString(item->name)).arg(uses).arg(uses == 1 ? "" : "s"));
            box.setInformativeText(uses == 1 ? tr("Removing it also deletes that layer.") : tr("Removing it also deletes those layers."));
            QPushButton* ok = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
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
    std::vector<Row> rows_;
    std::set<std::string> closed_; // folders closed (for this session)
    std::string dropFolder_;       // the folder media is being dragged onto
    bool dragOut_ = false;         // media from a folder dragged off it: it would leave the folder
    int rowsHeight_ = 0;
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
    auto* import = new ui::IconButton(ui::Icon::Import, tr("Import images, videos or audio (Ctrl+I)"), 28);
    connect(import, &QAbstractButton::clicked, this, &MediaPanel::openImportDialog);
    top->addWidget(import);
    auto* folder = new ui::IconButton(ui::Icon::Folder, tr("New folder: keep media tidy (only in bemotion, nothing moves on disk)"), 28);
    connect(folder, &QAbstractButton::clicked, this, [this] { list_->newFolder(); });
    top->addWidget(folder);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search media"));
    search_->setClearButtonEnabled(true);
    top->addWidget(search_);
    col->addLayout(top);

    messageRow_ = new QWidget;
    auto* mrow = new QHBoxLayout(messageRow_);
    mrow->setContentsMargins(0, 0, 0, 0);
    message_ = new QLabel;
    message_->setWordWrap(true);
    ui::setTone(message_, ui::Tone::Danger);
    mrow->addWidget(message_, 1);
    auto* dismiss = new QPushButton(tr("Dismiss"));
    connect(dismiss, &QPushButton::clicked, messageRow_, &QWidget::hide);
    mrow->addWidget(dismiss, 0, Qt::AlignTop);
    messageRow_->hide();
    col->addWidget(messageRow_);

    empty_ = new QWidget;
    auto* ecol = new QVBoxLayout(empty_);
    ecol->setContentsMargins(0, 12, 0, 0);
    auto* hint = new QLabel(tr("No media yet. Import images, videos or audio, or drop files onto the window."));
    hint->setWordWrap(true);
    ui::setTone(hint, ui::Tone::TextFaint);
    ecol->addWidget(hint);
    auto* importBig = ui::accentButton(tr("Import..."));
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
    hcol->addWidget(list_, 1); // down to the bottom: the room below the rows takes media out of its folder
    scroll_->setWidget(holder);
    col->addWidget(scroll_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        list_->filter = t.toStdString();
        list_->refresh();
    });
}

void MediaPanel::sync() {
    if (!isVisible()) return;
    const bool none = ctx_.comp().media.empty() && ctx_.comp().mediaFolders.empty();
    empty_->setVisible(none);
    scroll_->setVisible(!none);
    list_->refresh();
    // Proxies being made: their progress moves.
    for (const MediaItem& item : ctx_.comp().media)
        if (media::MediaManager::get().proxyProgress(item.id) >= 0.0f) {
            list_->update();
            break;
        }
}

void MediaPanel::showMessage(const std::string& text) {
    message_->setText(QString::fromStdString(text));
    messageRow_->setVisible(!text.empty());
}

void MediaPanel::importFiles(const std::vector<std::filesystem::path>& allFiles, bool addLayers) {
    if (allFiles.empty()) return;
    std::vector<std::string> errors;
    // SVG drawings become shape layers at the playhead (core/Svg.h), not media.
    std::vector<std::filesystem::path> files;
    bool drew = false;
    for (const auto& f : allFiles) {
        std::string ext = platform::toUtf8(f.extension());
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".svg") {
            files.push_back(f);
            continue;
        }
        std::ifstream in(f, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        std::string error;
        if (const uint64_t id = in ? addSvg(ctx_.comp(), text.str(), platform::toUtf8(f.stem()), ctx_.time, &error) : 0) {
            ctx_.select(id);
            drew = true;
        } else {
            errors.push_back(platform::toUtf8(f.filename()) + ": " + (error.empty() ? std::string("could not be read") : error));
        }
    }
    if (drew) ctx_.touch();
    const std::vector<uint64_t> ids = files.empty() ? std::vector<uint64_t>{} : media::MediaManager::get().import(ctx_.comp(), files, &errors);
    if (addLayers)
        for (uint64_t id : ids) ctx_.addMedia(id);
    // Settings › Performance: big videos get their proxy at once.
    if (settings().autoProxies) {
        std::vector<MediaItem> big;
        for (uint64_t id : ids)
            if (const MediaItem* item = ctx_.comp().findMedia(id); item && media::proxyWanted(*item) && !media::MediaManager::get().hasProxy(*item))
                big.push_back(*item);
        if (!big.empty()) media::MediaManager::get().makeProxies(big);
    }
    if (!ids.empty()) {
        list_->selected = ids.back();
        ctx_.touch();
    }
    std::string msg;
    for (const std::string& e : errors) {
        platform::log("import error: " + e);
        msg += (msg.empty() ? "" : "\n") + e;
    }
    showMessage(msg);
}

void MediaPanel::openImportDialog() {
    std::vector<std::string> extensions = media::supportedExtensions();
    extensions.push_back("svg"); // drawings: shape layers (importFiles)
    importFiles(platform::openFilesDialog(TR_NOOP("Import Media"), TR_NOOP("Media files"), extensions), false);
}

void MediaPanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat(ui::kMediaMime)) e->acceptProposedAction();
}

void MediaPanel::dropEvent(QDropEvent* e) {
    // Media of a folder dropped on the panel around the list (its search, its buttons): out of it.
    if (uint64_t id = 0; ui::readMime(e->mimeData(), ui::kMediaMime, id)) {
        if (const MediaItem* item = ctx_.comp().findMedia(id); item && !item->folder.empty())
            list_->apply({{"op", "media.move"}, {"media", id}, {"folder", ""}});
        e->acceptProposedAction();
        return;
    }
    // Files dropped here are only imported (like After Effects' Project panel).
    std::vector<std::filesystem::path> files;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile()) files.push_back(platform::fromUtf8(url.toLocalFile().toStdString()));
    importFiles(files, false);
    e->acceptProposedAction();
}

} // namespace bemotion
