#pragma once
// remake/presentation/hud/hud_text.gd: FUN_8004D15C's glyph walk with the game's fonts (HudData): one
// advance per character, "\n" starts a new line, a space only advances. `to_window` maps the layout's
// units (a text from (x, y)) to window pixels.
#include <string>

#include "content/hud_data.h"
#include "gfx/canvas.h"

namespace t3 {

// Godot's Transform2D restricted to what HudText.layout makes: a scale and an origin.
struct Layout2D {
    gfx::Vec2 scale{1, 1}, origin;
    gfx::Vec2 xform(gfx::Vec2 p) const { return {origin.x + p.x * scale.x, origin.y + p.y * scale.y}; }
    gfx::Vec2 basis_xform(gfx::Vec2 v) const { return {v.x * scale.x, v.y * scale.y}; }
};

struct HudText {
    static constexpr gd::Int NEWLINE = 0x0A;
    static constexpr gd::Int SPACE = 0x20;
    static constexpr gd::Int LAST_COLOUR = 31;   // the glyph sheets' colours: 0 to 31 (HudData.glyphs)

    static void draw(gfx::Canvas& item, const HudData& hud, const std::string& text, gd::Int font_index, gd::Int colour, float x,
                     float y, const Layout2D& to_window, const gfx::Color& tint = gfx::Color::white());
    // The linear map of a layout whose point (0, 0) lies at `origin` and (1, 1) at `unit` (window).
    static Layout2D layout(gfx::Vec2 origin, gfx::Vec2 unit) { return {unit - origin, origin}; }
};

}  // namespace t3
