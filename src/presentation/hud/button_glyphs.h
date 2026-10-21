#pragma once
// remake/presentation/hud/button_glyphs.gd
// The PlayStation face-button symbols (square, triangle, cross, circle in their colours) drawn as
// vector shapes for the touch controls and the settings' key bindings: shapes, not font glyphs,
// since the web builds' default font has no PlayStation symbols.
#include "gd/gd.h"
#include "gfx/canvas.h"

namespace t3 {

class ButtonGlyphs {
public:
    // COLOURS: the face buttons' colours (white for any other button).
    static gfx::Color COLOURS(gd::Int button);
    static constexpr float LINE_SHARE = 0.16f;   // stroke width per radius

    // A face button's symbol centred at `at` with radius `r`; `alpha` scales its opacity.
    static void draw(gfx::Canvas& item, gd::Int button, gfx::Vec2 at, float r, float alpha = 1.0f);
};

}  // namespace t3
