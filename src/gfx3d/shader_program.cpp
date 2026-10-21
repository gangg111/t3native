#include "gfx3d/shader_program.h"
#include "gfx/texture.h"

#include <regex>
#include <sstream>
#include <stdexcept>

#include "gfx/gl.h"
#include "gfx3d/resources.h"
#include "gfx3d/shader_sources.h"
#include "platform/log.h"

namespace t3::gfx3d {

const std::map<std::string, std::string>& base_defines() {
    static const std::map<std::string, std::string> d = {
        {"RENDERER_FORWARD_PLUS", "0"}, {"RENDERER_MOBILE", "1"}, {"RENDERER_COMPATIBILITY", "2"}, {"CURRENT_RENDERER", "0"}};
    return d;
}

namespace {

const char* HEADER =
    "#version 300 es\n"
    "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp sampler2DShadow;\n"
    "#define PI 3.1415926535897932384626433833\n#define TAU 6.2831853071795864769252867666\n"
    "#define E 2.7182818284590452353602874714\n";

// Godot's sRGB decode, for the `source_color` samplers.
const char* SRGB_HELPERS =
    "vec3 t3_srgb_to_linear(vec3 c) {\n"
    "\treturn mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n}\n"
    "vec4 t3_srgb_texture(sampler2D s, vec2 uv) { vec4 c = texture(s, uv); return vec4(t3_srgb_to_linear(c.rgb), c.a); }\n"
    "vec4 t3_srgb_textureLod(sampler2D s, vec2 uv, float lod) { vec4 c = textureLod(s, uv, lod); return vec4(t3_srgb_to_linear(c.rgb), c.a); }\n"
    "vec4 t3_srgb_texelFetch(sampler2D s, ivec2 p, int lod) { vec4 c = texelFetch(s, p, lod); return vec4(t3_srgb_to_linear(c.rgb), c.a); }\n";

// Godot's default light (scene_forward_lights.glsl, light_compute: diffuse_burley and
// specular_schlick_ggx).
const char* DEFAULT_LIGHT =
    "float t3_schlick(float u) { float m = clamp(1.0 - u, 0.0, 1.0); float m2 = m * m; return m2 * m2 * m; }\n"
    "float t3_d_ggx(float c, float a) { float x = c * a; float k = a / (1.0 - c * c + x * x); return k * k * (1.0 / PI); }\n"
    "float t3_v_ggx(float l, float v, float a) { return 0.5 / mix(2.0 * l * v, l + v, a); }\n"
    "void t3_default_light() {\n"
    "\tfloat NdotL = min(dot(NORMAL, LIGHT), 1.0);\n\tfloat cNdotL = max(NdotL, 0.0);\n"
    "\tfloat cNdotV = max(dot(NORMAL, VIEW), 1e-4);\n"
    "\tvec3 H = normalize(VIEW + LIGHT);\n\tfloat cNdotH = clamp(dot(NORMAL, H), 0.0, 1.0);\n"
    "\tfloat cLdotH = clamp(dot(LIGHT, H), 0.0, 1.0);\n"
    "\tfloat fd90 = 2.0 * cLdotH * cLdotH * ROUGHNESS - 0.5;\n"
    "\tfloat FdV = 1.0 + fd90 * t3_schlick(cNdotV);\n\tfloat FdL = 1.0 + fd90 * t3_schlick(cNdotL);\n"
    "\tDIFFUSE_LIGHT += LIGHT_COLOR * ((1.0 / PI) * FdV * FdL * cNdotL) * ATTENUATION;\n"
    "\tif (ROUGHNESS > 0.0) {\n"
    "\t\tfloat a = ROUGHNESS * ROUGHNESS;\n"
    "\t\tvec3 f0 = mix(vec3(0.16 * SPECULAR * SPECULAR), ALBEDO, METALLIC);\n"
    "\t\tfloat f90 = clamp(50.0 * f0.g, 0.0, 1.0);\n"
    "\t\tvec3 F = f0 + (f90 - f0) * t3_schlick(cLdotH);\n"
    "\t\tSPECULAR_LIGHT += cNdotL * t3_d_ggx(cNdotH, a) * t3_v_ggx(cNdotL, cNdotV, a) * F * LIGHT_COLOR * ATTENUATION * SPECULAR_AMOUNT;\n"
    "\t}\n}\n";

std::string replace_all(std::string s, const std::regex& re, const std::string& with) { return std::regex_replace(s, re, with); }

// `texture(name, ...)` and the like on a source_color sampler: decoded from sRGB.
std::string decode_srgb(std::string text, const GdShader& s) {
    for (const ShaderUniform& u : s.uniforms) {
        if (!u.source_color || u.type != "sampler2D") continue;
        for (const char* fn : {"texture", "textureLod", "texelFetch"}) {
            std::regex re(std::string("\\b") + fn + "\\s*\\(\\s*" + u.glsl + "\\s*,");
            text = replace_all(text, re, std::string("t3_srgb_") + fn + "(" + u.glsl + ",");
        }
    }
    return text;
}

std::string varying_decl(const ShaderVarying& v, const char* io) {
    std::string q = v.qualifier.empty() ? "" : v.qualifier + " ";
    return q + io + " " + v.type + " " + v.name + (v.array_size ? "[" + std::to_string(v.array_size) + "]" : "") + ";\n";
}

}  // namespace

GlslPair compose_glsl(const GdShader& s, Pass pass) {
    GlslPair out;
    std::string common = decode_srgb(s.common, s);
    bool world = s.mode("world_vertex_coords");
    bool skip = s.mode("skip_vertex_transform");

    // ---- vertex stage ----
    std::ostringstream v;
    v << HEADER;
    v << "layout(location = 0) in vec3 t3_a_vertex;\nlayout(location = 1) in vec3 t3_a_normal;\n"
         "layout(location = 2) in vec4 t3_a_color;\nlayout(location = 3) in vec2 t3_a_uv;\n"
         "layout(location = 4) in vec2 t3_a_uv2;\nlayout(location = 5) in vec4 t3_a_custom0;\n"
         "layout(location = 6) in vec4 t3_a_custom1;\nlayout(location = 7) in vec4 t3_a_custom2;\n"
         "layout(location = 8) in vec4 t3_a_custom3;\nlayout(location = 9) in vec4 t3_a_inst0;\n"
         "layout(location = 10) in vec4 t3_a_inst1;\nlayout(location = 11) in vec4 t3_a_inst2;\n"
         "layout(location = 12) in vec4 t3_a_inst_color;\nlayout(location = 13) in vec4 t3_a_inst_custom;\n";
    v << "uniform mat4 t3_model;\nuniform mat4 t3_view;\nuniform mat4 t3_inv_view;\nuniform mat4 t3_projection;\n"
         "uniform bool t3_instanced;\nuniform vec2 t3_viewport_size;\n";
    v << "out vec3 t3_v_vertex;\nout vec3 t3_v_normal;\nout vec2 t3_v_uv;\nout vec2 t3_v_uv2;\nout vec4 t3_v_color;\n";
    for (const ShaderVarying& var : s.varyings)
        if (var.from_vertex) v << varying_decl(var, "out");
    v << "vec3 VERTEX;\nvec3 NORMAL;\nvec2 UV;\nvec2 UV2;\nvec4 COLOR;\nvec4 CUSTOM0;\nvec4 CUSTOM1;\nvec4 CUSTOM2;\nvec4 CUSTOM3;\n"
         "vec4 INSTANCE_CUSTOM;\nint INSTANCE_ID;\nint VERTEX_ID;\nvec4 POSITION;\nfloat POINT_SIZE;\n"
         "mat4 MODEL_MATRIX;\nmat3 MODEL_NORMAL_MATRIX;\nmat4 VIEW_MATRIX;\nmat4 INV_VIEW_MATRIX;\nmat4 MODELVIEW_MATRIX;\n"
         "mat3 MODELVIEW_NORMAL_MATRIX;\nmat4 PROJECTION_MATRIX;\nmat4 INV_PROJECTION_MATRIX;\nvec2 VIEWPORT_SIZE;\n";
    v << SRGB_HELPERS << common;
    v << "void t3_user_vertex() {" << decode_srgb(s.vertex, s) << "}\n";
    v << "void main() {\n"
         "\tVERTEX = t3_a_vertex;\n\tNORMAL = t3_a_normal;\n\tUV = t3_a_uv;\n\tUV2 = t3_a_uv2;\n\tCOLOR = t3_a_color;\n"
         "\tCUSTOM0 = t3_a_custom0;\n\tCUSTOM1 = t3_a_custom1;\n\tCUSTOM2 = t3_a_custom2;\n\tCUSTOM3 = t3_a_custom3;\n"
         "\tINSTANCE_CUSTOM = vec4(0.0);\n\tMODEL_MATRIX = t3_model;\n"
         "\tif (t3_instanced) {\n"
         "\t\tMODEL_MATRIX = t3_model * transpose(mat4(t3_a_inst0, t3_a_inst1, t3_a_inst2, vec4(0.0, 0.0, 0.0, 1.0)));\n"
         "\t\tCOLOR *= t3_a_inst_color;\n\t\tINSTANCE_CUSTOM = t3_a_inst_custom;\n\t}\n"
         "\tVIEW_MATRIX = t3_view;\n\tINV_VIEW_MATRIX = t3_inv_view;\n\tPROJECTION_MATRIX = t3_projection;\n"
         "\tINV_PROJECTION_MATRIX = inverse(t3_projection);\n\tVIEWPORT_SIZE = t3_viewport_size;\n"
         "\tMODELVIEW_MATRIX = VIEW_MATRIX * MODEL_MATRIX;\n\tMODEL_NORMAL_MATRIX = mat3(MODEL_MATRIX);\n"
         "\tMODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
         "\tINSTANCE_ID = gl_InstanceID;\n\tVERTEX_ID = gl_VertexID;\n\tPOSITION = vec4(0.0);\n\tPOINT_SIZE = 1.0;\n";
    if (world) v << "\tVERTEX = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;\n\tNORMAL = normalize(MODEL_NORMAL_MATRIX * NORMAL);\n";
    v << "\tt3_user_vertex();\n";
    if (!skip) {
        if (world)
            v << "\tVERTEX = (VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;\n\tNORMAL = normalize(mat3(VIEW_MATRIX) * NORMAL);\n";
        else
            v << "\tVERTEX = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;\n"
                 "\tNORMAL = normalize(mat3(MODELVIEW_MATRIX) * NORMAL);\n";
    }
    v << "\tt3_v_vertex = VERTEX;\n\tt3_v_normal = NORMAL;\n\tt3_v_uv = UV;\n\tt3_v_uv2 = UV2;\n\tt3_v_color = COLOR;\n";
    v << (s.writes_position ? "\tgl_Position = POSITION;\n" : "\tgl_Position = PROJECTION_MATRIX * vec4(VERTEX, 1.0);\n");
    v << "\tgl_PointSize = POINT_SIZE;\n}\n";
    out.vertex = v.str();

    // ---- fragment stage ----
    std::ostringstream f;
    f << HEADER;
    f << "in vec3 t3_v_vertex;\nin vec3 t3_v_normal;\nin vec2 t3_v_uv;\nin vec2 t3_v_uv2;\nin vec4 t3_v_color;\n";
    for (const ShaderVarying& var : s.varyings) {
        if (var.from_vertex) f << varying_decl(var, "in");
        else f << var.type << " " << var.name << (var.array_size ? "[" + std::to_string(var.array_size) + "]" : "") << ";\n";
    }
    f << "uniform mat4 t3_view;\nuniform mat4 t3_inv_view;\nuniform mat4 t3_projection;\nuniform vec2 t3_viewport_size;\n"
         "uniform int t3_dir_count;\nuniform vec3 t3_dir_dir[" << MAX_DIRECTIONAL << "];\nuniform vec3 t3_dir_color[" << MAX_DIRECTIONAL
      << "];\nuniform float t3_dir_specular[" << MAX_DIRECTIONAL << "];\nuniform int t3_dir_shadowed;\n"
         "uniform int t3_omni_count;\nuniform vec3 t3_omni_pos[" << MAX_OMNI << "];\nuniform vec3 t3_omni_color[" << MAX_OMNI
      << "];\nuniform float t3_omni_range[" << MAX_OMNI << "];\nuniform float t3_omni_atten[" << MAX_OMNI
      << "];\nuniform float t3_omni_specular[" << MAX_OMNI << "];\n"
         "uniform vec3 t3_ambient;\nuniform sampler2DShadow t3_shadow_map;\nuniform mat4 t3_shadow_matrix[" << SHADOW_CASCADES
      << "];\nuniform vec4 t3_shadow_split;\nuniform vec2 t3_shadow_texel;\nuniform vec4 t3_shadow_normal_bias;\n"
         "uniform vec4 t3_shadow_depth_bias;\nuniform float t3_shadow_fade;\nuniform bool t3_receive_shadows;\n"
         "uniform float t3_alpha_scale;\n";
    if (pass == Pass::COLOR) f << "layout(location = 0) out vec4 t3_frag_color;\n";
    f << "vec3 VERTEX;\nvec4 FRAGCOORD;\nbool FRONT_FACING;\nvec3 NORMAL;\nvec2 UV;\nvec2 UV2;\nvec4 COLOR;\nvec3 VIEW;\n"
         "vec2 SCREEN_UV;\nvec2 VIEWPORT_SIZE;\nmat4 VIEW_MATRIX;\nmat4 INV_VIEW_MATRIX;\nmat4 PROJECTION_MATRIX;\n"
         "mat4 INV_PROJECTION_MATRIX;\n"
         "vec3 ALBEDO = vec3(1.0);\nfloat ALPHA = 1.0;\nvec3 EMISSION = vec3(0.0);\nfloat ROUGHNESS = 1.0;\n"
         "float SPECULAR = 0.5;\nfloat METALLIC = 0.0;\nfloat AO = 1.0;\nfloat ALPHA_SCISSOR_THRESHOLD = 0.5;\n"
         "float ALPHA_ANTIALIASING_EDGE = 0.0;\nvec2 ALPHA_TEXTURE_COORDINATE = vec2(0.0);\n"
         "vec3 LIGHT;\nvec3 LIGHT_COLOR;\nfloat ATTENUATION;\nbool LIGHT_IS_DIRECTIONAL;\nvec3 DIFFUSE_LIGHT;\n"
         "vec3 SPECULAR_LIGHT;\nfloat SPECULAR_AMOUNT;\n";
    // texture() with a bias exists in the fragment stage only.
    f << SRGB_HELPERS
      << "vec4 t3_srgb_texture(sampler2D s, vec2 uv, float bias) { vec4 c = texture(s, uv, bias); "
         "return vec4(t3_srgb_to_linear(c.rgb), c.a); }\n"
      << common;
    f << "void t3_user_fragment() {" << decode_srgb(s.fragment, s) << "}\n";
    if (s.has_light) f << "void t3_user_light() {" << decode_srgb(s.light, s) << "}\n";
    f << DEFAULT_LIGHT;
    // The sun's shadow, in Godot's four cascades (one quadrant of the map each, the first whose
    // split lies beyond the point): the point pushed along the normal (less where it faces the
    // light, as Godot's normal bias), looked up with 9 taps of hardware comparison kept inside the
    // cascade's quadrant; faded out towards the shadow's maximum distance.
    f << "float t3_shadow() {\n"
         "\tif (!t3_receive_shadows) return 1.0;\n"
         "\tfloat depth = -t3_v_vertex.z;\n"
         "\tint k = depth < t3_shadow_split.x ? 0 : depth < t3_shadow_split.y ? 1 : depth < t3_shadow_split.z ? 2 : 3;\n"
         "\tfloat ndl = clamp(dot(normalize(t3_v_normal), LIGHT), 0.0, 1.0);\n"
         "\tvec3 p = t3_v_vertex + normalize(t3_v_normal) * t3_shadow_normal_bias[k] * (1.0 - ndl);\n"
         "\tvec4 c = t3_shadow_matrix[k] * vec4(p, 1.0);\n"
         "\tvec3 s = c.xyz / c.w;\n"
         "\tvec2 lo = vec2(float(k & 1), float(k >> 1)) * 0.5 + 1.5 * t3_shadow_texel;\n"
         "\tvec2 hi = lo + 0.5 - 3.0 * t3_shadow_texel;\n"
         "\tif (any(lessThan(s.xy, lo)) || any(greaterThan(s.xy, hi)) || s.z >= 1.0) return 1.0;\n"
         "\ts.z -= t3_shadow_depth_bias[k];\n"
         "\tfloat sum = 0.0;\n"
         "\tfor (int y = -1; y <= 1; ++y)\n"
         "\t\tfor (int x = -1; x <= 1; ++x)\n"
         "\t\t\tsum += texture(t3_shadow_map, vec3(s.xy + vec2(float(x), float(y)) * t3_shadow_texel, s.z));\n"
         "\tfloat lit = sum / 9.0;\n"
         "\treturn mix(lit, 1.0, smoothstep(t3_shadow_fade * 0.9, t3_shadow_fade, depth));\n}\n";
    f << "float t3_omni_attenuation(float d, float range, float decay) {\n"
         "\tfloat nd = d / range;\n\tnd *= nd;\n\tnd *= nd;\n\tnd = max(1.0 - nd, 0.0);\n\tnd *= nd;\n"
         "\treturn nd * pow(max(d, 0.0001), -decay);\n}\n";
    const char* light_call = s.has_light ? "t3_user_light();" : "t3_default_light();";
    f << "void main() {\n"
         "\tVERTEX = t3_v_vertex;\n\tFRAGCOORD = gl_FragCoord;\n\tFRONT_FACING = gl_FrontFacing;\n"
         "\tNORMAL = normalize(t3_v_normal);\n\tif (!gl_FrontFacing) NORMAL = -NORMAL;\n"
         "\tUV = t3_v_uv;\n\tUV2 = t3_v_uv2;\n\tCOLOR = t3_v_color;\n\tVIEW = normalize(-VERTEX);\n"
         "\tVIEWPORT_SIZE = t3_viewport_size;\n\tSCREEN_UV = gl_FragCoord.xy / t3_viewport_size;\n"
         "\tVIEW_MATRIX = t3_view;\n\tINV_VIEW_MATRIX = t3_inv_view;\n\tPROJECTION_MATRIX = t3_projection;\n"
         "\tINV_PROJECTION_MATRIX = inverse(t3_projection);\n"
         "\tt3_user_fragment();\n";
    if (s.alpha_scissor) f << "\tif (ALPHA < ALPHA_SCISSOR_THRESHOLD) discard;\n\tALPHA = 1.0;\n";
    if (pass == Pass::DEPTH) {
        f << "}\n";
        out.fragment = f.str();
        return out;
    }
    f << "\tvec3 colour;\n";
    if (s.mode("unshaded")) {
        f << "\tcolour = ALBEDO;\n";
    } else {
        f << "\tDIFFUSE_LIGHT = vec3(0.0);\n\tSPECULAR_LIGHT = vec3(0.0);\n"
             "\tfor (int i = 0; i < " << MAX_DIRECTIONAL << "; ++i) {\n"
             "\t\tif (i >= t3_dir_count) break;\n"
             "\t\tLIGHT = t3_dir_dir[i];\n\t\tLIGHT_COLOR = t3_dir_color[i] * PI;\n\t\tLIGHT_IS_DIRECTIONAL = true;\n"
             "\t\tSPECULAR_AMOUNT = t3_dir_specular[i];\n"
             "\t\tATTENUATION = i == t3_dir_shadowed ? t3_shadow() : 1.0;\n"
             "\t\t" << light_call << "\n\t}\n"
             "\tfor (int i = 0; i < " << MAX_OMNI << "; ++i) {\n"
             "\t\tif (i >= t3_omni_count) break;\n"
             "\t\tvec3 d = t3_omni_pos[i] - VERTEX;\n\t\tfloat dist = length(d);\n"
             "\t\tif (dist >= t3_omni_range[i]) continue;\n"
             "\t\tLIGHT = d / max(dist, 1e-5);\n\t\tLIGHT_COLOR = t3_omni_color[i] * PI;\n\t\tLIGHT_IS_DIRECTIONAL = false;\n"
             "\t\tSPECULAR_AMOUNT = t3_omni_specular[i];\n"
             "\t\tATTENUATION = t3_omni_attenuation(dist, t3_omni_range[i], t3_omni_atten[i]);\n"
             "\t\t" << light_call << "\n\t}\n";
        f << (s.mode("ambient_light_disabled") ? "\tvec3 ambient = vec3(0.0);\n" : "\tvec3 ambient = t3_ambient * ALBEDO * AO;\n");
        f << "\tcolour = EMISSION + ambient + DIFFUSE_LIGHT * ALBEDO + SPECULAR_LIGHT;\n";
    }
    bool blended = s.writes_alpha && !s.alpha_scissor;
    f << "\tt3_frag_color = vec4(colour, " << (blended ? "clamp(ALPHA * t3_alpha_scale, 0.0, 1.0)" : "t3_alpha_scale") << ");\n}\n";
    out.fragment = f.str();
    return out;
}

std::vector<float> parse_default(const ShaderUniform& u) {
    static const std::map<std::string, int> sizes = {{"float", 1}, {"int", 1}, {"bool", 1}, {"uint", 1},  {"vec2", 2},
                                                     {"vec3", 3},  {"vec4", 4}, {"ivec2", 2}, {"ivec3", 3}, {"ivec4", 4},
                                                     {"mat3", 9},  {"mat4", 16}};
    auto it = sizes.find(u.type);
    if (it == sizes.end()) return {};
    int n = it->second * std::max(u.array_size, 1);
    std::vector<float> out(n, 0.0f);
    std::string d = u.default_value;
    if (d.empty()) return out;
    if (d == "true") return std::vector<float>(n, 1.0f);
    if (d == "false") return out;
    std::vector<float> nums;
    static const std::regex number(R"([-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?)");
    for (std::sregex_iterator i(d.begin(), d.end(), number), e; i != e; ++i) {
        // Skip digits that belong to a type name (vec3, mat4).
        size_t at = static_cast<size_t>(i->position());
        if (at > 0 && std::isalpha(static_cast<unsigned char>(d[at - 1]))) continue;
        nums.push_back(std::stof(i->str()));
    }
    if (nums.empty()) return out;
    if (u.type == "mat3" || u.type == "mat4") {
        int k = u.type == "mat3" ? 3 : 4;
        if (nums.size() == 1) {
            for (int c = 0; c < k; ++c) out[c * k + c] = nums[0];
        } else {
            for (size_t i = 0; i < nums.size() && i < out.size(); ++i) out[i] = nums[i];
        }
        return out;
    }
    if (nums.size() == 1) return std::vector<float>(n, nums[0]);
    for (size_t i = 0; i < nums.size() && i < out.size(); ++i) out[i] = nums[i];
    return out;
}

// ---- compiling ------------------------------------------------------------------------------------

std::vector<std::string>& shader_errors() {
    static std::vector<std::string> errors;
    return errors;
}

namespace {

void note_shader_error(const std::string& message) {
    // The first line of the driver's message is enough to read on a screenshot.
    std::string first = message.substr(0, message.find('\n', message.find(": ") + 2));
    for (const std::string& e : shader_errors())
        if (e == first) return;
    shader_errors().push_back(first);
}

unsigned compile_stage(unsigned type, const std::string& text, const std::string& name) {
    // The numbered source goes to the log with the error (the device's log file shows the line).
    auto dump = [&] {
        std::istringstream in(text);
        std::string line, out;
        int n = 0;
        while (std::getline(in, line)) out += std::to_string(++n) + ": " + line + "\n";
        Log::info("gfx3d: the source of " + name + ":\n" + out);
    };
    unsigned sh = glCreateShader(type);
    const char* src = text.c_str();
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        Log::error(std::string("gfx3d: ") + (type == GL_VERTEX_SHADER ? "vertex" : "fragment") + " stage of " + name +
                   " does not compile:\n" + log);
        note_shader_error(name + (type == GL_VERTEX_SHADER ? " (vertex): " : " (fragment): ") + log);
        dump();
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

std::unique_ptr<Program> build(GdShader shader, Pass pass, const std::string& name) {
    GlslPair glsl = compose_glsl(shader, pass);
    unsigned vs = compile_stage(GL_VERTEX_SHADER, glsl.vertex, name);
    unsigned fs = vs ? compile_stage(GL_FRAGMENT_SHADER, glsl.fragment, name) : 0;
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        return nullptr;
    }
    auto p = std::make_unique<Program>();
    p->name = name + (pass == Pass::DEPTH ? " [depth]" : " [colour]");
    p->generation = gl_generation();
    p->id = glCreateProgram();
    glAttachShader(p->id, vs);
    glAttachShader(p->id, fs);
    gfx::gl_programs() += 1;
    glLinkProgram(p->id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(p->id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p->id, sizeof log, nullptr, log);
        Log::error("gfx3d: " + name + " does not link:\n" + log);
        note_shader_error(name + " (link): " + log);
        return nullptr;
    }
    p->shader = std::move(shader);
    auto loc = [&](const char* n) { return glGetUniformLocation(p->id, n); };
    p->model = loc("t3_model");
    p->view = loc("t3_view");
    p->inv_view = loc("t3_inv_view");
    p->projection = loc("t3_projection");
    p->instanced = loc("t3_instanced");
    p->viewport_size = loc("t3_viewport_size");
    p->dir_count = loc("t3_dir_count");
    p->dir_dir = loc("t3_dir_dir");
    p->dir_color = loc("t3_dir_color");
    p->dir_specular = loc("t3_dir_specular");
    p->dir_shadowed = loc("t3_dir_shadowed");
    p->omni_count = loc("t3_omni_count");
    p->omni_pos = loc("t3_omni_pos");
    p->omni_color = loc("t3_omni_color");
    p->omni_range = loc("t3_omni_range");
    p->omni_atten = loc("t3_omni_atten");
    p->omni_specular = loc("t3_omni_specular");
    p->ambient = loc("t3_ambient");
    p->shadow_map = loc("t3_shadow_map");
    p->shadow_matrix = loc("t3_shadow_matrix");
    p->shadow_split = loc("t3_shadow_split");
    p->shadow_texel = loc("t3_shadow_texel");
    p->shadow_normal_bias = loc("t3_shadow_normal_bias");
    p->shadow_depth_bias = loc("t3_shadow_depth_bias");
    p->shadow_fade = loc("t3_shadow_fade");
    p->receive_shadows = loc("t3_receive_shadows");
    p->alpha_scale = loc("t3_alpha_scale");
    glUseProgram(p->id);
    if (p->shadow_map >= 0) glUniform1i(p->shadow_map, SHADOW_UNIT);
    int unit = 0;
    for (const ShaderUniform& u : p->shader.uniforms) {
        Program::User user{&u, loc(u.glsl.c_str()), -1, parse_default(u)};
        if (u.type == "sampler2D" || u.as_texture) {
            user.unit = unit++;
            if (user.location >= 0) glUniform1i(user.location, user.unit);
        }
        p->user.push_back(user);
    }
    return p;
}

std::map<std::string, std::unique_ptr<Program>>& cache() {
    static std::map<std::string, std::unique_ptr<Program>> c;
    return c;
}

std::string shader_text(const std::string& path) {
    auto it = shader_sources().find(path);
    if (it == shader_sources().end()) throw std::runtime_error("no shader " + path);
    return it->second;
}

const Program* cached(const std::string& key, const std::function<std::unique_ptr<Program>()>& make) {
    auto& c = cache();
    auto it = c.find(key);
    if (it != c.end()) {
        if (it->second && it->second->generation != gl_generation()) c.erase(it);
        else return it->second.get();
    }
    std::unique_ptr<Program> p = make();
    const Program* r = p.get();
    c[key] = std::move(p);   // a failure is remembered too (null), so it is logged once
    return r;
}

}  // namespace

Program::~Program() {
    if (id && generation == gl_generation()) glDeleteProgram(id);
}

const Program* program(const std::string& path, const std::map<std::string, std::string>& defines, Pass pass) {
    std::string key = path + (pass == Pass::DEPTH ? "|depth" : "|color");
    for (const auto& [k, v] : defines) key += "|" + k + "=" + v;
    return cached(key, [&]() -> std::unique_ptr<Program> {
        try {
            std::map<std::string, std::string> d = base_defines();
            for (const auto& [k, v] : defines) d[k] = v;
            return build(parse_gdshader(shader_text(path), d, shader_text), pass, path);
        } catch (const std::exception& e) {
            Log::error(std::string("gfx3d: ") + path + ": " + e.what());
            return nullptr;
        }
    });
}

const Program* program_from_code(const std::string& code, Pass pass) {
    std::string key = std::string(pass == Pass::DEPTH ? "depth|" : "color|") + code;
    return cached(key, [&]() -> std::unique_ptr<Program> {
        try {
            return build(parse_gdshader(code, base_defines(), shader_text), pass, "StandardMaterial3D");
        } catch (const std::exception& e) {
            Log::error(std::string("gfx3d: StandardMaterial3D: ") + e.what());
            return nullptr;
        }
    });
}

void forget_programs() { cache().clear(); }

}  // namespace t3::gfx3d
