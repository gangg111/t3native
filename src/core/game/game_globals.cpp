// remake/core/game/game_globals.gd
#include "core/game/game_globals.h"

#include "core/fight/fight_state.h"

namespace t3 {

void GameGlobals::set_tie_winner(const gd::Ref<FightState>& fight, gd::Int player) {
    fight->tie_choice = player & 0xFF;
    other_player = (player + 1) & 1;
    win_streak = 0;
}

void GameGlobals::count_win(const gd::Ref<FightState>& fight, gd::Int player) {
    if (player == fight->tie_choice) {
        win_streak = (win_streak + 1) & 0xFFFF;
    } else {
        fight->tie_choice = player & 0xFF;
        other_player = (fight->tie_choice + 1) & 1;
        win_streak = 1;
    }
}

}  // namespace t3
