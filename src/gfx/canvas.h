#pragma once
// A 2D canvas with Godot's CanvasItem drawing calls (draw_rect, draw_texture_rect,
// draw_texture_rect_region, draw_polygon, draw_primitive, draw_line, draw_circle, draw_arc,
// draw_set_transform), so the remake's views translate call for call. Calls are batched and drawn
// in order by `flush`, each batch with its blend mode (CanvasItemMaterial.BLEND_MODE_*), texture
// filter (CanvasItem.TEXTURE_FILTER_*) and clip rectangle (Control.clip_contents).
#include <cmath>
#include <memory>
#include <vector>

#include "gfx/texture.h"

namespace t3::gfx {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float k) const { return {x * k, y * k}; }
};

struct Rect2 {
    Vec2 position, size;
    Rect2() = default;
    Rect2(float x, float y, float w, float h) : position(x, y), size(w, h) {}
    Rect2(Vec2 p, Vec2 s) : position(p), size(s) {}
    Vec2 end() const { return position + size; }
    bool has_area() const { return size.x > 0 && size.y > 0; }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    // Godot's Color8(r, g, b, a): channels 0 ... 255.
    static Color c8(int r, int g, int b, int a = 255) { return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}; }
    static Color white() { return {1, 1, 1, 1}; }
    static Color black() { return {0, 0, 0, 1}; }
    Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
};

enum class Blend { MIX, ADD, SUB, MUL, PREMULT_ALPHA, DISABLED };
enum class Filter { NEAREST, LINEAR };

class Canvas {
public:
    Canvas();
    ~Canvas();
    Canvas(const Canvas&) = delete;

    // The target's size in pixels; drawing coordinates are pixels from its top left.
    void begin(int width, int height);
    // Draws everything since `begin` into the bound framebuffer, in order.
    void flush();

    void set_blend(Blend b) { blend_ = b; }
    // texel_mask_sub.gdshader's colour: the vertex colour with the texture's alpha (each opaque
    // texel stands for the vertex colour), for a CLUT of white entries.
    void set_texel_mask(bool on) { texel_mask_ = on; }
    void set_filter(Filter f) { filter_ = f; }
    Filter get_filter() const { return filter_; }
    // The following textures are signed distance fields in their alpha (Font's glyphs): the shape
    // is drawn in the vertex colour, `pixels_per_unit` screen pixels per unit of alpha, grown by
    // `grow` pixels (emboldening, an outline).
    struct Sdf {
        bool on = false;
        float pixels_per_unit = 0, grow = 0;
        bool operator==(const Sdf& o) const { return on == o.on && pixels_per_unit == o.pixels_per_unit && grow == o.grow; }
    };
    void set_sdf(const Sdf& s) { sdf_ = s; }
    // The scale the current transform applies (its x axis's length).
    float transform_scale() const { return std::sqrt(xf_[0] * xf_[0] + xf_[1] * xf_[1]); }
    // A clip rectangle in target pixels (an empty one: no clipping).
    void set_clip(const Rect2& clip) { clip_ = clip; }
    // draw_set_transform(position, rotation, scale) for the following calls.
    void set_transform(Vec2 position, float rotation = 0, Vec2 scale = {1, 1});
    void reset_transform() { set_transform({0, 0}); }

    void draw_rect(const Rect2& rect, const Color& c, bool filled = true, float width = -1);
    void draw_texture_rect(const TextureRef& tex, Rect2 rect, bool tile = false, const Color& modulate = Color::white(),
                           bool transpose = false);
    void draw_texture_rect_region(const TextureRef& tex, Rect2 rect, Rect2 src, const Color& modulate = Color::white(),
                                  bool transpose = false);
    // A convex (or fan-shaped) polygon; `colors` has one colour or one per point, `uvs` none or
    // one per point (0 ... 1).
    void draw_polygon(const std::vector<Vec2>& points, const std::vector<Color>& colors, const std::vector<Vec2>& uvs = {},
                      const TextureRef& tex = nullptr);
    void draw_colored_polygon(const std::vector<Vec2>& points, const Color& c, const std::vector<Vec2>& uvs = {},
                              const TextureRef& tex = nullptr);
    // 1 point, a 2-point line, a triangle or a 4-point quad (0, 1, 2), (0, 2, 3).
    void draw_primitive(const std::vector<Vec2>& points, const std::vector<Color>& colors, const std::vector<Vec2>& uvs = {},
                        const TextureRef& tex = nullptr);
    void draw_line(Vec2 from, Vec2 to, const Color& c, float width = 1);
    void draw_polyline(const std::vector<Vec2>& points, const Color& c, float width = 1);
    void draw_circle(Vec2 centre, float radius, const Color& c);
    void draw_arc(Vec2 centre, float radius, float start_angle, float end_angle, int point_count, const Color& c,
                  float width = 1);

    int width() const { return width_; }
    int height() const { return height_; }

    // Raw triangles in target pixels (already transformed), for helpers built on the canvas.
    struct Vertex {
        float x, y, u, v, r, g, b, a;
    };
    void triangles(const Vertex* v, int count, const TextureRef& tex);

private:
    Vec2 _xf(Vec2 p) const;
    void _quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 ua, Vec2 ub, Vec2 uc, Vec2 ud, const Color& ca, const Color& cb,
               const Color& cc, const Color& cd, const TextureRef& tex);

    struct Batch {
        TextureRef tex;
        Blend blend;
        Filter filter;
        Rect2 clip;
        bool texel_mask;
        Sdf sdf;
        int first, count;
    };
    bool texel_mask_ = false;
    Sdf sdf_;
    int u_mask_ = -1, u_sdf_ = -1;
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
    int width_ = 0, height_ = 0;
    Blend blend_ = Blend::MIX;
    Filter filter_ = Filter::NEAREST;
    Rect2 clip_;
    float xf_[6] = {1, 0, 0, 1, 0, 0};   // x' = a*x + c*y + e, y' = b*x + d*y + f
    unsigned program_ = 0, vao_ = 0, vbo_ = 0, white_ = 0, samplers_[2] = {0, 0};
    unsigned generation_ = gl_generation();   // the GL context they belong to (texture.h)
    int u_size_ = -1, u_tex_ = -1;
    size_t vbo_capacity_ = 0;
};

// Compiles and links a GLSL ES 3.00 program (attribute locations by name order: 0, 1, 2 ...);
// 0 when it fails (the log is written).
unsigned make_program(const char* vertex_src, const char* fragment_src, const std::vector<const char*>& attributes);

}  // namespace t3::gfx
