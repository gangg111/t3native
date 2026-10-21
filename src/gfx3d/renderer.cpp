#include "gfx3d/renderer.h"
#include "gfx/texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include "gfx/gl.h"
#include "gfx3d/shader_program.h"
#include "platform/log.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
#endif

namespace t3::gfx3d {

namespace {

enum Attr { A_VERTEX = 0, A_NORMAL, A_COLOR, A_UV, A_UV2, A_CUSTOM0, A_INST0 = 9, A_INST_COLOR = 12, A_INST_CUSTOM = 13 };
constexpr int INSTANCE_FLOATS = 20;   // three rows, colour, custom data
// Godot's glow levels 1 to 4 (its default weights: 0, 0.8, 0.4, 0.1; levels 5 to 7 are 0).
constexpr int GLOW_LEVELS = 4;
#define GLOW_LEVELS_TEXT "4"
constexpr float GLOW_WEIGHTS[GLOW_LEVELS] = {0.0f, 0.8f, 0.4f, 0.1f};
// Godot's DirectionalLight3D splits (PSSM 4: split 1, 2, 3 at 0.1, 0.2, 0.5 of the distance).
constexpr float SHADOW_SPLITS[SHADOW_CASCADES] = {0.1f, 0.2f, 0.5f, 1.0f};

// A column-major 4x4 matrix for the uniforms.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static Mat4 from(const Transform3D& t) {
        Mat4 r;
        t.to_gl(r.m);
        return r;
    }
    static Mat4 from(const Projection& p) {
        Mat4 r;
        std::memcpy(r.m, p.gl(), sizeof r.m);
        return r;
    }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
};

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
Vector3 linear_rgb(const gfx::Color& c) { return {srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b)}; }

GLenum gl_primitive(Primitive p) {
    switch (p) {
        case Primitive::POINTS: return GL_POINTS;
        case Primitive::LINES: return GL_LINES;
        case Primitive::LINE_STRIP: return GL_LINE_STRIP;
        case Primitive::TRIANGLES: return GL_TRIANGLES;
        case Primitive::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    }
    return GL_TRIANGLES;
}

// The surface's arrays interleaved into one buffer, each present array on its attribute.
void upload(Mesh::Surface& s) {
    const MeshArrays& a = s.arrays;
    size_t n = a.vertex.size();
    struct Part { int attr, comps; };
    std::vector<Part> parts{{A_VERTEX, 3}};
    if (a.normal.size() == n) parts.push_back({A_NORMAL, 3});
    if (a.color.size() == n) parts.push_back({A_COLOR, 4});
    if (a.tex_uv.size() == n) parts.push_back({A_UV, 2});
    if (a.tex_uv2.size() == n) parts.push_back({A_UV2, 2});
    for (int i = 0; i < 4; ++i)
        if (!a.custom[i].empty() && a.custom[i].size() == n * a.custom_components[i]) parts.push_back({A_CUSTOM0 + i, a.custom_components[i]});
    int stride = 0;
    for (const Part& p : parts) stride += p.comps;
    std::vector<float> data(n * stride);
    for (size_t v = 0; v < n; ++v) {
        float* o = &data[v * stride];
        for (const Part& p : parts) {
            switch (p.attr) {
                case A_VERTEX: o[0] = a.vertex[v].x, o[1] = a.vertex[v].y, o[2] = a.vertex[v].z; break;
                case A_NORMAL: o[0] = a.normal[v].x, o[1] = a.normal[v].y, o[2] = a.normal[v].z; break;
                case A_COLOR: o[0] = a.color[v].r, o[1] = a.color[v].g, o[2] = a.color[v].b, o[3] = a.color[v].a; break;
                case A_UV: o[0] = a.tex_uv[v].x, o[1] = a.tex_uv[v].y; break;
                case A_UV2: o[0] = a.tex_uv2[v].x, o[1] = a.tex_uv2[v].y; break;
                default: {
                    int i = p.attr - A_CUSTOM0;
                    for (int k = 0; k < p.comps; ++k) o[k] = a.custom[i][v * p.comps + k];
                }
            }
            o += p.comps;
        }
    }
    auto gpu = std::make_shared<SurfaceGpu>();
    glGenVertexArrays(1, &gpu->vao);
    glBindVertexArray(gpu->vao);
    glGenBuffers(1, &gpu->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gpu->vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);
    int offset = 0;
    for (const Part& p : parts) {
        glEnableVertexAttribArray(p.attr);
        glVertexAttribPointer(p.attr, p.comps, GL_FLOAT, GL_FALSE, stride * 4, reinterpret_cast<void*>(static_cast<intptr_t>(offset * 4)));
        gpu->attributes.push_back({p.attr, p.comps, offset});
        offset += p.comps;
    }
    gpu->stride = stride;
    if (!a.index.empty()) {
        glGenBuffers(1, &gpu->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu->ibo);
        std::vector<unsigned> idx(a.index.begin(), a.index.end());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
        gpu->count = static_cast<int>(idx.size());
    } else {
        gpu->count = static_cast<int>(n);
    }
    glBindVertexArray(0);
    s.gpu = gpu;
}

// What a material draws with: its program and the render state its render modes ask for.
struct Look {
    const Program* color = nullptr;
    const Program* depth = nullptr;
    // Native: a material whose vertex() can be captured (shader_program.h Pass::CAPTURE).
    const Program* capture = nullptr;
    const Program* color_fed = nullptr;
    const Program* depth_fed = nullptr;
    std::map<std::string, ShaderValue> params;
    const std::map<std::string, ShaderValue>* shared_params = nullptr;
    bool transparent = false;
    int priority = 0;   // the material's render_priority
};

}  // namespace

struct Renderer3D::Impl {
    unsigned generation = 0;
    int width = 0, height = 0, samples = 0;
    unsigned msaa_fbo = 0, msaa_color = 0, msaa_depth = 0;
    unsigned resolve_fbo = 0, resolve_tex = 0;
    // Native: GL_EXT_multisampled_render_to_texture (a tiled GPU's own resolve: the scene's samples
    // and depth stay in the tile memory, the texture gets the resolved colour, no blit). msaa_fbo
    // then renders straight into resolve_tex and there is no msaa_color nor resolve_fbo.
    bool in_tile_resolve = false;
    unsigned shadow_fbo = 0, shadow_tex = 0;
    int shadow_size = 0;
    unsigned final_program = 0, final_vao = 0;
    int final_source = -1, final_glow = -1, final_glow_weight = -1, final_glow_on = -1;
    unsigned glow_first = 0, glow_down = 0, glow_blur = 0;
    unsigned glow_tex[GLOW_LEVELS] = {}, glow_fbo[GLOW_LEVELS] = {}, glow_tmp[GLOW_LEVELS] = {}, glow_tmp_fbo[GLOW_LEVELS] = {};
    int glow_w[GLOW_LEVELS] = {}, glow_h[GLOW_LEVELS] = {};
    gfx::TextureRef white;
    std::map<int, unsigned> samplers;
    float anisotropy = 0;
    bool float_buffer = false;
    // Native: the captured vertices of each instance's surface (render's "captured vertices").
    struct Fed {
        unsigned vao = 0, vbo = 0, generation = 0;
        int count = 0;
        const SurfaceGpu* source = nullptr;   // the upload its VAO reads the other attributes from
        uint64_t frame = 0;                   // last drawn
    };
    std::map<std::pair<const void*, const void*>, Fed> fed;
    uint64_t frame = 0;

    void drop(Fed& f) {
        if (f.generation == gl_generation()) {
            if (f.vao) glDeleteVertexArrays(1, &f.vao);
            if (f.vbo) glDeleteBuffers(1, &f.vbo);
        }
        f = Fed{};
    }

    ~Impl() { release(); }

    // Deletes the GL objects when they belong to the current context, else only forgets them.
    void release() {
        if (generation == 0 || generation != gl_generation()) {
            forget();
            return;
        }
        if (msaa_fbo) glDeleteFramebuffers(1, &msaa_fbo);
        if (msaa_color) glDeleteRenderbuffers(1, &msaa_color);
        if (msaa_depth) glDeleteRenderbuffers(1, &msaa_depth);
        if (resolve_fbo) glDeleteFramebuffers(1, &resolve_fbo);
        if (resolve_tex) glDeleteTextures(1, &resolve_tex);
        if (shadow_fbo) glDeleteFramebuffers(1, &shadow_fbo);
        if (shadow_tex) glDeleteTextures(1, &shadow_tex);
        if (final_program) glDeleteProgram(final_program);
        for (unsigned p : {glow_first, glow_down, glow_blur})
            if (p) glDeleteProgram(p);
        release_glow();
        if (final_vao) glDeleteVertexArrays(1, &final_vao);
        for (auto& [k, s] : samplers) glDeleteSamplers(1, &s);
        for (auto& [k, f] : fed) drop(f);
        forget();
    }

    void forget() {
        msaa_fbo = msaa_color = msaa_depth = resolve_fbo = resolve_tex = shadow_fbo = shadow_tex = 0;
        final_program = final_vao = 0;
        glow_first = glow_down = glow_blur = 0;
        for (int k = 0; k < GLOW_LEVELS; ++k) glow_tex[k] = glow_fbo[k] = glow_tmp[k] = glow_tmp_fbo[k] = 0, glow_w[k] = glow_h[k] = 0;
        samplers.clear();
        fed.clear();
        width = height = shadow_size = 0;
        white.reset();
    }

    void ensure_context() {
        if (generation == gl_generation()) return;
        release();
        generation = gl_generation();
        anisotropy = 0;
        // The limits that matter for the shaders (diagnostics for drivers that drop draws silently).
        GLint vu = 0, vt = 0, ft = 0, ct = 0;
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vu);
        glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vt);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &ft);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &ct);
        Log::info("gfx3d limits: vertex uniform vectors " + std::to_string(vu) + ", vertex/fragment/combined texture units " +
                  std::to_string(vt) + "/" + std::to_string(ft) + "/" + std::to_string(ct));
        // GL_EXT_texture_filter_anisotropic: Godot's anisotropic filter for the floor.
        int n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (int i = 0; i < n; ++i) {
            const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            if (e && (std::strcmp(e, "GL_EXT_texture_filter_anisotropic") == 0 || std::strcmp(e, "GL_ARB_texture_filter_anisotropic") == 0))
                anisotropy = 4.0f;
        }
        gfx::Image im;
        im.width = im.height = 1;
        im.rgba = {255, 255, 255, 255};
        white = std::make_shared<gfx::Texture>(im);
        make_final_pass();
    }

    // A full-screen pass's program (one triangle, `uv` 0 ... 1).
    static unsigned screen_program(const char* fs) {
        const char* vs = "#version 300 es\nout vec2 uv;\nvoid main() {\n"
                         "\tvec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                         "\tuv = p;\n\tgl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}\n";
        auto stage = [](GLenum type, const char* src) {
            unsigned s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            int ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048];
                glGetShaderInfoLog(s, sizeof log, nullptr, log);
                Log::error(std::string("gfx3d: a screen pass does not compile:\n") + log);
            }
            return s;
        };
        unsigned v = stage(GL_VERTEX_SHADER, vs), f = stage(GL_FRAGMENT_SHADER, fs);
        unsigned p = glCreateProgram();
        glAttachShader(p, v);
        glAttachShader(p, f);
        gfx::gl_programs() += 1;
        glLinkProgram(p);
        glDeleteShader(v);
        glDeleteShader(f);
        return p;
    }

    void make_final_pass() {
        // Godot's linear tone mapper (tonemap.glsl): the colour clamped to 1 and encoded to sRGB;
        // then the glow (its levels weighted, times the intensity) tone mapped and encoded to sRGB
        // too, and blended over it by SCREEN (Godot 4's default mode, applied in sRGB).
        final_program = screen_program(
            "#version 300 es\nprecision highp float;\nin vec2 uv;\nuniform sampler2D source;\n"
            "uniform sampler2D glow[" GLOW_LEVELS_TEXT "];\nuniform float glow_weight[" GLOW_LEVELS_TEXT "];\nuniform bool glow_on;\n"
            "layout(location = 0) out vec4 colour;\n"
            "vec3 to_srgb(vec3 c) {\n\tc = clamp(c, 0.0, 1.0);\n"
            "\treturn mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));\n}\n"
            "void main() {\n\tvec3 c = to_srgb(texture(source, uv).rgb);\n"
            // GLSL ES indexes sampler arrays with constants only: the levels written out.
            "\tif (glow_on) {\n\t\tvec3 g = texture(glow[0], uv).rgb * glow_weight[0] + texture(glow[1], uv).rgb * glow_weight[1]"
            " + texture(glow[2], uv).rgb * glow_weight[2] + texture(glow[3], uv).rgb * glow_weight[3];\n"
            "\t\tg = to_srgb(g);\n\t\tc = max(c + g - c * g, vec3(0.0));\n\t}\n"
            "\tcolour = vec4(c, 1.0);\n}\n");
        final_source = glGetUniformLocation(final_program, "source");
        final_glow = glGetUniformLocation(final_program, "glow");
        final_glow_weight = glGetUniformLocation(final_program, "glow_weight");
        final_glow_on = glGetUniformLocation(final_program, "glow_on");
        // The glow's first level: Godot's gaussian_glow first pass, the source halved (four taps)
        // and kept where it is brighter than the threshold (smoothstep over hdr_scale), capped.
        glow_first = screen_program(
            "#version 300 es\nprecision highp float;\nin vec2 uv;\nuniform sampler2D source;\nuniform vec2 texel;\n"
            "uniform vec3 hdr;\nlayout(location = 0) out vec4 colour;\nvoid main() {\n"
            "\tvec3 c = 0.25 * (texture(source, uv + texel * vec2(-0.5, -0.5)).rgb + texture(source, uv + texel * vec2(0.5, -0.5)).rgb"
            " + texture(source, uv + texel * vec2(-0.5, 0.5)).rgb + texture(source, uv + texel * vec2(0.5, 0.5)).rgb);\n"
            "\tfloat l = max(c.r, max(c.g, c.b));\n"
            "\tcolour = vec4(min(c * smoothstep(hdr.x, hdr.x + hdr.y, l), vec3(hdr.z)), 1.0);\n}\n");
        // The next level: the previous one halved.
        glow_down = screen_program(
            "#version 300 es\nprecision highp float;\nin vec2 uv;\nuniform sampler2D source;\nuniform vec2 texel;\n"
            "layout(location = 0) out vec4 colour;\nvoid main() {\n"
            "\tcolour = 0.25 * (texture(source, uv + texel * vec2(-0.5, -0.5)) + texture(source, uv + texel * vec2(0.5, -0.5))"
            " + texture(source, uv + texel * vec2(-0.5, 0.5)) + texture(source, uv + texel * vec2(0.5, 0.5)));\n}\n");
        // A separable gaussian blur, nine taps along `step`.
        glow_blur = screen_program(
            "#version 300 es\nprecision highp float;\nin vec2 uv;\nuniform sampler2D source;\nuniform vec2 step;\n"
            "layout(location = 0) out vec4 colour;\nconst float w[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, "
            "0.0540540541, 0.0162162162);\nvoid main() {\n\tvec4 c = texture(source, uv) * w[0];\n"
            "\tfor (int i = 1; i < 5; ++i) c += (texture(source, uv + step * float(i)) + texture(source, uv - step * float(i))) * w[i];\n"
            "\tcolour = c;\n}\n");
        glGenVertexArrays(1, &final_vao);
    }

    // Godot's glow levels 1 ... GLOW_LEVELS (level k at 1 / 2^k of the view), each with a blurred
    // copy: made for w x h when the buffer is a float one (the glow needs values above 1).
    void ensure_glow(int w, int h) {
        if (glow_tex[0] && glow_w[0] == std::max(1, w >> 1) && glow_h[0] == std::max(1, h >> 1)) return;
        release_glow();
        for (int k = 0; k < GLOW_LEVELS; ++k) {
            glow_w[k] = std::max(1, w >> (k + 1));
            glow_h[k] = std::max(1, h >> (k + 1));
            for (int pass = 0; pass < 2; ++pass) {
                unsigned& tex = pass ? glow_tmp[k] : glow_tex[k];
                unsigned& fbo = pass ? glow_tmp_fbo[k] : glow_fbo[k];
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, glow_w[k], glow_h[k], 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glGenFramebuffers(1, &fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            }
        }
    }

    void release_glow() {
        for (int k = 0; k < GLOW_LEVELS; ++k) {
            if (generation == gl_generation()) {
                if (glow_tex[k]) glDeleteTextures(1, &glow_tex[k]), glDeleteFramebuffers(1, &glow_fbo[k]);
                if (glow_tmp[k]) glDeleteTextures(1, &glow_tmp[k]), glDeleteFramebuffers(1, &glow_tmp_fbo[k]);
            }
            glow_tex[k] = glow_fbo[k] = glow_tmp[k] = glow_tmp_fbo[k] = 0;
            glow_w[k] = glow_h[k] = 0;
        }
    }

    void draw_screen(unsigned fbo, int w, int h, unsigned source_tex) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        // Every pixel is drawn again: a tiled GPU need not read the old ones in first.
        const GLenum colour = GL_COLOR_ATTACHMENT0;
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &colour);
        glViewport(0, 0, w, h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source_tex);
        glBindSampler(0, 0);
        glBindVertexArray(final_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // The glow's levels from the resolved view (Environment glow_hdr_threshold, glow_hdr_scale,
    // glow_hdr_luminance_cap: Godot's defaults 1, 2 and 12).
    void make_glow(int w, int h) {
        ensure_glow(w, h);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glUseProgram(glow_first);
        glUniform1i(glGetUniformLocation(glow_first, "source"), 0);
        glUniform2f(glGetUniformLocation(glow_first, "texel"), 1.0f / w, 1.0f / h);
        glUniform3f(glGetUniformLocation(glow_first, "hdr"), 1.0f, 2.0f, 12.0f);
        draw_screen(glow_fbo[0], glow_w[0], glow_h[0], resolve_tex);
        for (int k = 0; k < GLOW_LEVELS; ++k) {
            if (k > 0) {
                glUseProgram(glow_down);
                glUniform1i(glGetUniformLocation(glow_down, "source"), 0);
                glUniform2f(glGetUniformLocation(glow_down, "texel"), 1.0f / glow_w[k - 1], 1.0f / glow_h[k - 1]);
                draw_screen(glow_fbo[k], glow_w[k], glow_h[k], glow_tex[k - 1]);
            }
            glUseProgram(glow_blur);
            glUniform1i(glGetUniformLocation(glow_blur, "source"), 0);
            int step = glGetUniformLocation(glow_blur, "step");
            glUniform2f(step, 1.0f / glow_w[k], 0.0f);
            draw_screen(glow_tmp_fbo[k], glow_w[k], glow_h[k], glow_tex[k]);
            glUniform2f(step, 0.0f, 1.0f / glow_h[k]);
            draw_screen(glow_fbo[k], glow_w[k], glow_h[k], glow_tmp[k]);
        }
    }

    // The multisampled linear buffer and its resolve target, for w x h: RGBA16F when the driver can
    // render and multisample it, else RGB10_A2 (core GLES 3.0), else RGBA8.
    void ensure_buffers(int w, int h, int msaa) {
        if (w == width && h == height && msaa_fbo) return;
        if (msaa_fbo) glDeleteFramebuffers(1, &msaa_fbo), glDeleteRenderbuffers(1, &msaa_color), glDeleteRenderbuffers(1, &msaa_depth);
        if (resolve_fbo) glDeleteFramebuffers(1, &resolve_fbo), glDeleteTextures(1, &resolve_tex);
        width = w;
        height = h;
        in_tile_resolve = false;
        int max_samples = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        for (GLenum format : {GLenum(GL_RGBA16F), GLenum(GL_RGB10_A2), GLenum(GL_RGBA8)}) {
            samples = std::max(1, std::min(msaa, max_samples));
            if (tile_buffers(format, w, h)) return;
            glGenFramebuffers(1, &msaa_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
            glGenRenderbuffers(1, &msaa_color);
            glBindRenderbuffer(GL_RENDERBUFFER, msaa_color);
            glGetError();
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color);
            glGenRenderbuffers(1, &msaa_depth);
            glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth);
            bool ok = glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (ok) {
                glGenTextures(1, &resolve_tex);
                glBindTexture(GL_TEXTURE_2D, resolve_tex);
                GLenum type = format == GL_RGBA16F ? GL_HALF_FLOAT : (format == GL_RGB10_A2 ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE);
                glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), w, h, 0, format == GL_RGB10_A2 ? GL_RGBA : GL_RGBA, type, nullptr);
                // Linear: the scaled view is stretched to the target from it (scaling_3d_scale).
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glGenFramebuffers(1, &resolve_fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0);
                ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                if (ok) {
                    float_buffer = format == GL_RGBA16F;
                    Log::info(std::string("gfx3d: ") + (format == GL_RGBA16F ? "RGBA16F" : format == GL_RGB10_A2 ? "RGB10_A2" : "RGBA8") +
                              " x" + std::to_string(samples) + ", " + std::to_string(w) + "x" + std::to_string(h));
                    return;
                }
                glDeleteFramebuffers(1, &resolve_fbo), glDeleteTextures(1, &resolve_tex);
                resolve_fbo = resolve_tex = 0;
            }
            glDeleteFramebuffers(1, &msaa_fbo), glDeleteRenderbuffers(1, &msaa_color), glDeleteRenderbuffers(1, &msaa_depth);
            msaa_fbo = msaa_color = msaa_depth = 0;
        }
        Log::error("gfx3d: no colour buffer format renders");
    }

    // The scene's buffer resolved in the tile memory (GL_EXT_multisampled_render_to_texture, Android):
    // false (nothing made) where the driver has no such extension or does not take the format.
    bool tile_buffers(GLenum format, int w, int h) {
#ifdef __ANDROID__
        using TextureMs = void (*)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
        using StorageMs = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
        static TextureMs texture_ms = nullptr;
        static StorageMs storage_ms = nullptr;
        static int state = 0;   // 0 not looked at, 1 there, -1 not
        if (state == 0) {
            const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            state = -1;
            if (ext != nullptr && std::strstr(ext, "GL_EXT_multisampled_render_to_texture") != nullptr) {
                texture_ms = reinterpret_cast<TextureMs>(eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
                storage_ms = reinterpret_cast<StorageMs>(eglGetProcAddress("glRenderbufferStorageMultisampleEXT"));
                if (texture_ms && storage_ms) state = 1;
            }
        }
        if (state < 0) return false;
        int tile_samples = 1;
        glGetIntegerv(0x8D57 /* GL_MAX_SAMPLES_EXT */, &tile_samples);
        samples = std::max(1, std::min(samples, tile_samples));
        glGetError();
        glGenTextures(1, &resolve_tex);
        glBindTexture(GL_TEXTURE_2D, resolve_tex);
        GLenum type = format == GL_RGBA16F ? GL_HALF_FLOAT : (format == GL_RGB10_A2 ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), w, h, 0, GL_RGBA, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &msaa_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
        texture_ms(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0, samples);
        glGenRenderbuffers(1, &msaa_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth);
        storage_ms(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth);
        if (glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            in_tile_resolve = true;
            float_buffer = format == GL_RGBA16F;
            Log::info(std::string("gfx3d: ") + (format == GL_RGBA16F ? "RGBA16F" : format == GL_RGB10_A2 ? "RGB10_A2" : "RGBA8") +
                      " x" + std::to_string(samples) + " resolved in the tiles, " + std::to_string(w) + "x" + std::to_string(h));
            return true;
        }
        glDeleteFramebuffers(1, &msaa_fbo), glDeleteRenderbuffers(1, &msaa_depth), glDeleteTextures(1, &resolve_tex);
        msaa_fbo = msaa_depth = resolve_tex = 0;
#else
        (void)format, (void)w, (void)h;
#endif
        return false;
    }

    void ensure_shadow(int size) {
        if (shadow_fbo && shadow_size == size) return;
        if (shadow_fbo) glDeleteFramebuffers(1, &shadow_fbo), glDeleteTextures(1, &shadow_tex);
        shadow_size = size;
        glGenTextures(1, &shadow_tex);
        glBindTexture(GL_TEXTURE_2D, shadow_tex);
        // 16 bits, as the remake's RenderQuality asks Godot (directional_shadow_atlas_set_size(size,
        // true)): half the memory a frame's shadow writes (a phone spent most of its GPU time there).
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers(1, &shadow_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_tex, 0);
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
    }

    unsigned sampler(ShaderUniform::Filter filter, int repeat) {
        int key = static_cast<int>(filter) * 4 + (repeat + 1);
        auto it = samplers.find(key);
        if (it != samplers.end()) return it->second;
        unsigned s = 0;
        glGenSamplers(1, &s);
        GLenum min = GL_LINEAR_MIPMAP_LINEAR, mag = GL_LINEAR;
        switch (filter) {
            case ShaderUniform::Filter::NEAREST: min = mag = GL_NEAREST; break;
            case ShaderUniform::Filter::LINEAR: min = mag = GL_LINEAR; break;
            case ShaderUniform::Filter::NEAREST_MIPMAP: min = GL_NEAREST_MIPMAP_LINEAR, mag = GL_NEAREST; break;
            default: break;
        }
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(min));
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(mag));
        // Spatial shaders repeat by default (Godot's texture_repeat).
        GLint wrap = repeat == 0 ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap);
        if (filter == ShaderUniform::Filter::LINEAR_MIPMAP_ANISOTROPIC && anisotropy > 0)
            glSamplerParameterf(s, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, anisotropy);
        samplers[key] = s;
        return s;
    }
};

Renderer3D::Renderer3D(RenderSettings settings) : impl_(std::make_unique<Impl>()), settings_(settings) {}
Renderer3D::~Renderer3D() = default;

void Renderer3D::graphics_lost() {
    ++gl_generation();
    forget_programs();
}

namespace {

struct DirLight {
    const DirectionalLight3D* node;
    Vector3 to_light_world;   // unit, towards the light
    Vector3 colour;           // linear x energy
};
struct OmniLight {
    const OmniLight3D* node;
    Vector3 world;
    Vector3 colour;
};
struct Item {
    Mesh::Surface* surface;
    const GeometryInstance3D* instance;
    MultiMesh* multimesh;
    Transform3D model;
    Look look;
    float depth;   // view-space distance of the bounds' centre (transparent sorting)
    unsigned fed_vao = 0;   // native: this frame's captured vertices (0: vertex() runs in each draw)
};

bool modes_transparent(const GdShader& s) {
    return (s.writes_alpha && !s.alpha_scissor) || s.mode("blend_add") || s.mode("blend_sub") || s.mode("blend_mul") ||
           s.mode("blend_premul_alpha");
}

Look look_of(const Material* m) {
    Look look;
    if (const auto* sm = dynamic_cast<const ShaderMaterial*>(m)) {
        if (!sm->shader) return look;
        look.color = program(sm->shader->path, sm->shader->defines, Pass::COLOR);
        look.depth = program(sm->shader->path, sm->shader->defines, Pass::DEPTH);
        if (look.color && capturable(look.color->shader)) {
            look.capture = program(sm->shader->path, sm->shader->defines, Pass::CAPTURE);
            look.color_fed = program(sm->shader->path, sm->shader->defines, Pass::COLOR_FED);
            look.depth_fed = program(sm->shader->path, sm->shader->defines, Pass::DEPTH_FED);
        }
        look.shared_params = &sm->parameters;
    } else {
        static const StandardMaterial3D fallback;
        const auto* st = dynamic_cast<const StandardMaterial3D*>(m);
        if (!st) st = &fallback;
        std::string code = st->shader_code();
        look.color = program_from_code(code, Pass::COLOR);
        look.depth = program_from_code(code, Pass::DEPTH);
        look.params["albedo"] = st->albedo_color;
        look.params["texture_albedo"] = st->albedo_texture;
        look.params["has_texture"] = static_cast<bool>(st->albedo_texture);
        look.params["uv1_scale"] = st->uv1_scale;
        look.params["uv1_offset"] = st->uv1_offset;
        look.params["alpha_scissor_threshold"] = st->alpha_scissor_threshold;
        look.params["roughness"] = st->roughness;
        look.params["metallic"] = st->metallic;
        look.params["specular"] = st->metallic_specular;
    }
    if (look.color) look.transparent = modes_transparent(look.color->shader) || look.color->shader.mode("depth_test_disabled");
    look.priority = m ? m->render_priority : 0;
    return look;
}

const ShaderValue* param(const Look& look, const std::string& name) {
    if (look.shared_params) {
        auto it = look.shared_params->find(name);
        if (it != look.shared_params->end()) return &it->second;
    }
    auto it = look.params.find(name);
    return it != look.params.end() ? &it->second : nullptr;
}

std::vector<float> uniform_values(const Program::User& u, const ShaderValue* v);

// A user uniform: the material's value (a source_color colour decoded to linear, as Godot does), or
// the shader's default.
void set_uniform(const Program::User& u, const ShaderValue* v) {
    const ShaderUniform& s = *u.uniform;
    int loc = u.location;
    if (loc < 0 || s.type == "sampler2D") return;
    std::vector<float> f = uniform_values(u, v);
    if (f.empty()) return;
    const std::string& t = s.type;
    int count = std::max(s.array_size, 1);
    if (t == "float") glUniform1fv(loc, std::min<int>(count, static_cast<int>(f.size())), f.data());
    else if (t == "int" || t == "bool" || t == "uint") {
        std::vector<int> iv(f.begin(), f.end());
        glUniform1iv(loc, std::min<int>(count, static_cast<int>(iv.size())), iv.data());
    } else if (t == "vec2") glUniform2fv(loc, std::min<int>(count, static_cast<int>(f.size() / 2)), f.data());
    else if (t == "vec3") {
        if (f.size() == 4 && count == 1) f.resize(3);
        glUniform3fv(loc, std::min<int>(count, static_cast<int>(f.size() / 3)), f.data());
    } else if (t == "vec4") {
        if (f.size() == 3) f.push_back(1.0f);
        glUniform4fv(loc, std::min<int>(count, static_cast<int>(f.size() / 4)), f.data());
    } else if (t == "ivec2" || t == "ivec3" || t == "ivec4") {
        std::vector<int> iv(f.begin(), f.end());
        if (t == "ivec2") glUniform1iv(loc, 0, nullptr);
        (void)iv;   // no ivec uniforms in the remake's shaders
    } else if (t == "mat3" && f.size() >= 9) glUniformMatrix3fv(loc, 1, GL_FALSE, f.data());
    else if (t == "mat4" && f.size() >= 16) glUniformMatrix4fv(loc, 1, GL_FALSE, f.data());
}

// An array read from a texture (ShaderUniform::as_texture): its values into the program's one-row
// RGBA32F texture (made for this GL context), bound to the uniform's unit.
void set_array_texture(const Program::User& u, const ShaderValue* v) {
    int n = u.uniform->array_size;
    std::vector<float> f = uniform_values(u, v);
    f.resize(static_cast<size_t>(n) * 4, 0.0f);
    glActiveTexture(GL_TEXTURE0 + u.unit);
    if (u.texture == 0 || u.texture_generation != gl_generation()) {
        glGenTextures(1, &u.texture);
        glBindTexture(GL_TEXTURE_2D, u.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, n, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        u.texture_generation = gl_generation();
    } else {
        glBindTexture(GL_TEXTURE_2D, u.texture);
    }
    glBindSampler(u.unit, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RGBA, GL_FLOAT, f.data());
}

// The floats of a user uniform's value (the material's, else the shader's default).
std::vector<float> uniform_values(const Program::User& u, const ShaderValue* v) {
    const ShaderUniform& s = *u.uniform;
    std::vector<float> f;
    if (v && !std::holds_alternative<std::monostate>(*v)) {
        if (const bool* b = std::get_if<bool>(v)) f = {*b ? 1.0f : 0.0f};
        else if (const int* i = std::get_if<int>(v)) f = {static_cast<float>(*i)};
        else if (const float* x = std::get_if<float>(v)) f = {*x};
        else if (const gfx::Vec2* p = std::get_if<gfx::Vec2>(v)) f = {p->x, p->y};
        else if (const Vector3* p3 = std::get_if<Vector3>(v)) f = {p3->x, p3->y, p3->z};
        else if (const Vector4* p4 = std::get_if<Vector4>(v)) f = {p4->x, p4->y, p4->z, p4->w};
        else if (const gfx::Color* c = std::get_if<gfx::Color>(v)) {
            if (s.source_color) {
                Vector3 l = linear_rgb(*c);
                f = {l.x, l.y, l.z, c->a};
            } else {
                f = {c->r, c->g, c->b, c->a};
            }
        } else if (const Basis* b = std::get_if<Basis>(v)) {
            for (int col = 0; col < 3; ++col)
                for (int row = 0; row < 3; ++row) f.push_back(b->rows[row][col]);
        } else if (const auto* arr = std::get_if<std::vector<Vector4>>(v)) {
            for (const Vector4& q : *arr) f.insert(f.end(), {q.x, q.y, q.z, q.w});
        } else if (const auto* fl = std::get_if<std::vector<float>>(v)) {
            f = *fl;
        }
    }
    if (f.empty()) f = u.fallback;
    return f;
}

}  // namespace

void Renderer3D::prepare(Node3D& root, const Camera3D& camera) {
    impl_->ensure_context();
    // The material's textures as bind_user binds them (a late upload, the mipmaps it samples).
    auto textures = [](const Program* p, const Look& look) {
        if (!p) return;
        for (const Program::User& u : p->user) {
            if (u.uniform->as_texture || u.uniform->type != "sampler2D") continue;
            const ShaderValue* val = param(look, u.uniform->name);
            const auto* t = val ? std::get_if<gfx::TextureRef>(val) : nullptr;
            if (!t || !*t) continue;
            ShaderUniform::Filter f = u.uniform->filter;
            if (f == ShaderUniform::Filter::DEFAULT || f == ShaderUniform::Filter::NEAREST_MIPMAP ||
                f == ShaderUniform::Filter::LINEAR_MIPMAP || f == ShaderUniform::Filter::LINEAR_MIPMAP_ANISOTROPIC)
                (*t)->ensure_mipmaps();
            (*t)->id();
        }
    };
    std::function<void(Node3D&)> walk = [&](Node3D& n) {
        if (auto* g3 = dynamic_cast<GeometryInstance3D*>(&n)) {
            Mesh* mesh = nullptr;
            if (auto* mi = dynamic_cast<MeshInstance3D*>(g3)) mesh = mi->mesh.get();
            else if (auto* mmi = dynamic_cast<MultiMeshInstance3D*>(g3)) mesh = mmi->multimesh ? mmi->multimesh->mesh.get() : nullptr;
            if (mesh) {
                for (Mesh::Surface& s : mesh->surfaces) {
                    if (s.arrays.vertex.empty()) continue;
                    Look look = look_of(g3->material_override ? g3->material_override.get() : s.material.get());
                    if (!look.color) continue;
                    textures(look.color, look);
                    if (!s.gpu || s.gpu->generation != gl_generation()) upload(s);
                }
            }
        }
        for (const gd::Ref<Node3D>& c : n.get_children()) walk(*c);
    };
    walk(root);
    const Environment* env = camera.environment.get();
    if (env && env->background_mode == Environment::BG_SKY && env->sky_material) {
        Look sky = look_of(env->sky_material.get());
        textures(sky.color, sky);
    }
}

void Renderer3D::render(Node3D& root, Camera3D& camera, int width, int height, unsigned target_fbo) {
    stats_ = {};
    if (width <= 0 || height <= 0) return;
    Impl& g = *impl_;
    g.ensure_context();
    // The 3D view's own size (scaling_3d_scale); the final pass stretches it to the target.
    float scale = std::clamp(settings_.scale, 0.25f, 1.0f);
    int iw = std::max(1, static_cast<int>(std::lround(width * scale)));
    int ih = std::max(1, static_cast<int>(std::lround(height * scale)));
    g.ensure_buffers(iw, ih, settings_.msaa);
    if (!g.msaa_fbo) return;
    camera.viewport_size = {static_cast<float>(width), static_cast<float>(height)};

    // ---- what the camera sees ----
    Transform3D cam = camera.get_global_transform();
    Transform3D view = cam.affine_inverse();
    Projection proj = camera.get_camera_projection();
    Mat4 view_m = Mat4::from(view), inv_view_m = Mat4::from(cam), proj_m = Mat4::from(proj);

    std::vector<Item> items;
    std::vector<DirLight> dirs;
    std::vector<OmniLight> omnis;
    std::function<void(Node3D&, const Transform3D&)> walk = [&](Node3D& n, const Transform3D& parent) {
        if (!n.visible) return;
        Transform3D t = parent * n.transform;
        if (auto* l = dynamic_cast<DirectionalLight3D*>(&n)) {
            Vector3 c = linear_rgb(l->light_color) * l->light_energy;
            dirs.push_back({l, t.basis.get_column(2).normalized(), c});
        } else if (auto* o = dynamic_cast<OmniLight3D*>(&n)) {
            omnis.push_back({o, t.origin, linear_rgb(o->light_color) * o->light_energy});
        } else if (auto* g3 = dynamic_cast<GeometryInstance3D*>(&n)) {
            if (g3->layers & camera.cull_mask) {
                Mesh* mesh = nullptr;
                MultiMesh* mm = nullptr;
                if (auto* mi = dynamic_cast<MeshInstance3D*>(g3)) mesh = mi->mesh.get();
                else if (auto* mmi = dynamic_cast<MultiMeshInstance3D*>(g3)) {
                    mm = mmi->multimesh.get();
                    if (mm) mesh = mm->mesh.get();
                }
                if (mesh) {
                    for (Mesh::Surface& s : mesh->surfaces) {
                        if (s.arrays.vertex.empty()) continue;
                        const Material* m = g3->material_override ? g3->material_override.get() : s.material.get();
                        Item item{&s, g3, mm, t, look_of(m), 0.0f};
                        if (!item.look.color) {
                            ++stats_.programs_failed;
                            continue;
                        }
                        AABB box = g3->custom_aabb.has_volume() ? g3->custom_aabb : s.aabb;
                        item.depth = -view.xform(t.xform(box.get_center())).z;
                        items.push_back(std::move(item));
                    }
                }
            }
        }
        for (const gd::Ref<Node3D>& c : n.get_children()) walk(*c, t);
    };
    walk(root, Transform3D());
    if (dirs.size() > static_cast<size_t>(MAX_DIRECTIONAL)) dirs.resize(MAX_DIRECTIONAL);

    // ---- the sun's shadow ----
    int sun = -1;
    for (size_t i = 0; i < dirs.size(); ++i)
        if (dirs[i].node->shadow_enabled) {
            sun = static_cast<int>(i);
            break;
        }
    Mat4 shadow_m[SHADOW_CASCADES];
    float shadow_split[SHADOW_CASCADES] = {}, shadow_normal_bias[SHADOW_CASCADES] = {}, shadow_depth_bias[SHADOW_CASCADES] = {};
    float fade = 0;
    const Environment* env = camera.environment.get();
    Vector3 bg = env && env->background_mode == Environment::BG_COLOR ? linear_rgb(env->background_color) : linear_rgb({0.3f, 0.3f, 0.3f});
    Vector3 ambient;
    if (env) {
        if (env->ambient_light_source == Environment::AMBIENT_SOURCE_COLOR) ambient = linear_rgb(env->ambient_light_color) * env->ambient_light_energy;
        else if (env->ambient_light_source == Environment::AMBIENT_SOURCE_BG) ambient = bg * env->ambient_light_energy;
    }

    // The material's uniforms and textures.
    auto bind_user = [&](const Program& p, const Look& look) {
        for (const Program::User& u : p.user) {
            const ShaderValue* val = param(look, u.uniform->name);
            if (u.uniform->as_texture) {
                set_array_texture(u, val);
            } else if (u.uniform->type == "sampler2D") {
                gfx::TextureRef tex;
                if (val) {
                    if (const auto* t = std::get_if<gfx::TextureRef>(val)) tex = *t;
                }
                if (!tex) tex = g.white;
                ShaderUniform::Filter f = u.uniform->filter;
                bool mip = f == ShaderUniform::Filter::DEFAULT || f == ShaderUniform::Filter::NEAREST_MIPMAP ||
                           f == ShaderUniform::Filter::LINEAR_MIPMAP || f == ShaderUniform::Filter::LINEAR_MIPMAP_ANISOTROPIC;
                if (mip) tex->ensure_mipmaps();
                glActiveTexture(GL_TEXTURE0 + u.unit);
                glBindTexture(GL_TEXTURE_2D, tex->id());
                glBindSampler(u.unit, g.sampler(f, u.uniform->repeat));
            } else {
                set_uniform(u, val);
            }
        }
    };

    // One draw: the program's built-ins, the material's uniforms and textures, the render state
    // its modes ask for, the surface (uploaded on first use) and a MultiMesh's instances. `lit`:
    // the colour pass, with the lights, the shadow and the ambient light.
    auto draw = [&](Item& it, const Program& p, const Mat4& v, const Mat4& iv, const Mat4& pr, int vw, int vh, bool lit) {
        const GdShader& s = p.shader;
        glUseProgram(p.id);
        Mat4 model = Mat4::from(it.model);
        if (p.model >= 0) glUniformMatrix4fv(p.model, 1, GL_FALSE, model.m);
        if (p.view >= 0) glUniformMatrix4fv(p.view, 1, GL_FALSE, v.m);
        if (p.inv_view >= 0) glUniformMatrix4fv(p.inv_view, 1, GL_FALSE, iv.m);
        if (p.projection >= 0) glUniformMatrix4fv(p.projection, 1, GL_FALSE, pr.m);
        if (p.viewport_size >= 0) glUniform2f(p.viewport_size, static_cast<float>(vw), static_cast<float>(vh));
        if (p.instanced >= 0) glUniform1i(p.instanced, it.multimesh ? 1 : 0);
        if (p.alpha_scale >= 0) glUniform1f(p.alpha_scale, 1.0f - it.instance->transparency);
        // Render state. Godot's front faces wind clockwise.
        glFrontFace(GL_CW);
        if (s.mode("cull_disabled")) glDisable(GL_CULL_FACE);
        else glEnable(GL_CULL_FACE), glCullFace(s.mode("cull_front") ? GL_FRONT : GL_BACK);
        if (s.mode("depth_test_disabled")) glDisable(GL_DEPTH_TEST);
        else glEnable(GL_DEPTH_TEST), glDepthFunc(GL_LEQUAL);
        if (!lit) {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        } else {
            bool write = s.mode("depth_draw_always") || (!s.mode("depth_draw_never") && !it.look.transparent);
            glDepthMask(write ? GL_TRUE : GL_FALSE);
            if (!it.look.transparent) {
                glDisable(GL_BLEND);
            } else {
                glEnable(GL_BLEND);
                if (s.mode("blend_add")) {
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
                } else if (s.mode("blend_sub")) {
                    glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
                } else if (s.mode("blend_mul")) {
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
                } else if (s.mode("blend_premul_alpha")) {
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                }
            }
            // The lights whose cull mask takes this instance's layers.
            float dd[MAX_DIRECTIONAL * 3], dc[MAX_DIRECTIONAL * 3], ds[MAX_DIRECTIONAL];
            int nd = 0, shadowed = -1;
            Basis vb = view.basis;
            for (size_t i = 0; i < dirs.size(); ++i) {
                if (!(dirs[i].node->light_cull_mask & it.instance->layers)) continue;
                Vector3 d = vb.xform(dirs[i].to_light_world).normalized();
                dd[nd * 3] = d.x, dd[nd * 3 + 1] = d.y, dd[nd * 3 + 2] = d.z;
                dc[nd * 3] = dirs[i].colour.x, dc[nd * 3 + 1] = dirs[i].colour.y, dc[nd * 3 + 2] = dirs[i].colour.z;
                ds[nd] = dirs[i].node->light_specular;
                if (static_cast<int>(i) == sun) shadowed = nd;
                ++nd;
            }
            if (p.dir_count >= 0) glUniform1i(p.dir_count, nd);
            if (nd) {
                if (p.dir_dir >= 0) glUniform3fv(p.dir_dir, nd, dd);
                if (p.dir_color >= 0) glUniform3fv(p.dir_color, nd, dc);
                if (p.dir_specular >= 0) glUniform1fv(p.dir_specular, nd, ds);
            }
            if (p.dir_shadowed >= 0) glUniform1i(p.dir_shadowed, shadowed);
            float op[MAX_OMNI * 3], oc[MAX_OMNI * 3], orng[MAX_OMNI], oat[MAX_OMNI], osp[MAX_OMNI];
            int no = 0;
            for (const OmniLight& o : omnis) {
                if (no == MAX_OMNI) break;
                if (!(o.node->light_cull_mask & it.instance->layers)) continue;
                Vector3 pv = view.xform(o.world);
                op[no * 3] = pv.x, op[no * 3 + 1] = pv.y, op[no * 3 + 2] = pv.z;
                oc[no * 3] = o.colour.x, oc[no * 3 + 1] = o.colour.y, oc[no * 3 + 2] = o.colour.z;
                orng[no] = o.node->omni_range;
                oat[no] = o.node->omni_attenuation;
                osp[no] = o.node->light_specular;
                ++no;
            }
            if (p.omni_count >= 0) glUniform1i(p.omni_count, no);
            if (no) {
                if (p.omni_pos >= 0) glUniform3fv(p.omni_pos, no, op);
                if (p.omni_color >= 0) glUniform3fv(p.omni_color, no, oc);
                if (p.omni_range >= 0) glUniform1fv(p.omni_range, no, orng);
                if (p.omni_atten >= 0) glUniform1fv(p.omni_atten, no, oat);
                if (p.omni_specular >= 0) glUniform1fv(p.omni_specular, no, osp);
            }
            if (p.ambient >= 0) glUniform3f(p.ambient, ambient.x, ambient.y, ambient.z);
            if (sun >= 0) {
                if (p.shadow_matrix >= 0) {
                    float m[16 * SHADOW_CASCADES];
                    for (int k = 0; k < SHADOW_CASCADES; ++k) std::memcpy(m + 16 * k, shadow_m[k].m, sizeof shadow_m[k].m);
                    glUniformMatrix4fv(p.shadow_matrix, SHADOW_CASCADES, GL_FALSE, m);
                }
                float texel = 1.0f / static_cast<float>(g.shadow_size);
                if (p.shadow_texel >= 0) glUniform2f(p.shadow_texel, texel, texel);
                if (p.shadow_split >= 0) glUniform4fv(p.shadow_split, 1, shadow_split);
                if (p.shadow_normal_bias >= 0) glUniform4fv(p.shadow_normal_bias, 1, shadow_normal_bias);
                if (p.shadow_depth_bias >= 0) glUniform4fv(p.shadow_depth_bias, 1, shadow_depth_bias);
                if (p.shadow_fade >= 0) glUniform1f(p.shadow_fade, fade);
            }
            if (p.receive_shadows >= 0) glUniform1i(p.receive_shadows, sun >= 0 && !s.mode("shadows_disabled") ? 1 : 0);
        }
        bind_user(p, it.look);
        // The geometry.
        Mesh::Surface& surface = *it.surface;
        if (!surface.gpu || surface.gpu->generation != gl_generation()) upload(surface);
        glBindVertexArray(it.fed_vao ? it.fed_vao : surface.gpu->vao);
        glVertexAttrib4f(A_NORMAL, 0, 0, 1, 0);
        glVertexAttrib4f(A_COLOR, 1, 1, 1, 1);
        glVertexAttrib4f(A_UV, 0, 0, 0, 0);
        glVertexAttrib4f(A_UV2, 0, 0, 0, 0);
        for (int i = 0; i < 4; ++i) glVertexAttrib4f(A_CUSTOM0 + i, 0, 0, 0, 0);
        GLenum prim = gl_primitive(surface.primitive);
        int instances = 0;
        if (MultiMesh* mm = it.multimesh) {
            instances = mm->visible_instance_count < 0 ? mm->get_instance_count()
                                                       : std::min(mm->visible_instance_count, mm->get_instance_count());
            if (instances <= 0) return;
            if (!mm->gpu_buffer || mm->gpu_generation != gl_generation()) {
                glGenBuffers(1, &mm->gpu_buffer);
                mm->gpu_generation = gl_generation();
                mm->dirty_ = true;
            }
            glBindBuffer(GL_ARRAY_BUFFER, mm->gpu_buffer);
            if (mm->dirty_) {
                std::vector<float> data(static_cast<size_t>(mm->get_instance_count()) * INSTANCE_FLOATS);
                for (int i = 0; i < mm->get_instance_count(); ++i) {
                    float* o = &data[static_cast<size_t>(i) * INSTANCE_FLOATS];
                    const Transform3D& t = mm->transforms_[i];
                    for (int r = 0; r < 3; ++r) {
                        o[r * 4] = t.basis.rows[r].x, o[r * 4 + 1] = t.basis.rows[r].y, o[r * 4 + 2] = t.basis.rows[r].z;
                        o[r * 4 + 3] = t.origin[r];
                    }
                    const gfx::Color& c = mm->use_colors ? mm->colors_[i] : gfx::Color::white();
                    o[12] = c.r, o[13] = c.g, o[14] = c.b, o[15] = c.a;
                    const gfx::Color& d = mm->custom_[i];
                    o[16] = d.r, o[17] = d.g, o[18] = d.b, o[19] = d.a;
                }
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * 4), data.data(), GL_DYNAMIC_DRAW);
                mm->dirty_ = false;
            }
            for (int k = 0; k < 5; ++k) {
                glEnableVertexAttribArray(A_INST0 + k);
                glVertexAttribPointer(A_INST0 + k, 4, GL_FLOAT, GL_FALSE, INSTANCE_FLOATS * 4, reinterpret_cast<void*>(static_cast<intptr_t>(k * 16)));
                glVertexAttribDivisor(A_INST0 + k, 1);
            }
        }
        if (instances > 0) {
            if (surface.gpu->ibo) glDrawElementsInstanced(prim, surface.gpu->count, GL_UNSIGNED_INT, nullptr, instances);
            else glDrawArraysInstanced(prim, 0, surface.gpu->count, instances);
            for (int k = 0; k < 5; ++k) {
                glVertexAttribDivisor(A_INST0 + k, 0);
                glDisableVertexAttribArray(A_INST0 + k);
            }
        } else if (surface.gpu->ibo) {
            glDrawElements(prim, surface.gpu->count, GL_UNSIGNED_INT, nullptr);
        } else {
            glDrawArrays(prim, 0, surface.gpu->count);
        }
    };

    // ---- native: the captured vertices ----
    // A material's vertex() whose results can be kept (Look::capture: a fighter's skinning and
    // curved patches) runs once a frame per instance, into its own buffer (two fighters may share a
    // mesh); the shadow's cascades and the scene then draw those vertices.
    ++g.frame;
    // The capture is the shadow's work moved ahead: it is timed with the shadow pass.
    bool shadow_marked = false;
    for (Item& it : items) {
        if (!it.look.capture || !it.look.color_fed || !it.look.depth_fed || it.multimesh) continue;
        if (!shadow_marked && on_pass) on_pass(PASS_SHADOW);
        shadow_marked = true;
        Mesh::Surface& surface = *it.surface;
        if (!surface.gpu || surface.gpu->generation != gl_generation()) upload(surface);
        int n = static_cast<int>(surface.arrays.vertex.size());
        if (n == 0) continue;
        Impl::Fed& fed = g.fed[{it.instance, &surface}];
        if (fed.generation != gl_generation() || fed.count != n || fed.source != surface.gpu.get()) {
            g.drop(fed);
            fed.generation = gl_generation();
            fed.count = n;
            fed.source = surface.gpu.get();
            glGenBuffers(1, &fed.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, fed.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(n) * 6 * 4, nullptr, GL_STREAM_COPY);
            glGenVertexArrays(1, &fed.vao);
            glBindVertexArray(fed.vao);
            glBindBuffer(GL_ARRAY_BUFFER, surface.gpu->vbo);
            for (const std::array<int, 3>& a : surface.gpu->attributes) {
                glEnableVertexAttribArray(a[0]);
                glVertexAttribPointer(a[0], a[1], GL_FLOAT, GL_FALSE, surface.gpu->stride * 4,
                                      reinterpret_cast<void*>(static_cast<intptr_t>(a[2] * 4)));
            }
            glBindBuffer(GL_ARRAY_BUFFER, fed.vbo);
            glEnableVertexAttribArray(A_FED_VERTEX);
            glVertexAttribPointer(A_FED_VERTEX, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
            glEnableVertexAttribArray(A_FED_NORMAL);
            glVertexAttribPointer(A_FED_NORMAL, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(static_cast<intptr_t>(12)));
            if (surface.gpu->ibo) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surface.gpu->ibo);
            glBindVertexArray(0);
        }
        fed.frame = g.frame;
        const Program& p = *it.look.capture;
        glUseProgram(p.id);
        Mat4 model = Mat4::from(it.model);
        if (p.model >= 0) glUniformMatrix4fv(p.model, 1, GL_FALSE, model.m);
        if (p.view >= 0) glUniformMatrix4fv(p.view, 1, GL_FALSE, view_m.m);
        if (p.inv_view >= 0) glUniformMatrix4fv(p.inv_view, 1, GL_FALSE, inv_view_m.m);
        if (p.projection >= 0) glUniformMatrix4fv(p.projection, 1, GL_FALSE, proj_m.m);
        if (p.viewport_size >= 0) glUniform2f(p.viewport_size, static_cast<float>(iw), static_cast<float>(ih));
        if (p.instanced >= 0) glUniform1i(p.instanced, 0);
        bind_user(p, it.look);
        glBindVertexArray(surface.gpu->vao);
        glVertexAttrib4f(A_NORMAL, 0, 0, 1, 0);
        glVertexAttrib4f(A_COLOR, 1, 1, 1, 1);
        glVertexAttrib4f(A_UV, 0, 0, 0, 0);
        glVertexAttrib4f(A_UV2, 0, 0, 0, 0);
        for (int i = 0; i < 4; ++i) glVertexAttrib4f(A_CUSTOM0 + i, 0, 0, 0, 0);
        glEnable(GL_RASTERIZER_DISCARD);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, fed.vbo);
        glBeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, n);
        glEndTransformFeedback();
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
        glDisable(GL_RASTERIZER_DISCARD);
        it.fed_vao = fed.vao;
    }
    // Buffers of instances no longer drawn go after a few frames.
    for (auto f = g.fed.begin(); f != g.fed.end();) {
        if (g.frame - f->second.frame > 3) {
            g.drop(f->second);
            f = g.fed.erase(f);
        } else {
            ++f;
        }
    }
    glBindVertexArray(0);

    if (sun >= 0) {
        if (on_pass && !shadow_marked) on_pass(PASS_SHADOW);
        const DirectionalLight3D& l = *dirs[sun].node;
        g.ensure_shadow(settings_.shadow_size);
        fade = std::min(l.directional_shadow_max_distance, camera.far);
        float tan_y = 1.0f / proj.m[1][1], tan_x = 1.0f / proj.m[0][0];
        float off_x = proj.m[2][0] / proj.m[0][0], off_y = proj.m[2][1] / proj.m[1][1];
        Vector3 lz = dirs[sun].to_light_world;
        Vector3 up = std::fabs(lz.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
        Vector3 lx = up.cross(lz).normalized();
        Vector3 ly = lz.cross(lx);
        Basis lb(lx, ly, lz);
        int quadrant = g.shadow_size / 2;

        glBindFramebuffer(GL_FRAMEBUFFER, g.shadow_fbo);
        glViewport(0, 0, g.shadow_size, g.shadow_size);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glClearDepthf(1.0f);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        // Native: which cascades each caster's shadow can be read from. A pixel reads the cascade
        // its view depth falls in (t3_shadow), and a caster's shadow lands within its bounds swept
        // away from the light down to RECEIVER_DROP below their lowest point: the cascades whose
        // depth range misses that volume's never read it, so it is not drawn there (a fighter,
        // skinned on the GPU in each cascade it is drawn in, cost a phone most of its frame).
        // Without bounds, or under a sun near the horizon, a caster goes into every cascade.
        constexpr float RECEIVER_DROP = 2.0f, DEPTH_MARGIN = 0.5f;
        float split_at[SHADOW_CASCADES];
        for (int k = 0; k < SHADOW_CASCADES; ++k) split_at[k] = fade * SHADOW_SPLITS[k];
        std::vector<unsigned> cascades_of(items.size(), (1u << SHADOW_CASCADES) - 1);
        for (size_t i = 0; i < items.size(); ++i) {
            const Item& it = items[i];
            AABB box = it.instance->custom_aabb.has_volume() ? it.instance->custom_aabb : it.surface->aabb;
            if (!box.has_volume() || lz.y < 0.05f) continue;
            Vector3 world[8];
            float top = -1e30f, bottom = 1e30f;
            for (int c = 0; c < 8; ++c) {
                world[c] = it.model.xform(Vector3(box.position.x + ((c & 1) ? box.size.x : 0.0f),
                                                  box.position.y + ((c & 2) ? box.size.y : 0.0f),
                                                  box.position.z + ((c & 4) ? box.size.z : 0.0f)));
                top = std::max(top, world[c].y), bottom = std::min(bottom, world[c].y);
            }
            Vector3 sweep = lz * -((top - (bottom - RECEIVER_DROP)) / lz.y);
            float near_d = 1e30f, far_d = -1e30f;
            for (int c = 0; c < 16; ++c) {
                float d = -view.xform(c < 8 ? world[c] : world[c - 8] + sweep).z;
                near_d = std::min(near_d, d), far_d = std::max(far_d, d);
            }
            near_d -= DEPTH_MARGIN, far_d += DEPTH_MARGIN;
            unsigned mask = 0;
            for (int k = 0; k < SHADOW_CASCADES; ++k) {
                float lo = k == 0 ? -1e30f : split_at[k - 1], hi = k == SHADOW_CASCADES - 1 ? 1e30f : split_at[k];
                if (far_d >= lo && near_d < hi) mask |= 1u << k;
            }
            cascades_of[i] = mask;
        }
        float from = camera.near;
        for (int k = 0; k < SHADOW_CASCADES; ++k) {
            float to = fade * SHADOW_SPLITS[k];
            shadow_split[k] = to;
            // The slice of the view as a sphere (stable under turning), its centre snapped to whole
            // shadow texels in the light's frame (stable under moving).
            Vector3 corners[8];
            for (int c = 0; c < 8; ++c) {
                float d = (c >> 2) ? to : from;
                float sx = (c & 1) ? 1.0f : -1.0f, sy = (c & 2) ? 1.0f : -1.0f;
                corners[c] = cam.xform({(sx * tan_x + off_x) * d, (sy * tan_y + off_y) * d, -d});
            }
            Vector3 centre;
            for (const Vector3& c : corners) centre += c;
            centre = centre / 8.0f;
            float radius = 0;
            for (const Vector3& c : corners) radius = std::max(radius, c.distance_to(centre));
            radius = std::ceil(radius * 16.0f) / 16.0f;
            Vector3 c_light = lb.xform_inv(centre);
            float texel_world = 2.0f * radius / static_cast<float>(quadrant);
            c_light.x = std::floor(c_light.x / texel_world) * texel_world;
            c_light.y = std::floor(c_light.y / texel_world) * texel_world;
            // Casters up to 100 m towards the light from the slice still shadow it.
            const float behind = 100.0f;
            Transform3D light_world(lb, lb.xform(c_light));
            Transform3D light_view = light_world.affine_inverse();
            float depth_range = 2.0f * radius + behind;
            Projection lp = Projection::orthogonal(-radius, radius, -radius, radius, -radius - behind, radius);
            // Clip space to the cascade's quadrant of the map.
            Mat4 bias;
            bias.m[0] = bias.m[5] = 0.25f;
            bias.m[10] = 0.5f;
            bias.m[12] = 0.25f + 0.5f * static_cast<float>(k & 1);
            bias.m[13] = 0.25f + 0.5f * static_cast<float>(k >> 1);
            bias.m[14] = 0.5f;
            shadow_m[k] = bias * Mat4::from(lp) * Mat4::from(light_view) * inv_view_m;
            shadow_normal_bias[k] = l.shadow_normal_bias * texel_world;
            shadow_depth_bias[k] = l.shadow_bias * texel_world / depth_range;

            if (on_pass) on_pass(PASS_CASCADE + k);
            glViewport((k & 1) * quadrant, (k >> 1) * quadrant, quadrant, quadrant);
            Mat4 lv = Mat4::from(light_view), lw = Mat4::from(light_world), lpm = Mat4::from(lp);
            for (size_t i = 0; i < items.size(); ++i) {
                Item& it = items[i];
                if (it.look.transparent || !it.look.depth || it.instance->cast_shadow == GeometryInstance3D::SHADOW_CASTING_SETTING_OFF)
                    continue;
                if (!(l.light_cull_mask & it.instance->layers)) continue;
                if (!(cascades_of[i] & (1u << k))) continue;
                draw(it, it.fed_vao ? *it.look.depth_fed : *it.look.depth, lv, lw, lpm, quadrant, quadrant, false);
                ++stats_.shadow_draws;
            }
            from = to;
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // ---- colour ----
    if (on_pass) on_pass(PASS_SCENE);
    glBindFramebuffer(GL_FRAMEBUFFER, g.msaa_fbo);
    glViewport(0, 0, iw, ih);
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClearDepthf(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // The sky (BG_SKY): one triangle over the screen behind everything, no depth written.
    if (env && env->background_mode == Environment::BG_SKY && env->sky_material) {
        Look sky = look_of(env->sky_material.get());
        if (sky.color) {
            const Program& p = *sky.color;
            glUseProgram(p.id);
            if (p.inv_view >= 0) glUniformMatrix4fv(p.inv_view, 1, GL_FALSE, inv_view_m.m);
            if (p.projection >= 0) glUniformMatrix4fv(p.projection, 1, GL_FALSE, proj_m.m);
            if (p.viewport_size >= 0) glUniform2f(p.viewport_size, static_cast<float>(iw), static_cast<float>(ih));
            bind_user(p, sky);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glBindVertexArray(g.final_vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDepthMask(GL_TRUE);
            ++stats_.draws;
        } else {
            ++stats_.programs_failed;
        }
    }
    std::vector<Item*> opaque, blended;
    for (Item& it : items) (it.look.transparent ? blended : opaque).push_back(&it);
    // Godot's transparent order: render_priority first (lower drawn first), then back to front.
    std::stable_sort(blended.begin(), blended.end(), [](const Item* a, const Item* b) {
        if (a->look.priority != b->look.priority) return a->look.priority < b->look.priority;
        return a->depth > b->depth;
    });
    if (sun >= 0) {
        glActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
        glBindTexture(GL_TEXTURE_2D, g.shadow_tex);
        glBindSampler(SHADOW_UNIT, 0);
    }
    for (std::vector<Item*>* list : {&opaque, &blended}) {
        for (Item* it : *list) {
            draw(*it, it->fed_vao ? *it->look.color_fed : *it->look.color, view_m, inv_view_m, proj_m, iw, ih, true);
            ++stats_.draws;
        }
    }
    // Leave the state the 2D canvas expects.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);

    // ---- resolve and encode ----
    if (on_pass) on_pass(PASS_POST);
    // Native: the scene's depth is not needed past its pass, nor its samples past the resolve: a
    // tiled GPU then writes neither to memory.
    const GLenum depth_only = GL_DEPTH_ATTACHMENT;
    const GLenum colour_only = GL_COLOR_ATTACHMENT0;
    glBindFramebuffer(GL_FRAMEBUFFER, g.msaa_fbo);
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &depth_only);
    if (!g.in_tile_resolve) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.resolve_fbo);
        glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, &colour_only);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g.msaa_fbo);
        glBlitFramebuffer(0, 0, iw, ih, 0, 0, iw, ih, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 1, &colour_only);
    }
    // The glow needs values above 1: only from a float buffer.
    bool glow = env && env->glow_enabled && g.float_buffer;
    if (glow) g.make_glow(iw, ih);
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glUseProgram(g.final_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.resolve_tex);
    glBindSampler(0, 0);
    glUniform1i(g.final_source, 0);
    glUniform1i(g.final_glow_on, glow ? 1 : 0);
    if (glow) {
        int units[GLOW_LEVELS];
        float weights[GLOW_LEVELS];
        for (int k = 0; k < GLOW_LEVELS; ++k) {
            units[k] = 1 + k;
            weights[k] = GLOW_WEIGHTS[k] * env->glow_intensity;
            glActiveTexture(GL_TEXTURE0 + 1 + k);
            glBindTexture(GL_TEXTURE_2D, g.glow_tex[k]);
            glBindSampler(1 + k, 0);
        }
        glUniform1iv(g.final_glow, GLOW_LEVELS, units);
        glUniform1fv(g.final_glow_weight, GLOW_LEVELS, weights);
        glActiveTexture(GL_TEXTURE0);
    }
    glBindVertexArray(g.final_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

}  // namespace t3::gfx3d
