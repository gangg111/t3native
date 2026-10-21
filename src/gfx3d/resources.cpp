#include "gfx3d/resources.h"

#include <sstream>

#include "gfx/gl.h"

namespace t3 {

gd::Ref<Shader> Shader::load(const std::string& path) {
    static std::map<std::string, gd::Ref<Shader>> loaded;
    gd::Ref<Shader>& s = loaded[path];
    if (!s) {
        s = gd::make<Shader>();
        s->path = path;
    }
    return s;
}

gd::Ref<Shader> Shader::with_define(const std::string& name) const {
    static std::map<std::string, gd::Ref<Shader>> variants;
    gd::Ref<Shader>& s = variants[path + "#" + name];
    if (!s) {
        s = gd::make<Shader>(*this);
        s->defines[name] = "";
    }
    return s;
}

// BaseMaterial3D's generated shader, for the properties the remake uses: the albedo colour times
// the texture (sRGB, as `source_color`), the vertex colour when asked, alpha or alpha scissor,
// and the billboard as Godot's BILLBOARD_ENABLED builds MODELVIEW_MATRIX (keeping the scale).
std::string StandardMaterial3D::shader_code() const {
    std::ostringstream s;
    s << "shader_type spatial;\nrender_mode ";
    const char* blend[] = {"blend_mix", "blend_add", "blend_sub", "blend_mul"};
    const char* cull[] = {"cull_back", "cull_front", "cull_disabled"};
    const char* depth[] = {"depth_draw_opaque", "depth_draw_always", "depth_draw_never"};
    s << blend[blend_mode] << ", " << cull[cull_mode] << ", " << depth[depth_draw_mode];
    if (shading_mode == SHADING_MODE_UNSHADED) s << ", unshaded";
    if (no_depth_test) s << ", depth_test_disabled";
    if (disable_receive_shadows) s << ", shadows_disabled";
    s << ";\n";
    const char* filters[] = {"filter_nearest", "filter_linear", "filter_nearest_mipmap", "filter_linear_mipmap"};
    s << "uniform vec4 albedo : source_color;\n";
    s << "uniform sampler2D texture_albedo : source_color, " << filters[texture_filter] << ", repeat_enable;\n";
    s << "uniform bool has_texture;\nuniform vec3 uv1_scale;\nuniform vec3 uv1_offset;\n";
    s << "uniform float roughness;\nuniform float metallic;\nuniform float specular;\n";
    if (transparency == TRANSPARENCY_ALPHA_SCISSOR) s << "uniform float alpha_scissor_threshold;\n";
    s << "void vertex() {\n\tUV = UV * uv1_scale.xy + uv1_offset.xy;\n";
    if (billboard_mode == BILLBOARD_ENABLED) {
        s << "\tMODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);\n"
             "\tMODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0), "
             "vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0), vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0), "
             "vec4(0.0, 0.0, 0.0, 1.0));\n";
    }
    s << "}\nvoid fragment() {\n\tvec4 albedo_tex = has_texture ? texture(texture_albedo, UV) : vec4(1.0);\n";
    if (vertex_color_use_as_albedo) {
        // vertex_color_is_srgb: Godot decodes the colour in vertex() (mix of the two sRGB curves).
        if (vertex_color_is_srgb)
            s << "\talbedo_tex *= vec4(mix(pow((COLOR.rgb + 0.055) / 1.055, vec3(2.4)), COLOR.rgb / 12.92, "
                 "lessThan(COLOR.rgb, vec3(0.04045))), COLOR.a);\n";
        else
            s << "\talbedo_tex *= COLOR;\n";
    }
    s << "\tALBEDO = albedo.rgb * albedo_tex.rgb;\n";
    s << "\tROUGHNESS = roughness;\n\tMETALLIC = metallic;\n\tSPECULAR = specular;\n";
    if (transparency == TRANSPARENCY_ALPHA) s << "\tALPHA = albedo.a * albedo_tex.a;\n";
    if (transparency == TRANSPARENCY_ALPHA_SCISSOR)
        s << "\tALPHA = albedo.a * albedo_tex.a;\n\tALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold;\n";
    s << "}\n";
    return s.str();
}

AABB Mesh::get_aabb() const {
    AABB r;
    bool first = true;
    for (const Surface& s : surfaces) {
        if (s.arrays.vertex.empty()) continue;
        r = first ? s.aabb : r.merge(s.aabb);
        first = false;
    }
    return r;
}

void Mesh::release_gpu() {
    for (Surface& s : surfaces) s.gpu.reset();
}

SurfaceGpu::~SurfaceGpu() {
    if (generation != gfx3d::gl_generation()) return;
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
}

namespace {
AABB bounds(const std::vector<Vector3>& v) {
    if (v.empty()) return {};
    AABB r(v[0], {});
    for (const Vector3& p : v) r = r.expand(p);
    return r;
}
}  // namespace

void ArrayMesh::add_surface_from_arrays(Primitive primitive, MeshArrays arrays) {
    Surface s;
    s.primitive = primitive;
    s.aabb = bounds(arrays.vertex);
    s.arrays = std::move(arrays);
    surfaces.push_back(std::move(s));
}

// Godot's QuadMesh: clockwise seen from +z (the renderer's front faces, as Godot's).
void QuadMesh::_build() {
    clear_surfaces();
    float hx = size_.x / 2, hy = size_.y / 2;
    MeshArrays a;
    a.vertex = {{-hx, hy, 0}, {hx, hy, 0}, {-hx, -hy, 0}, {hx, -hy, 0}};
    a.normal = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    a.tex_uv = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    a.index = {0, 1, 2, 1, 3, 2};
    add_surface_from_arrays(Primitive::TRIANGLES, std::move(a));
}

void ImmediateMesh::surface_begin(Primitive primitive, const gd::Ref<Material>& material) {
    building_ = Surface{};
    building_.primitive = primitive;
    building_.material = material;
    open_ = true;
    has_uv_ = has_color_ = has_normal_ = false;
}

void ImmediateMesh::surface_add_vertex(const Vector3& v) {
    MeshArrays& a = building_.arrays;
    a.vertex.push_back(v);
    // An attribute set for some vertex is kept for every one (Godot fills the earlier ones).
    if (has_uv_) a.tex_uv.resize(a.vertex.size() - 1), a.tex_uv.push_back(uv_);
    if (has_color_) a.color.resize(a.vertex.size() - 1, gfx::Color::white()), a.color.push_back(color_);
    if (has_normal_) a.normal.resize(a.vertex.size() - 1), a.normal.push_back(normal_);
}

void ImmediateMesh::surface_end() {
    if (!open_) return;
    open_ = false;
    building_.aabb = bounds(building_.arrays.vertex);
    surfaces.push_back(std::move(building_));
    building_ = Surface{};
}

void MultiMesh::set_instance_count(int n) {
    transforms_.assign(n, Transform3D());
    colors_.assign(n, gfx::Color::white());
    custom_.assign(n, gfx::Color(0, 0, 0, 0));
    dirty_ = true;
}

MultiMesh::~MultiMesh() { release_gpu(); }

void MultiMesh::release_gpu() {
    if (gpu_buffer && gpu_generation == gfx3d::gl_generation()) glDeleteBuffers(1, &gpu_buffer);
    gpu_buffer = 0;
    dirty_ = true;
}

}  // namespace t3
