#pragma once
// remake/core/scripted/scripted_fighter.gd
// A fighter driven by a scene script (the Enbu event format, modes.md#enbu-attract-demonstration):
// the runner record of `enbu.ovl` (`0x8010C43C + 10·i`: state, costume, move slot, end frame,
// frame) around the fighter's own state.
#include "gd/gd.h"

namespace t3 {

class FighterState;
class FighterSkeleton;

class ScriptedFighter {
public:
    enum State : gd::Int {
        IDLE,          // 0: nothing runs; the fighter is not drawn
        MOVE,          // 1: the move advances one pose frame per frame
        RELOAD_WAIT,   // 2: a costume change counts down
        RELOAD_START,  // 3
        RELOAD_LOAD,   // 4: load the model's members
        RELOAD_MODEL,  // 5: set up the model (the new costume is drawn from here on)
        RELOAD_PARTS,  // 6: set up the parts
    };

    static constexpr gd::Int RELOAD_DELAY = 2;

    gd::Ref<FighterState> fighter;           // FighterState.new(), made in the constructor
    gd::Int state = State::IDLE;
    gd::Int costume = 0;
    gd::Int slot = 0;
    gd::Int end_frame = 0;
    gd::Int frame = 0;
    bool drawn = false;                      // the fighter was animated (and drawn) this frame
    gd::Int model_costume = 0;               // the costume slot whose model the fighter shows
    gd::Ref<FighterSkeleton> skeleton;       // joints of the last animated frame
    bool stick_shown = true;                 // part 18's draw flag (FighterBody.stick_shown)

    ScriptedFighter();
};

}  // namespace t3
