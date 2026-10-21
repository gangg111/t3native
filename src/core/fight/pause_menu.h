#pragma once
// remake/core/fight/pause_menu.gd
// The pause menu (FUN_80078498, FUN_8007854C): CANCEL, COMMAND and RESET chosen with up and
// down, confirmed with Start or a face button; CANCEL closes the pause at the next pause check,
// RESET leaves the fight (MenuExitCheck, which also leaves on Start + Select); COMMAND shows
// the move lists (MoveListScreen.pause_page) until a button returns to the menu. Tekken Ball adds
// HOW TO, a picture of its rules (volley.ovl FUN_800B4088).
#include <string>

#include "core/fighter/fighter_input.h"
#include "gd/gd.h"
#include "platform/pad_state.h"

namespace t3 {

class FightState;
class SimEvents;

class PauseMenu {
public:
    static inline const gd::Arr<std::string> ITEMS{"CANCEL", "COMMAND", "RESET", "HOW TO"};
    static constexpr gd::Int PAGE_CANCEL = 1;
    static constexpr gd::Int PAGE_COMMAND = 2;
    static constexpr gd::Int PAGE_RESET = 3;
    static constexpr gd::Int PAGE_HOW_TO = 4;
    static constexpr gd::Int HOW_TO_DELAY = 3;
    static constexpr gd::Int OPENED = -1;                  // 0x80098DDC = 0xFF: the menu was just opened
    static constexpr gd::Int CONFIRM_DELAY = 2;
    static constexpr gd::Int CONFIRM = PadState::CONFIRM;
    static constexpr gd::Int SOUND_MOVE = 0x546C;
    static constexpr gd::Int SOUND_CONFIRM = 0x50F4;
    // MenuExitCheck's soft reset: Start and Select held with nothing else but L1 / R1.
    static constexpr gd::Int SOFT_RESET = PadState::START | PadState::SELECT;
    static constexpr gd::Int SOFT_RESET_MASK = ~(PadState::L1 | PadState::R1) & 0xFFFF;

    // One paused frame of `player` (1 or 2).
    static void step(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<FighterInput::PadWords>& pads,
                     const gd::Ref<SimEvents>& events);

    // The menu's items: HOW TO only in Tekken Ball.
    static gd::Int item_count(const gd::Ref<FightState>& fight);

    // FUN_800B4088: the HOW TO picture until Start or a face button returns to the menu.
    static void _how_to(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<FighterInput::PadWords>& pads,
                        const gd::Ref<SimEvents>& events);

    // The remake's Escape on the menu: as CANCEL confirmed. False when the menu is not taking input
    // (not shown, inert, or the COMMAND page, which Escape's Start leaves).
    static bool escape(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // MenuExitCheck (0x80051304): RESET chosen, or Select pressed while Start and Select are held
    // (the shoulder buttons may be held too).
    static bool exit_requested(const gd::Ref<FightState>& fight, const gd::Ref<FighterInput::PadWords>& pads);
};

}  // namespace t3
