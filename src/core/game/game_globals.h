#pragma once
// remake/core/game/game_globals.gd
// The game flow's globals outside the progress block and the mode region (modes.md): the
// players' choices and flags, the save request, the display flag. Comments give the addresses.
#include "gd/gd.h"

namespace t3 {

class FightState;

class GameGlobals {
public:
    gd::PI32 player_char{0, 0};      // 0x800AE224: character per player
    gd::PI32 player_costume{0, 0};   // 0x800AE260
    gd::PI32 player_cpu{0, 0};       // 0x800A95F0: the player's fighter is a CPU
    gd::PI32 player_active{0, 0};    // 0x800AE6C0: a human plays on this pad
    gd::PI32 player_keep{0, 0};      // 0x800AE484: the pick is kept (quick select)
    gd::PI32 controller{0, 0};       // 0x800A9621 + 0x2A*p (FUN_8002A3C0)
    gd::Int other_player = 0;        // 0x800B0A06: the side that did not start
    gd::Int win_streak = 0;          // 0x800AE218: wins in a row of the tie winner
    gd::Int save_pending = 0;        // 0x800AE428
    gd::Int save_error = 0;          // 0x800AE429: frames of AUTO SAVE ERROR!
    gd::Int display_on = 0;          // 0x80095850 (FUN_80029860)

    // FUN_80051474: `player` wins ties; the other side is the other player.
    void set_tie_winner(const gd::Ref<FightState>& fight, gd::Int player);
    // FUN_80051498: another win for `player` (a new winner restarts the streak at 1).
    void count_win(const gd::Ref<FightState>& fight, gd::Int player);
};

}  // namespace t3
