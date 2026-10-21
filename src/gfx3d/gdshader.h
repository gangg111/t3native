#pragma once
// A Godot shader (`.gdshader`, spatial) read into the parts the renderer needs to build GLSL ES
// 3.00 from it (shader_program.h), so the remake's shaders are used as written:
// - `#include`, and `#ifdef` / `#ifndef` / `#if A == B` / `#else` / `#endif` evaluated here with the
//   defines given (CRISP, CURRENT_RENDERER ...): the result holds no conditionals;
// - `shader_type` and `render_mode`;
// - each `uniform` with its hints (source_color, filter_*, repeat_*) and default value removed from
//   the text and kept as metadata (GLSL ES has neither);
// - each `varying`, with its interpolation qualifier and whether vertex() writes it (a varying only
//   fragment() and light() use is a plain global of the fragment stage);
// - the bodies of vertex(), fragment() and light(); the rest (constants, helper functions) is
//   common to both stages;
// - identifiers that are reserved words in GLSL ES 3.x or desktop GLSL (a Godot shader may name a
//   variable `shared`: Godot prefixes every user identifier) renamed with RESERVED_PREFIX, since
//   phone drivers reject them where a lenient compiler takes them.
inline const char* RESERVED_PREFIX = "t3r_";
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace t3::gfx3d {

struct ShaderUniform {
    enum class Filter { DEFAULT, NEAREST, LINEAR, NEAREST_MIPMAP, LINEAR_MIPMAP, LINEAR_MIPMAP_ANISOTROPIC };
    std::string type;               // GLSL type: float, vec4, sampler2D, mat3 ...
    std::string name;               // the shader's name (set_shader_parameter's key)
    std::string glsl;               // its name in the GLSL (a reserved word renamed), for the location
    int array_size = 0;             // 0: not an array
    std::string default_value;      // the GLSL expression after `=`, "" when none
    bool source_color = false;      // sampled colours are sRGB: decoded to linear
    Filter filter = Filter::DEFAULT;
    int repeat = -1;                // -1 default, 0 repeat_disable, 1 repeat_enable
};

struct ShaderVarying {
    std::string qualifier;          // "", "flat", "smooth"
    std::string type;
    std::string name;
    int array_size = 0;
    bool from_vertex = false;       // vertex() writes it: an interpolant
};

struct GdShader {
    std::string type;                       // "spatial"
    std::set<std::string> render_modes;
    std::vector<ShaderUniform> uniforms;
    std::vector<ShaderVarying> varyings;
    std::map<std::string, int> int_constants;   // `const int NAME = N;` (array sizes)
    std::string common;                     // constants and functions of both stages
    std::string vertex, fragment, light;    // the bodies, without their braces ("" when absent)
    bool has_vertex = false, has_fragment = false, has_light = false;
    bool writes_alpha = false;              // fragment() assigns ALPHA
    bool alpha_scissor = false;             // fragment() assigns ALPHA_SCISSOR_THRESHOLD
    bool writes_position = false;           // vertex() assigns POSITION

    bool mode(const std::string& m) const { return render_modes.count(m) != 0; }
    const ShaderUniform* uniform(const std::string& name) const;
};

// The text of an `#include`d path ("res://..."), or throws when unknown.
using IncludeReader = std::function<std::string(const std::string& path)>;

// Parses `source`; `defines` maps macro names to values ("" for a bare #define). Throws
// std::runtime_error with the line on anything it cannot read.
GdShader parse_gdshader(const std::string& source, const std::map<std::string, std::string>& defines,
                        const IncludeReader& include);

}  // namespace t3::gfx3d
