#include "render/GL.h"

namespace bemotion::gl {

namespace {
Api g_api{};
int g_state = 0; // 0 not tried, 1 ok, -1 failed

template <typename F>
bool get(F& fn, const char* name) {
    fn = reinterpret_cast<F>(glfwGetProcAddress(name));
    return fn != nullptr;
}
} // namespace

bool load() {
    if (g_state != 0) return g_state > 0;
    Api& a = g_api;
    const bool ok = get(a.GenFramebuffers, "glGenFramebuffers") && get(a.DeleteFramebuffers, "glDeleteFramebuffers") &&
                    get(a.BindFramebuffer, "glBindFramebuffer") &&
                    get(a.FramebufferTexture2D, "glFramebufferTexture2D") &&
                    get(a.CheckFramebufferStatus, "glCheckFramebufferStatus") && get(a.CreateShader, "glCreateShader") &&
                    get(a.ShaderSource, "glShaderSource") && get(a.CompileShader, "glCompileShader") &&
                    get(a.GetShaderiv, "glGetShaderiv") && get(a.GetShaderInfoLog, "glGetShaderInfoLog") &&
                    get(a.DeleteShader, "glDeleteShader") && get(a.CreateProgram, "glCreateProgram") &&
                    get(a.AttachShader, "glAttachShader") && get(a.LinkProgram, "glLinkProgram") &&
                    get(a.GetProgramiv, "glGetProgramiv") && get(a.UseProgram, "glUseProgram") &&
                    get(a.GetUniformLocation, "glGetUniformLocation") && get(a.Uniform1i, "glUniform1i") &&
                    get(a.Uniform1f, "glUniform1f") && get(a.Uniform2f, "glUniform2f") && get(a.Uniform4f, "glUniform4f") &&
                    get(a.GenVertexArrays, "glGenVertexArrays") && get(a.BindVertexArray, "glBindVertexArray") &&
                    get(a.GenBuffers, "glGenBuffers") && get(a.BindBuffer, "glBindBuffer") &&
                    get(a.BufferData, "glBufferData") && get(a.VertexAttribPointer, "glVertexAttribPointer") &&
                    get(a.EnableVertexAttribArray, "glEnableVertexAttribArray") && get(a.ActiveTexture, "glActiveTexture") &&
                    get(a.BlendFuncSeparate, "glBlendFuncSeparate") && get(a.BlendEquation, "glBlendEquation");
    g_state = ok ? 1 : -1;
    return ok;
}

const Api& api() { return g_api; }

} // namespace bemotion::gl
