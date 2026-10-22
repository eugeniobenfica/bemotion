#pragma once

// Developer aid: replays scripted mouse/keyboard input so UI behavior can be
// checked (and captured) without a person at the screen.
//
//   BEMOTION_SCRIPT=steps.txt bemotion project.bemotion
//
// One command per line ('#' starts a comment); coordinates are window pixels:
//   wait N                 let N frames pass
//   move X Y [N]           move the mouse there (over N frames, default 1)
//   down B / up B          press / release mouse button B (0 left, 1 right, 2 middle)
//   wheel DY               scroll (positive = up)
//   key NAME down|up       NAME: Space, Delete, Escape, Ctrl, Shift, Alt, A..Z, 0..9
//   capture FILE.bmp       save the next rendered frame
//   drop FILE...           drop files on the window (as from the file manager)
//   quit                   close the app

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bemotion {

class DevScript {
public:
    bool load(const std::filesystem::path& file);
    bool active() const { return next_ < commands_.size() || moveFrames_ > 0 || waitFrames_ > 0; }

    // Feeds this frame's events to ImGui. Call between the backend NewFrame and ImGui::NewFrame.
    void apply();

    // A capture requested this frame (empty if none).
    std::string takeCapture();
    bool wantsQuit() const { return quit_; }
    std::vector<std::filesystem::path> takeDrops() { return std::exchange(drops_, {}); }

private:
    struct Command {
        std::string op;
        std::vector<std::string> args;
    };

    std::vector<Command> commands_;
    size_t next_ = 0;
    int waitFrames_ = 0;
    int moveFrames_ = 0;
    float mouseX_ = 0, mouseY_ = 0, targetX_ = 0, targetY_ = 0;
    std::string capture_;
    std::vector<std::filesystem::path> drops_;
    bool quit_ = false;
};

} // namespace bemotion
