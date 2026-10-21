#pragma once
// remake/app/skip_buttons.gd: a convenience of the remake: the game skips its start-up, attract
// sequence, replays and movies on Start only; Cross and Circle skip them too (the steps see Start held
// with them).
#include "gd/gd.h"

namespace t3 {

class GameFlow;

struct SkipButtons {
    static const gd::Int BUTTONS;   // PadState.CROSS | PadState.CIRCLE

    // The pads with Start added to each holding Cross or Circle while the game is on something they
    // skip (not an ending or Theater movie: `skips_blocking_movie` reads those buttons themselves).
    static gd::PI32 apply(const gd::PI32& pads, const GameFlow& flow, bool movie_playing);
    // Whether an ending or Theater movie is skipped this frame: Start, Cross or Circle newly pressed.
    static bool skips_blocking_movie(const GameFlow& flow);
    // Whether what shows is skipped with Start.
    static bool skippable(const GameFlow& flow, bool movie_playing);
};

}  // namespace t3
