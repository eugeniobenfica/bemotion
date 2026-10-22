#include "render/OffscreenRenderer.h"

#include "render/GL.h"
#include "render/SceneRenderer.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>


#include <algorithm>
#include <cstring>

namespace bemotion::render {

OffscreenRenderer::~OffscreenRenderer() { release(); }

bool OffscreenRenderer::init(int width, int height, std::string& error) {
    release();
    if (!gl::load()) {
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

    gl::api().GenFramebuffers(1, &fbo_);
    gl::api().BindFramebuffer(gl::kFramebuffer, fbo_);
    gl::api().FramebufferTexture2D(gl::kFramebuffer, gl::kColorAttachment0, GL_TEXTURE_2D, texture_, 0);
    const bool complete = gl::api().CheckFramebufferStatus(gl::kFramebuffer) == gl::kFramebufferComplete;
    gl::api().BindFramebuffer(gl::kFramebuffer, 0);
    if (!complete) {
        error = "Could not create the off-screen framebuffer.";
        release();
        return false;
    }
    return true;
}

void OffscreenRenderer::release() {
    effects_.release();
    if (fbo_) gl::api().DeleteFramebuffers(1, &fbo_);
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
    effects_.beginFrame();
    drawComposition(&dl, comp, t, View{{0.0f, 0.0f}, scale}, background, &effects_);

    ImDrawData data;
    data.Valid = true;
    data.DisplayPos = ImVec2(0, 0);
    data.DisplaySize = size;
    data.FramebufferScale = ImVec2(1, 1);
    data.Textures = &ImGui::GetPlatformIO().Textures; // uploads glyphs baked for this frame
    data.AddDrawList(&dl);

    gl::api().BindFramebuffer(gl::kFramebuffer, fbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(&data);

    rgba.resize(static_cast<size_t>(width_) * height_ * 4);
    std::vector<uint8_t> flipped(rgba.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    gl::api().BindFramebuffer(gl::kFramebuffer, 0);

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
