#pragma once
// remake/presentation/screens/fade_overlay.gd: full-screen fades with the semantics of FUN_8004E2E8
// (draw_sim.fade): level 0x100 draws nothing, below it a subtractive grey of 255 - level darkens,
// above it an additive grey of level - 0x100 brightens (at most 255). Several fades of one frame are
// applied in order.
#include "gfx/canvas.h"

namespace t3 {

class FadeOverlay {
public:
    static constexpr gd::Int NEUTRAL = 0x100;
    static constexpr gd::Int MAX_LEVEL = 0x1FF;

    gd::PI32 levels;

    void show_levels(const gd::PI32& frame_levels) { levels = frame_levels.duplicate(); }

    void draw(gfx::Canvas& canvas, gfx::Vec2 size) const {
        for (gd::Int level_in : levels) {
            if (level_in == NEUTRAL) continue;
            gd::Int level = gd::mini(level_in, MAX_LEVEL);
            float v = 0.0f;
            if (level < NEUTRAL) {
                v = static_cast<float>(0xFF - level) / 255.0f;
                canvas.set_blend(gfx::Blend::SUB);
            } else {
                v = static_cast<float>(level - NEUTRAL) / 255.0f;
                canvas.set_blend(gfx::Blend::ADD);
            }
            canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color(v, v, v, 1.0f));
        }
        canvas.set_blend(gfx::Blend::MIX);
    }
};

}  // namespace t3
