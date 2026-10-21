// remake/core/fight/pause_menu.gd
// The pause menu (FUN_80078498, FUN_8007854C): CANCEL, COMMAND and RESET chosen with up and
// down, confirmed with Start or a face button; CANCEL closes the pause at the next pause check,
// RESET leaves the fight (MenuExitCheck, which also leaves on Start + Select); COMMAND shows
// the move lists (MoveListScreen.pause_page) until a button returns to the menu. Tekken Ball adds
// HOW TO, a picture of its rules (volley.ovl FUN_800B4088).
#include "core/fight/pause_menu.h"

#include "core/fight/fight_state.h"
#include "core/fight/move_list_screen.h"
#include "core/game/game_mode.h"
#include "core/sim_events.h"

namespace t3 {

// One paused frame of `player` (1 or 2).
void PauseMenu::step(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<FighterInput::PadWords>& pads,
                     const gd::Ref<SimEvents>& events) {
    if (fight->pause_cursor == OPENED) {
        fight->pause_page = 0;
        fight->pause_cursor = 0;
        fight->pause_delay = CONFIRM_DELAY;
        events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
        events->add(SimEvents::Kind::SOUND, -1, SOUND_CONFIRM, 0);
    }
    if (fight->pause_page == PAGE_COMMAND) {
        MoveListScreen::pause_page(fight, player, pads, events);
        return;
    }
    if (fight->pause_page == PAGE_HOW_TO) {
        _how_to(fight, player, pads, events);
        return;
    }
    // FUN_8007854C draws nothing while the menu is inert.
    if (fight->pause_delay != 0) {
        fight->pause_delay -= 1;
        return;
    }
    events->add(SimEvents::Kind::PAUSE_MENU, -1, player);
    gd::Int pressed = pads->physical_pressed[gd::clampi(player - 1, 0, 1)];
    gd::Int count = item_count(fight);
    if ((pressed & PadState::UP) != 0) {
        fight->pause_cursor = (fight->pause_cursor != 0 ? fight->pause_cursor : count) - 1;
        events->add(SimEvents::Kind::SOUND, -1, SOUND_MOVE, 0);
    }
    if ((pressed & PadState::DOWN) != 0) {
        fight->pause_cursor = gd::mod(fight->pause_cursor + 1, count);
        events->add(SimEvents::Kind::SOUND, -1, SOUND_MOVE, 0);
    }
    if ((pressed & CONFIRM) != 0) {
        fight->pause_delay = CONFIRM_DELAY;
        gd::Int page = pages(fight)[gd::clampi(fight->pause_cursor, 0, count - 1)];
        if (page == PAGE_SAVE_STATE || page == PAGE_LOAD_STATE) {
            // Native: the app saves or loads after this step; the menu stays.
            fight->state_request = page;
        } else {
            fight->pause_page = page;
        }
        events->add(SimEvents::Kind::SOUND, -1, SOUND_CONFIRM, 0);
    }
}

// The menu's items: HOW TO only in Tekken Ball.
gd::Int PauseMenu::item_count(const gd::Ref<FightState>& fight) {
    return pages(fight).size();
}

gd::Arr<gd::Int> PauseMenu::pages(const gd::Ref<FightState>& fight) {
    gd::Arr<gd::Int> out{PAGE_CANCEL, PAGE_COMMAND, PAGE_RESET};
    if (fight->mode == GameMode::BALL) out.append(PAGE_HOW_TO);
    if (fight->state_items) {
        out.append(PAGE_SAVE_STATE);
        out.append(PAGE_LOAD_STATE);
    }
    return out;
}

// FUN_800B4088: the HOW TO picture until Start or a face button returns to the menu.
void PauseMenu::_how_to(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<FighterInput::PadWords>& pads,
                        const gd::Ref<SimEvents>& events) {
    if (fight->pause_delay != 0) {
        fight->pause_delay -= 1;
        return;
    }
    if ((pads->physical_pressed[gd::clampi(player - 1, 0, 1)] & CONFIRM) != 0) {
        fight->pause_cursor = 0;
        fight->pause_page = 0;
        fight->pause_delay = HOW_TO_DELAY;
        events->add(SimEvents::Kind::SOUND, -1, SOUND_CONFIRM, 0);
    }
}

// The remake's Escape on the menu: as CANCEL confirmed. False when the menu is not taking input
// (not shown, inert, or the COMMAND page, which Escape's Start leaves).
bool PauseMenu::escape(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    if (fight->paused_player == 0 || fight->pause_page != 0 || fight->pause_delay != 0 || fight->pause_cursor == OPENED) {
        return false;
    }
    fight->pause_cursor = PAGE_CANCEL - 1;
    fight->pause_page = PAGE_CANCEL;
    fight->pause_delay = CONFIRM_DELAY;
    events->add(SimEvents::Kind::SOUND, -1, SOUND_CONFIRM, 0);
    return true;
}

// MenuExitCheck (0x80051304): RESET chosen, or Select pressed while Start and Select are held
// (the shoulder buttons may be held too).
bool PauseMenu::exit_requested(const gd::Ref<FightState>& fight, const gd::Ref<FighterInput::PadWords>& pads) {
    for (gd::Int p = 0; p < 2; ++p) {
        if ((pads->physical[p] & SOFT_RESET_MASK) == SOFT_RESET && (pads->physical_pressed[p] & PadState::SELECT) != 0) {
            return true;
        }
    }
    return fight->pause_page == PAGE_RESET;
}

}  // namespace t3
