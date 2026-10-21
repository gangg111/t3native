#pragma once
// remake/presentation/hud/hud_frame.gd: the fight's 2D layout: the game's 384 x 480 frame-buffer units,
// whose 368 shown columns (ScreenCanvas.SCREEN) start at x = 8 (centre 192), laid into the scene's 4:3
// frame (CameraRig.frame_rect, in the window).
#include "gfx/canvas.h"

namespace t3 {

class HudFrame {
public:
    static constexpr float LEFT = 8.0f;

    // The window point of frame-buffer point (x, y) in the frame `box` (window pixels).
    static gfx::Vec2 point(const gfx::Rect2& box, float x, float y);
    // Window pixels per frame-buffer unit.
    static gfx::Vec2 unit_scale(const gfx::Rect2& box);
};

}  // namespace t3
