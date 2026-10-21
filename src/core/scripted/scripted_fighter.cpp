// remake/core/scripted/scripted_fighter.gd
// A fighter driven by a scene script (the Enbu event format, modes.md#enbu-attract-demonstration):
// the runner record of `enbu.ovl` (`0x8010C43C + 10·i`: state, costume, move slot, end frame,
// frame) around the fighter's own state.
#include "core/scripted/scripted_fighter.h"

#include "core/fighter/fighter_skeleton.h"
#include "core/fighter/fighter_state.h"

namespace t3 {

ScriptedFighter::ScriptedFighter() {
    fighter = gd::make<FighterState>();
}

}  // namespace t3
