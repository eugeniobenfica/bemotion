#pragma once

// Minimal OpenGL 3.3 function loader for bemotion's own GPU work (off-screen
// rendering, effect shaders). opengl32 on Windows only exports OpenGL 1.1, so
// newer entry points are looked up at runtime through GLFW.

#include <GLFW/glfw3.h>

#include <cstddef>

namespace bemotion::gl {

#ifdef _WIN32
#define BEMOTION_GL_CALL __stdcall
#else
#define BEMOTION_GL_CALL
#endif

// Enums not present in the OpenGL 1.1 header.
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kFramebufferBinding = 0x8CA6;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kStaticDraw = 0x88E4;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kVertexArrayBinding = 0x85B5;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kFuncAdd = 0x8006;

using GLchar = char;
using GLsizeiptr = ptrdiff_t;

struct Api {
    void(BEMOTION_GL_CALL* GenFramebuffers)(GLsizei, GLuint*);
    void(BEMOTION_GL_CALL* DeleteFramebuffers)(GLsizei, const GLuint*);
    void(BEMOTION_GL_CALL* BindFramebuffer)(GLenum, GLuint);
    void(BEMOTION_GL_CALL* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum(BEMOTION_GL_CALL* CheckFramebufferStatus)(GLenum);
    GLuint(BEMOTION_GL_CALL* CreateShader)(GLenum);
    void(BEMOTION_GL_CALL* ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void(BEMOTION_GL_CALL* CompileShader)(GLuint);
    void(BEMOTION_GL_CALL* GetShaderiv)(GLuint, GLenum, GLint*);
    void(BEMOTION_GL_CALL* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void(BEMOTION_GL_CALL* DeleteShader)(GLuint);
    GLuint(BEMOTION_GL_CALL* CreateProgram)();
    void(BEMOTION_GL_CALL* AttachShader)(GLuint, GLuint);
    void(BEMOTION_GL_CALL* LinkProgram)(GLuint);
    void(BEMOTION_GL_CALL* GetProgramiv)(GLuint, GLenum, GLint*);
    void(BEMOTION_GL_CALL* UseProgram)(GLuint);
    GLint(BEMOTION_GL_CALL* GetUniformLocation)(GLuint, const GLchar*);
    void(BEMOTION_GL_CALL* Uniform1i)(GLint, GLint);
    void(BEMOTION_GL_CALL* Uniform1f)(GLint, GLfloat);
    void(BEMOTION_GL_CALL* Uniform2f)(GLint, GLfloat, GLfloat);
    void(BEMOTION_GL_CALL* Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void(BEMOTION_GL_CALL* GenVertexArrays)(GLsizei, GLuint*);
    void(BEMOTION_GL_CALL* BindVertexArray)(GLuint);
    void(BEMOTION_GL_CALL* GenBuffers)(GLsizei, GLuint*);
    void(BEMOTION_GL_CALL* BindBuffer)(GLenum, GLuint);
    void(BEMOTION_GL_CALL* BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void(BEMOTION_GL_CALL* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void(BEMOTION_GL_CALL* EnableVertexAttribArray)(GLuint);
    void(BEMOTION_GL_CALL* ActiveTexture)(GLenum);
    void(BEMOTION_GL_CALL* BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void(BEMOTION_GL_CALL* BlendEquation)(GLenum);
};

// Loads the entry points once; false if the driver lacks any of them.
bool load();
const Api& api();

} // namespace bemotion::gl
