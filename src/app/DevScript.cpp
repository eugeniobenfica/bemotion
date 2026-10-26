#include "app/DevScript.h"

#include "app/Platform.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTest>
#include <QWheelEvent>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace bemotion {

namespace {

Qt::Key keyFromName(const std::string& name) {
    static const std::pair<const char*, Qt::Key> kNames[] = {
        {"Space", Qt::Key_Space},     {"Delete", Qt::Key_Delete},     {"Escape", Qt::Key_Escape},
        {"Enter", Qt::Key_Return},    {"Ctrl", Qt::Key_Control},      {"Shift", Qt::Key_Shift},
        {"Alt", Qt::Key_Alt},         {"Left", Qt::Key_Left},         {"Right", Qt::Key_Right},
        {"Up", Qt::Key_Up},           {"Down", Qt::Key_Down},         {"Home", Qt::Key_Home},
        {"End", Qt::Key_End},         {"Tab", Qt::Key_Tab},           {"Backspace", Qt::Key_Backspace},
        {"Comma", Qt::Key_Comma},     {"LeftArrow", Qt::Key_Left},    {"RightArrow", Qt::Key_Right},
        {"Backslash", Qt::Key_Backslash},
    };
    for (const auto& [n, k] : kNames)
        if (name == n) return k;
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return static_cast<Qt::Key>(Qt::Key_A + (name[0] - 'A'));
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') return static_cast<Qt::Key>(Qt::Key_0 + (name[0] - '0'));
    if (name.size() >= 2 && name[0] == 'F') {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return static_cast<Qt::Key>(Qt::Key_F1 + n - 1);
    }
    return Qt::Key_unknown;
}

Qt::MouseButton buttonOf(int b) { return b == 1 ? Qt::RightButton : b == 2 ? Qt::MiddleButton : Qt::LeftButton; }

} // namespace

bool DevScript::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream words(line);
        Command c;
        if (!(words >> c.op)) continue;
        for (std::string w; words >> std::quoted(w);) c.args.push_back(w); // "quoted" for spaces
        commands_.push_back(std::move(c));
    }
    return true;
}

// The widget under the mouse (a popup menu first, or the one holding the press), with the
// point in its coordinates.
QWidget* DevScript::target(QWidget* window, QPoint& local) const {
    const QPoint p(static_cast<int>(mouseX_), static_cast<int>(mouseY_));
    const QPoint global = window->mapToGlobal(p);
    QWidget* w = grabbed_.data();
    if (!w) {
        if (QWidget* popup = QApplication::activePopupWidget()) {
            w = popup->childAt(popup->mapFromGlobal(global));
            if (!w) w = popup;
        } else if (QWidget* top = QApplication::activeModalWidget(); top && top != window) {
            w = top->childAt(top->mapFromGlobal(global));
            if (!w) w = top;
        } else {
            w = window->childAt(p);
            if (!w) w = window;
        }
    }
    local = w->mapFromGlobal(global);
    return w;
}

void DevScript::mouseMove(QWidget* window) {
    QPoint local;
    QWidget* w = target(window, local);
    QMouseEvent e(QEvent::MouseMove, local, w->mapToGlobal(local), Qt::NoButton, Qt::MouseButtons(buttons_),
                  Qt::KeyboardModifiers(modifiers_));
    QApplication::sendEvent(w, &e);
}

void DevScript::mouseButton(QWidget* window, int button, bool down) {
    QPoint local;
    QWidget* w = target(window, local);
    const QPoint global = w->mapToGlobal(local);
    const Qt::MouseButton b = buttonOf(button);
    if (down) buttons_ |= b;
    else buttons_ &= ~b;
    if (down) {
        const QPoint p(static_cast<int>(mouseX_), static_cast<int>(mouseY_));
        const bool dbl = frame_ - lastPressFrame_ < 25 && (p - lastPressPos_).manhattanLength() < 4;
        lastPressFrame_ = dbl ? -100 : frame_;
        lastPressPos_ = p;
        grabbed_ = w;
        if (!dbl && (w->focusPolicy() & Qt::ClickFocus)) w->setFocus(Qt::MouseFocusReason);
        QMouseEvent e(dbl ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress, local, global, b, Qt::MouseButtons(buttons_),
                      Qt::KeyboardModifiers(modifiers_));
        QApplication::sendEvent(w, &e);
    } else {
        QMouseEvent e(QEvent::MouseButtonRelease, local, global, b, Qt::MouseButtons(buttons_), Qt::KeyboardModifiers(modifiers_));
        QApplication::sendEvent(w, &e);
        if (!buttons_) grabbed_ = nullptr;
        // A right click also asks for the context menu, as the system does. Posted: a menu's own
        // event loop then runs after this step, and later steps can drive it.
        if (b == Qt::RightButton)
            QApplication::postEvent(w, new QContextMenuEvent(QContextMenuEvent::Mouse, local, global, Qt::KeyboardModifiers(modifiers_)));
    }
}

void DevScript::step(QWidget* mainWindow) {
    ++frame_;
    // Mouse coordinates are in the chosen window (see "window").
    QWidget* window = other_ ? other_.data() : mainWindow;
    if (moveFrames_ > 0) {
        mouseX_ += (targetX_ - mouseX_) / moveFrames_;
        mouseY_ += (targetY_ - mouseY_) / moveFrames_;
        --moveFrames_;
        mouseMove(window);
        return;
    }
    if (waitFrames_ > 0) {
        --waitFrames_;
        return;
    }
    // Run commands until one consumes this frame.
    while (next_ < commands_.size()) {
        const Command& c = commands_[next_++];
        if (const std::string trace = platform::env("BEMOTION_SCRIPT_TRACE"); !trace.empty()) { // a log of the commands run
            std::ofstream log(platform::fromUtf8(trace), std::ios::app);
            log << "[frame " << frame_ << "] " << c.op << "\n";
        }
        auto num = [&](size_t i, float fallback = 0.0f) { return i < c.args.size() ? std::strtof(c.args[i].c_str(), nullptr) : fallback; };
        if (c.op == "wait") {
            waitFrames_ = static_cast<int>(num(0, 1)) - 1;
            return;
        }
        if (c.op == "window") {
            const QString title = QString::fromStdString(c.args.empty() ? "" : c.args[0]);
            other_ = nullptr;
            for (QWidget* w : QApplication::topLevelWidgets())
                if (w != mainWindow && w->isVisible() && w->windowTitle() == title) other_ = w;
            continue;
        }
        if (c.op == "move") {
            targetX_ = num(0);
            targetY_ = num(1);
            moveFrames_ = std::max(1, static_cast<int>(num(2, 1)));
            mouseX_ += (targetX_ - mouseX_) / moveFrames_;
            mouseY_ += (targetY_ - mouseY_) / moveFrames_;
            --moveFrames_;
            mouseMove(window);
            return;
        }
        if (c.op == "down" || c.op == "up") {
            mouseButton(window, static_cast<int>(num(0)), c.op == "down");
            return;
        }
        if (c.op == "wheel") {
            QPoint local;
            QWidget* w = target(window, local);
            // Up the parents until one takes it, as the system's wheel goes (a sent event does not).
            for (QWidget* t = w; t; t = t->isWindow() ? nullptr : t->parentWidget()) {
                const QPoint at = t->mapFromGlobal(w->mapToGlobal(local));
                QWheelEvent e(at, t->mapToGlobal(at), QPoint(), QPoint(0, static_cast<int>(num(0) * 120)), Qt::MouseButtons(buttons_),
                              Qt::KeyboardModifiers(modifiers_), Qt::NoScrollPhase, false);
                e.setAccepted(false);
                if (QApplication::sendEvent(t, &e) && e.isAccepted()) break;
            }
            return;
        }
        if (c.op == "key" && c.args.size() >= 2) {
            const Qt::Key key = keyFromName(c.args[0]);
            const bool down = c.args[1] == "down";
            const Qt::KeyboardModifier mod = key == Qt::Key_Control ? Qt::ControlModifier
                                             : key == Qt::Key_Shift ? Qt::ShiftModifier
                                             : key == Qt::Key_Alt   ? Qt::AltModifier
                                                                    : Qt::NoModifier;
            if (mod != Qt::NoModifier) {
                if (down) modifiers_ |= mod;
                else modifiers_ &= ~mod;
            }
            QWidget* focus = QApplication::activePopupWidget() ? QApplication::activePopupWidget() // an open menu first
                           : QApplication::focusWidget()      ? QApplication::focusWidget()
                                                              : window;
            if (down) QTest::keyPress(focus, key, Qt::KeyboardModifiers(modifiers_));
            else QTest::keyRelease(focus, key, Qt::KeyboardModifiers(modifiers_));
            return;
        }
        if (c.op == "type") {
            std::string text;
            for (const std::string& a : c.args) text += (text.empty() ? "" : " ") + a;
            QWidget* focus = QApplication::activePopupWidget() ? QApplication::activePopupWidget() // an open menu first
                           : QApplication::focusWidget()      ? QApplication::focusWidget()
                                                              : window;
            QTest::keyClicks(focus, QString::fromStdString(text));
            return;
        }
        if (c.op == "capture" && !c.args.empty()) {
            capture_ = c.args[0];
            return;
        }
        if (c.op == "drop") {
            for (const std::string& f : c.args) drops_.emplace_back(std::u8string(f.begin(), f.end()));
            return;
        }
        if (c.op == "quit") {
            quit_ = true;
            return;
        }
    }
}

} // namespace bemotion
