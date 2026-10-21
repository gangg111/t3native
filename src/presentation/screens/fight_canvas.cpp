// remake/presentation/screens/fight_canvas.gd
#include "presentation/screens/fight_canvas.h"

#include "presentation/hud/hud_frame.h"
#include "presentation/screens/screen_box.h"

namespace t3 {

void FightCanvas::place(gfx::Rect2 frame_rect) {
    // Rect2(frame_rect.position * size, frame_rect.size * size): Vector2 * Vector2 per component.
    gfx::Rect2 f(gfx::Vec2(frame_rect.position.x * size.x, frame_rect.position.y * size.y),
                 gfx::Vec2(frame_rect.size.x * size.x, frame_rect.size.y * size.y));
    // f != frame (gfx::Rect2 has no operator!=).
    if (f.position.x != frame.position.x || f.position.y != frame.position.y || f.size.x != frame.size.x ||
        f.size.y != frame.size.y) {
        frame = f;
        redraw();
    }
}

// The fight's frame (the clipped passes are placed in it as well).
gfx::Rect2 FightCanvas::layout_box() const {
    return frame.has_area() ? frame : ScreenBox::rect(size);
}

gfx::Vec2 FightCanvas::pt(float x, float y) const {
    return HudFrame::point(box, x, y);
}

}  // namespace t3
