#include "gfx/gpu_timer.h"

#include <cstdint>
#include <cstring>

#include "gfx/gl.h"
#include "gfx/texture.h"   // gl_generation

#ifdef __ANDROID__
#include <EGL/egl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace t3::gfx {

namespace {
constexpr unsigned TIME_ELAPSED = 0x88BF;   // GL_TIME_ELAPSED_EXT
constexpr unsigned GPU_DISJOINT = 0x8FBB;   // GL_GPU_DISJOINT_EXT
using QueryObjectUi64 = void (*)(GLuint, GLenum, uint64_t*);
QueryObjectUi64 g_result = nullptr;         // glGetQueryObjectui64vEXT
}  // namespace

bool GpuTimer::available() { return _ready(); }

bool GpuTimer::_ready() {
    if (state_ != 0 && generation_ == gl_generation()) return state_ > 0;
    // A new context: the old query names are gone with it (forgotten, never deleted).
    generation_ = gl_generation();
    open_ = false;
    next_ = 0;
    for (bool& p : pending_) p = false;
    state_ = -1;
#ifdef __ANDROID__
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (ext == nullptr || std::strstr(ext, "GL_EXT_disjoint_timer_query") == nullptr) return false;
    // Not on Mali: a Galaxy A22's G52 (driver r26p0) showed the timed frames black, two of three
    // (the whole frame, then each pass, then the shadow's parts in turn): measured from a screen
    // recording, 396 of 593 frames black with the queries, none between scenes without them.
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer != nullptr && std::strstr(renderer, "Mali") != nullptr) return false;
    if (g_result == nullptr) g_result = reinterpret_cast<QueryObjectUi64>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
    if (g_result == nullptr) return false;
    glGenQueries(RING, ids_);
    GLint disjoint = 0;
    glGetIntegerv(GPU_DISJOINT, &disjoint);   // clears the flag
    state_ = 1;
#elif defined(_WIN32)
    // The development host: desktop GL 3.3's own timer queries (no disjoint flag there).
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version == nullptr || std::strncmp(version, "OpenGL ES", 9) == 0 || version[0] < '3') return false;
    if (g_result == nullptr) g_result = reinterpret_cast<QueryObjectUi64>(wglGetProcAddress("glGetQueryObjectui64v"));
    if (g_result == nullptr) return false;
    glGenQueries(RING, ids_);
    desktop_ = true;
    state_ = 1;
#endif
    return state_ > 0;
}

void GpuTimer::begin() {
    if (!_ready() || open_ || pending_[next_]) return;   // a full ring: this frame is not timed
    glBeginQuery(TIME_ELAPSED, ids_[next_]);
    open_ = true;
}

void GpuTimer::end() {
    if (!open_) return;
    glEndQuery(TIME_ELAPSED);
    open_ = false;
    pending_[next_] = true;
    next_ = (next_ + 1) % RING;
}

void GpuTimer::collect(std::vector<double>& out) {
    if (!_ready()) return;
    GLint disjoint = 0;
    if (!desktop_) glGetIntegerv(GPU_DISJOINT, &disjoint);
    // The oldest first: from the slot after the newest round to it.
    for (int k = 0; k < RING; ++k) {
        int i = (next_ + k) % RING;
        if (!pending_[i]) continue;
        GLuint done = 0;
        glGetQueryObjectuiv(ids_[i], GL_QUERY_RESULT_AVAILABLE, &done);
        if (!done && !disjoint) continue;
        if (done && !disjoint) {
            uint64_t ns = 0;
            g_result(ids_[i], GL_QUERY_RESULT, &ns);
            // A driver's occasional garbage (a phone's log had 92233720368 ms) would swamp the
            // window's mean: a frame's part is never a second long.
            if (ns < 1000000000ULL) out.push_back(static_cast<double>(ns) / 1e6);
        }
        // A disjoint period: the results in flight are not to be trusted (a pending one is
        // dropped once it is done, so its name can be used again).
        if (done) pending_[i] = false;
    }
}

}  // namespace t3::gfx
