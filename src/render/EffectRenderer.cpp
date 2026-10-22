#include "render/EffectRenderer.h"

#include "render/Fonts.h"
#include "render/GL.h"
#include "render/SceneRenderer.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace bemotion::render {

namespace {

constexpr GLenum kRGBA16F = 0x881A;
constexpr GLenum kClampToBorder = 0x812D;
constexpr int kMaxTarget = 4096;

// --- shaders ------------------------------------------------------------------------

constexpr const char* kVertex = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() { vUV = aPos; gl_Position = vec4(aPos * 2.0 - 1.0, 0.0, 1.0); })";

// Tinted silhouette, optionally dilated (max alpha over a disc) for outlines.
constexpr const char* kDilate = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uRadius; uniform vec4 uColor;
void main() {
    float a = texture(uSrc, vUV).a;
    if (uRadius > 0.5) {
        int rings = clamp(int(uRadius / 2.0) + 1, 1, 8);
        for (int ring = 1; ring <= rings; ++ring) {
            float r = uRadius * float(ring) / float(rings);
            int n = clamp(int(r * 1.2), 8, 64);
            for (int i = 0; i < n; ++i) {
                float ang = 6.2831853 * float(i) / float(n);
                a = max(a, texture(uSrc, vUV + vec2(cos(ang), sin(ang)) * r * uTexel).a);
            }
        }
    }
    a = clamp(a, 0.0, 1.0) * uColor.a;
    o = vec4(uColor.rgb * a, a);
})";

// Separable Gaussian blur on premultiplied colors.
constexpr const char* kBlur = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uDir; uniform float uSigma;
void main() {
    if (uSigma < 0.3) { o = texture(uSrc, vUV); return; }
    float radius = ceil(uSigma * 3.0);
    float stepSize = max(1.0, radius / 48.0);
    int n = int(radius / stepSize);
    vec4 sum = vec4(0.0); float wsum = 0.0;
    for (int i = -n; i <= n; ++i) {
        float x = float(i) * stepSize;
        float w = exp(-x * x / (2.0 * uSigma * uSigma));
        sum += texture(uSrc, vUV + uDir * x) * w;
        wsum += w;
    }
    o = sum / wsum;
})";

// A over (B shifted and scaled).
constexpr const char* kComposite = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uA; uniform sampler2D uB; uniform vec2 uOffset; uniform float uGain;
void main() {
    vec4 a = texture(uA, vUV);
    vec4 b = min(texture(uB, vUV - uOffset) * uGain, vec4(1.0));
    o = a + b * (1.0 - a.a);
})";

constexpr const char* kScale = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc; uniform float uGain;
void main() { o = texture(uSrc, vUV) * uGain; })";

// Directional blur: average along a line centered on each pixel (premultiplied).
constexpr const char* kDirBlur = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uStep; uniform float uLength;
void main() {
    int n = clamp(int(uLength), 1, 96);
    vec4 sum = vec4(0.0);
    for (int i = 0; i <= n; ++i) sum += texture(uSrc, vUV + uStep * (float(i) / float(n) - 0.5) * uLength);
    o = sum / float(n + 1);
})";

// Color effects on straight colors (the texture is premultiplied).
//   0 brightness & contrast   uA = brightness, uB = contrast
//   1 hue / saturation        uA = hue (deg), uB = saturation, uC = lightness
//   2 tint                    uColor = white maps to, uAmount
//   3 fill                    uColor, uAmount
//   4 invert                  uAmount
constexpr const char* kColorOps = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc; uniform int uMode; uniform float uA; uniform float uB; uniform float uC;
uniform vec4 uColor; uniform float uAmount;
vec3 hueShift(vec3 c, float deg) {
    const mat3 toYIQ = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 toRGB = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYIQ * c;
    float a = radians(deg), cs = cos(a), sn = sin(a);
    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return toRGB * yiq;
}
void main() {
    vec4 p = texture(uSrc, vUV);
    if (p.a < 0.0001) { o = vec4(0.0); return; }
    vec3 c = p.rgb / p.a;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    if (uMode == 0) {
        c += uA / 300.0;
        float k = uB >= 0.0 ? 1.0 / max(0.01, 1.0 - uB / 101.0) : 1.0 + uB / 100.0;
        c = (c - 0.5) * k + 0.5;
    } else if (uMode == 1) {
        c = hueShift(c, -uA); // positive = toward the next hue on the wheel, like After Effects
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(l), c, 1.0 + uB / 100.0);
        c = uC >= 0.0 ? mix(c, vec3(1.0), uC / 100.0) : c * (1.0 + uC / 100.0);
    } else if (uMode == 2) {
        c = mix(c, uColor.rgb * luma, uAmount * uColor.a);
    } else if (uMode == 3) {
        c = mix(c, uColor.rgb, uAmount * uColor.a);
    } else {
        c = mix(c, 1.0 - c, uAmount);
    }
    c = clamp(c, 0.0, 1.0);
    o = vec4(c * p.a, p.a);
})";

// Wave warp: displaces pixels across the wave direction (all in pixels).
constexpr const char* kWave = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uTexel; uniform vec2 uDir; uniform float uHeight; uniform float uWidth;
uniform float uPhase;
void main() {
    vec2 px = vUV / uTexel;
    float d = uHeight * sin(6.2831853 * (dot(px, uDir) / max(uWidth, 1.0) - uPhase));
    vec2 perp = vec2(-uDir.y, uDir.x);
    o = texture(uSrc, vUV + perp * d * uTexel);
})";

constexpr const char* kUnpremultiply = R"(#version 330 core
in vec2 vUV; out vec4 o;
uniform sampler2D uSrc;
void main() { vec4 c = texture(uSrc, vUV); o = c.a > 0.0001 ? vec4(c.rgb / c.a, min(c.a, 1.0)) : vec4(0.0); })";

struct Programs {
    GLuint dilate = 0, blur = 0, composite = 0, scale = 0, unpremultiply = 0, dirBlur = 0, colorOps = 0, wave = 0;
    GLuint vao = 0, vbo = 0;
    int state = 0; // 0 not built, 1 ok, -1 failed
};

Programs& programs() {
    static Programs p;
    return p;
}

GLuint compile(GLenum type, const char* source) {
    const auto& gl = gl::api();
    const GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &source, nullptr);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, gl::kCompileStatus, &ok);
    if (!ok) {
        char log[1024];
        gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "effect shader error: %s\n", log);
    }
    return ok ? s : 0;
}

GLuint link(const char* fragment) {
    const auto& gl = gl::api();
    const GLuint vs = compile(gl::kVertexShader, kVertex), fs = compile(gl::kFragmentShader, fragment);
    if (!vs || !fs) return 0;
    const GLuint p = gl.CreateProgram();
    gl.AttachShader(p, vs);
    gl.AttachShader(p, fs);
    gl.LinkProgram(p);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(p, gl::kLinkStatus, &ok);
    return ok ? p : 0;
}

bool ensurePrograms() {
    Programs& p = programs();
    if (p.state != 0) return p.state > 0;
    p.state = -1;
    if (!gl::load()) return false;
    p.dilate = link(kDilate);
    p.blur = link(kBlur);
    p.composite = link(kComposite);
    p.scale = link(kScale);
    p.unpremultiply = link(kUnpremultiply);
    p.dirBlur = link(kDirBlur);
    p.colorOps = link(kColorOps);
    p.wave = link(kWave);
    if (!p.dilate || !p.blur || !p.composite || !p.scale || !p.unpremultiply || !p.dirBlur || !p.colorOps || !p.wave)
        return false;
    const auto& gl = gl::api();
    const float quad[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    gl.GenVertexArrays(1, &p.vao);
    gl.BindVertexArray(p.vao);
    gl.GenBuffers(1, &p.vbo);
    gl.BindBuffer(gl::kArrayBuffer, p.vbo);
    gl.BufferData(gl::kArrayBuffer, sizeof(quad), quad, gl::kStaticDraw);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    gl.BindVertexArray(0);
    p.state = 1;
    return true;
}

// Saves and restores the GL state our passes touch.
struct StateGuard {
    GLint fbo = 0, program = 0, vao = 0, texture = 0, viewport[4] = {};
    GLboolean blend = GL_FALSE, scissor = GL_FALSE;
    StateGuard() {
        glGetIntegerv(gl::kFramebufferBinding, &fbo);
        glGetIntegerv(gl::kCurrentProgram, &program);
        glGetIntegerv(gl::kVertexArrayBinding, &vao);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_VIEWPORT, viewport);
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
    }
    ~StateGuard() {
        const auto& gl = gl::api();
        gl.BindFramebuffer(gl::kFramebuffer, static_cast<GLuint>(fbo));
        gl.UseProgram(static_cast<GLuint>(program));
        gl.BindVertexArray(static_cast<GLuint>(vao));
        gl.ActiveTexture(gl::kTexture0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        scissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }
};

void bindTexture(int unit, GLuint texture) {
    gl::api().ActiveTexture(gl::kTexture0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

GLint uniform(GLuint program, const char* name) { return gl::api().GetUniformLocation(program, name); }

} // namespace

EffectRenderer& screenEffects() {
    static EffectRenderer instance;
    return instance;
}

bool hasActiveEffects(const Layer& layer) {
    return std::any_of(layer.effects.begin(), layer.effects.end(), [](const Effect& e) { return e.enabled; });
}

void EffectRenderer::release() {
    if (!gl::load()) return;
    for (Target& t : pool_) {
        if (t.fbo) gl::api().DeleteFramebuffers(1, &t.fbo);
        if (t.texture) glDeleteTextures(1, &t.texture);
    }
    pool_.clear();
    used_ = 0;
}

EffectRenderer::Target& EffectRenderer::acquire(int width, int height) {
    if (used_ >= pool_.size()) pool_.emplace_back();
    Target& t = pool_[used_++];
    const auto& gl = gl::api();
    if (!t.texture) {
        glGenTextures(1, &t.texture);
        glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToBorder); // outside = transparent
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToBorder);
        gl.GenFramebuffers(1, &t.fbo);
    }
    if (t.width != width || t.height != height) {
        glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, kRGBA16F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(gl::kFramebuffer, t.fbo);
        gl.FramebufferTexture2D(gl::kFramebuffer, gl::kColorAttachment0, GL_TEXTURE_2D, t.texture, 0);
        t.width = width;
        t.height = height;
    }
    return t;
}

void EffectRenderer::drawLayer(ImDrawList* dl, const Layer& layer, double t, double frameDuration,
                               const Affine& viewMatrix, float compToScreen) {
    const float opacity = std::clamp(layer.transform.opacity.valueAt(t) * 0.01f, 0.0f, 1.0f);
    if (opacity <= 0.0f) return;
    if (!ensurePrograms()) { // no shader support: draw the plain layer
        render::drawLayer(dl, layer, t, viewMatrix * layer.matrixAt(t));
        return;
    }

    // Moments to render: one, or several across the shutter for motion blur.
    std::vector<double> times{t};
    for (const Effect& e : layer.effects)
        if (e.enabled && e.type == EffectType::MotionBlur) {
            const double shutter = std::clamp(e.shutter.valueAt(t), 0.0f, 720.0f) / 360.0 * frameDuration;
            const int n = shutter > 1e-6 ? std::clamp(e.samples, 2, 64) : 1;
            times.clear();
            for (int i = 0; i < n; ++i) times.push_back(t - shutter * (n > 1 ? double(i) / (n - 1) : 0.0));
            break;
        }

    // Output-space bounds of every moment, grown by what the effects add around it.
    Vec2 mn{1e9f, 1e9f}, mx{-1e9f, -1e9f};
    for (double ti : times) {
        Vec2 lmn, lmx;
        if (!localBounds(layer, ti, lmn, lmx)) continue;
        const Affine m = viewMatrix * layer.matrixAt(ti);
        for (const Vec2& c : {lmn, Vec2{lmx.x, lmn.y}, lmx, Vec2{lmn.x, lmx.y}}) {
            const Vec2 p = m.apply(c);
            mn = {std::min(mn.x, p.x), std::min(mn.y, p.y)};
            mx = {std::max(mx.x, p.x), std::max(mx.y, p.y)};
        }
    }
    float margin = 4.0f; // anti-aliasing fringe
    for (const Effect& e : layer.effects) margin += effectMargin(e, t) * compToScreen;
    // Only what can reach the visible area matters.
    const ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
    const float x0 = std::max(mn.x - margin, clipMin.x - margin), y0 = std::max(mn.y - margin, clipMin.y - margin);
    const float x1 = std::min(mx.x + margin, clipMax.x + margin), y1 = std::min(mx.y + margin, clipMax.y + margin);
    const Vec2 origin{std::floor(x0), std::floor(y0)};
    const int w = std::min(kMaxTarget, static_cast<int>(std::ceil(x1 - origin.x)));
    const int h = std::min(kMaxTarget, static_cast<int>(std::ceil(y1 - origin.y)));
    if (w <= 0 || h <= 0) return;

    // References into the pool are held across acquire() calls: make sure it never reallocates meanwhile.
    pool_.reserve(used_ + 8 + 4 * layer.effects.size());
    const auto& gl = gl::api();
    const Programs& prog = programs();
    StateGuard guard;
    glDisable(GL_SCISSOR_TEST);

    // Renders the layer at one moment into `target` through ImGui's renderer.
    auto renderContent = [&](double ti, Target& target) {
        ImDrawList list(ImGui::GetDrawListSharedData());
        list._ResetForNewFrame();
        list.PushClipRect(ImVec2(0, 0), ImVec2(static_cast<float>(w), static_cast<float>(h)));
        list.PushTexture(ImGui::GetIO().Fonts->TexRef);
        render::drawLayer(&list, layer, ti, Affine::translate(origin * -1.0f) * viewMatrix * layer.matrixAt(ti), 1.0f);
        ImDrawData data;
        data.Valid = true;
        data.DisplayPos = ImVec2(0, 0);
        data.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        data.FramebufferScale = ImVec2(1, 1);
        data.Textures = Fonts::texturesForOffscreenDraw();
        data.AddDrawList(&list);
        gl.BindFramebuffer(gl::kFramebuffer, target.fbo);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(&data);
        glDisable(GL_SCISSOR_TEST);
    };

    // Runs a full-target shader pass.
    auto pass = [&](GLuint program, Target& target, bool additive) {
        gl.BindFramebuffer(gl::kFramebuffer, target.fbo);
        glViewport(0, 0, w, h);
        if (additive) {
            glEnable(GL_BLEND);
            gl.BlendEquation(gl::kFuncAdd);
            gl.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        } else {
            glDisable(GL_BLEND);
        }
        gl.UseProgram(program);
        gl.BindVertexArray(prog.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };
    const float texelX = 1.0f / w, texelY = 1.0f / h;

    // 1. Content (averaged over the shutter for motion blur).
    Target* current = &acquire(w, h);
    if (times.size() == 1) {
        renderContent(times[0], *current);
    } else {
        Target& moment = acquire(w, h);
        gl.BindFramebuffer(gl::kFramebuffer, current->fbo);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        for (double ti : times) {
            renderContent(ti, moment);
            gl.UseProgram(prog.scale);
            bindTexture(0, moment.texture);
            gl.Uniform1i(uniform(prog.scale, "uSrc"), 0);
            gl.Uniform1f(uniform(prog.scale, "uGain"), 1.0f / static_cast<float>(times.size()));
            pass(prog.scale, *current, true);
        }
    }

    // 2. Effects, in stack order. Each puts something underneath the current image.
    auto silhouette = [&](Target& src, Target& dst, float radius, Color color, float alpha) {
        gl.UseProgram(prog.dilate);
        bindTexture(0, src.texture);
        gl.Uniform1i(uniform(prog.dilate, "uSrc"), 0);
        gl.Uniform2f(uniform(prog.dilate, "uTexel"), texelX, texelY);
        gl.Uniform1f(uniform(prog.dilate, "uRadius"), radius);
        gl.Uniform4f(uniform(prog.dilate, "uColor"), color.r, color.g, color.b, std::clamp(color.a * alpha, 0.0f, 1.0f));
        pass(prog.dilate, dst, false);
    };
    auto blur = [&](Target& a, Target& tmp, float sigma) {
        for (int axis = 0; axis < 2; ++axis) {
            Target& src = axis == 0 ? a : tmp;
            Target& dst = axis == 0 ? tmp : a;
            gl.UseProgram(prog.blur);
            bindTexture(0, src.texture);
            gl.Uniform1i(uniform(prog.blur, "uSrc"), 0);
            gl.Uniform2f(uniform(prog.blur, "uDir"), axis == 0 ? texelX : 0.0f, axis == 0 ? 0.0f : texelY);
            gl.Uniform1f(uniform(prog.blur, "uSigma"), sigma);
            pass(prog.blur, dst, false);
        }
    };
    auto under = [&](Target& top, Target& below, Vec2 offsetPx, float gain) -> Target& {
        Target& out = acquire(w, h);
        gl.UseProgram(prog.composite);
        bindTexture(0, top.texture);
        bindTexture(1, below.texture);
        gl.Uniform1i(uniform(prog.composite, "uA"), 0);
        gl.Uniform1i(uniform(prog.composite, "uB"), 1);
        // Texture rows go bottom-up: a downward screen offset is a negative v offset.
        gl.Uniform2f(uniform(prog.composite, "uOffset"), offsetPx.x * texelX, -offsetPx.y * texelY);
        gl.Uniform1f(uniform(prog.composite, "uGain"), gain);
        pass(prog.composite, out, false);
        return out;
    };

    // Effects that transform the current image (rather than add something underneath).
    auto colorOp = [&](int mode, float a, float b, float c, Color color, float amount) {
        Target& out = acquire(w, h);
        gl.UseProgram(prog.colorOps);
        bindTexture(0, current->texture);
        gl.Uniform1i(uniform(prog.colorOps, "uSrc"), 0);
        gl.Uniform1i(uniform(prog.colorOps, "uMode"), mode);
        gl.Uniform1f(uniform(prog.colorOps, "uA"), a);
        gl.Uniform1f(uniform(prog.colorOps, "uB"), b);
        gl.Uniform1f(uniform(prog.colorOps, "uC"), c);
        gl.Uniform4f(uniform(prog.colorOps, "uColor"), color.r, color.g, color.b, color.a);
        gl.Uniform1f(uniform(prog.colorOps, "uAmount"), std::clamp(amount, 0.0f, 1.0f));
        pass(prog.colorOps, out, false);
        current = &out;
    };

    for (const Effect& e : layer.effects) {
        if (!e.enabled || e.type == EffectType::MotionBlur) continue;
        const float v1 = e.value1.valueAt(t), v2 = e.value2.valueAt(t), v3 = e.value3.valueAt(t);
        switch (e.type) {
        case EffectType::GaussianBlur: {
            Target& tmp = acquire(w, h);
            blur(*current, tmp, std::max(0.0f, e.size.valueAt(t)) * compToScreen * 0.5f);
            continue;
        }
        case EffectType::DirectionalBlur: {
            Target& out = acquire(w, h);
            const float a = degToRad(v1 - 90.0f); // 0° = vertical, like After Effects
            gl.UseProgram(prog.dirBlur);
            bindTexture(0, current->texture);
            gl.Uniform1i(uniform(prog.dirBlur, "uSrc"), 0);
            // Texture rows go bottom-up: flip y.
            gl.Uniform2f(uniform(prog.dirBlur, "uStep"), std::cos(a) * texelX, -std::sin(a) * texelY);
            gl.Uniform1f(uniform(prog.dirBlur, "uLength"), std::max(0.0f, e.size.valueAt(t)) * compToScreen);
            pass(prog.dirBlur, out, false);
            current = &out;
            continue;
        }
        case EffectType::BrightnessContrast: colorOp(0, v1, v2, 0, {}, 0); continue;
        case EffectType::HueSaturation: colorOp(1, v1, v2, v3, {}, 0); continue;
        case EffectType::Tint: colorOp(2, 0, 0, 0, e.color.valueAt(t), e.opacity.valueAt(t) * 0.01f); continue;
        case EffectType::Fill: colorOp(3, 0, 0, 0, e.color.valueAt(t), e.opacity.valueAt(t) * 0.01f); continue;
        case EffectType::Invert: colorOp(4, 0, 0, 0, {}, e.opacity.valueAt(t) * 0.01f); continue;
        case EffectType::WaveWarp: {
            Target& out = acquire(w, h);
            const float a = degToRad(e.shutter.valueAt(t));
            gl.UseProgram(prog.wave);
            bindTexture(0, current->texture);
            gl.Uniform1i(uniform(prog.wave, "uSrc"), 0);
            gl.Uniform2f(uniform(prog.wave, "uTexel"), texelX, texelY);
            gl.Uniform2f(uniform(prog.wave, "uDir"), std::cos(a), std::sin(a));
            gl.Uniform1f(uniform(prog.wave, "uHeight"), v1 * compToScreen);
            gl.Uniform1f(uniform(prog.wave, "uWidth"), std::max(2.0f, v2) * compToScreen);
            gl.Uniform1f(uniform(prog.wave, "uPhase"), static_cast<float>(v3 * (t - layer.inPoint)));
            pass(prog.wave, out, false);
            current = &out;
            continue;
        }
        default: break;
        }
        const float size = std::max(0.0f, e.size.valueAt(t)) * compToScreen;
        const float strength = std::max(0.0f, e.opacity.valueAt(t)) * 0.01f;
        const Color color = e.color.valueAt(t);
        Target& shape = acquire(w, h);
        switch (e.type) {
        case EffectType::Outline:
            silhouette(*current, shape, size, color, std::min(strength, 1.0f));
            current = &under(*current, shape, {0, 0}, 1.0f);
            break;
        case EffectType::Glow: {
            Target& tmp = acquire(w, h);
            silhouette(*current, shape, 0.0f, color, 1.0f);
            blur(shape, tmp, size * 0.5f);
            current = &under(*current, shape, {0, 0}, strength); // intensity above 100% brightens the halo
            break;
        }
        case EffectType::DropShadow: {
            Target& tmp = acquire(w, h);
            silhouette(*current, shape, 0.0f, color, std::min(strength, 1.0f));
            blur(shape, tmp, size * 0.5f);
            current = &under(*current, shape, e.offset.valueAt(t) * compToScreen, 1.0f);
            break;
        }
        default: break;
        }
    }

    // 3. Back to straight alpha for ImGui's blending, then into the draw list.
    Target& result = acquire(w, h);
    gl.UseProgram(prog.unpremultiply);
    bindTexture(0, current->texture);
    gl.Uniform1i(uniform(prog.unpremultiply, "uSrc"), 0);
    pass(prog.unpremultiply, result, false);
    gl.ActiveTexture(gl::kTexture0);

    const ImTextureID id = static_cast<ImTextureID>(static_cast<uintptr_t>(result.texture));
    dl->AddImage(ImTextureRef(id), ImVec2(origin.x, origin.y), ImVec2(origin.x + w, origin.y + h), ImVec2(0, 1),
                 ImVec2(1, 0), IM_COL32(255, 255, 255, static_cast<int>(opacity * 255.0f + 0.5f)));
}

} // namespace bemotion::render
