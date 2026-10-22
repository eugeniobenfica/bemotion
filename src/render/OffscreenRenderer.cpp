#include "render/OffscreenRenderer.h"

#include "render/SceneRenderer.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>

namespace bemotion::render {

namespace {

// Framebuffer objects are OpenGL 3.0 entry points, so they are loaded at
// runtime (opengl32 only exports OpenGL 1.1 on Windows).
#ifdef _WIN32
#define BEMOTION_GL_CALL __stdcall
#else
#define BEMOTION_GL_CALL
#endif
constexpr GLenum kFramebuffer = 0x8D40;         // GL_FRAMEBUFFER
constexpr GLenum kColorAttachment0 = 0x8CE0;    // GL_COLOR_ATTACHMENT0
constexpr GLenum kFramebufferComplete = 0x8CD5; // GL_FRAMEBUFFER_COMPLETE

using GenFramebuffersFn = void(BEMOTION_GL_CALL*)(GLsizei, GLuint*);
using DeleteFramebuffersFn = void(BEMOTION_GL_CALL*)(GLsizei, const GLuint*);
using BindFramebufferFn = void(BEMOTION_GL_CALL*)(GLenum, GLuint);
using FramebufferTexture2DFn = void(BEMOTION_GL_CALL*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusFn = GLenum(BEMOTION_GL_CALL*)(GLenum);

struct FboApi {
    GenFramebuffersFn gen = nullptr;
    DeleteFramebuffersFn del = nullptr;
    BindFramebufferFn bind = nullptr;
    FramebufferTexture2DFn texture2D = nullptr;
    CheckFramebufferStatusFn status = nullptr;

    bool load() {
        if (gen) return true;
        gen = reinterpret_cast<GenFramebuffersFn>(glfwGetProcAddress("glGenFramebuffers"));
        del = reinterpret_cast<DeleteFramebuffersFn>(glfwGetProcAddress("glDeleteFramebuffers"));
        bind = reinterpret_cast<BindFramebufferFn>(glfwGetProcAddress("glBindFramebuffer"));
        texture2D = reinterpret_cast<FramebufferTexture2DFn>(glfwGetProcAddress("glFramebufferTexture2D"));
        status = reinterpret_cast<CheckFramebufferStatusFn>(glfwGetProcAddress("glCheckFramebufferStatus"));
        return gen && del && bind && texture2D && status;
    }
};

FboApi& fbo() {
    static FboApi api;
    return api;
}

} // namespace

OffscreenRenderer::~OffscreenRenderer() { release(); }

bool OffscreenRenderer::init(int width, int height, std::string& error) {
    release();
    if (!fbo().load()) {
        error = "This graphics driver does not support off-screen rendering (OpenGL 3.0).";
        return false;
    }
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (width > maxSize || height > maxSize) {
        error = "The export resolution is larger than the graphics card supports.";
        return false;
    }

    width_ = width;
    height_ = height;
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    fbo().gen(1, &fbo_);
    fbo().bind(kFramebuffer, fbo_);
    fbo().texture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, texture_, 0);
    const bool complete = fbo().status(kFramebuffer) == kFramebufferComplete;
    fbo().bind(kFramebuffer, 0);
    if (!complete) {
        error = "Could not create the off-screen framebuffer.";
        release();
        return false;
    }
    return true;
}

void OffscreenRenderer::release() {
    if (fbo_) fbo().del(1, &fbo_);
    if (texture_) glDeleteTextures(1, &texture_);
    fbo_ = texture_ = 0;
}

void OffscreenRenderer::render(const Composition& comp, double t, float scale, bool background,
                               std::vector<uint8_t>& rgba) {
    const ImVec2 size(static_cast<float>(width_), static_cast<float>(height_));
    ImDrawList dl(ImGui::GetDrawListSharedData());
    dl._ResetForNewFrame();
    dl.PushClipRect(ImVec2(0, 0), size);
    dl.PushTexture(ImGui::GetIO().Fonts->TexRef);
    drawComposition(&dl, comp, t, View{{0.0f, 0.0f}, scale}, background);

    ImDrawData data;
    data.Valid = true;
    data.DisplayPos = ImVec2(0, 0);
    data.DisplaySize = size;
    data.FramebufferScale = ImVec2(1, 1);
    data.Textures = &ImGui::GetPlatformIO().Textures; // uploads glyphs baked for this frame
    data.AddDrawList(&dl);

    fbo().bind(kFramebuffer, fbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(&data);

    rgba.resize(static_cast<size_t>(width_) * height_ * 4);
    std::vector<uint8_t> flipped(rgba.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    fbo().bind(kFramebuffer, 0);

    // GL rows are bottom-up; the blend mode leaves color premultiplied by alpha.
    const size_t stride = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        const uint8_t* src = flipped.data() + static_cast<size_t>(height_ - 1 - y) * stride;
        uint8_t* dst = rgba.data() + static_cast<size_t>(y) * stride;
        std::memcpy(dst, src, stride);
        if (background) continue;
        for (size_t x = 0; x < stride; x += 4) {
            const int a = dst[x + 3];
            if (a == 0 || a == 255) continue;
            for (int c = 0; c < 3; ++c) dst[x + c] = static_cast<uint8_t>(std::min(255, dst[x + c] * 255 / a));
        }
    }
}

} // namespace bemotion::render
