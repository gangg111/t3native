// remake/core/fight/move_list_screen.gd
// The in-fight move lists (modes.md#pause, "Move list"; the verified port
// tools/research/command_list_sim.py): the pause menu's COMMAND page (FUN_80079298) and practice's
// COMMAND LIST (FUN_8007906C) show each side's list (FUN_800789E4), scrolled by that side's pad.
// This is their input and scrolling; MoveListView draws what the MOVE_LIST event asks for.
#include "core/fight/move_list_screen.h"

#include "core/fight/fight_state.h"
#include "core/fighter/fighter_state.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

// The number of moves in a fighter's list.
gd::Int MoveListScreen::count(const gd::Ref<FighterState>& f) {
    return !f->move_text.is_empty() ? gd::Int(f->move_text[0]) : 0;
}

// FUN_800789E4 before it draws: up and down move by one, left and right by five while up and
// down are not held, wrapping (sound 0x546C); the list slides 64 pixels a step and eases in,
// keeping two thirds of the offset each frame.
void MoveListScreen::scroll(const gd::Ref<FightState>& fight, gd::Int player, gd::Int pad_player,
                            const gd::Ref<FighterInput::PadWords>& pads, const gd::Ref<SimEvents>& events) {
    gd::Int n = count(fight->fighters[player]);
    gd::Int pressed = pads->physical_pressed[pad_player];
    gd::Int d = pressed == PadState::DOWN ? 1 : 0;
    if (pressed == PadState::UP) {
        d = -1;
    }
    if ((pads->physical[pad_player] & (PadState::UP | PadState::DOWN)) == 0) {
        if (pressed == PadState::RIGHT) {
            d = PAGE;
        } else if (pressed == PadState::LEFT) {
            d = -PAGE;
        }
    }
    if (d != 0) {
        events->add(SimEvents::Kind::SOUND, -1, SOUND_MOVE, 0);
    }
    gd::Int cur = fight->command_cursor[player];
    gd::Int v = (cur < n ? cur : 0) + d;
    if (v < 0) {
        v += n;
    }
    if (!(v < n)) {
        v -= n;
    }
    fight->command_cursor[player] = v;
    fight->command_offset[player] = Fx::w32(fight->command_offset[player] + ROW_STEP * d);
    gd::Int off = fight->command_offset[player];
    if (off != 0) {
        gd::Int a = gd::absi(off);
        a = gd::maxi(a - Fx::div_trunc(a + 3, 3), 0);
        off = off < 0 ? -a : a;
    }
    fight->command_offset[player] = off;
}

// The lists of the sides in `mask`, each scrolled by its own pad.
void MoveListScreen::_lists(const gd::Ref<FightState>& fight, gd::Int mask, const gd::Ref<FighterInput::PadWords>& pads,
                            const gd::Ref<SimEvents>& events) {
    for (gd::Int p = 0; p < 2; ++p) {
        if ((gd::shr(mask, p) & 1) != 0) {
            scroll(fight, p, p, pads, events);
        }
    }
}

// FUN_80079298: the pause menu's COMMAND page of the pausing `player` (1 or 2): every human
// side's list; a face button or Start returns to the menu (cursor on CANCEL). Tekken Force shows
// only the first record's list, under the left column. (The game's extra fight frame of a lagging
// frame, which scrolls with text off, is not modelled: bug #39.)
void MoveListScreen::pause_page(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<FighterInput::PadWords>& pads,
                                const gd::Ref<SimEvents>& events) {
    if (fight->pause_delay != 0) {
        fight->pause_delay -= 1;
        return;
    }
    if ((pads->physical_pressed[player == 1 ? 0 : 1] & EXIT_BUTTONS) != 0) {
        fight->pause_page = 0;
        fight->pause_cursor = 0;
        fight->pause_delay = EXIT_DELAY;
        // FUN_80048760: the stage's horizon band (stages.md, "Clear colour") behind the scene for
        // two frames; the remake's panorama and floor always cover it.
        events->add(SimEvents::Kind::SOUND, -1, SOUND_EXIT, 0);
    }
    if (fight->mode == GameMode::FORCE) {
        events->add(SimEvents::Kind::MOVE_LIST, -1, 1, 0);
        scroll(fight, 0, fight->force->human(), pads, events);
        return;
    }
    events->add(SimEvents::Kind::MOVE_LIST, -1, fight->human_mask, player - 1);
    _lists(fight, fight->human_mask, pads, events);
}

// FUN_8007906C: practice's COMMAND LIST: the lists in `mask` and PUSH BUTTON TO EXIT under
// `player`'s column (the menu's own item input turns it off).
void MoveListScreen::practice_page(const gd::Ref<FightState>& fight, gd::Int mask, gd::Int player,
                                   const gd::Ref<FighterInput::PadWords>& pads, const gd::Ref<SimEvents>& events) {
    if (fight->pause_delay != 0) {
        fight->pause_delay -= 1;
        return;
    }
    events->add(SimEvents::Kind::MOVE_LIST, -1, mask, player);
    _lists(fight, mask, pads, events);
}

}  // namespace t3
