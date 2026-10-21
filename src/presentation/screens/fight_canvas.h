#pragma once
// remake/presentation/screens/fight_canvas.gd: a PSX canvas drawn over the fight, laid out in the fight
// HUD's frame (HudFrame: the 368 shown columns start at x = 8 of the 4:3 frame the camera rig reports).
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class FightCanvas : public PsxCanvas {
public:
    gfx::Rect2 frame;   // the fight's 4:3 frame in the window (FightHud.place)

    void place(gfx::Rect2 frame_rect);
    // The fight's frame (the clipped passes are placed in it as well).
    gfx::Rect2 layout_box() const override;
    // HudFrame's x - 8: every helper of the base class (r, fill, line, image, text ...) maps through it.
    gfx::Vec2 pt(float x, float y) const override;
};

}  // namespace t3
