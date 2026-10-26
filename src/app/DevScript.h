#pragma once

// Developer aid: replays scripted mouse/keyboard input so UI behavior can be
// checked (and captured) without a person at the screen.
//
//   BEMOTION_SCRIPT=steps.txt bemotion project.bemotion
//
// One command per line ('#' starts a comment; "quote" arguments with spaces); coordinates are window pixels:
//   wait N                 let N frames (~16 ms each) pass
//   move X Y [N]           move the mouse there (over N frames, default 1)
//   down B / up B          press / release mouse button B (0 left, 1 right, 2 middle);
//                          a second press soon after at the same spot is a double click;
//                          releasing the right one opens the context menu there
//   wheel DY               scroll (positive = up)
//   key NAME down|up       NAME: Space, Delete, Escape, Enter, Ctrl, Shift, Alt, A..Z, 0..9, F1..F12,
//                          Left, Right, Up, Down, Home, End, Tab, Backspace, Comma
//   type TEXT              types the text into the focused field
//   capture FILE           save the window (format from the extension: .png, .bmp, ...);
//                          other open windows go to FILE-<window title>.<ext> (menus: "menu",
//                          an open list: "popup")
//   drop FILE...           drop files on the window (as from the file manager)
//   window TITLE           later mouse commands go to that open window ("Settings", a dialog...);
//                          "window main" (or any title not open) goes back to the main window
//   quit                   close the app (without asking to save)
//
// BEMOTION_SCRIPT_TRACE=<file> logs each command run, with its frame.

#include <QPoint>
#include <QPointer>
#include <QWidget>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bemotion {

class DevScript {
public:
    bool load(const std::filesystem::path& file);
    bool active() const { return next_ < commands_.size() || moveFrames_ > 0 || waitFrames_ > 0; }

    // Runs this frame's commands against `window`.
    void step(QWidget* window);

    std::string takeCapture() { return std::exchange(capture_, {}); }
    bool wantsQuit() const { return quit_; }
    std::vector<std::filesystem::path> takeDrops() { return std::exchange(drops_, {}); }

private:
    struct Command {
        std::string op;
        std::vector<std::string> args;
    };
    void mouseMove(QWidget* window);
    void mouseButton(QWidget* window, int button, bool down);
    QWidget* target(QWidget* window, QPoint& local) const;

    std::vector<Command> commands_;
    size_t next_ = 0;
    int waitFrames_ = 0;
    int moveFrames_ = 0;
    float mouseX_ = 0, mouseY_ = 0, targetX_ = 0, targetY_ = 0;
    int buttons_ = 0;           // Qt::MouseButtons
    int modifiers_ = 0;         // Qt::KeyboardModifiers
    QPointer<QWidget> grabbed_; // receives moves and the release after a press
    QPointer<QWidget> other_;   // the window mouse commands go to (null: the main one)
    int frame_ = 0, lastPressFrame_ = -100;
    QPoint lastPressPos_;
    std::string capture_;
    std::vector<std::filesystem::path> drops_;
    bool quit_ = false;
};

} // namespace bemotion
