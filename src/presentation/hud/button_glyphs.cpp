// remake/presentation/hud/button_glyphs.gd
#include "presentation/hud/button_glyphs.h"

#include <algorithm>
#include <cmath>

#include "platform/pad_state.h"

namespace t3 {

// The GDScript constant is a Dictionary {button: Color}; `COLOURS.get(button, Color.WHITE)`.
gfx::Color ButtonGlyphs::COLOURS(gd::Int button) {
    if (button == PadState::SQUARE) return gfx::Color(0.93f, 0.5f, 0.75f);
    if (button == PadState::TRIANGLE) return gfx::Color(0.3f, 0.85f, 0.65f);
    if (button == PadState::CROSS) return gfx::Color(0.5f, 0.65f, 1.0f);
    if (button == PadState::CIRCLE) return gfx::Color(1.0f, 0.4f, 0.4f);
    return gfx::Color::white();
}

// A face button's symbol centred at `at` with radius `r`; `alpha` scales its opacity.
void ButtonGlyphs::draw(gfx::Canvas& item, gd::Int button, gfx::Vec2 at, float r, float alpha) {
    gfx::Color colour = COLOURS(button);
    colour.a *= alpha;
    float s = r * 0.55f;
    float width = std::max(1.0f, r * LINE_SHARE);
    if (button == PadState::SQUARE) {
        item.draw_rect(gfx::Rect2(at - gfx::Vec2(s, s), gfx::Vec2(2 * s, 2 * s)), colour, false, width);
    } else if (button == PadState::TRIANGLE) {
        float h = s * 1.1f;
        item.draw_polyline({at + gfx::Vec2(0, -h), at + gfx::Vec2(h * 0.95f, h * 0.7f), at + gfx::Vec2(-h * 0.95f, h * 0.7f),
                            at + gfx::Vec2(0, -h)},
                           colour, width);
    } else if (button == PadState::CROSS) {
        item.draw_line(at + gfx::Vec2(-s, -s), at + gfx::Vec2(s, s), colour, width);
        item.draw_line(at + gfx::Vec2(s, -s), at + gfx::Vec2(-s, s), colour, width);
    } else if (button == PadState::CIRCLE) {
        item.draw_arc(at, s, 0.0f, 6.28318530717958648f, 32, colour, width);   // TAU
    }
}

}  // namespace t3
