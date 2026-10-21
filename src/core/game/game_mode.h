#pragma once
// remake/core/game/game_mode.gd
// `g_gameMode` (0x800AFF50, modes.md#modes): the mode a fight runs in.
#include "gd/gd.h"

namespace t3 {

class GameMode {
public:
    enum : gd::Int {
        ARCADE = 0,
        VS = 1,
        TEAM = 2,
        TIME_ATTACK = 3,
        SURVIVAL = 4,
        PRACTICE = 5,
        DEMO = 6,    // the demonstration fight of the title sequence
        BALL = 7,    // Tekken Ball
        FORCE = 8,   // Tekken Force
    };
};

}  // namespace t3
