#include "gfx/canvas.h"

#include <cmath>
#include <string>

#include "gfx/gl.h"
#include "platform/log.h"

namespace t3::gfx {

namespace {

const char* VERTEX = R"(#version 300 es
in vec2 a_pos;
in vec2 a_uv;
in vec4 a_color;
uniform vec2 u_size;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);
}
)";

const char* FRAGMENT = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
uniform int u_mask;
// A signed distance field in the texels' alpha (Font): (on, screen pixels per unit of alpha,
// pixels added to the shape: emboldening, an outline's half width).
uniform vec3 u_sdf;
out vec4 o_color;
void main() {
    vec4 t = texture(u_tex, v_uv);
    if (u_sdf.x > 0.5) {
        float d = (t.a - 128.0 / 255.0) * u_sdf.y + u_sdf.z;
        o_color = vec4(v_color.rgb, v_color.a * clamp(d + 0.5, 0.0, 1.0));
        return;
    }
    o_color = u_mask == 1 ? vec4(v_color.rgb, t.a) : t * v_color;
}
)";

unsigned compile(GLenum kind, const char* src) {
    unsigned s = glCreateShader(kind);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        Log::error(std::string("shader: ") + log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

unsigned make_program(const char* vertex_src, const char* fragment_src, const std::vector<const char*>& attributes) {
    unsigned vs = compile(GL_VERTEX_SHADER, vertex_src);
    unsigned fs = compile(GL_FRAGMENT_SHADER, fragment_src);
    if (!vs || !fs) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    for (size_t i = 0; i < attributes.size(); ++i) glBindAttribLocation(p, static_cast<GLuint>(i), attributes[i]);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        Log::error(std::string("program: ") + log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

Canvas::Canvas() {
    program_ = make_program(VERTEX, FRAGMENT, {"a_pos", "a_uv", "a_color"});
    u_size_ = glGetUniformLocation(program_, "u_size");
    u_tex_ = glGetUniformLocation(program_, "u_tex");
    u_mask_ = glGetUniformLocation(program_, "u_mask");
    u_sdf_ = glGetUniformLocation(program_, "u_sdf");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(8));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(16));
    glBindVertexArray(0);
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &white_);
    glBindTexture(GL_TEXTURE_2D, white_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glGenSamplers(2, samplers_);
    for (int k = 0; k < 2; ++k) {
        GLint f = k == 0 ? GL_NEAREST : GL_LINEAR;
        glSamplerParameteri(samplers_[k], GL_TEXTURE_MIN_FILTER, f);
        glSamplerParameteri(samplers_[k], GL_TEXTURE_MAG_FILTER, f);
        glSamplerParameteri(samplers_[k], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplers_[k], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

Canvas::~Canvas() {
    if (generation_ != gl_generation()) return;   // a lost context's objects: forgotten
    if (program_) glDeleteProgram(program_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (white_) glDeleteTextures(1, &white_);
    glDeleteSamplers(2, samplers_);
}

void Canvas::begin(int width, int height) {
    width_ = width;
    height_ = height;
    vertices_.clear();
    batches_.clear();
    blend_ = Blend::MIX;
    filter_ = Filter::NEAREST;
    texel_mask_ = false;
    sdf_ = Sdf{};
    clip_ = Rect2();
    reset_transform();
}

void Canvas::set_transform(Vec2 position, float rotation, Vec2 scale) {
    float c = std::cos(rotation), s = std::sin(rotation);
    xf_[0] = c * scale.x;
    xf_[1] = s * scale.x;
    xf_[2] = -s * scale.y;
    xf_[3] = c * scale.y;
    xf_[4] = position.x;
    xf_[5] = position.y;
}

Vec2 Canvas::_xf(Vec2 p) const { return {xf_[0] * p.x + xf_[2] * p.y + xf_[4], xf_[1] * p.x + xf_[3] * p.y + xf_[5]}; }

void Canvas::triangles(const Vertex* v, int count, const TextureRef& tex) {
    if (count <= 0) return;
    bool same = !batches_.empty() && batches_.back().tex == tex && batches_.back().blend == blend_ &&
                batches_.back().texel_mask == texel_mask_ && batches_.back().sdf == sdf_ &&
                batches_.back().filter == filter_ && batches_.back().clip.position.x == clip_.position.x &&
                batches_.back().clip.position.y == clip_.position.y && batches_.back().clip.size.x == clip_.size.x &&
                batches_.back().clip.size.y == clip_.size.y;
    if (!same) batches_.push_back({tex, blend_, filter_, clip_, texel_mask_, sdf_, static_cast<int>(vertices_.size()), 0});
    vertices_.insert(vertices_.end(), v, v + count);
    batches_.back().count += count;
}

void Canvas::_quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 ua, Vec2 ub, Vec2 uc, Vec2 ud, const Color& ca, const Color& cb,
                   const Color& cc, const Color& cd, const TextureRef& tex) {
    a = _xf(a); b = _xf(b); c = _xf(c); d = _xf(d);
    Vertex v[6] = {
        {a.x, a.y, ua.x, ua.y, ca.r, ca.g, ca.b, ca.a}, {b.x, b.y, ub.x, ub.y, cb.r, cb.g, cb.b, cb.a},
        {c.x, c.y, uc.x, uc.y, cc.r, cc.g, cc.b, cc.a}, {a.x, a.y, ua.x, ua.y, ca.r, ca.g, ca.b, ca.a},
        {c.x, c.y, uc.x, uc.y, cc.r, cc.g, cc.b, cc.a}, {d.x, d.y, ud.x, ud.y, cd.r, cd.g, cd.b, cd.a},
    };
    triangles(v, 6, tex);
}

void Canvas::draw_rect(const Rect2& rect, const Color& c, bool filled, float width) {
    Vec2 p = rect.position, e = rect.end();
    if (filled) {
        _quad(p, {e.x, p.y}, e, {p.x, e.y}, {}, {}, {}, {}, c, c, c, c, nullptr);
        return;
    }
    float w = width < 0 ? 1 : width;
    draw_rect(Rect2(p.x, p.y, rect.size.x, w), c);
    draw_rect(Rect2(p.x, e.y - w, rect.size.x, w), c);
    draw_rect(Rect2(p.x, p.y + w, w, rect.size.y - 2 * w), c);
    draw_rect(Rect2(e.x - w, p.y + w, w, rect.size.y - 2 * w), c);
}

void Canvas::draw_texture_rect(const TextureRef& tex, Rect2 rect, bool tile, const Color& modulate, bool transpose) {
    if (!tex) return;
    float u1 = 1, v1 = 1;
    if (tile) {
        u1 = std::fabs(rect.size.x) / tex->width();
        v1 = std::fabs(rect.size.y) / tex->height();
    }
    draw_texture_rect_region(tex, rect, Rect2(0, 0, u1 * tex->width(), v1 * tex->height()), modulate, transpose);
}

void Canvas::draw_texture_rect_region(const TextureRef& tex, Rect2 rect, Rect2 src, const Color& modulate, bool transpose) {
    if (!tex) return;
    // A negative size flips the texture in place: the rect keeps its position and the absolute size
    // (Godot's canvas_item_add_texture_rect_region sets FLIP_H / FLIP_V and negates the size).
    float u0 = src.position.x / tex->width(), v0 = src.position.y / tex->height();
    float u1 = (src.position.x + src.size.x) / tex->width(), v1 = (src.position.y + src.size.y) / tex->height();
    if (rect.size.x < 0) {
        rect.size.x = -rect.size.x;
        std::swap(u0, u1);
    }
    if (rect.size.y < 0) {
        rect.size.y = -rect.size.y;
        std::swap(v0, v1);
    }
    Vec2 p = rect.position, e = rect.end();
    Vec2 ua(u0, v0), ub(u1, v0), uc(u1, v1), ud(u0, v1);
    if (transpose) std::swap(ub, ud);
    _quad(p, {e.x, p.y}, e, {p.x, e.y}, ua, ub, uc, ud, modulate, modulate, modulate, modulate, tex);
}

void Canvas::draw_polygon(const std::vector<Vec2>& points, const std::vector<Color>& colors, const std::vector<Vec2>& uvs,
                          const TextureRef& tex) {
    if (points.size() < 3 || colors.empty()) return;
    std::vector<Vertex> v;
    auto vert = [&](size_t i) {
        Vec2 p = _xf(points[i]);
        const Color& c = colors.size() == 1 ? colors[0] : colors[i];
        Vec2 uv = uvs.size() == points.size() ? uvs[i] : Vec2();
        return Vertex{p.x, p.y, uv.x, uv.y, c.r, c.g, c.b, c.a};
    };
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        v.push_back(vert(0));
        v.push_back(vert(i));
        v.push_back(vert(i + 1));
    }
    triangles(v.data(), static_cast<int>(v.size()), tex);
}

void Canvas::draw_colored_polygon(const std::vector<Vec2>& points, const Color& c, const std::vector<Vec2>& uvs,
                                  const TextureRef& tex) {
    draw_polygon(points, {c}, uvs, tex);
}

void Canvas::draw_primitive(const std::vector<Vec2>& points, const std::vector<Color>& colors, const std::vector<Vec2>& uvs,
                            const TextureRef& tex) {
    if (points.empty() || colors.empty()) return;
    auto col = [&](size_t i) { return colors.size() == 1 ? colors[0] : colors[i]; };
    if (points.size() == 1) {
        draw_rect(Rect2(points[0].x, points[0].y, 1, 1), col(0));
    } else if (points.size() == 2) {
        draw_line(points[0], points[1], col(0), 1);
    } else {
        draw_polygon(points, colors, uvs, tex);
    }
}

void Canvas::draw_line(Vec2 from, Vec2 to, const Color& c, float width) {
    Vec2 d = to - from;
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len <= 0) return;
    float w = width <= 0 ? 1 : width;
    Vec2 n(-d.y / len * w * 0.5f, d.x / len * w * 0.5f);
    _quad(from + n, to + n, to - n, from - n, {}, {}, {}, {}, c, c, c, c, nullptr);
}

void Canvas::draw_polyline(const std::vector<Vec2>& points, const Color& c, float width) {
    for (size_t i = 0; i + 1 < points.size(); ++i) draw_line(points[i], points[i + 1], c, width);
}

void Canvas::draw_circle(Vec2 centre, float radius, const Color& c) {
    const int n = 32;
    std::vector<Vec2> pts;
    for (int i = 0; i < n; ++i) {
        float a = 6.2831853f * i / n;
        pts.emplace_back(centre.x + std::cos(a) * radius, centre.y + std::sin(a) * radius);
    }
    draw_colored_polygon(pts, c);
}

void Canvas::draw_arc(Vec2 centre, float radius, float start_angle, float end_angle, int point_count, const Color& c,
                      float width) {
    std::vector<Vec2> pts;
    for (int i = 0; i < point_count; ++i) {
        float a = start_angle + (end_angle - start_angle) * i / std::max(1, point_count - 1);
        pts.emplace_back(centre.x + std::cos(a) * radius, centre.y + std::sin(a) * radius);
    }
    draw_polyline(pts, c, width);
}

void Canvas::flush() {
    if (vertices_.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    size_t bytes = vertices_.size() * sizeof(Vertex);
    if (bytes > vbo_capacity_) {
        vbo_capacity_ = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vbo_capacity_), nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices_.data());
    glUseProgram(program_);
    glUniform2f(u_size_, static_cast<float>(width_), static_cast<float>(height_));
    glUniform1i(u_tex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, width_, height_);
    for (const Batch& b : batches_) {
        switch (b.blend) {
            case Blend::DISABLED: glDisable(GL_BLEND); break;
            case Blend::MIX:
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case Blend::ADD:
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
                break;
            case Blend::SUB:
                glEnable(GL_BLEND);
                glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
                break;
            case Blend::MUL:
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
                break;
            case Blend::PREMULT_ALPHA:
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
        }
        if (b.clip.has_area()) {
            glEnable(GL_SCISSOR_TEST);
            int x0 = static_cast<int>(std::floor(b.clip.position.x));
            int y0 = static_cast<int>(std::floor(b.clip.position.y));
            int x1 = static_cast<int>(std::ceil(b.clip.end().x));
            int y1 = static_cast<int>(std::ceil(b.clip.end().y));
            glScissor(x0, height_ - y1, x1 - x0, y1 - y0);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        glUniform1i(u_mask_, b.texel_mask ? 1 : 0);
        glUniform3f(u_sdf_, b.sdf.on ? 1.0f : 0.0f, b.sdf.pixels_per_unit, b.sdf.grow);
        glBindTexture(GL_TEXTURE_2D, b.tex ? b.tex->id() : white_);
        glBindSampler(0, samplers_[b.filter == Filter::LINEAR ? 1 : 0]);
        glDrawArrays(GL_TRIANGLES, b.first, b.count);
    }
    glDisable(GL_SCISSOR_TEST);
    glBindSampler(0, 0);
    glBindVertexArray(0);
    vertices_.clear();
    batches_.clear();
}

}  // namespace t3::gfx
