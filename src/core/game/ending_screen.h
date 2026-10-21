#pragma once
// remake/core/game/ending_screen.gd
// Game state 19 (ending.ovl FUN_8010E8C0; modes.md#arcade-ending-and-staff-roll): after an
// arcade clear the winner's ending movie, then the staff roll over black, then the ranking.
// Theater (the same state with 0x800AE158 set) is TheaterScreen.
//
// The game plays the movie inside one frame (the main loop waits); GameFlow does the same by
// holding its frames while `movie_blocking` is set, and finishes the frame when the movie ends.
#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;
class StaffRoll;

class EndingScreen : public FlowPart {
public:
    static constexpr gd::Int SECOND_CLEAR_MOVIE = 0x18;
    static constexpr gd::Int CHARACTERS_WITH_ENDINGS = 0x15;
    static constexpr gd::Int MUSIC_ROLL = 4;

    gd::Ref<StaffRoll> roll;                 // StaffRoll.new(flow.data), made in the constructor
    gd::Int all_movies = 0;                  // 0x800AE164: every movie is open (Theater's disc buttons)

    // _init(flow): super(flow), then the staff roll over the flow's data.
    explicit EndingScreen(GameFlow* flow);

    // The screen overlay was loaded: its flags start clear.
    void loaded();
    void step();
    // The movie has ended (the rest of sub-state 2's frame): the roll starts.
    void after_movie();
    // FUN_8010E7D0: the ending of the human's character and costume (Doctor B.'s second clear has
    // its own); with no single human, movie 0 and Theater's flag set (as the game does).
    gd::Int ending_movie();
};

}  // namespace t3
