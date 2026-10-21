// remake/presentation/camera/framing_bars.gd
// Frames the part of the window the 3D view may not extend into (CameraRig.visible_rect) with
// the stage's clear colour (remake-plan.md#aspect-ratios).
#include "presentation/camera/framing_bars.h"

namespace t3 {

namespace {

bool _same(const gfx::Rect2& a, const gfx::Rect2& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.size.x == b.size.x && a.size.y == b.size.y;
}

bool _same(const gfx::Color& a, const gfx::Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

}  // namespace

void FramingBars::_ready() {
    // set_anchors_and_offsets_preset(PRESET_FULL_RECT) and MOUSE_FILTER_IGNORE: the app draws it
    // over the whole window (draw's `window`) and it takes no input.
}

void FramingBars::frame(const gfx::Rect2& rect, const gfx::Color& clear) {
    if (_same(rect, shown) && _same(clear, colour)) return;
    shown = rect;
    colour = clear;
    // queue_redraw(): drawn every frame.
}

void FramingBars::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    _draw(canvas);
}

void FramingBars::_draw(gfx::Canvas& canvas) {
    gfx::Rect2 r(gfx::Vec2(shown.position.x * size.x, shown.position.y * size.y),
                 gfx::Vec2(shown.size.x * size.x, shown.size.y * size.y));
    if (r.position.x > 0) {
        canvas.draw_rect(gfx::Rect2(0, 0, r.position.x, size.y), colour);
        canvas.draw_rect(gfx::Rect2(r.end().x, 0, size.x - r.end().x, size.y), colour);
    }
    if (r.position.y > 0) {
        canvas.draw_rect(gfx::Rect2(0, 0, size.x, r.position.y), colour);
        canvas.draw_rect(gfx::Rect2(0, r.end().y, size.x, size.y - r.end().y), colour);
    }
}

}  // namespace t3
