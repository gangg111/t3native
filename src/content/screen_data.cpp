// remake/content/screen_data.gd
#include "content/screen_data.h"

#include "content/json_file.h"
#include "gd/io.h"

namespace t3 {

namespace {
// `d.get(key, [a, b])` as ints.
gd::PI32 ints_or(const gd::Json& d, const std::string& key, gd::Int a, gd::Int b) {
    return d.has(key) ? JsonFile::ints(d[key]) : gd::PI32{a, b};
}
// `var x: int = d.get(key, fallback)` (Variant float -> int truncates).
gd::Int int_or(const gd::Json& d, const std::string& key, gd::Int fallback) {
    return d.has(key) ? d[key].to_int() : fallback;
}
// `d.get(key, [])` / `d.get(key, {})`: a missing key iterates as empty.
const std::vector<gd::Json>& items_of(const gd::Json& d, const std::string& key) {
    static const std::vector<gd::Json> none;
    return d.has(key) && d[key].is_array() ? d[key].items() : none;
}
gd::Json object_or_empty(const gd::Json& d, const std::string& key) {
    return d.has(key) ? d[key] : gd::Json::parse("{}");
}
// `str(v)` of a JSON value that is a string.
std::string str_of(const gd::Json& v) {
    return v.is_string() ? v.str() : gd::str(v.to_int());
}
// String.length(): code points of the UTF-8 text.
gd::Int length(const std::string& s) {
    gd::Int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}
// String.lstrip(" ").
std::string lstrip_spaces(const std::string& s) {
    size_t i = s.find_first_not_of(' ');
    return i == std::string::npos ? std::string() : s.substr(i);
}
// String.strip_edges(): without the whitespace and control characters at both ends.
std::string strip_edges(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= 32) ++a;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= 32) --b;
    return s.substr(a, b - a);
}
// Color.lerp.
gfx::Color lerp(const gfx::Color& a, const gfx::Color& b, float w) {
    return {a.r + (b.r - a.r) * w, a.g + (b.g - a.g) * w, a.b + (b.b - a.b) * w, a.a + (b.a - a.a) * w};
}
}  // namespace

gd::Ref<ScreenData> ScreenData::load_from(const std::string& dir) {
    gd::Json data = JsonFile::read(gd::path_join(dir, "screens.json"));
    gd::Ref<ScreenData> s = gd::make<ScreenData>();
    s->directory = dir;
    gd::PI32 size = ints_or(data, "screen", 368, 480);
    s->screen = gd::Vector2i(size[0], size[1]);
    for (const gd::Json& f : items_of(data, "fonts")) {
        gd::Int advance = f["advance"].to_int();
        gd::Int height = f["height"].to_int();
        s->font_advance.append(advance);
        s->font_height.append(height);
    }
    gd::Json presents = object_or_empty(data, "presents");
    gd::PI32 at = ints_or(presents, "at", 0, 0);
    gd::Int x = at[0];
    gd::Int presents_font = int_or(presents, "font", 0);
    for (const gd::Json& part : items_of(presents, "parts")) {
        gd::Int colour = part["colour"].to_int();
        gd::Ref<Text> t = _text(str_of(part["text"]), presents_font, colour, gd::Vector2i(x, at[1]));
        s->presents.append(t);
        x += length(t->text) * gd::Int(s->font_advance[t->font]);
    }
    gd::Json prompts = object_or_empty(data, "start_prompts");
    gd::Int prompt_font = int_or(prompts, "font", 0);
    gd::Int prompt_colour = int_or(prompts, "colour", 5);
    gd::Int centre = int_or(prompts, "centre_x", 184);
    gd::Int prompt_y = int_or(prompts, "y", 0);
    for (const gd::Json& p : items_of(prompts, "texts")) {
        s->prompts.append(_text(str_of(p["text"]), prompt_font, prompt_colour, gd::Vector2i(centre, prompt_y)));
    }
    for (const gd::Json& c : items_of(data, "copyright")) {
        gd::PI32 a = JsonFile::ints(c["at"]);
        gd::Int font = c["font"].to_int();
        gd::Int colour = c["colour"].to_int();
        s->copyright.append(_text(str_of(c["text"]), font, colour, gd::Vector2i(a[0], a[1])));
    }
    gd::PI32 logo = ints_or(data, "logo_at", 0, 0);
    s->logo_at = gd::Vector2i(logo[0], logo[1]);
    for (const gd::Json& font : items_of(data, "text_colours")) {
        gd::Arr<gd::Arr<gfx::Color>> palette;
        for (const gd::Json& entry : font.items()) {
            gd::Arr<gfx::Color> colours;
            for (const gd::Json& rgb : entry.items()) {
                gd::PI32 c = JsonFile::ints(rgb);
                colours.append(gfx::Color::c8(static_cast<int>(c[0]), static_cast<int>(c[1]), static_cast<int>(c[2])));
            }
            palette.append(colours);
        }
        s->colours.append(palette);
    }
    return s;
}

gd::Ref<ScreenData::Text> ScreenData::_text(const std::string& text, gd::Int font, gd::Int colour, gd::Vector2i at) {
    gd::Ref<Text> t = gd::make<Text>();
    t->text = text;
    t->font = font;
    t->colour = colour;
    t->at = at;
    return t;
}

// The centre x of a left-aligned original text run (the original font is fixed-pitch).
float ScreenData::run_centre(const gd::Ref<Text>& t) const {
    std::string text = t->text;
    gd::Int lead = length(text) - length(lstrip_spaces(text));
    gd::Int body = length(strip_edges(text));
    return static_cast<float>(gd::Int(t->at.x) + gd::Int(font_advance[t->font]) * (lead + body / 2.0));
}

// Fill and outline colours of a text colour: the glyph gradient's upper part, the outline.
gd::Arr<gfx::Color> ScreenData::text_colour(gd::Int font, gd::Int colour) const {
    gd::Arr<gfx::Color> entry = colours[font][colour];
    gfx::Color top = entry[0];
    gfx::Color bottom = entry[1];
    gfx::Color outline = entry[2];
    return gd::Arr<gfx::Color>{lerp(top, bottom, 0.25f), outline};
}

}  // namespace t3
