#include "app/DevScript.h"

#include <imgui.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace bemotion {

namespace {

ImGuiKey keyFromName(const std::string& name) {
    if (name == "Space") return ImGuiKey_Space;
    if (name == "Delete") return ImGuiKey_Delete;
    if (name == "Escape") return ImGuiKey_Escape;
    if (name == "Ctrl") return ImGuiKey_LeftCtrl;
    if (name == "Shift") return ImGuiKey_LeftShift;
    if (name == "Alt") return ImGuiKey_LeftAlt;
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'A'));
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (name[0] - '0'));
    return ImGuiKey_None;
}

void setModifier(ImGuiIO& io, ImGuiKey key, bool down) {
    if (key == ImGuiKey_LeftCtrl) io.AddKeyEvent(ImGuiMod_Ctrl, down);
    if (key == ImGuiKey_LeftShift) io.AddKeyEvent(ImGuiMod_Shift, down);
    if (key == ImGuiKey_LeftAlt) io.AddKeyEvent(ImGuiMod_Alt, down);
}

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
        for (std::string w; words >> w;) c.args.push_back(w);
        commands_.push_back(std::move(c));
    }
    return true;
}

void DevScript::apply() {
    ImGuiIO& io = ImGui::GetIO();
    if (moveFrames_ > 0) {
        mouseX_ += (targetX_ - mouseX_) / moveFrames_;
        mouseY_ += (targetY_ - mouseY_) / moveFrames_;
        --moveFrames_;
        io.AddMousePosEvent(mouseX_, mouseY_);
        return;
    }
    if (waitFrames_ > 0) {
        --waitFrames_;
        return;
    }
    // Run commands until one consumes this frame.
    while (next_ < commands_.size()) {
        const Command& c = commands_[next_++];
        auto num = [&](size_t i, float fallback = 0.0f) {
            return i < c.args.size() ? std::strtof(c.args[i].c_str(), nullptr) : fallback;
        };
        if (c.op == "wait") {
            waitFrames_ = static_cast<int>(num(0, 1)) - 1;
            return;
        }
        if (c.op == "move") {
            targetX_ = num(0);
            targetY_ = num(1);
            moveFrames_ = static_cast<int>(num(2, 1));
            mouseX_ += (targetX_ - mouseX_) / moveFrames_;
            mouseY_ += (targetY_ - mouseY_) / moveFrames_;
            --moveFrames_;
            io.AddMousePosEvent(mouseX_, mouseY_);
            return;
        }
        if (c.op == "down" || c.op == "up") {
            io.AddMouseButtonEvent(static_cast<int>(num(0)), c.op == "down");
            return;
        }
        if (c.op == "wheel") {
            io.AddMouseWheelEvent(0.0f, num(0));
            return;
        }
        if (c.op == "key" && c.args.size() >= 2) {
            const ImGuiKey key = keyFromName(c.args[0]);
            const bool down = c.args[1] == "down";
            setModifier(io, key, down);
            io.AddKeyEvent(key, down);
            return;
        }
        if (c.op == "capture" && !c.args.empty()) {
            capture_ = c.args[0];
            return;
        }
        if (c.op == "quit") {
            quit_ = true;
            return;
        }
    }
}

std::string DevScript::takeCapture() {
    std::string c = std::move(capture_);
    capture_.clear();
    return c;
}

} // namespace bemotion
