#pragma once
// remake/presentation/screens/screen_box.gd: the 4:3 area of the window that stands for the original
// 368 x 480 screen: 2D screens are laid out in the original coordinates and scaled into it, centred
// in the window.
#include <algorithm>

#include "gfx/canvas.h"

namespace t3 {

struct ScreenBox {
    static constexpr float ASPECT = 4.0f / 3.0f;

    static gfx::Rect2 rect(gfx::Vec2 window) {
        float w = std::min(window.x, window.y * ASPECT);
        float h = w / ASPECT;
        return gfx::Rect2((window.x - w) / 2.0f, (window.y - h) / 2.0f, w, h);
    }
    // A point of the 368 x 480 screen in window pixels.
    static gfx::Vec2 point(const gfx::Rect2& box, gfx::Vec2 screen, float x, float y) {
        return box.position + gfx::Vec2(x / screen.x * box.size.x, y / screen.y * box.size.y);
    }
    // Window pixels per original display line.
    static float line_scale(const gfx::Rect2& box, gfx::Vec2 screen) { return box.size.y / screen.y; }
};

}  // namespace t3
