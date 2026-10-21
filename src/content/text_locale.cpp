// remake/content/text_locale.gd
#include "content/text_locale.h"

#include <cstdio>

#include "content/json_file.h"
#include "gd/io.h"

namespace t3 {

namespace {
// `str(v)` of a JSON value the converter writes as a string.
std::string jstr(const gd::Json& v) { return v.is_string() ? v.str() : std::string(); }

void write_string(std::string& out, const std::string& s) {
    out += '"';
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += ch;
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else {
            out += ch;
        }
    }
    out += '"';
}

// gd::Json has no public constructors: the localized copy of a value is written as JSON text
// (each string through `text`) and parsed back. Numbers keep every bit (%.17g).
void write_localized(std::string& out, const TextLocale& t, const std::string& block, const gd::Json& v) {
    switch (v.type()) {
        case gd::Json::Type::Null: out += "null"; break;
        case gd::Json::Type::Bool: out += v.boolean() ? "true" : "false"; break;
        case gd::Json::Type::Number: {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.17g", v.num());
            out += buf;
            break;
        }
        case gd::Json::Type::String: write_string(out, t.text(block, v.str())); break;
        case gd::Json::Type::Array: {
            out += '[';
            bool first = true;
            for (const gd::Json& e : v.items()) {
                if (!first) out += ',';
                first = false;
                write_localized(out, t, block, e);
            }
            out += ']';
            break;
        }
        case gd::Json::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, e] : v.members()) {
                if (!first) out += ',';
                first = false;
                write_string(out, k);
                out += ':';
                write_localized(out, t, block, e);
            }
            out += '}';
            break;
        }
    }
}
}  // namespace

gd::Ref<TextLocale> TextLocale::load_from(const std::string& imported_root) {
    auto t = gd::make<TextLocale>();
    t->theater_help.resize(THEATER_LINES);
    t->theater_help.fill("");
    // res://content/...: `res://` is the folder that holds `imported/` (the remake's project folder;
    // the APK's assets on Android).
    std::string res_root = imported_root;
    while (!res_root.empty() && (res_root.back() == '/' || res_root.back() == '\\')) res_root.pop_back();
    size_t cut = res_root.find_last_of("/\\:");
    res_root = cut == std::string::npos ? "" : res_root.substr(0, cut + (res_root[cut] == ':' ? 1 : 0));
    t->_merge(JsonFile::read(res_root.empty() || res_root.back() == ':' ? res_root + STATIC_PATH : gd::path_join(res_root, STATIC_PATH)));
    std::string dir = gd::path_join(imported_root, USA_DIR);
    if (gd::file_exists(gd::path_join(dir, "texts.json"))) {
        t->usa_dir = dir;
        gd::Json usa = JsonFile::read(gd::path_join(dir, "texts.json"));
        t->_merge(usa);
        gd::Arr<std::string> titles;
        for (const gd::Json& v : usa.get("staff_titles").items()) titles.append(jstr(v));
        t->staff_titles = titles;
        gd::Arr<std::string> rows;
        for (const gd::Json& v : usa.get("staff_rows").items()) rows.append(jstr(v));
        t->staff_rows = rows;
    }
    return t;
}

void TextLocale::_merge(const gd::Json& d) {
    const gd::Json& more = d.get("blocks");
    for (const auto& [block, table] : more.members()) {
        if (!blocks.has(block)) {
            blocks.set(block, gd::Dict<std::string, std::string>());
        }
        // Dictionary.merge(more[block], true): overwrite existing keys.
        gd::Dict<std::string, std::string> target = blocks.at(block);
        for (const auto& [k, v] : table.members()) target.set(k, jstr(v));
    }
    const gd::Json& help = d.get("theater_help");
    for (gd::Int i = 0; i < gd::mini(help.size(), THEATER_LINES); ++i) {
        if (!jstr(help[i]).empty()) {
            theater_help[i] = jstr(help[i]);
        }
    }
    practice_prompt = d.has("practice_prompt") ? jstr(d["practice_prompt"]) : practice_prompt;
    record_rank = d.has("record_rank") ? jstr(d["record_rank"]) : record_rank;
}

std::string TextLocale::text(const std::string& block, const std::string& s) const {
    if (!english) {
        return s;
    }
    gd::Dict<std::string, std::string> table = blocks.get(block, gd::Dict<std::string, std::string>());
    if (table.is_empty()) {
        return s;
    }
    gd::Int i = 0;
    gd::Int length = static_cast<gd::Int>(s.size());
    while (i + 1 < length && s[i] == '%' && _is_letter(static_cast<unsigned char>(s[i + 1]))) {
        i += 2;
    }
    std::string key = s.substr(static_cast<size_t>(i));
    return table.has(key) ? s.substr(0, static_cast<size_t>(i)) + table.at(key) : s;
}

gd::Json TextLocale::texts(const std::string& block, const gd::Json& value) const {
    std::string out;
    write_localized(out, *this, block, value);
    return gd::Json::parse(out);
}

std::string TextLocale::theater_line(gd::Int k) const {
    return english && k >= 0 && k < theater_help.size() ? theater_help[k] : "";
}

std::string TextLocale::usa_file(const std::string& name) const {
    if (!english || usa_dir.empty()) {
        return "";
    }
    std::string path = gd::path_join(usa_dir, name);
    // `FileAccess.file_exists(path) or ResourceLoader.exists(path)`: no resource loader natively.
    return gd::file_exists(path) ? path : "";
}

std::string TextLocale::file_or(const std::string& name, const std::string& fallback) const {
    std::string english = usa_file(name);   // shadows the member, as in the GDScript
    return !english.empty() ? english : fallback;
}

bool TextLocale::_is_letter(gd::Int c) {
    return (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A);
}

}  // namespace t3
