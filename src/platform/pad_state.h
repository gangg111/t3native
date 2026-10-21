#pragma once
// remake/platform/pad_state.gd
// Button bits of the game's pad word (PsyQ libetc layout, as in 0x800AE230).
// Directions are screen directions; the fight code turns them into forward/back.
// Names are plain text: the default font of web builds has no PlayStation symbols.
#include <string>

#include "gd/gd.h"

namespace t3 {

class PadState {
public:
    static constexpr gd::Int L2 = 0x0001;
    static constexpr gd::Int R2 = 0x0002;
    static constexpr gd::Int L1 = 0x0004;
    static constexpr gd::Int R1 = 0x0008;
    static constexpr gd::Int TRIANGLE = 0x0010;
    static constexpr gd::Int CIRCLE = 0x0020;
    static constexpr gd::Int CROSS = 0x0040;
    static constexpr gd::Int SQUARE = 0x0080;
    static constexpr gd::Int SELECT = 0x0100;
    static constexpr gd::Int START = 0x0800;
    static constexpr gd::Int UP = 0x1000;
    static constexpr gd::Int RIGHT = 0x2000;
    static constexpr gd::Int DOWN = 0x4000;
    static constexpr gd::Int LEFT = 0x8000;
    static constexpr gd::Int FACE_BUTTONS = TRIANGLE | CIRCLE | CROSS | SQUARE;
    static constexpr gd::Int CONFIRM = START | FACE_BUTTONS;   // Start or any face button
    static constexpr gd::Int HORIZONTAL = LEFT | RIGHT;
    static constexpr gd::Int VERTICAL = UP | DOWN;
    static constexpr gd::Int DIRECTIONS = HORIZONTAL | VERTICAL;

    static inline const gd::Dict<gd::Int, std::string> NAMES = [] {
        gd::Dict<gd::Int, std::string> d;
        d.set(L2, "L2"); d.set(R2, "R2"); d.set(L1, "L1"); d.set(R1, "R1");
        d.set(TRIANGLE, "Triangle"); d.set(CIRCLE, "Circle"); d.set(CROSS, "Cross"); d.set(SQUARE, "Square");
        d.set(SELECT, "Select"); d.set(START, "Start"); d.set(UP, "Up"); d.set(RIGHT, "Right");
        d.set(DOWN, "Down"); d.set(LEFT, "Left");
        return d;
    }();

    static std::string describe(gd::Int bits);
};

}  // namespace t3
