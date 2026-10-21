// remake/core/game/ending_screen.gd
// Game state 19 (ending.ovl FUN_8010E8C0; modes.md#arcade-ending-and-staff-roll): after an
// arcade clear the winner's ending movie, then the staff roll over black, then the ranking.
// Theater (the same state with 0x800AE158 set) is TheaterScreen.
//
// The game plays the movie inside one frame (the main loop waits); GameFlow does the same by
// holding its frames while `movie_blocking` is set, and finishes the frame when the movie ends.
#include "core/game/ending_screen.h"

#include "content/flow_data.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_state.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/game/staff_roll.h"
#include "core/game/theater_screen.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

EndingScreen::EndingScreen(GameFlow* flow) : FlowPart(flow) {
    roll = gd::make<StaffRoll>(flow->data);
}

// The screen overlay was loaded: its flags start clear.
void EndingScreen::loaded() {
    roll->reset();
}

void EndingScreen::step() {
    switch (g->sub) {
        case 0:
            g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
            g->theater->disc = 3;
            all_movies = g->theater->all_movies();
            g->theater->movie = ending_movie();
            if (g->theater->kind != 0) {
                g->theater->open();
                return;
            }
            g->sub = 2;
            break;
        case 2:
            g->start_blocking_movie(g->theater->movie);
            break;
        case 3:
            if (roll->step(g)) {
                g->goto_ranking();
            }
            break;
        default:
            g->theater->step();
            break;
    }
}

// The movie has ended (the rest of sub-state 2's frame): the roll starts.
void EndingScreen::after_movie() {
    if (g->theater->kind != 0) {
        g->theater->after_movie();
        return;
    }
    roll->start();
    g->sim->events->add(SimEvents::Kind::MUSIC_PREPARE, -1, MUSIC_ROLL);
    g->sub = 3;
}

// FUN_8010E7D0: the ending of the human's character and costume (Doctor B.'s second clear has
// its own); with no single human, movie 0 and Theater's flag set (as the game does).
gd::Int EndingScreen::ending_movie() {
    gd::Int mask = g->fight->human_mask;
    g->theater->kind = 0;
    if (mask >= 3 || mask == 0) {
        g->theater->kind = 1;
        return 0;
    }
    gd::Int key = g->fight->fighters[mask - 1]->costume_key;
    gd::Int character = key >= 0 ? Fx::s16(key) >> 2 : (Fx::s16(key) + 3) >> 2;
    gd::Int costume = key & 3;
    if (character < 0 || character >= CHARACTERS_WITH_ENDINGS) {
        character = 0;
        costume = 0;
    }
    gd::Int m = g->data->ending_movies[character * 4 + costume];
    if (character == Character::GUN_JACK && (g->progress->cleared2 & (gd::Int(1) << Character::GUN_JACK)) != 0) {
        m = SECOND_CLEAR_MOVIE;
    }
    return m;
}

}  // namespace t3
