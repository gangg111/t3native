#pragma once
// remake/core/fight/move_list_screen.gd
// The in-fight move lists (modes.md#pause, "Move list"; the verified port
// tools/research/command_list_sim.py): the pause menu's COMMAND page (FUN_80079298) and practice's
// COMMAND LIST (FUN_8007906C) show each side's list (FUN_800789E4), scrolled by that side's pad.
// This is their input and scrolling; MoveListView draws what the MOVE_LIST event asks for.
#include "core/fighter/fighter_input.h"
#include "gd/gd.h"
#include "platform/pad_state.h"

namespace t3 {

class FightState;
class FighterState;
class SimEvents;

class MoveListScreen {
public:
    static constexpr gd::Int ROW_STEP = 64;
    static constexpr gd::Int SOUND_MOVE = 0x546C;
    static constexpr gd::Int SOUND_EXIT = 0x50F4;
    static constexpr gd::Int EXIT_BUTTONS = PadState::CONFIRM;
    static constexpr gd::Int PAGE = 5;
    static constexpr gd::Int EXIT_DELAY = 2;

    // The number of moves in a fighter's list.
    static gd::Int count(const gd::Ref<FighterState>& f);

    // FUN_800789E4 before it draws: up and down move by one, left and right by five while up and
    // down are not held, wrapping (sound 0x546C); the list slides 64 pixels a step and eases in,
    // keeping two thirds of the offset each frame.
    static void scroll(const gd::Ref<FightState>& fight, gd::Int player, gd::Int pad_player,
                       const gd::Ref<FighterInput::PadWords>& pads, const gd::Ref<SimEvents>& events);

    // The lists of the sides in `mask`, each scrolled by its own pad.
    static void _lists(const gd::Ref<FightState>& fight, gd::Int mask, const gd::Ref<FighterInput::PadWords>& pads,
                       const gd::Ref<SimEvents>& events);

    // FUN_80079298: the pause menu's COMMAND page of the pausing `player` (1 or 2): every human
    // side's list; a face button or Start returns to the menu (cursor on CANCEL). Tekken Force shows
    // only the first record's list, under the left column. (The game's extra fight frame of a lagging
    // frame, which scrolls with text off, is not modelled: bug #39.)
    static void pause_page(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<FighterInput::PadWords>& pads,
                           const gd::Ref<SimEvents>& events);

    // FUN_8007906C: practice's COMMAND LIST: the lists in `mask` and PUSH BUTTON TO EXIT under
    // `player`'s column (the menu's own item input turns it off).
    static void practice_page(const gd::Ref<FightState>& fight, gd::Int mask, gd::Int player,
                              const gd::Ref<FighterInput::PadWords>& pads, const gd::Ref<SimEvents>& events);
};

}  // namespace t3
