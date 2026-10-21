#pragma once
// OpenGL ES 3.0 for the renderer, the same source on both platforms:
// - Android: <GLES3/gl3.h> prototypes, linked against libGLESv3.
// - Windows (the development host): the same declarations as function pointers with the same
//   names, loaded from a GL 4.3 core context (ARB_ES3_compatibility runs `#version 300 es`).
// T3_GL_FUNCTIONS lists every entry point the renderer uses; add one there before calling it.

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl3.h>
#endif

#define T3_GL_FUNCTIONS(X)                                                                             \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                        \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                          \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                                              \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                              \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                    \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                                  \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                                            \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                                            \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                                      \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                                                \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                                                                \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                                        \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                                            \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                    \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)                                        \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                                \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                                    \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                              \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                                        \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                      \
    X(PFNGLCLEARPROC, glClear)                                                                        \
    X(PFNGLCLEARCOLORPROC, glClearColor)                                                              \
    X(PFNGLCLEARDEPTHFPROC, glClearDepthf)                                                            \
    X(PFNGLCOLORMASKPROC, glColorMask)                                                                \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                        \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                        \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                          \
    X(PFNGLCULLFACEPROC, glCullFace)                                                                  \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                        \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                              \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                        \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                                            \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                          \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                                      \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                              \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                                                \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                                                \
    X(PFNGLDISABLEPROC, glDisable)                                                                    \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                                  \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                                              \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                                                          \
    X(PFNGLENABLEPROC, glEnable)                                                                      \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                    \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                    \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                          \
    X(PFNGLFRONTFACEPROC, glFrontFace)                                                                \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                              \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                    \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                                  \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                                            \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                    \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                                      \
    X(PFNGLGETERRORPROC, glGetError)                                                                  \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                                            \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                                \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                          \
    X(PFNGLGETPROGRAMBINARYPROC, glGetProgramBinary)                                                  \
    X(PFNGLPROGRAMBINARYPROC, glProgramBinary)                                                        \
    X(PFNGLPROGRAMPARAMETERIPROC, glProgramParameteri)                                                \
    X(PFNGLVALIDATEPROGRAMPROC, glValidateProgram)                                                    \
    X(PFNGLGENQUERIESPROC, glGenQueries)                                                              \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries)                                                        \
    X(PFNGLBEGINQUERYPROC, glBeginQuery)                                                              \
    X(PFNGLENDQUERYPROC, glEndQuery)                                                                  \
    X(PFNGLGETQUERYOBJECTUIVPROC, glGetQueryObjectuiv)                                                \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                  \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                            \
    X(PFNGLGETSTRINGPROC, glGetString)                                                                \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)                                          \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                              \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                            \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                                            \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                                              \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                            \
    X(PFNGLSCISSORPROC, glScissor)                                                                    \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                          \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                                              \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                                        \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                                                        \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                                                \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                                \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                                                \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                                                \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                                \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                                              \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)                                            \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                                  \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                              \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer)                                          \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                            \
    X(PFNGLVIEWPORTPROC, glViewport)                                                                  \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample)                      \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor)                                            \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f)                                                      \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)                                            \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced)                                        \
    X(PFNGLUNIFORM1FVPROC, glUniform1fv)                                                              \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv)                                                              \
    X(PFNGLUNIFORM2FVPROC, glUniform2fv)                                                              \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                                              \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                                                  \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)                                                            \
    X(PFNGLREADBUFFERPROC, glReadBuffer)                                                              \
    X(PFNGLPOLYGONOFFSETPROC, glPolygonOffset)                                                        \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                                              \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf)                                                \
    X(PFNGLTEXPARAMETERFPROC, glTexParameterf)

#ifndef __ANDROID__
#define T3_GL_DECLARE(type, name) extern type name;
T3_GL_FUNCTIONS(T3_GL_DECLARE)
#undef T3_GL_DECLARE
#endif

namespace t3::gl {

// Loads the entry points (host) through `get` (wglGetProcAddress, falling back to the opengl32
// exports for GL 1.1); false and the first missing name in `missing` when one is absent. A no-op
// returning true on Android.
using ProcLoader = void* (*)(const char* name);
bool load(ProcLoader get, const char** missing = nullptr);

// The GL error flags raised since the last call, as text ("" when none); logs them with `where`.
const char* check(const char* where);

}  // namespace t3::gl
