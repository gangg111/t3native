#include "gfx/gl.h"

#include <cstdio>
#include <string>

#include "platform/log.h"

#ifndef __ANDROID__
#define T3_GL_DEFINE(type, name) type name = nullptr;
T3_GL_FUNCTIONS(T3_GL_DEFINE)
#undef T3_GL_DEFINE
#endif

namespace t3::gl {

bool load(ProcLoader get, const char** missing) {
#ifdef __ANDROID__
    (void)get;
    (void)missing;
    return true;
#else
#define T3_GL_LOAD(type, name)                                    \
    name = reinterpret_cast<type>(get(#name));                    \
    if (!name) {                                                  \
        if (missing) *missing = #name;                            \
        return false;                                             \
    }
    T3_GL_FUNCTIONS(T3_GL_LOAD)
#undef T3_GL_LOAD
    return true;
#endif
}

const char* check(const char* where) {
    static std::string last;
    last.clear();
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%04X ", e);
        last += b;
    }
    if (!last.empty()) Log::error(std::string("GL error ") + last + "at " + where);
    return last.c_str();
}

}  // namespace t3::gl
