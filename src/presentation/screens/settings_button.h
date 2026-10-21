#pragma once
// remake/presentation/screens/settings_button.gd
// The way into the settings on the main menu: a gear and "Select: settings" at the top left of
// the window (with the touch controls shown, in their top row right of the left macros: `place`). A
// click or tap on it, or near it, opens them, as Select does.
//
// Native: `extends Control` is a class with `visible` and `size`, drawn by `draw(canvas, window)`.
// The signal `pressed` is a list of std::function. _gui_input arrives through `pointer` (window
// pixels); `tr` is SettingsView::tr. NOTIFICATION_TRANSLATION_CHANGED needs nothing: the button is
// drawn every frame.
#include <functional>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "platform/app.h"

namespace t3 {

class SettingsButton {
public:
    // signal pressed
    std::vector<std::function<void()>> pressed;

    static constexpr float SIZE_SHARE = 0.06f;        // the gear's size as a share of the window height
    static constexpr float MIN_SIZE = 28.0f;
    static constexpr float MARGIN = 0.4f;             // of the gear's size
    static constexpr float HIT_SLACK = 0.5f;          // of the gear's size: taps this far outside the band count
    static inline const gfx::Color COLOUR{1, 1, 1, 0.8f};
    static inline const gfx::Color BAND{0, 0, 0, 0.45f};
    static constexpr int TEETH = 8;

    gfx::FontRef font;
    gfx::Rect2 _anchor;                               // where the band starts and its height (empty: the corner)

    // Control: shown or not and the window's size.
    bool visible = false;
    gfx::Vec2 size;

    // _ready
    SettingsButton();
    // Puts the band at `anchor`'s position with its height; an empty rectangle puts it back in the
    // window's corner.
    void place(gfx::Rect2 anchor);
    float _gear_size() const;
    gfx::Rect2 area() const;
    bool _has_point(gfx::Vec2 point) const;
    // _gui_input (true: accept_event).
    bool pointer(PointerEvent kind, const Pointer& p);
    // Control._draw at the window's size.
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw(gfx::Canvas& canvas);
};

}  // namespace t3
