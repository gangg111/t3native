#include "app/skip_buttons.h"

#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_input.h"
#include "core/game/game_flow.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "platform/pad_state.h"

namespace t3 {

const gd::Int SkipButtons::BUTTONS = PadState::CROSS | PadState::CIRCLE;

gd::PI32 SkipButtons::apply(const gd::PI32& pads, const GameFlow& flow, bool movie_playing) {
    gd::PI32 out = pads.duplicate();
    if (skippable(flow, movie_playing) && !flow.movie_blocking) {
        for (gd::Int p = 0; p < out.size(); ++p)
            if ((gd::Int(out[p]) & BUTTONS) != 0) out[p] |= PadState::START;
    }
    return out;
}

bool SkipButtons::skips_blocking_movie(const GameFlow& flow) {
    if (!flow.movie_blocking) return false;
    const gd::PI32& pressed = flow.sim->pads->physical_pressed;
    return ((gd::Int(pressed[0]) | gd::Int(pressed[1])) & (PadState::START | BUTTONS)) != 0;
}

bool SkipButtons::skippable(const GameFlow& flow, bool movie_playing) {
    if (movie_playing) return true;
    switch (flow.state) {
        case GameFlow::State::TITLE:
        case GameFlow::State::ENBU: return true;
        case GameFlow::State::TRANSITION: {
            gd::Int target = flow.progress->transition_target;
            return target == GameFlow::State::TITLE || target == GameFlow::State::ENBU;
        }
        case GameFlow::State::FIGHT:
            // Not the win poses after a replay: the button held there picks the pose.
            return gd::Int(flow.region->mode) == GameMode::DEMO || flow.fight->replay_playing != 0;
        default: return false;
    }
}

}  // namespace t3
