#pragma once
// remake/presentation/screens/screen_mask.gd: black outside what the console showed of a 2D screen:
// the 4:3 screen in the window, and in it the display rectangle the game draws to (PsxCanvas.DISPLAY:
// y 20-467 of the 480 lines). The screens draw past it (the VS background's last column, sliding
// pictures).
#include "presentation/screens/psx_canvas.h"
#include "presentation/screens/screen_box.h"

namespace t3 {

class ScreenMask {
public:
    bool visible = false;
    // Native: the wide mode's select screen: its background past the 4:3 box stays (its foreground
    // is clipped to the box by the canvas itself, ScreenCanvas::wide).
    bool open_sides = false;

    void draw(gfx::Canvas& canvas, gfx::Vec2 size) const {
        if (!visible) return;
        gfx::Rect2 box = ScreenBox::rect(size);
        float scale = box.size.y / static_cast<float>(ScreenCanvas::SCREEN_Y);
        float top = box.position.y + PsxCanvas::DISPLAY().position.y * scale;
        float bottom = box.position.y + PsxCanvas::DISPLAY().end().y * scale;
        if (!open_sides) {
            canvas.draw_rect(gfx::Rect2(0, 0, box.position.x, size.y), gfx::Color::black());
            canvas.draw_rect(gfx::Rect2(box.end().x, 0, size.x - box.end().x, size.y), gfx::Color::black());
        }
        canvas.draw_rect(gfx::Rect2(0, 0, size.x, top), gfx::Color::black());
        canvas.draw_rect(gfx::Rect2(0, bottom, size.x, size.y - bottom), gfx::Color::black());
    }
};

}  // namespace t3
