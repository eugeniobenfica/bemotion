#pragma once

#include "core/Project.h"

#include <filesystem>

namespace bemotion {

// The opening animation played when the app starts, before the home screen.
// It is an ordinary bemotion project (assets/welcome/Welcome.bemotion, also
// the Welcome project users can open and edit), drawn full-window on top of
// the home screen. A click or any key skips to its ending.
class IntroScreen {
public:
    bool start(const std::filesystem::path& project); // false when it cannot be loaded
    bool active() const { return active_; }
    void draw(double dt);

private:
    void skip();

    Project project_;
    double time_ = 0.0;
    double reveal_ = 0.0; // after the animation: the home screen fades in
    bool active_ = false;
};

} // namespace bemotion
