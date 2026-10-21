// gfx3d::parse_gdshader on a hand-written shader and on the remake's own (embedded) shaders. The
// expected values are read off the shader texts by hand.
#include <regex>

#include "gfx3d/gdshader.h"
#include "gfx3d/shader_sources.h"
#include "support/test_suite.h"

using namespace t3::gfx3d;

namespace {

std::string embedded(const std::string& path) {
    auto it = shader_sources().find(path);
    if (it == shader_sources().end()) throw std::runtime_error("no shader " + path);
    return it->second;
}

const std::map<std::string, std::string> FORWARD = {
    {"CURRENT_RENDERER", "0"}, {"RENDERER_FORWARD_PLUS", "0"}, {"RENDERER_MOBILE", "1"}, {"RENDERER_COMPATIBILITY", "2"}};

}  // namespace

T3_TEST(gdshader, uniforms_varyings_and_stages) {
    const char* src = R"(shader_type spatial;
render_mode unshaded, cull_disabled;
// a comment with { braces } and uniform words
const int N = 3;
uniform vec4 rows[N];
uniform sampler2D atlas : source_color, filter_nearest, repeat_disable;
uniform vec3 tint : source_color = vec3(0.5, 0.25, 1.0);
uniform int frame = -1;
varying flat vec4 window;
varying vec3 later;
float twice(float x) { return 2.0 * x; }
void vertex() {
	window = CUSTOM0;
}
void fragment() {
	later = vec3(1.0);
	ALBEDO = texture(atlas, UV).rgb * twice(0.5);
	ALPHA = 0.5;
}
)";
    GdShader s = parse_gdshader(src, {}, [](const std::string&) -> std::string { return ""; });
    t.expect_equal(s.type, std::string("spatial"), "type");
    t.expect(s.mode("unshaded") && s.mode("cull_disabled") && s.render_modes.size() == 2, "render modes");
    t.expect_equal(static_cast<int>(s.uniforms.size()), 4, "uniforms");
    const ShaderUniform* rows = s.uniform("rows");
    t.expect(rows && rows->array_size == 3 && rows->type == "vec4", "rows[N] with N = 3");
    const ShaderUniform* atlas = s.uniform("atlas");
    t.expect(atlas && atlas->source_color && atlas->filter == ShaderUniform::Filter::NEAREST && atlas->repeat == 0, "atlas hints");
    const ShaderUniform* tint = s.uniform("tint");
    t.expect(tint && tint->source_color && tint->default_value == "vec3(0.5, 0.25, 1.0)", "tint default");
    t.expect(s.uniform("frame") && s.uniform("frame")->default_value == "-1", "frame default");
    t.expect_equal(static_cast<int>(s.varyings.size()), 2, "varyings");
    t.expect(s.varyings[0].qualifier == "flat" && s.varyings[0].from_vertex, "window: flat, from vertex()");
    t.expect(!s.varyings[1].from_vertex, "later: fragment only");
    t.expect(s.has_vertex && s.has_fragment && !s.has_light, "stages");
    t.expect(s.writes_alpha && !s.alpha_scissor, "ALPHA written, no scissor");
    t.expect(s.common.find("float twice(float x)") != std::string::npos, "helper in common");
    t.expect(s.common.find("uniform vec3 tint;") != std::string::npos, "uniform without hint or default");
    t.expect(s.common.find("braces") == std::string::npos, "comments removed");
}

T3_TEST(gdshader, conditionals) {
    const char* src = "shader_type spatial;\n#ifdef CRISP\nuniform sampler2D a : filter_nearest;\n#else\n"
                      "uniform sampler2D a : filter_linear;\n#endif\nvoid fragment() {\n#if CURRENT_RENDERER != RENDERER_COMPATIBILITY\n"
                      "EMISSION = vec3(1.0);\n#else\nALBEDO = vec3(1.0);\n#endif\n}\n";
    auto none = [](const std::string&) -> std::string { return ""; };
    GdShader smooth = parse_gdshader(src, FORWARD, none);
    std::map<std::string, std::string> crisp_defs = FORWARD;
    crisp_defs["CRISP"] = "";
    GdShader crisp = parse_gdshader(src, crisp_defs, none);
    t.expect(smooth.uniform("a")->filter == ShaderUniform::Filter::LINEAR, "smooth: linear");
    t.expect(crisp.uniform("a")->filter == ShaderUniform::Filter::NEAREST, "crisp: nearest");
    t.expect(smooth.fragment.find("EMISSION") != std::string::npos && smooth.fragment.find("ALBEDO") == std::string::npos,
             "forward+ branch");
}

T3_TEST(gdshader, remake_fighter_shader) {
    GdShader s = parse_gdshader(embedded("res://presentation/fighter/fighter.gdshader"), FORWARD, embedded);
    // source: fighter.gdshader's render_mode line and fighter_skin.gdshaderinc's declarations
    t.expect(s.mode("cull_back") && s.mode("depth_draw_opaque") && s.mode("ambient_light_disabled"), "render modes");
    t.expect(s.uniform("bone_rows") && s.uniform("bone_rows")->array_size == 84, "bone_rows[JOINT_ROWS] = 84");
    t.expect(s.uniform("albedo_texture") && s.uniform("albedo_texture")->filter == ShaderUniform::Filter::LINEAR, "smooth atlas");
    t.expect(s.uniform("hidden_joints") && s.uniform("hidden_joints")->default_value == "0", "hidden_joints = 0");
    t.expect(s.has_vertex && s.has_fragment && s.has_light, "three stages");
    t.expect(s.alpha_scissor && s.writes_alpha, "alpha scissor");
    t.expect_equal(static_cast<int>(s.varyings.size()), 3, "texel_srgb, unlit_intensity, unlit");
    for (const ShaderVarying& v : s.varyings) t.expect(!v.from_vertex, v.name + " is fragment-only");
    t.expect(s.fragment.find("EMISSION = unlit") != std::string::npos, "Forward+ emission");
    t.expect(s.light.find("- unlit") != std::string::npos, "Forward+ light branch");
}

T3_TEST(gdshader, reserved_words_are_renamed) {
    GdShader s = parse_gdshader(embedded("res://presentation/fighter/fighter.gdshader"), FORWARD, embedded);
    // source: fighter_skin.gdshaderinc names a parameter `shared` (a GLSL ES 3.1 keyword)
    t.expect(!std::regex_search(s.common, std::regex("\\bshared\\b")), "no bare `shared` in the GLSL");
    t.expect(s.common.find("bool t3r_shared") != std::string::npos, "renamed parameter");
    const char* src = "shader_type spatial;\nuniform float filter = 2.0;\nvoid fragment() { ALBEDO = vec3(filter); }\n";
    GdShader u = parse_gdshader(src, {}, [](const std::string&) -> std::string { return ""; });
    // source: by hand: the parameter keeps its name, the GLSL uses the prefixed one
    t.expect(u.uniform("filter") && u.uniform("filter")->glsl == "t3r_filter", "uniform name kept, GLSL renamed");
    t.expect(u.fragment.find("vec3(t3r_filter)") != std::string::npos, "body renamed");
}

T3_TEST(gdshader, large_arrays_read_from_a_texture) {
    GdShader s = parse_gdshader(embedded("res://presentation/fighter/fighter.gdshader"), FORWARD, embedded);
    // source: fighter_skin.gdshaderinc: bone_rows[JOINT_ROWS], 84 >= TEXTURE_ARRAY_MIN
    const ShaderUniform* rows = s.uniform("bone_rows");
    t.expect(rows && rows->as_texture && rows->array_size == 84, "bone_rows read from a texture");
    t.expect(s.common.find("uniform highp sampler2D bone_rows;") != std::string::npos, "declared as a highp sampler");
    t.expect(!std::regex_search(s.common, std::regex("bone_rows\\s*\\[")), "no array reads left");
    // source: by hand: fighter_skin.gdshaderinc line 57's `bone_rows[3 * j]`
    t.expect(s.common.find("texelFetch(bone_rows, ivec2(int(3 * j), 0), 0)") != std::string::npos, "a read as a fetch");
    // source: by hand: nested brackets and other words that start with the name
    t.expect_equal(array_reads_as_fetches("a[b[i]] + ab[1] + a [2]", "a"),
                   std::string("texelFetch(a, ivec2(int(b[i]), 0), 0) + ab[1] + texelFetch(a, ivec2(int(2), 0), 0)"), "matching brackets");
}

T3_TEST(gdshader, remake_other_shaders) {
    for (const char* path : {"res://presentation/fighter/fighter_double_sided.gdshader", "res://presentation/stage/floor.gdshader",
                             "res://presentation/stage/panorama.gdshader", "res://presentation/effects/effect_object.gdshader",
                             "res://presentation/effects/effect_object_half.gdshader"}) {
        GdShader s = parse_gdshader(embedded(path), FORWARD, embedded);
        t.expect(s.type == "spatial" && s.has_fragment, path);
    }
    GdShader fx = parse_gdshader(embedded("res://presentation/effects/effect_object.gdshader"), FORWARD, embedded);
    // source: effect_object.gdshader's render_mode line
    t.expect(fx.mode("skip_vertex_transform") && fx.mode("blend_add") && fx.mode("depth_draw_never"), "effect modes");
    t.expect(fx.varyings.size() == 1 && fx.varyings[0].from_vertex, "sprite from vertex()");
    GdShader floor = parse_gdshader(embedded("res://presentation/stage/floor.gdshader"), FORWARD, embedded);
    t.expect(floor.uniform("pattern")->repeat == 1 &&
                 floor.uniform("pattern")->filter == ShaderUniform::Filter::LINEAR_MIPMAP_ANISOTROPIC,
             "floor pattern repeats, anisotropic");
    t.expect(floor.uniform("far_edge")->default_value == "vec3(0.0, 0.0, 1.0e9)", "far_edge default");
}
