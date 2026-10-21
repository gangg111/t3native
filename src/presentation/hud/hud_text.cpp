#include "presentation/hud/hud_text.h"

namespace t3 {

void HudText::draw(gfx::Canvas& item, const HudData& hud, const std::string& text, gd::Int font_index, gd::Int colour, float x,
                   float y, const Layout2D& to_window, const gfx::Color& tint) {
    gd::Ref<HudData::FontInfo> info = hud.font(font_index);
    if (!info) return;
    gfx::TextureRef sheet = hud.glyphs(font_index, gd::clampi(colour, 0, LAST_COLOUR));
    gfx::Vec2 glyph = to_window.basis_xform(gfx::Vec2(static_cast<float>(info->width), static_cast<float>(info->height)));
    gd::Int row = 0;
    gd::Int column = 0;
    // `text.unicode_at(i)`: the game's texts are single-byte (ASCII and the game's own codes).
    for (unsigned char ch : text) {
        gd::Int code = ch;
        if (code == NEWLINE) {
            row += 1;
            column = 0;
            continue;
        }
        if (code != SPACE) {
            gd::Int n = code - info->first;
            gfx::Rect2 source(static_cast<float>(gd::mod(n, info->columns) * info->width),
                              static_cast<float>(gd::div(n, info->columns) * info->height), static_cast<float>(info->width),
                              static_cast<float>(info->height));
            gfx::Vec2 at = to_window.xform(gfx::Vec2(x + column * info->advance, y + row * info->line));
            item.draw_texture_rect_region(sheet, gfx::Rect2(at, glyph), source, tint);
        }
        column += 1;
    }
}

}  // namespace t3
