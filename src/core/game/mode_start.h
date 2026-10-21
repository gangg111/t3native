#pragma once
// remake/core/game/mode_start.gd
// FUN_800DAF2C (title.ovl; menu_sim.mode_start, verified): a mode starts. The options go into
// the fight variables, the players' choices are cleared, the mode's rule bytes (0x800AFF54 …)
// and counters are set, and the game goes to the fight preparation (state 7).
//
// Not mechanical: the static functions take the flow as `GameFlow*` (the callers are FlowParts
// passing `g` and GameFlow passing `this`; tests pass `flow.get()`).
#include "gd/gd.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;
class GameGlobals;
class ModeRegion;

class ModeStart {
public:
    // FUN_800DB4E4: the mode overlay each mode needs (arcade 0, practice 1, force 2, volley 3).
    // (untyped const array of ints)
    static inline const gd::Arr<gd::Int> MODE_OVERLAYS{0, 0, 0, 0, 0, 1, 0, 3, 2};
    // Per mode the rule bytes 0x800AFF54 … 0x800AFF5C.
    static inline gd::Arr<gd::PI32> FEATURES{
        gd::PI32{1, 1, 0, 1, 0, 0, 0, 1, 0}, gd::PI32{1, 0, 0, 1, 1, 0, 1, 1, 1},
        gd::PI32{1, 1, 1, 0, 1, 1, 1, 1, 1}, gd::PI32{0, 0, 0, 0, 0, 0, 0, 1, 0},
        gd::PI32{0, 0, 0, 1, 0, 1, 1, 1, 0}, gd::PI32{1, 0, 0, 0, 0, 0, 0, 1, 0},
        gd::PI32{0, 0, 0, 0, 0, 0, 1, 1, 0}, gd::PI32{1, 1, 0, 0, 1, 0, 0, 0, 1},
        gd::PI32{1, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    static constexpr gd::Int NO_CHARACTER = 0x16;
    static constexpr gd::Int NO_PICK = 0x58;
    static constexpr gd::Int QUICK_CHALLENGE = PadState::START | PadState::L1 | PadState::R1;  // quick select

    static void start(GameFlow* g, gd::Int mode, gd::Int players);
    // The options into the fight variables, the mode's counters cleared.
    static void _options(GameFlow* g, gd::Int mode);
    // The players' controllers and choices cleared; `players` picks who starts active.
    static void _players(GameFlow* g, gd::Int players);
    // The mode's rule bytes and its own settings; true when the ranking shows its usage page next.
    static bool _mode_rules(GameFlow* g, gd::Int mode);
    // FUN_80051644: the arcade and time attack stage records (ten of 8 bytes at 0x800AFF90).
    static void _clear_stage_records(const gd::Ref<ModeRegion>& region);
    // FUN_800DB54C: the demonstration fight's characters: the next of the list, and a random other
    // one; both in their second costume when bit 9 of the total usage is set.
    static void _demo_fighters(GameFlow* g);
    // FUN_8004F3CC: in a mirror match the side that chose later (unless only the other one is a
    // human) takes another costume: 0 and 1 swap, 2 and 3 become 0.
    static void mirror_costumes(const gd::Ref<GameGlobals>& globals, gd::Int player);
};

}  // namespace t3
