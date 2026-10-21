// remake/core/fighter/fighter_input.gd
// A fighter's input state (moves.md#input-state): the pads' mapped words, InputSource and
// InputUpdate with the 60-frame history and the command buffer, the STEP key helper of
// CpuOrPadInput, InputClear and LatchButtonTaps.
#include "core/fighter/fighter_input.h"

#include "content/fight_tables.h"
#include "core/ai/ai_decision.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/fighter_state.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"

namespace t3 {

FighterInput::PadWords::PadWords() {
    for (gd::Int p = 0; p < 2; ++p) {
        _counters[p].resize(16);
    }
}

void FighterInput::PadWords::read(const gd::PI32& buttons, const gd::Arr<gd::PI32>& tables) {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int raw = buttons[p] & 0xFFFF;
        physical_pressed[p] = (raw ^ physical[p]) & raw;
        physical[p] = raw;
        gd::Int mapped = 0;
        for (gd::Int bit = 0; bit < 16; ++bit) {
            if ((raw & gd::shl(1, bit)) != 0) {
                mapped |= gd::Int(tables[p][bit]);
            }
        }
        gd::Int word = mapped;
        if ((mapped & OPPOSITE_VERTICAL) == OPPOSITE_VERTICAL) {
            word = mapped & 0xAFFF;
        }
        if ((mapped & OPPOSITE_HORIZONTAL) == OPPOSITE_HORIZONTAL) {
            word &= 0x5FFF;
        }
        pressed[p] = (word ^ held[p]) & word;
        held[p] = word;
        repeat[p] = _repeat(p);
    }
}

gd::Int FighterInput::PadWords::_repeat(gd::Int p) {
    gd::Int word = 0;
    gd::PI32 counters = _counters[p];
    for (gd::Int k = 0; k < 16; ++k) {
        gd::Int bit = 15 - k;
        word <<= 1;
        if ((physical[p] & gd::shl(1, bit)) == 0) {
            counters[bit] = 0;
        } else if ((physical_pressed[p] & gd::shl(1, bit)) == 0) {
            gd::Int c = counters[bit] + 1;
            counters[bit] = c & 0xFF;
            if (c > REPEAT_DELAY) {
                word |= (c & 3) == 0 ? 1 : 0;
                counters[bit] = (c - REPEAT_STEP) & 0xFF;
            }
        } else {
            counters[bit] = 1;
            word |= 1;
        }
    }
    return word;
}

void FighterInput::source(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                          const gd::Ref<PadWords>& pads, const gd::Ref<AiDecision>& cpu) {
    switch (gd::Int(fight->input_mode[f->index])) {
        case 0:
            clear(fight, f);
            break;
        case 1:
            if (f->is_cpu != 0) {
                _cpu_input(fight, f, cpu);
            } else {
                _pad_input(fight, f, pads);
            }
            break;
        case 2: {
            gd::Int word = f->script_input & 0xFFFF;
            update(fight, f, 0, word, word & (word ^ fight->script_previous[f->index]));
            fight->script_previous[f->index] = word;
            break;
        }
        default:
            break;
    }
}

void FighterInput::_cpu_input(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                              const gd::Ref<AiDecision>& cpu) {
    gd::PI32 words{0, 0};
    if (f->health != 0) {
        words = cpu->update(f);
    }
    if (fight->mode == GameMode::PRACTICE && fight->practice_pads) {
        fight->practice_pads(f->index, words[0]);
    }
    update(fight, f, 0, words[0], words[1] & 0xFFFF);
}

void FighterInput::_pad_input(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                              const gd::Ref<PadWords>& pads) {
    gd::Int p = f->index;
    if (fight->mode == GameMode::FORCE) {
        update_side(f, fight->fighters[f->cur_opp_index]);
        p = fight->force->human();
    } else {
        update_side(f, fight->fighters[(f->player_index + 1) & 1]);
    }
    // Practice's dummy on CONTROLLER takes the other pad (FUN_800B4800).
    if (fight->mode == GameMode::PRACTICE && fight->practice_swap) {
        p = (p + 1) & 1;
    }
    gd::Int held = pads->held[p];
    gd::Int pressed = pads->pressed[p];
    gd::Int step_dir = 0;
    gd::Int step_press = 0;
    if ((pressed & STEP_UP) != 0) {
        step_press = PadState::UP;
        fight->step_helper_side[p] = 0;
        fight->step_helper_timer[p] = STEP_HOLD;
    } else if ((pressed & STEP_DOWN) != 0) {
        step_press = PadState::DOWN;
        fight->step_helper_side[p] = 1;
        fight->step_helper_timer[p] = STEP_HOLD;
    }
    if (fight->step_helper_timer[p] > 0) {
        fight->step_helper_timer[p] -= 1;
        gd::Int left = fight->step_helper_timer[p];
        if (left > 0x21) {
            step_dir = fight->step_helper_side[p] == 0 ? PadState::UP : PadState::DOWN;
        }
        bool holding = (fight->step_helper_side[p] == 0 && (pads->held[p] & STEP_UP) != 0)
            || (fight->step_helper_side[p] == 1 && (pads->held[p] & STEP_DOWN) != 0);
        if (holding && (held & PadState::FACE_BUTTONS) == 0) {
            held &= 0xFFFF0FFFLL;
            pressed &= 0xFFFF0FFFLL;
        }
    }
    if (fight->mode == GameMode::PRACTICE && fight->practice_pads) {
        fight->practice_pads(f->index, held | step_dir);
    }
    update(fight, f, f->side_flag, held | step_dir, (pressed | step_press) & 0xFFFF);
}

void FighterInput::update_side(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    if (f->fixed_facing == 0) {
        f->side_flag = opp->screen_x <= f->screen_x ? 1 : 0;
    } else {
        f->side_flag = f->fixed_facing_side;
    }
}

void FighterInput::update(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f, gd::Int side,
                          gd::Int held, gd::Int pressed) {
    f->in_hist_index = (f->in_hist_index & 0xFFFFFFFFLL) < 0x3B ? f->in_hist_index + 1 : 0;
    gd::Int direction = NEUTRAL;
    if (side < 2) {
        gd::PI32 table = fight->tables->directions[side];
        for (gd::Int d = 0; d < 9; ++d) {
            if (table[d] == (held & DIRECTIONS)) {
                direction = d + 1;
                break;
            }
        }
    }
    gd::Int held_bits = (held & PAD_LP) != 0 ? HIST_LP : 0;
    if ((held & PAD_RP) != 0) {
        held_bits |= HIST_RP;
    }
    if ((held & PAD_LK) != 0) {
        held_bits |= HIST_LK;
    }
    if ((held & PAD_RK) != 0) {
        held_bits |= HIST_RK;
    }
    f->in_held = held_bits >> 4;
    gd::Int bits = 0;
    if ((pressed & PAD_LP) != 0) {
        bits = HIST_LP;
        f->press_count[0] += 1;
    }
    if ((pressed & PAD_RP) != 0) {
        bits |= HIST_RP;
        f->press_count[1] += 1;
    }
    if ((pressed & PAD_LK) != 0) {
        bits |= HIST_LK;
        f->press_count[2] += 1;
    }
    if ((pressed & PAD_RK) != 0) {
        bits |= HIST_RK;
        f->press_count[3] += 1;
    }
    fight->latch_register = bits;
    f->in_history[f->in_hist_index] = (bits | direction) & 0xFF;
    f->in_pressed = bits >> 4;
    f->in_dir = gd::shl(1, (direction & 0xF) + 4);
    gd::Int old = f->in_pressed_hist[f->in_hist_index];
    if ((old & 0x80) != 0) {
        f->press_count[0] -= 1;
    }
    if ((old & 0x10) != 0) {
        f->press_count[1] -= 1;
    }
    if ((old & 0x40) != 0) {
        f->press_count[2] -= 1;
    }
    if ((old & 0x20) != 0) {
        f->press_count[3] -= 1;
    }
    f->in_pressed_hist[f->in_hist_index] = pressed & 0xFFFF;
    if ((f->cmd_count & 0xFFFFFFFFLL) > 9) {
        return;
    }
    if (f->cmd_merge_timer > 0) {
        f->cmd_merge_timer -= 1;
    }
    if (bits == 0) {
        return;
    }
    if (f->cmd_merge_timer > 0 && (held_bits & ~bits) == (f->cmd_buffer[f->cmd_count] & 0xF0)) {
        f->cmd_buffer[f->cmd_count] = (held_bits + direction) & 0xFF;
    } else {
        if (f->cmd_count > 8) {
            return;
        }
        f->cmd_count += 1;
        f->cmd_buffer[f->cmd_count] = (held_bits + direction) & 0xFF;
    }
    f->cmd_merge_timer = 3;
}

void FighterInput::clear(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f) {
    for (gd::Int i = 0; i < HISTORY; ++i) {
        f->in_history[i] = NEUTRAL;
        f->in_pressed_hist[i] = 0;
    }
    f->in_dir = NEUTRAL;
    f->cmd_count = -1;
    f->cmd_read = -1;
    f->in_hist_index = 0;
    f->in_held = 0;
    f->in_pressed = 0;
    for (gd::Int i = 0; i < 4; ++i) {
        f->press_count[i] = 0;
    }
    f->script_input = 0;
    fight->script_previous[f->index] = 0;
    fight->tap_previous[f->index] = 0;
    fight->latch_register = NEUTRAL;
}

void FighterInput::buffer_enable(const gd::Ref<FighterState>& f, bool on) {
    if (on && f->cmd_count < 0) {
        f->cmd_count = 0;
        f->cmd_read = 0;
        f->cmd_merge_timer = 0;
    } else if (!on && f->cmd_count >= 0) {
        f->cmd_count = -1;
        f->cmd_read = -1;
        f->cmd_merge_timer = -1;
    }
}

void FighterInput::latch_taps(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f) {
    if (fight->round_frame_seen == fight->round_frame) {
        return;
    }
    bool frozen = f->cond_flag_used == 1 && (f->tap_lp != 0 || f->tap_rp != 0 || f->tap_both != 0);
    if (!frozen) {
        gd::Int entry = f->in_history[f->in_hist_index];
        fight->latch_register = entry;
        gd::Int fresh = entry & (entry ^ fight->tap_previous[f->index]);
        gd::Int lp = fresh & HIST_LP;
        gd::Int rp = fresh & HIST_RP;
        if ((entry & (HIST_LP | HIST_RP)) == (HIST_LP | HIST_RP) && (lp != 0 || rp != 0)) {
            f->tap_both = 1;
        } else if (lp != 0) {
            f->tap_lp = 1;
        } else if (rp != 0) {
            f->tap_rp = 1;
        } else {
            f->tap_both = 0;
            f->tap_lp = 0;
            f->tap_rp = 0;
        }
    } else if (fight->rules->fix_tap_latch_register) {
        fight->latch_register = f->in_history[f->in_hist_index];
    }
    fight->tap_previous[f->index] = fight->latch_register & 0xFFFF;
}

}  // namespace t3
