#pragma once
// GLSL ES 3.00 programs from Godot spatial shaders (gdshader.h): the vertex and fragment stages
// around the shader's vertex(), fragment() and light() with Godot's built-ins and its semantics:
// - vertex(): VERTEX, NORMAL in model space (world with world_vertex_coords), transformed to view
//   space by MODELVIEW_MATRIX afterwards unless skip_vertex_transform;
// - fragment(): VERTEX, NORMAL, VIEW in view space, NORMAL flipped on back faces; ALPHA_SCISSOR_THRESHOLD
//   discards below it and makes the surface opaque;
// - lighting: per light LIGHT (view space, towards the light), LIGHT_COLOR = colour (linear) x energy
//   x PI, ATTENUATION (shadow for the sun, distance for omni lights), the shader's light() or Godot's
//   default (Burley diffuse, Schlick-GGX specular); then EMISSION + ambient x ALBEDO (unless
//   ambient_light_disabled) + DIFFUSE_LIGHT x ALBEDO + SPECULAR_LIGHT; unshaded: ALBEDO;
// - `source_color` samplers decoded from sRGB in the shader (the textures are plain RGBA8).
// The output is linear; the renderer encodes it to sRGB at the end (Godot's linear tone mapper).
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gfx3d/gdshader.h"

namespace t3::gfx3d {

enum class Pass { COLOR, DEPTH };

// Lights a draw can take (forward rendering).
constexpr int MAX_DIRECTIONAL = 4;
constexpr int MAX_OMNI = 8;
// Texture unit of the sun's shadow map; the shader's samplers take units from 0.
constexpr int SHADOW_UNIT = 15;
// The sun's cascades (Godot's PSSM 4 splits), each a quadrant of the shadow map.
constexpr int SHADOW_CASCADES = 4;

struct GlslPair {
    std::string vertex;
    std::string fragment;
};

// The two stages' source for `s` (pure text: the tests read it).
GlslPair compose_glsl(const GdShader& s, Pass pass);

// A uniform's default value as numbers (`vec3(0.5, 1.0, 0.0)`, `-1`, `mat3(1.0)`), filled out to
// the type's size; empty for samplers or no default.
std::vector<float> parse_default(const ShaderUniform& u);

struct Program {
    unsigned id = 0;
    unsigned generation = 0;
    std::string name;                // the shader and pass, for the log
    GdShader shader;
    struct User {
        const ShaderUniform* uniform;
        int location;
        int unit;                    // samplers: texture unit
        std::vector<float> fallback; // the default value
        // An array read from a texture (ShaderUniform::as_texture): its RGBA32F texture, made by the
        // renderer for this GL context (`texture_generation`).
        mutable unsigned texture = 0;
        mutable unsigned texture_generation = 0;
    };
    std::vector<User> user;
    // Built-in uniforms' locations (-1 when the stage does not use them).
    int model = -1, view = -1, inv_view = -1, projection = -1, instanced = -1, viewport_size = -1;
    int dir_count = -1, dir_dir = -1, dir_color = -1, dir_specular = -1, dir_shadowed = -1;
    int omni_count = -1, omni_pos = -1, omni_color = -1, omni_range = -1, omni_atten = -1, omni_specular = -1;
    int ambient = -1, shadow_map = -1, shadow_matrix = -1, shadow_texel = -1, shadow_normal_bias = -1, shadow_fade = -1;
    int shadow_depth_bias = -1, receive_shadows = -1, shadow_split = -1;
    int alpha_scale = -1;
    ~Program();
};

// The program of a `res://` shader with `defines` for `pass`, compiled once (null and logged when it
// fails to parse or compile).
const Program* program(const std::string& path, const std::map<std::string, std::string>& defines, Pass pass);
// The program of generated shader text (StandardMaterial3D), cached by the text.
const Program* program_from_code(const std::string& code, Pass pass);
// Forgets every program (the GL context was lost).
void forget_programs();
// The shaders that failed to compile or link so far (name, stage and the driver's first line):
// the app shows them over the game, so a device's failure is seen on a screenshot.
std::vector<std::string>& shader_errors();

// Native: the folder of the linked programs' binaries (glGetProgramBinary), "" for none: a program
// whose sources and driver were seen before is taken from there instead of compiled (the first
// fight's shaders cost a hundred milliseconds on a phone). The app sets it to user://shaders.
std::string& program_cache_dir();

// The defines every shader is read with (Godot's renderer macros: the Forward+ paths).
const std::map<std::string, std::string>& base_defines();

}  // namespace t3::gfx3d
