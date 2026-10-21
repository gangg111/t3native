// gfx::Font's metrics against FreeType's (measured with Pillow's ImageFont on the same TTF,
// 2026-10-21), and its UTF-8 decoding and line breaking by hand.
#include <cmath>

#include "gfx/font.h"
#include "support/test_suite.h"

using t3::gfx::Font;

T3_TEST(font, metrics_match_freetype) {
    auto f = Font::fallback();
    // source: Pillow ImageFont.truetype(OpenSans-SemiBold.ttf, s).getmetrics(): (18, 5) and (52, 15)
    t.expect_equal(f->get_ascent(16), 18.0f, "ascent 16");
    t.expect_equal(f->get_descent(16), 5.0f, "descent 16");
    t.expect_equal(f->get_ascent(48), 52.0f, "ascent 48");
    t.expect_equal(f->get_descent(48), 15.0f, "descent 48");
    t.expect_equal(f->get_height(16), 23.0f, "height 16");
}

T3_TEST(font, string_width_matches_freetype) {
    auto f = Font::fallback();
    // source: Pillow getlength("Tekken 3"): 68 at 16 px, 209 at 48 px (whole-pixel advances there,
    // fractional here: within a pixel per glyph)
    t.expect(std::fabs(f->get_string_size("Tekken 3", t3::gfx::HorizontalAlignment::LEFT, -1, 16).x - 68.0f) <= 2.0f, "16 px");
    t.expect(std::fabs(f->get_string_size("Tekken 3", t3::gfx::HorizontalAlignment::LEFT, -1, 48).x - 209.0f) <= 3.0f, "48 px");
}

T3_TEST(font, utf8) {
    // source: "Aé€😀" encoded by hand: 41, C3 A9, E2 82 AC, F0 9F 98 80
    auto cps = Font::codepoints("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    t.expect(cps.size() == 4 && cps[0] == U'A' && cps[1] == 0xE9 && cps[2] == 0x20AC && cps[3] == 0x1F600, "decoded");
    t.expect(Font::codepoints("\xFF")[0] == 0xFFFD, "invalid byte");
}

T3_TEST(font, wrap) {
    auto f = Font::fallback();
    // source: by hand: "\n" always breaks; a width of one word keeps one word a line
    auto lines = f->wrap("one two\nthree", -1, 16);
    t.expect(lines.size() == 2 && lines[0] == "one two" && lines[1] == "three", "mandatory breaks");
    float word = f->get_string_size("three", t3::gfx::HorizontalAlignment::LEFT, -1, 16).x;
    auto narrow = f->wrap("one two three", word + 1, 16);
    t.expect(narrow.size() == 3 && narrow[2] == "three", "word breaks");
}
