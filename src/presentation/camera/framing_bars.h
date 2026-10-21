#pragma once
// remake/presentation/camera/framing_bars.gd
// Frames the part of the window the 3D view may not extend into (CameraRig.visible_rect) with
// the stage's clear colour (remake-plan.md#aspect-ratios).
//
// A Control in the remake: here `draw(canvas, window)` sets `size` and draws (_draw).
#include "gfx/canvas.h"

namespace t3 {

class FramingBars {
public:
    // Control's own members.
    bool visible = true;
    gfx::Vec2 size;

    gfx::Color colour = gfx::Color::black();
    gfx::Rect2 shown{0, 0, 1, 1};

    void _ready();
    void frame(const gfx::Rect2& rect, const gfx::Color& clear);
    // The Control's draw: sets `size` (the window) and draws with `canvas` (_draw).
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw(gfx::Canvas& canvas);
};

}  // namespace t3
