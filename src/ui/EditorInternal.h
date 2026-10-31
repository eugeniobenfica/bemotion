#pragma once

// What the editor window's source files share (EditorWindow.cpp: the window, docks, update loop
// and files; EditorMenus.cpp: menus and shortcuts). Internal to them.

#include "core/Expressions.h"
#include "core/Lottie.h"
#include "app/Automation.h"
#include "app/Platform.h"
#include "app/PluginHost.h"
#include "app/Recovery.h"
#include "app/RecentProjects.h"
#include "app/Settings.h"
#include "core/Arrange.h"
#include "core/Clipboard.h"
#include "core/Serialization.h"
#include "media/AudioEngine.h"
#include "media/MediaManager.h"
#include "media/Recorder.h"
#include "ui/AppSignals.h"
#include "ui/Dialogs.h"
#include "ui/Keymap.h"
#include "ui/LayerMenus.h"
#include "ui/Mime.h"
#include "ui/Modules.h"
#include "ui/PluginInstall.h"
#include "ui/Theme.h"
#include "ui/PreviewWindow.h"
#include "ui/Tools.h"
#include "ui/Widgets.h"
#include "ui/panels/ColorPanel.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ScopesPanel.h"
#include "ui/panels/ToolSettingsPanel.h"
#include "ui/panels/ObjectsPanel.h"
#include "ui/panels/PropertiesPanel.h"
#include "ui/panels/TimelinePanel.h"
#include "ui/panels/ToolsPanel.h"
#include "ui/panels/AnimationsPanel.h"
#include "ui/panels/ViewportPanel.h"
#include <AutoHideDockContainer.h>
#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSplitter>
#include <QPushButton>
#include <QStackedWidget>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <cfloat>
#include <fstream>
#include <sstream>

namespace bemotion::editor_internal {

using ui::Action;

inline std::filesystem::path layoutFile() { return platform::appDataDir() / "layout.ads"; }

// While the toolbar's grip is dragged: the places it can go (the window edges, the viewport, the
// timeline), the one under the mouse lit with its name, and the bar's picture where it would land.
class ToolbarOverlay : public QWidget {
public:
    struct Zone {
        QRect rect;
        QString label;
    };
    explicit ToolbarOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    std::vector<Zone> zones;
    int hot = -1;          // the zone under the mouse
    QRect landing;         // where the bar goes (empty: nowhere, it stays)
    QPixmap picture;       // the bar, horizontal
    QPixmap uprightPicture; // ... and vertical (left / right edges)
    bool upright = false;
    QPoint cursorAt;       // the picture follows the mouse when it is over no zone

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
            const QRectF r = QRectF(zones[i].rect).adjusted(1.5, 1.5, -1.5, -1.5);
            const ui::Pt a(r.topLeft()), b(r.bottomRight());
            if (i == hot) {
                d.rectFilled(a, b, theme::withAlpha(theme::kAccent, 34), 8.0f);
                d.rect(a, b, theme::kAccent, 8.0f, 2.0f);
            } else {
                d.rect(a, b, theme::withAlpha(theme::kAccent, 90), 8.0f, 1.0f);
            }
        }
        // The name of the place under the mouse, in a chip by the landing spot.
        if (hot >= 0) {
            const QString label = zones[hot].label;
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            const float w = d.textWidth(label) + 20.0f, h = d.textHeight() + 10.0f;
            const QRect near = landing.isEmpty() ? zones[hot].rect : landing;
            float x = near.center().x() - w * 0.5f, y = near.bottom() + 8.0f;
            if (y + h > height() - 4) y = near.top() - h - 8.0f;
            x = std::clamp(x, 4.0f, std::max(4.0f, width() - w - 4.0f));
            d.rectFilled({x, y}, {x + w, y + h}, theme::kAccent, h * 0.5f);
            d.text({x + 10.0f, y + 5.0f}, theme::kOnAccent, label);
        }
        const QPixmap& pic = upright ? uprightPicture : picture;
        if (!landing.isEmpty()) {
            p.setOpacity(0.95);
            p.drawPixmap(landing.topLeft(), pic);
            p.setOpacity(1.0);
            d.rect(ui::Pt(landing.left() - 1.5f, landing.top() - 1.5f), ui::Pt(landing.right() + 2.5f, landing.bottom() + 2.5f), theme::kAccent, 8.0f, 1.5f);
        } else {
            p.setOpacity(0.55);
            p.drawPixmap(cursorAt, picture);
        }
    }
};

// Layers on the system clipboard: our own type, plus the same text for other apps.
inline constexpr const char* kLayersMime = "application/x-bemotion-layers";

// True while the user is in the middle of an edit: mouse held, or typing in a field.
inline bool interacting() {
    if (QApplication::mouseButtons() != Qt::NoButton) return true;
    if (QApplication::activePopupWidget()) return true; // a menu or list open (it may be previewing)
    QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QPlainTextEdit*>(f));
}

inline bool typingInField() {
    QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QPlainTextEdit*>(f));
}

// The tab a minimized panel leaves on the window edge: its names (upright on the side edges, like
// a book's spine) and a chevron pointing where it comes back from. A click brings it back.
class RestoreHandle : public QAbstractButton {
public:
    RestoreHandle(int edge, const QString& names, QWidget* parent) : QAbstractButton(parent), edge_(edge) {
        setText(names);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        const int along = std::clamp(fontMetrics().horizontalAdvance(names) + 40, 64, 320);
        setFixedSize(edge_ < 2 ? QSize(24, along) : QSize(along, 24));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        ui::Draw d(p);
        const bool hot = underMouse();
        const ui::Pt a(0.5f, 0.5f), b(width() - 0.5f, height() - 0.5f);
        d.rectFilled(a, b, hot ? theme::withAlpha(theme::kAccent, 40) : theme::kBgRaised, 6.0f);
        d.rect(a, b, hot ? theme::kAccent : theme::kBorder, 6.0f);
        const int to = edge_ ^ 1; // toward the inside: where the panel comes back
        const ui::Icon icon = to == 1 ? ui::Icon::ChevronRight : to == 0 ? ui::Icon::ChevronLeft
                              : to == 3 ? ui::Icon::ChevronDown : ui::Icon::ChevronUp;
        const ui::Rgba ink = hot ? theme::kText : theme::kTextDim;
        // Laid out as if horizontal, then turned for the side edges (reading upward on the left,
        // downward on the right).
        const float len = static_cast<float>(edge_ < 2 ? height() : width()), across = static_cast<float>(edge_ < 2 ? width() : height());
        p.save();
        if (edge_ == 0) {
            p.translate(0, height());
            p.rotate(-90);
        } else if (edge_ == 1) {
            p.translate(width(), 0);
            p.rotate(90);
        }
        ui::Draw r(p);
        r.text({12.0f, (across - r.textHeight()) * 0.5f}, ink, p.fontMetrics().elidedText(text(), Qt::ElideRight, static_cast<int>(len - 40)));
        p.restore();
        const ui::Pt chevron = edge_ == 0 ? ui::Pt(across * 0.5f, 14.0f) : edge_ == 1 ? ui::Pt(across * 0.5f, len - 14.0f) : ui::Pt(len - 14.0f, across * 0.5f);
        ui::drawIcon(d, icon, chevron, 11.0f, hot ? theme::kAccent : theme::kTextDim);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    int edge_;
};

// A project opened in a new context (history from its saved state).
inline EditorContext opened(Project project, std::filesystem::path path) {
    EditorContext ctx;
    ctx.open(std::move(project), std::move(path));
    return ctx;
}

} // namespace bemotion::editor_internal
