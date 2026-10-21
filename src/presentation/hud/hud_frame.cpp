// remake/presentation/hud/hud_frame.gd
#include "presentation/hud/hud_frame.h"

#include "presentation/screens/screen_box.h"
#include "presentation/screens/screen_canvas.h"

namespace t3 {

// The window point of frame-buffer point (x, y) in the frame `box` (window pixels).
gfx::Vec2 HudFrame::point(const gfx::Rect2& box, float x, float y) {
    return ScreenBox::point(box, ScreenCanvas::SCREEN(), x - LEFT, y);
}

// Window pixels per frame-buffer unit.
gfx::Vec2 HudFrame::unit_scale(const gfx::Rect2& box) {
    // box.size / Vector2(ScreenCanvas.SCREEN), per component.
    gfx::Vec2 screen = ScreenCanvas::SCREEN();
    return gfx::Vec2(box.size.x / screen.x, box.size.y / screen.y);
}

}  // namespace t3
