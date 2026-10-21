#pragma once
// Godot's 3D resources as the views use them: Shader (a `res://` .gdshader of t3native/shaders),
// ShaderMaterial, StandardMaterial3D (the properties the remake sets), ArrayMesh, ImmediateMesh and
// MultiMesh. They hold data only; the renderer (renderer.h) uploads and draws them. GPU objects
// are made lazily and dropped with `release_gpu()` when the GL context is lost.
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/texture.h"
#include "gfx3d/math3d.h"

namespace t3 {

// A shader parameter's value (Variant): what set_shader_parameter takes.
using ShaderValue = std::variant<std::monostate, bool, int, float, gfx::Vec2, Vector3, Vector4, gfx::Color, Basis,
                                 std::vector<Vector4>, std::vector<float>, gfx::TextureRef>;

// A `.gdshader` by its `res://` path, with the defines it is compiled with (RenderQuality's CRISP
// variant).
class Shader {
public:
    std::string path;
    std::map<std::string, std::string> defines;

    // Godot's preload("res://..."): one object per path, so materials can compare shaders.
    static gd::Ref<Shader> load(const std::string& path);
    // This shader compiled with `name` defined (one object per path and define).
    gd::Ref<Shader> with_define(const std::string& name) const;
};

class Material {
public:
    int render_priority = 0;   // Godot's Material.render_priority: the transparent pass draws lower first
    virtual ~Material() = default;
};

class ShaderMaterial : public Material {
public:
    gd::Ref<Shader> shader;
    std::map<std::string, ShaderValue> parameters;

    void set_shader_parameter(const std::string& name, const ShaderValue& value) { parameters[name] = value; }
    ShaderValue get_shader_parameter(const std::string& name) const {
        auto it = parameters.find(name);
        return it == parameters.end() ? ShaderValue{} : it->second;
    }
};

// BaseMaterial3D's enums and the properties the remake sets (all others at Godot's defaults).
class StandardMaterial3D : public Material {
public:
    enum ShadingMode { SHADING_MODE_UNSHADED, SHADING_MODE_PER_PIXEL };
    enum BlendMode { BLEND_MODE_MIX, BLEND_MODE_ADD, BLEND_MODE_SUB, BLEND_MODE_MUL };
    enum Transparency { TRANSPARENCY_DISABLED, TRANSPARENCY_ALPHA, TRANSPARENCY_ALPHA_SCISSOR };
    enum CullMode { CULL_BACK, CULL_FRONT, CULL_DISABLED };
    enum DepthDrawMode { DEPTH_DRAW_OPAQUE_ONLY, DEPTH_DRAW_ALWAYS, DEPTH_DRAW_DISABLED };
    enum BillboardMode { BILLBOARD_DISABLED, BILLBOARD_ENABLED };
    enum TextureFilter { TEXTURE_FILTER_NEAREST, TEXTURE_FILTER_LINEAR, TEXTURE_FILTER_NEAREST_WITH_MIPMAPS,
                         TEXTURE_FILTER_LINEAR_WITH_MIPMAPS };

    ShadingMode shading_mode = SHADING_MODE_PER_PIXEL;
    BlendMode blend_mode = BLEND_MODE_MIX;
    Transparency transparency = TRANSPARENCY_DISABLED;
    CullMode cull_mode = CULL_BACK;
    DepthDrawMode depth_draw_mode = DEPTH_DRAW_OPAQUE_ONLY;
    BillboardMode billboard_mode = BILLBOARD_DISABLED;
    TextureFilter texture_filter = TEXTURE_FILTER_LINEAR_WITH_MIPMAPS;
    gfx::Color albedo_color = gfx::Color::white();
    gfx::TextureRef albedo_texture;
    Vector3 uv1_scale = Vector3::ONE();
    Vector3 uv1_offset;
    bool vertex_color_use_as_albedo = false;
    bool vertex_color_is_srgb = false;   // the vertex colours are decoded from sRGB
    float roughness = 1.0f;
    float metallic = 0.0f;
    float metallic_specular = 0.5f;      // SPECULAR
    bool no_depth_test = false;          // drawn in the transparent pass, as Godot does
    bool disable_receive_shadows = false;
    bool disable_fog = false;
    float alpha_scissor_threshold = 0.5f;

    // The generated shader for these settings (a `res://` path of the built-in variants).
    std::string shader_code() const;
};

// Mesh.PrimitiveType.
enum class Primitive { POINTS, LINES, LINE_STRIP, TRIANGLES, TRIANGLE_STRIP };

// Mesh.ARRAY_*: the vertex arrays of a surface. CUSTOM0..3 hold `custom_components[i]` floats per
// vertex (Godot's ARRAY_CUSTOM_R/RG/RGB/RGBA_FLOAT; the half formats are given as floats too).
struct MeshArrays {
    std::vector<Vector3> vertex;
    std::vector<Vector3> normal;
    std::vector<gfx::Vec2> tex_uv;
    std::vector<gfx::Vec2> tex_uv2;
    std::vector<gfx::Color> color;
    std::array<std::vector<float>, 4> custom;
    std::array<int, 4> custom_components{4, 4, 4, 4};
    std::vector<int> index;
};

namespace gfx3d {
// Counts GL contexts: objects made under an older one are forgotten, never deleted (their names may
// belong to the new context's objects). Renderer3D::graphics_lost() advances it. The same counter as
// gfx::gl_generation() (the 2D canvas, the textures and the render targets).
inline unsigned& gl_generation() { return gfx::gl_generation(); }
}  // namespace gfx3d

// The GL objects of one uploaded surface (renderer.cpp).
struct SurfaceGpu {
    unsigned vao = 0, vbo = 0, ibo = 0;
    int count = 0;
    // The interleaved buffer's attributes (location, components, offset in floats) and stride (in
    // floats): a captured draw's own VAO reads them too (renderer.cpp).
    std::vector<std::array<int, 3>> attributes;
    int stride = 0;
    unsigned generation = gfx3d::gl_generation();
    ~SurfaceGpu();
};

class Mesh {
public:
    struct Surface {
        Primitive primitive = Primitive::TRIANGLES;
        MeshArrays arrays;
        gd::Ref<Material> material;
        AABB aabb;
        std::shared_ptr<SurfaceGpu> gpu;   // made by the renderer
    };
    std::vector<Surface> surfaces;

    virtual ~Mesh() = default;
    int get_surface_count() const { return static_cast<int>(surfaces.size()); }
    gd::Ref<Material> surface_get_material(int i) const { return surfaces[i].material; }
    void surface_set_material(int i, const gd::Ref<Material>& m) { surfaces[i].material = m; }
    void clear_surfaces() { surfaces.clear(); }
    AABB get_aabb() const;
    void release_gpu();
};

class ArrayMesh : public Mesh {
public:
    void add_surface_from_arrays(Primitive primitive, MeshArrays arrays);
};

// ImmediateMesh: surfaces built vertex by vertex, each attribute set before surface_add_vertex.
class ImmediateMesh : public Mesh {
public:
    void surface_begin(Primitive primitive, const gd::Ref<Material>& material = nullptr);
    void surface_set_uv(gfx::Vec2 uv) { uv_ = uv, has_uv_ = true; }
    void surface_set_color(gfx::Color c) { color_ = c, has_color_ = true; }
    void surface_set_normal(const Vector3& n) { normal_ = n, has_normal_ = true; }
    void surface_add_vertex(const Vector3& v);
    void surface_end();

private:
    Surface building_;
    bool open_ = false, has_uv_ = false, has_color_ = false, has_normal_ = false;
    gfx::Vec2 uv_;
    gfx::Color color_;
    Vector3 normal_;
};

// Godot's QuadMesh (a PlaneMesh facing +z): one `size` quad centred on the origin, normal +z, UV
// (0, 0) at its top left; `quad.size = v` is `set_size(v)`, which builds the surface again.
class QuadMesh : public ArrayMesh {
public:
    QuadMesh() { _build(); }
    gfx::Vec2 get_size() const { return size_; }
    void set_size(gfx::Vec2 size) {
        size_ = size;
        _build();
    }

private:
    gfx::Vec2 size_{1, 1};
    void _build();
};

class MultiMesh {
public:
    enum TransformFormat { TRANSFORM_2D, TRANSFORM_3D };
    TransformFormat transform_format = TRANSFORM_3D;
    bool use_colors = false;
    bool use_custom_data = false;
    gd::Ref<Mesh> mesh;
    int visible_instance_count = -1;   // -1: all

    int get_instance_count() const { return static_cast<int>(transforms_.size()); }
    void set_instance_count(int n);
    void set_instance_transform(int i, const Transform3D& t) { transforms_[i] = t, dirty_ = true; }
    void set_instance_color(int i, gfx::Color c) { colors_[i] = c, dirty_ = true; }
    void set_instance_custom_data(int i, gfx::Color c) { custom_[i] = c, dirty_ = true; }
    const Transform3D& get_instance_transform(int i) const { return transforms_[i]; }

    // The instance buffer (renderer.cpp).
    unsigned gpu_buffer = 0;
    unsigned gpu_generation = 0;
    bool dirty_ = true;
    std::vector<Transform3D> transforms_;
    std::vector<gfx::Color> colors_;
    std::vector<gfx::Color> custom_;
    ~MultiMesh();
    void release_gpu();
};

}  // namespace t3
