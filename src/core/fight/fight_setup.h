#pragma once
// remake/core/fight/fight_setup.gd
// What a VS match is started with (the menus' and the fight setup screen's choices): the two
// fighters, the stage, the options and the random seeds.
#include "gd/gd.h"

namespace t3 {

class FightSetup {
public:
    gd::PI32 chars{0, 1};           // character ids
    gd::PI32 costumes{0, 0};        // costume 0-3 of each character
    gd::Int stage = 0;
    gd::Int round_time = 2;         // ROUND TIME option: 20, 30, 40, 50, 60 s, 5 infinite
    gd::Int rounds = 1;             // FIGHT COUNT option: rounds to win − 1
    bool chip_damage = false;       // GUARD DAMAGE option
    gd::PI32 handicaps{3, 3};       // health table index per player (3: 140)
    gd::PI32 cpu{0, 0};             // 1: the player is CPU-controlled
    gd::Int ai_difficulty = 1;      // the AI group: DIFFICULTY LEVEL (0x800AE6D0)
    gd::Int ai_level = 0;           // the CPU level 0-9 (0x800AE6D9)
    bool attract = false;           // the demonstration fight (0x800AE39C)
    gd::Arr<gd::PI32> key_tables;   // per player: mapped word of each physical bit
    gd::Int seed = 1;               // rand(), the camera and the frame generator
    gd::Int unlocked = 0x3FF;       // unlocked characters (bit per id) Mokujin may copy

    // The key configuration of a new game. (An untyped constant Array of ints: gd::Arr<gd::Int>.)
    static inline const gd::Arr<gd::Int> DEFAULT_KEY_TABLE{
        0, 0, 0, 0, 0x10, 0x20, 0x40, 0x80, 0x100, 0, 0, 0x800, 0x1000, 0x2000, 0x4000, 0x8000};

    // The humans as a bit per player (0x800AE3D8).
    gd::Int human_mask() const;

    FightSetup();
};

}  // namespace t3
