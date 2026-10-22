#include "ui/IntroScreen.h"

#include "core/Serialization.h"
#include "render/Fonts.h"
#include "render/SceneRenderer.h"

#include <imgui.h>

#include <algorithm>

namespace bemotion {

namespace {

constexpr double kRevealSeconds = 0.35; // the home screen fading in afterwards
constexpr double kSkipToEnd = 0.45;     // a skip jumps to the project's own fade-out

ImU32 color(const Color& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alpha));
}

} // namespace

bool IntroScreen::start(const std::filesystem::path& project) {
    if (!loadProject(project, project_)) return false;
    project_.comp.fitDuration();
    time_ = reveal_ = 0.0;
    active_ = true;
    return true;
}

void IntroScreen::skip() { time_ = std::max(time_, project_.comp.duration - kSkipToEnd); }

void IntroScreen::draw(double dt) {
    if (!active_) return;
    const Composition& comp = project_.comp;
    // Text faces load between frames: start the clock once they are in.
    if (Fonts::ensureLoaded(comp)) {
        const double step = std::min(dt, 1.0 / 20.0); // a slow first frame must not skip the start
        if (time_ < comp.duration) time_ += step;
        else reveal_ += step;
    }
    if (reveal_ >= kRevealSeconds) {
        active_ = false;
        return;
    }

    // A full-window layer on top that takes the mouse while it plays.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##intro", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);
    ImGui::InvisibleButton("##skip", vp->Size);
    bool skipped = ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !skipped; ++k)
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false) && !(k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY))
            skipped = true;
    if (skipped) skip();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fade = static_cast<float>(1.0 - reveal_ / kRevealSeconds);
    const ImVec2 a = vp->Pos, b(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    dl->AddRectFilled(a, b, color(comp.background, fade));
    if (time_ < comp.duration) {
        // The whole composition, as large as fits.
        const float zoom = std::min(vp->Size.x / comp.width, vp->Size.y / comp.height);
        const Vec2 origin{a.x + (vp->Size.x - comp.width * zoom) * 0.5f, a.y + (vp->Size.y - comp.height * zoom) * 0.5f};
        render::drawComposition(dl, comp, time_, render::View{origin, zoom, {}}, false);
    }
    ImGui::End();
}

} // namespace bemotion
