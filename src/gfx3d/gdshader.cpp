#include "gfx3d/gdshader.h"

#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace t3::gfx3d {

const ShaderUniform* GdShader::uniform(const std::string& name) const {
    for (const ShaderUniform& u : uniforms)
        if (u.name == name) return &u;
    return nullptr;
}

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("gdshader: " + what); }

// ---- the preprocessor ----------------------------------------------------------------------------

struct Preprocessor {
    std::map<std::string, std::string> defines;
    const IncludeReader& include;
    int depth = 0;

    // A token's value: a number, or a macro's value (resolved again).
    long value(const std::string& token, int guard = 0) {
        std::string t = trim(token);
        if (t.empty()) fail("empty #if operand");
        if (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-') return std::stol(t);
        auto it = defines.find(t);
        if (it == defines.end()) fail("#if uses an unknown macro " + t);
        if (guard > 8) fail("#if macro loop at " + t);
        return it->second.empty() ? 1 : value(it->second, guard + 1);
    }

    bool condition(const std::string& expr) {
        static const std::regex binary(R"(^\s*(\w+)\s*(==|!=)\s*(\w+)\s*$)");
        std::smatch m;
        if (std::regex_match(expr, m, binary)) {
            long a = value(m[1]), b = value(m[3]);
            return m[2] == "==" ? a == b : a != b;
        }
        return value(expr) != 0;
    }

    std::string run(const std::string& source) {
        if (++depth > 16) fail("#include nesting too deep");
        std::ostringstream out;
        std::istringstream in(source);
        std::string line;
        // Per open conditional: whether this branch is active and whether one was taken.
        struct Frame { bool active; bool taken; bool parent; };
        std::vector<Frame> stack;
        auto active = [&] { return stack.empty() || stack.back().active; };
        while (std::getline(in, line)) {
            std::string t = trim(line);
            if (starts_with(t, "#ifdef") || starts_with(t, "#ifndef") || starts_with(t, "#if ")) {
                bool parent = active();
                bool c = false;
                if (parent) {
                    if (starts_with(t, "#ifdef")) c = defines.count(trim(t.substr(6))) != 0;
                    else if (starts_with(t, "#ifndef")) c = defines.count(trim(t.substr(7))) == 0;
                    else c = condition(t.substr(3));
                }
                stack.push_back({parent && c, c, parent});
                out << '\n';
                continue;
            }
            if (starts_with(t, "#else")) {
                if (stack.empty()) fail("#else without #if");
                Frame& f = stack.back();
                f.active = f.parent && !f.taken;
                f.taken = true;
                out << '\n';
                continue;
            }
            if (starts_with(t, "#endif")) {
                if (stack.empty()) fail("#endif without #if");
                stack.pop_back();
                out << '\n';
                continue;
            }
            if (!active()) {
                out << '\n';
                continue;
            }
            if (starts_with(t, "#include")) {
                size_t a = t.find('"'), b = t.rfind('"');
                if (a == std::string::npos || b <= a) fail("bad #include: " + t);
                out << run(include(t.substr(a + 1, b - a - 1))) << '\n';
                continue;
            }
            if (starts_with(t, "#define")) {
                std::istringstream d(t.substr(7));
                std::string name, rest;
                d >> name;
                std::getline(d, rest);
                defines[name] = trim(rest);
                out << line << '\n';
                continue;
            }
            out << line << '\n';
        }
        if (!stack.empty()) fail("unterminated #if");
        --depth;
        return out.str();
    }
};

// Comments removed, newlines kept (so braces or words in comments do not count).
std::string strip_comments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            if (i < s.size()) out += '\n';
        } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') out += '\n';
                ++i;
            }
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// GLSL ES 3.00 to 3.20 and desktop GLSL keywords and reserved words a Godot shader may use as names
// (Godot's own language has none of them).
const char* const RESERVED[] = {
    "shared",   "sample",    "patch",     "common",      "partition", "active",    "filter",     "input",
    "output",   "buffer",    "coherent",  "volatile",    "restrict",  "readonly",  "writeonly",  "resource",
    "noperspective", "subroutine", "superp", "half",     "fixed",     "unsigned",  "long",       "short",
    "double",   "union",     "enum",      "typedef",     "template",  "this",      "goto",       "inline",
    "noinline", "public",    "static",    "extern",      "external",  "interface", "class",      "namespace",
    "using",    "sizeof",    "cast",      "asm",         "precise",   "attribute", "atomic_uint", "centroid",
    "layout",   "invariant", "subroutine", "image2D",    "imageCube", "dmat2",     "dmat3",      "dmat4"};

std::string rename_reserved(std::string text) {
    for (const char* word : RESERVED) {
        if (text.find(word) == std::string::npos) continue;
        text = std::regex_replace(text, std::regex(std::string("\\b") + word + "\\b"), std::string(RESERVED_PREFIX) + word);
    }
    return text;
}

bool mentions(const std::string& body, const std::string& word) {
    return std::regex_search(body, std::regex("\\b" + word + "\\b"));
}

// `name =` (not `==`), `name +=` and the like.
bool assigns(const std::string& body, const std::string& word) {
    return std::regex_search(body, std::regex("\\b" + word + "\\b\\s*(\\[[^\\]]*\\])?\\s*([-+*/]?=)(?!=)"));
}

ShaderUniform::Filter filter_of(const std::string& hint) {
    if (hint == "filter_nearest") return ShaderUniform::Filter::NEAREST;
    if (hint == "filter_linear") return ShaderUniform::Filter::LINEAR;
    if (hint == "filter_nearest_mipmap") return ShaderUniform::Filter::NEAREST_MIPMAP;
    if (hint == "filter_linear_mipmap") return ShaderUniform::Filter::LINEAR_MIPMAP;
    if (hint == "filter_linear_mipmap_anisotropic" || hint == "filter_nearest_mipmap_anisotropic")
        return ShaderUniform::Filter::LINEAR_MIPMAP_ANISOTROPIC;
    return ShaderUniform::Filter::DEFAULT;
}

int array_size(GdShader& s, const std::string& token) {
    if (token.empty()) return 0;
    if (std::isdigit(static_cast<unsigned char>(token[0]))) return std::stoi(token);
    auto it = s.int_constants.find(token);
    if (it == s.int_constants.end()) fail("array size " + token + " is not a known int constant");
    return it->second;
}

void statement(GdShader& s, const std::string& raw) {
    std::string st = trim(raw);
    if (st.empty()) return;
    static const std::regex shader_type(R"(^shader_type\s+(\w+)$)");
    static const std::regex render_mode(R"(^render_mode\s+(.+)$)");
    static const std::regex uniform(
        R"(^uniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*(?::\s*([^=]+?))?\s*(?:=\s*([\s\S]+))?$)");
    static const std::regex varying(R"(^varying\s+(?:(flat|smooth)\s+)?(\w+)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?$)");
    static const std::regex int_const(R"(^const\s+int\s+(\w+)\s*=\s*(-?\d+)$)");
    std::smatch m;
    if (std::regex_match(st, m, shader_type)) {
        s.type = m[1];
        return;
    }
    if (std::regex_match(st, m, render_mode)) {
        std::string modes = m[1];
        std::stringstream ss(modes);
        std::string mode;
        while (std::getline(ss, mode, ',')) s.render_modes.insert(trim(mode));
        return;
    }
    if (starts_with(st, "global uniform") || starts_with(st, "instance uniform")) fail("unsupported: " + st);
    if (std::regex_match(st, m, uniform)) {
        ShaderUniform u;
        u.type = m[1];
        u.glsl = m[2];
        u.name = starts_with(u.glsl, RESERVED_PREFIX) ? u.glsl.substr(std::string(RESERVED_PREFIX).size()) : u.glsl;
        u.array_size = array_size(s, m[3]);
        u.default_value = trim(m[5]);
        std::stringstream hints(m[4]);
        std::string hint;
        while (std::getline(hints, hint, ',')) {
            hint = trim(hint);
            if (hint.empty()) continue;
            if (hint == "source_color") u.source_color = true;
            else if (hint == "repeat_enable") u.repeat = 1;
            else if (hint == "repeat_disable") u.repeat = 0;
            else if (starts_with(hint, "filter_")) u.filter = filter_of(hint);
            // Other hints (hint_range, hint_default_white ...) only shape the editor.
        }
        s.uniforms.push_back(u);
        s.common += "uniform " + u.type + " " + u.glsl + (u.array_size ? "[" + std::to_string(u.array_size) + "]" : "") + ";\n";
        return;
    }
    if (std::regex_match(st, m, varying)) {
        ShaderVarying v;
        v.qualifier = m[1];
        v.type = m[2];
        v.name = m[3];
        v.array_size = array_size(s, m[4]);
        s.varyings.push_back(v);
        return;
    }
    if (std::regex_match(st, m, int_const)) s.int_constants[m[1]] = std::stoi(m[2]);
    s.common += st + ";\n";
}

}  // namespace

GdShader parse_gdshader(const std::string& source, const std::map<std::string, std::string>& defines,
                        const IncludeReader& include) {
    Preprocessor pre{defines, include};
    std::string text = rename_reserved(strip_comments(pre.run(source)));
    GdShader s;
    static const std::regex stage_function(R"(^\s*void\s+(vertex|fragment|light|sky)\s*\(\s*\)\s*$)");
    std::string stmt;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        // A preprocessor line at the top level (a #define the code uses): common to both stages.
        if (c == '#' && trim(stmt).empty()) {
            size_t e = text.find('\n', i);
            if (e == std::string::npos) e = text.size();
            s.common += text.substr(i, e - i) + "\n";
            i = e;
            continue;
        }
        if (c == ';') {
            statement(s, stmt);
            stmt.clear();
            ++i;
            continue;
        }
        if (c == '{') {
            // A function (or struct) body: up to the matching brace.
            int depth = 0;
            size_t j = i;
            for (; j < text.size(); ++j) {
                if (text[j] == '{') ++depth;
                else if (text[j] == '}' && --depth == 0) break;
            }
            if (j >= text.size()) fail("unbalanced braces");
            std::string body = text.substr(i + 1, j - i - 1);
            std::smatch m;
            if (std::regex_match(stmt, m, stage_function)) {
                std::string name = m[1];
                if (name == "vertex") s.vertex = body, s.has_vertex = true;
                else if (name == "fragment") s.fragment = body, s.has_fragment = true;
                else if (name == "light") s.light = body, s.has_light = true;
                else fail("unsupported stage function " + name + "()");
                stmt.clear();
                i = j + 1;
            } else if (std::regex_search(stmt, std::regex(R"(^\s*struct\b)"))) {
                // `struct X { ... };`: the statement ends at its semicolon.
                stmt += text.substr(i, j - i + 1);
                i = j + 1;
            } else {
                s.common += trim(stmt) + " {" + body + "}\n";
                stmt.clear();
                i = j + 1;
            }
            continue;
        }
        stmt += c;
        ++i;
    }
    if (!trim(stmt).empty()) fail("text after the last statement: " + trim(stmt));
    if (s.type.empty()) fail("no shader_type");
    s.writes_alpha = assigns(s.fragment, "ALPHA");
    s.alpha_scissor = assigns(s.fragment, "ALPHA_SCISSOR_THRESHOLD");
    s.writes_position = assigns(s.vertex, "POSITION");
    for (ShaderVarying& v : s.varyings) v.from_vertex = mentions(s.vertex, v.name);
    for (ShaderUniform& u : s.uniforms) {
        if (u.type != "vec4" || u.array_size < TEXTURE_ARRAY_MIN) continue;
        u.as_texture = true;
        std::string decl = "uniform vec4 " + u.glsl + "[" + std::to_string(u.array_size) + "];\n";
        size_t at = s.common.find(decl);
        if (at != std::string::npos) s.common.replace(at, decl.size(), "uniform highp sampler2D " + u.glsl + ";\n");
        for (std::string* code : {&s.common, &s.vertex, &s.fragment, &s.light}) *code = array_reads_as_fetches(*code, u.glsl);
    }
    return s;
}

std::string array_reads_as_fetches(const std::string& code, const std::string& name) {
    std::string out;
    size_t i = 0;
    while (i < code.size()) {
        size_t at = code.find(name, i);
        if (at == std::string::npos) break;
        bool word_start = at == 0 || !(std::isalnum(static_cast<unsigned char>(code[at - 1])) || code[at - 1] == '_');
        size_t k = at + name.size();
        while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
        bool word_end = k >= code.size() || !(std::isalnum(static_cast<unsigned char>(code[at + name.size()])) || code[at + name.size()] == '_');
        if (!word_start || !word_end || k >= code.size() || code[k] != '[') {
            out += code.substr(i, at + name.size() - i);
            i = at + name.size();
            continue;
        }
        int depth = 0;
        size_t j = k;
        for (; j < code.size(); ++j) {
            if (code[j] == '[') ++depth;
            else if (code[j] == ']' && --depth == 0) break;
        }
        if (j >= code.size()) fail("unbalanced brackets after " + name);
        out += code.substr(i, at - i) + "texelFetch(" + name + ", ivec2(int(" + code.substr(k + 1, j - k - 1) + "), 0), 0)";
        i = j + 1;
    }
    return out + code.substr(i);
}

}  // namespace t3::gfx3d
