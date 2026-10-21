#pragma once
// Godot's Font for the views that draw text with it (ThemeDB.fallback_font, FontVariation):
// get_ascent / get_descent / get_height, get_string_size, draw_string, draw_string_outline and
// draw_multiline_string, with Godot's meaning of their arguments (the position is the left end of
// the baseline; `width` with an alignment places the text within it; sizes are pixels per em).
// The face is Open Sans SemiBold (third_party/fonts, the face of Godot's default theme), built into
// the program. Glyphs are rendered once as signed distance fields (stb_truetype) into one atlas and
// drawn at any size by the canvas (Canvas::set_sdf), so emboldening and outlines are thresholds.
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gfx/canvas.h"

namespace t3::gfx {

enum class HorizontalAlignment { LEFT, CENTER, RIGHT, FILL };

class Font {
public:
    static constexpr int DEFAULT_SIZE = 16;   // Godot's default theme font size

    // ThemeDB.fallback_font.
    static std::shared_ptr<Font> fallback();
    // A FontVariation of this face with `variation_embolden` = `strength` (Godot: the outline moves
    // out by strength x size / 32 pixels on each side).
    std::shared_ptr<Font> with_embolden(float strength) const;

    float get_ascent(int font_size = DEFAULT_SIZE) const;
    float get_descent(int font_size = DEFAULT_SIZE) const;
    float get_height(int font_size = DEFAULT_SIZE) const { return get_ascent(font_size) + get_descent(font_size); }
    // (width, height) of one line; with `width` > 0 and FILL the width is `width`.
    Vec2 get_string_size(const std::string& text, HorizontalAlignment alignment = HorizontalAlignment::LEFT, float width = -1,
                         int font_size = DEFAULT_SIZE) const;
    void draw_string(Canvas& canvas, Vec2 pos, const std::string& text, HorizontalAlignment alignment = HorizontalAlignment::LEFT,
                     float width = -1, int font_size = DEFAULT_SIZE, const Color& modulate = Color::white()) const;
    // The text's outline `size` wide (the shape grown by size / 4, as Godot draws it), in `modulate`.
    void draw_string_outline(Canvas& canvas, Vec2 pos, const std::string& text,
                             HorizontalAlignment alignment = HorizontalAlignment::LEFT, float width = -1,
                             int font_size = DEFAULT_SIZE, int size = 1, const Color& modulate = Color::white()) const;
    // Lines broken at "\n" and, with `width` > 0, between words to fit it (Godot's default
    // BREAK_MANDATORY | BREAK_WORD_BOUND); `max_lines` < 0: all. Each line is a get_height() lower.
    void draw_multiline_string(Canvas& canvas, Vec2 pos, const std::string& text,
                               HorizontalAlignment alignment = HorizontalAlignment::LEFT, float width = -1,
                               int font_size = DEFAULT_SIZE, int max_lines = -1, const Color& modulate = Color::white()) const;
    // The lines draw_multiline_string draws.
    std::vector<std::string> wrap(const std::string& text, float width, int font_size) const;

    // UTF-8 to code points (invalid bytes as U+FFFD).
    static std::vector<char32_t> codepoints(const std::string& text);

    struct Face;

private:
    void _draw(Canvas& canvas, Vec2 pos, const std::string& text, HorizontalAlignment alignment, float width, int font_size,
               float grow, const Color& modulate) const;
    float _width(const std::vector<char32_t>& cps, int font_size) const;

    std::shared_ptr<Face> face_;
    float embolden_ = 0;
};

using FontRef = std::shared_ptr<Font>;

}  // namespace t3::gfx
