#include "app/Application.h"

#include "app/AppIcon.h"
#include "app/Platform.h"
#include "core/Serialization.h"
#include "render/Fonts.h"
#include "ui/EditorScreen.h"
#include "ui/Theme.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>

#ifdef _WIN32
#include <timeapi.h>
#endif

namespace mirage {

namespace {

// Saves the current GL back buffer as a 32-bit BMP (bottom-up, as GL stores it).
void writeFramebufferBmp(const char* path, int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]); // RGBA -> BGRA
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const uint32_t dataSize = static_cast<uint32_t>(px.size());
    u16(0x4D42); u32(54 + dataSize); u32(0); u32(54);                 // file header
    u32(40); u32(w); u32(h); u16(1); u16(32); u32(0); u32(dataSize);    // info header
    u32(2835); u32(2835); u32(0); u32(0);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
}

} // namespace

Application::Application() = default;
Application::~Application() = default;

int Application::run(int argc, char** argv) {
    if (!init()) return 1;

    if (argc > 1) openProject(platform::fromUtf8(argv[1]));

    // Frames are paced in software at the monitor refresh rate: driver vsync
    // (swap interval 1) can stall indefinitely on some GPU drivers.
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    const double frameBudget = 1.0 / std::clamp(mode ? mode->refreshRate : 60, 30, 240);

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            glfwWaitEvents();
            last = glfwGetTime();
            continue;
        }
        glfwPollEvents();
        const double start = glfwGetTime();
        frame(start - last);
        last = start;
        const double remaining = frameBudget - (glfwGetTime() - start);
        if (remaining > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
    }
    shutdown();
    return 0;
}

bool Application::init() {
    glfwSetErrorCallback([](int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc); });
    if (!glfwInit()) return false;
#ifdef _WIN32
    timeBeginPeriod(1); // 1 ms sleep granularity for frame pacing
#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    const float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    window_ = glfwCreateWindow(static_cast<int>(1440 * scale), static_cast<int>(900 * scale), "Mirage", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(0);
    glfwSetWindowUserPointer(window_, this);
    glfwSetWindowCloseCallback(window_, &Application::onWindowClose);
    glfwSetWindowSizeLimits(window_, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMaximizeWindow(window_);
#ifdef _WIN32
    platform::setMainWindowHandle(glfwGetWin32Window(window_));
#endif

    // Window / taskbar icon.
    std::vector<GLFWimage> images;
    std::vector<std::vector<uint8_t>> pixels;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        pixels.push_back(renderAppIcon(size));
        images.push_back(GLFWimage{size, size, pixels.back().data()});
    }
    glfwSetWindowIcon(window_, static_cast<int>(images.size()), images.data());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    iniPath_ = platform::toUtf8(platform::appDataDir() / "layout.ini");
    io.IniFilename = iniPath_.c_str();

    theme::apply();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    Fonts::load(platform::assetsDir());

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    recents_.load();
    home_.refresh(recents_);
    updateTitle();
    return true;
}

void Application::shutdown() {
    editor_.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void Application::frame(double dt) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (editor_) {
        switch (editor_->draw(dt)) {
        case EditorScreen::Request::GoHome: goHome(); break;
        case EditorScreen::Request::Quit:
            editor_.reset();
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            break;
        case EditorScreen::Request::None: break;
        }
    } else {
        HomeScreen::Result r = home_.draw(recents_);
        if (r.action == HomeScreen::Result::Action::Open) openProject(r.path);
        else if (r.action == HomeScreen::Result::Action::Create) createProject(std::move(r.project), r.path);
    }

    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    glViewport(0, 0, w, h);
    const ImVec4 bg = theme::vec(theme::kBgDeep);
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Developer aid: MIRAGE_CAPTURE=<file.bmp> saves frame MIRAGE_CAPTURE_FRAME (default 60) and quits.
    ++frameIndex_;
    if (const char* capture = std::getenv("MIRAGE_CAPTURE")) {
        const char* at = std::getenv("MIRAGE_CAPTURE_FRAME");
        if (frameIndex_ == (at ? std::atoi(at) : 60)) {
            writeFramebufferBmp(capture, w, h);
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
    glfwSwapBuffers(window_);

    updateTitle();
}

void Application::openProject(const std::filesystem::path& path) {
    Project project;
    std::string err;
    if (!loadProject(path, project, &err)) {
        home_.setError("Could not open \"" + platform::toUtf8(path.filename()) + "\": " + err);
        return;
    }
    recents_.touch(path, project.name);
    editor_ = std::make_unique<EditorScreen>(std::move(project), path, recents_);
}

void Application::createProject(Project project, const std::filesystem::path& path) {
    std::string err;
    if (!saveProject(project, path, &err)) {
        home_.setError("Could not create the project: " + err);
        return;
    }
    recents_.touch(path, project.name);
    editor_ = std::make_unique<EditorScreen>(std::move(project), path, recents_);
}

void Application::goHome() {
    editor_.reset();
    recents_.load();
    home_.refresh(recents_);
}

void Application::updateTitle() {
    const std::string title = editor_ ? editor_->windowTitle() : std::string("Mirage");
    if (title != title_) {
        title_ = title;
        glfwSetWindowTitle(window_, title_.c_str());
    }
}

void Application::onWindowClose(GLFWwindow* window) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app && app->editor_) {
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        app->editor_->requestQuit();
    }
}

} // namespace mirage
