// remake/core/game/practice_mode.gd
// PRACTICE MODE (practice.ovl, mode 5; modes.md#practice): the practice state S (0x800B9180,
// kept as bytes), its set-up at each round start (FUN_800B2D74), and the frame hook FightFrame
// calls last (FUN_800B3084): the pause menu (practice_sim.py's verified menu_cursor/menu_input),
// the training dummy's scripted input (FUN_800B4684, FUN_800B87C8), the attack data
// (FUN_800B3808), the freeze signal (FUN_800B4600, FUN_800B3520, FUN_800B6CF4), the key ring and
// the combo guide (FUN_800B7A88 … FUN_800B86F0), and the replay settings (FUN_800B8D50).
#include "core/game/practice_mode.h"

#include "content/flow_data.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/ai/cpu_opponent.h"
#include "core/camera/camera_director.h"
#include "core/combat/hit_slot.h"
#include "core/combat/state_bit.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/move_list_screen.h"
#include "core/fight/replay_state.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_sounds.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/move_system.h"
#include "core/fighter/transition.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

PracticeMode::PracticeMode(GameFlow* flow) : FlowPart(flow) {
    s = gd::make<ByteBlock>(SIZE);
}

// Several byte fields at once: offset, value, offset, value …
void PracticeMode::_puts(const gd::Arr<gd::Int>& pairs) {
    for (gd::Int i = 0; i < pairs.size(); i += 2) {
        put(pairs[i], pairs[i + 1]);
    }
}

gd::Int PracticeMode::b(gd::Int at) {
    return s->u8(at);
}

gd::Int PracticeMode::sb(gd::Int at) {
    return s->s8(at);
}

void PracticeMode::put(gd::Int at, gd::Int v) {
    s->put8(at, v);
}

gd::Int PracticeMode::pd(gd::Int player, gd::Int at) {
    return PLAYER_DATA + 0x20 * player + at;
}

// ---- the fight's hooks

// FUN_800B4800: the dummy on CONTROLLER reads the other pad.
bool PracticeMode::pad_swap() {
    return b(SWAP) != 0;
}

// FUN_800B7AEC: a fighter's pad word of the frame (the key display's source).
void PracticeMode::record_pad(gd::Int index, gd::Int word) {
    pads[index] = word & 0xFFFF;
}

// FUN_800B2D74 (FUN_8002AB68 in mode 5): the settings of a new practice session's round.
void PracticeMode::setup() {
    gd::Ref<FightState> fight = g->fight;
    put(PAUSED, 0);
    put(PAUSED_BEFORE, 0);
    put(MODE_SELECT, 1);
    put(SUB, 0);
    fight->practice_intro = 1;
    put(INTRO_DELAY, 0x1E);
    for (gd::Int at : gd::Arr<gd::Int>{CURSOR_FREE, CURSOR_VS, CURSOR_COMBO, FREEZE_SIGNAL, COUNTER_ATTACKS,
                                       REPLAY_SETTING, SWAP, GUIDE_PROMPT, DUMMY, COMBO_TYPE, RESUMING}) {
        put(at, 0);
    }
    put(ATTACK_DATA, 1);
    put(DUMMY_KIND, 1);
    put(CPU_DIFFICULTY, 1);
    put(CPU_LEVEL, 2);
    put(KEY_DISPLAY, 1);
    put(GUIDE_LENGTH, 0xF);
    put(GUIDE, 0);
    put(COMBO_PLAYER, fight->tie_choice);
    for (gd::Int p = 0; p < 2; ++p) {
        put(ACTING + p, 0);
        put(CAN_ACT + p, 0);
    }
    key_ring_reset(0);
    key_ring_reset(1);
    put(MENU_DELAY, 0);
    put(COMMAND_LIST, 0);
    put(GUIDE_STATE, GuideState::IDLE);
    _guide_moves_reset();
    _input_modes(1);
    _signal_clear();
    put(MARKER_INDEX, 0);
    put(PROMPT_TIMER, 0);
    put(PROMPT, 0);
    put(CHANGED, 1);
    put(SWAP, 0);
    _ai_reset(fight->fighters[1]);
    _input_modes(0);
    fight->counter_forced[0] = 0;
    fight->counter_forced[1] = 0;
    fight->counter_forced[2] = 0;
    for (gd::Int p = 0; p < 2; ++p) {
        for (gd::Int at = 0; at < 0x20; ++at) {
            put(pd(p, at), 0);
        }
    }
}

// AiReset (0x800596B0) of the dummy with the page's CPU DIFFICULTY and LEVEL.
void PracticeMode::_ai_reset(const gd::Ref<FighterState>& f) {
    if (f->ai_slot >= 0 && g->sim->ai->ai_self != nullptr) {
        g->sim->ai->reset(f, sb(CPU_DIFFICULTY), sb(CPU_LEVEL), -1);
    }
}

// FUN_8002BE54: every fighter's input cleared and its input mode set.
void PracticeMode::_input_modes(gd::Int mode) {
    for (const gd::Ref<FighterState>& f : g->fight->fighters) {
        FighterInput::clear(g->fight, f);
        g->fight->input_mode[f->index] = mode;
    }
}

void PracticeMode::_signal_clear() {
    for (gd::Int p = 0; p < 2; ++p) {
        g->fight->signal_colour[p] = Color8(0, 0, 0, 0);
    }
    for (gd::Int i = 0; i < 4; ++i) {
        s->put16(MARKERS + 8 * i + 2, 0);
    }
}

// FUN_800B3084 (FightFrame's last step in mode 5): returns the paused flag (0x800958DC).
gd::Int PracticeMode::frame() {
    gd::Ref<FightState> fight = g->fight;
    gd::Ref<GameGlobals> gl = g->globals;
    fight->hud_shown = 0;
    put(PLAYER, fight->tie_choice);
    put(OTHER, gl->other_player);
    gd::Ref<FighterState> me = fight->fighters[fight->tie_choice];
    gd::Ref<FighterState> dummy = fight->fighters[gl->other_player];
    gd::Ref<FighterInput::PadWords> pads_now = g->sim->pads;
    gd::Int p = fight->tie_choice;
    s->put16(HELD, pads_now->physical[p]);
    s->put16(PRESSED, pads_now->physical_pressed[p]);
    s->put16(REPEAT, pads_now->repeat[p]);
    s->put16(MAPPED_HELD, pads_now->held[p]);
    s->put16(MAPPED_PRESSED, pads_now->pressed[p]);
    if (fight->replay_playing == 0) {
        put(PAUSED, fight->pause_request);
        if (fight->practice_intro != 0) {
            s->put16(HELD, 0);
            s->put16(PRESSED, 0);
            s->put16(REPEAT, 0);
            if (fight->pause_request != 0) {
                put(INTRO_DELAY, sb(INTRO_DELAY) - 1);
                if (sb(INTRO_DELAY) < 0) {
                    put(INTRO_DELAY, 0);
                    fight->practice_intro = 0;
                    _input_modes(1);
                    key_ring_reset(0);
                    key_ring_reset(1);
                }
            }
        }
        if (b(PAUSED) != b(PAUSED_BEFORE)) {
            if (b(PAUSED) == 0) {
                _resume(me, dummy);
            } else {
                put(COMMAND_LIST, 0);
                s->put16(PRESSED, s->u16(PRESSED) & 0xF7FF);
                put(GUIDE_STATE, GuideState::IDLE);
                _guide_moves_reset();
                _input_modes(1);
                _signal_clear();
                put(MARKER_INDEX, 0);
                put(PROMPT_TIMER, 0);
                put(PROMPT, 0);
                put(CHANGED, 1);
                put(SWAP, 0);
                put(MENU_DELAY, 3);
                if (b(RESUMING) != 0) {
                    g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
                }
            }
        }
        put(PAUSED_BEFORE, b(PAUSED));
        if (b(MENU_DELAY) != 0) {
            put(MENU_DELAY, b(MENU_DELAY) - 1);
        }
        if (b(PAUSED) == 0) {
            _attack_data(me, dummy);
        } else if (b(MENU_DELAY) == 0) {
            if (b(COMMAND_LIST) == 0) {
                _menu_cursor();
            }
            _menu_input();
        }
    }
    _freeze_signal(me, dummy);
    _signal_colours();
    _replay_control(dummy, me);
    fight->practice_swap = b(SWAP) != 0;
    return b(PAUSED);
}

// OK: the menu closes at the next pause check.
void PracticeMode::resume() {
    g->fight->pause_close = 1;
    put(RESUMING, 1);
}

// The remake's Escape on the menu (not while COMMAND LIST is shown, which Escape's Start turns
// off): as OK. False when the menu is not taking input.
bool PracticeMode::escape() {
    if (b(PAUSED) == 0 || b(COMMAND_LIST) != 0 || b(MENU_DELAY) != 0) {
        return false;
    }
    resume();
    return true;
}

// FUN_800B2BC4: leaving the pause menu applies the dummy and the page's settings.
void PracticeMode::_resume(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy) {
    _dummy_control(me, dummy);
    if (sb(DUMMY_KIND) == 2) {
        _ai_reset(dummy);
    }
    put(RESUMED_INPUT, 0);
    switch (sb(SUB)) {
        case 1:
            put(COUNTER_ATTACKS, 0);
            put(GUIDE, 0);
            key_ring_reset(b(PLAYER));
            break;
        case 0:
            put(GUIDE, ((s->u16(HELD) & 0x2F) == 0x2F && sb(REPLAY_SETTING) != 4) ? 1 : 0);
            key_ring_reset(b(PLAYER));
            replay_reset(b(PLAYER));
            break;
        case 2:
            put(COUNTER_ATTACKS, 0);
            put(GUIDE, 1);
            put(KEY_DISPLAY, 1);
            key_ring_reset(sb(COMBO_PLAYER));
            replay_reset(sb(COMBO_PLAYER));
            load_guide(sb(COMBO_PLAYER), 0);
            break;
        default:
            break;
    }
}

// ---- the training dummy (FUN_800B4684, FUN_800B87C8 and its guards)

void PracticeMode::_dummy_control(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy) {
    gd::Ref<FightState> fight = g->fight;
    me->human_guard = 1;
    me->is_cpu = 0;
    fight->input_mode[me->index] = 1;
    switch (sb(SUB)) {
        case 1:
            put(DUMMY_KIND, 2);
            break;
        case 0:
            put(DUMMY_KIND, sb(DUMMY) == 8 ? 0 : 1);
            break;
        case 2:
            put(DUMMY_KIND, 1);
            put(DUMMY, 0);
            break;
        default:
            break;
    }
    switch (sb(DUMMY_KIND)) {
        case 1:
            dummy->human_guard = 0;
            dummy->is_cpu = 0;
            fight->input_mode[dummy->index] = 2;
            dummy_input(dummy, me, sb(DUMMY));
            break;
        case 0:
            dummy->human_guard = 1;
            dummy->is_cpu = 0;
            fight->input_mode[dummy->index] = 1;
            break;
        case 2:
            dummy->human_guard = 0;
            dummy->is_cpu = 1;
            fight->input_mode[dummy->index] = 1;
            break;
        default:
            break;
    }
}

// FUN_800B87C8: the dummy's scripted word for the TRAINING DUMMY setting.
void PracticeMode::dummy_input(const gd::Ref<FighterState>& dummy, const gd::Ref<FighterState>& me, gd::Int setting) {
    if (dummy->bank_type == BankType::DOCTOR_B) {
        return;
    }
    if (setting == 6 || setting == 7) {
        dummy->human_guard = 0;
        if (dummy->in_reaction != 0) {
            _tech_roll(dummy, setting == 6);
            return;
        }
        _standing_ok(dummy);
        return;
    }
    if (!_standing_ok(dummy)) {
        return;
    }
    switch (setting) {
        case 1:
            dummy->script_input = 0x4000;
            break;
        case 2:
            if ((me->attack & 0x10) != 0 && me->pose_frame <= me->pose_move->active_last) {
                dummy->script_input = 0x8000;
            }
            break;
        case 3:
            dummy->script_input = 0x4000;
            if ((me->attack & 0x08) != 0 && me->pose_frame <= me->pose_move->active_last) {
                dummy->script_input = 0xC000;
            }
            break;
        case 4:
            if ((me->attack & 0x18) != 0 && me->pose_frame <= me->pose_move->active_last) {
                dummy->script_input = (me->attack & 0x10) != 0 ? 0x8000 : 0xC000;
            }
            break;
        case 5:
            dummy->human_guard = 1;
            return;
        default:
            break;
    }
    dummy->human_guard = 0;
}

// FUN_800B8904: lying down, the dummy holds up; true when it may act. A stance slot empty in
// both banks has no row (bug #61, not reproduced).
bool PracticeMode::_standing_ok(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> row = f->move_row != nullptr ? f->move_row : g->fight->move_for_slot(f, f->pose_move->stance_slot);
    f->script_input = row != nullptr && (row->state & (StateBit::DOWN_REVERSED | StateBit::DOWN)) != 0 ? 0x1000 : 0;
    return f->script_input == 0;
}

// FUN_800B8AEC: UKEMI MAE (LK+RK) or OKU (LP+RP) on alternate frames, during a reaction with
// an air window and a tech-roll branch.
void PracticeMode::_tech_roll(const gd::Ref<FighterState>& f, bool forward) {
    if (f->move_row != nullptr || f->in_reaction == 0 || f->pose_move->air_first == 0 || !_has_tech_roll(f)) {
        return;
    }
    f->script_input = (g->region->frame_counter & 1) != 0 ? (forward ? 0x60 : 0x90) : 0;
}

// FUN_800B8B94: a branch row of the move leads to the tech roll (transition 0x1C).
bool PracticeMode::_has_tech_roll(const gd::Ref<FighterState>& f) {
    gd::Arr<gd::PI32> rows = f->pose_move->bank->branches;
    gd::Int i = f->pose_move->branches;
    while (i >= 0 && i < rows.size() && gd::Int(rows[i][MoveSystem::ROW_COMMAND]) != MoveSystem::END) {
        if (gd::Int(rows[i][MoveSystem::ROW_FLAGS]) == 0x1C) {
            return true;
        }
        i += 1;
    }
    return false;
}

// FUN_800B899C: the guide's replay may start once the fighter stands in its stance (a turned
// fighter first turns back).
bool PracticeMode::_ready_to_replay(const gd::Ref<FighterState>& f) {
    if (f->facing_quadrant != 0) {
        f->script_input = f->move_row == nullptr ? 0x8000 : 0;
        return false;
    }
    if (f->cur_slot == 3 || f->cur_slot == 4 || f->cur_slot == 5 || f->cur_slot == f->pose_move->stance_slot) {
        f->script_input = 0;
        return true;
    }
    return false;
}

// ---- the freeze signal (FUN_800B4600, FUN_800B3520, FUN_800B6CF4)

void PracticeMode::_freeze_signal(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy) {
    if (b(FREEZE_SIGNAL) == 0) {
        return;
    }
    put(CAN_ACT + me->player_index, g->sim->moves->can_act(me, dummy) ? 1 : 0);
    put(CAN_ACT + dummy->player_index, g->sim->moves->can_act(dummy, me) ? 1 : 0);
}

// The back light colours: green while a fighter can act, red while it cannot.
void PracticeMode::_signal_colours() {
    gd::Ref<FightState> fight = g->fight;
    if (fight->practice_intro != 0 || b(PAUSED) != 0) {
        return;
    }
    for (gd::Int p = 0; p < 2; ++p) {
        if (b(FREEZE_SIGNAL) == 0) {
            fight->signal_colour[p] = Color8(0, 0, 0, 0);
        } else if (b(CAN_ACT + p) == 0) {
            fight->signal_colour[p] = Color8(250, 0, 0, 1);
        } else {
            fight->signal_colour[p] = Color8(0, 250, 0, 1);
        }
    }
}

// ---- the key ring and the combo guide

// FUN_800B7A88: the ring emptied (its flag bits kept).
void PracticeMode::key_ring_reset(gd::Int player) {
    for (gd::Int i = 0; i < RING_WORDS; ++i) {
        s->put32(RING + 4 * i, s->u32(RING + 4 * i) & 0x80000000LL);
    }
    s->put32(HEADS + 4 * player, 0);
    guide_wait[player] = GUIDE_WAIT_FRAMES;
}

// FUN_800B8244: the ring's replay restarts at its first entry.
void PracticeMode::replay_reset(gd::Int player) {
    _replay_rewind(player);
}

// FUN_800B82F4: the recording ends (its last entry counts once), and the replay rewinds.
void PracticeMode::_recording_end(gd::Int player) {
    gd::Int at = RING + 4 * s->s32(HEADS + 4 * player);
    s->put32(at, (s->u32(at) & 0x8000FFFFLL) | 0x10000);
    _replay_rewind(player);
}

void PracticeMode::_replay_rewind(gd::Int player) {
    if (s->s32(HEADS + 4 * player) < 0) {
        s->put32(REPLAY + 4, s->u32(REPLAY + 4) & 0x80000000LL);
    } else {
        s->put32(REPLAY + 4, s->u32(RING));
    }
    s->put32(REPLAY, 0);
    s->put32(REPLAY + 8, 0);
    for (gd::Int i = 0; i < RING_WORDS; ++i) {
        s->put16(MARKS + 2 * i, 0);
    }
}

gd::Arr<gd::Dict<std::string, gd::PI32>> PracticeMode::_combos(gd::Int player) {
    gd::Int kind = g->fight->fighters[player]->bank_type;
    return kind >= 0 && kind < g->data->practice_combos.size() ? g->data->practice_combos[kind]
                                                               : gd::Arr<gd::Dict<std::string, gd::PI32>>();
}

// FUN_800B8108: the combo count of the combo player's character.
gd::Int PracticeMode::combo_count() {
    return _combos(sb(COMBO_PLAYER)).size();
}

// FUN_800B7FB4: combo `combo` of the player's character into the ring as the guide.
gd::Int PracticeMode::load_guide(gd::Int player, gd::Int combo) {
    gd::Arr<gd::Dict<std::string, gd::PI32>> list = _combos(player);
    if (list.is_empty()) {
        return 0;
    }
    gd::PI32 steps = list[combo].at("steps");
    if (steps.is_empty()) {
        key_ring_reset(player);
        return 0;
    }
    s->put32(HEADS + 4 * player, steps.size() - 1);
    for (gd::Int i = 0; i < steps.size(); ++i) {
        s->put32(RING + 4 * i, steps[i]);
    }
    return steps.size();
}

// FUN_800B85A0: the guide's move slots from MARKS, their count at REPLAY + 8.
gd::Int PracticeMode::_guide_moves(gd::Int player, gd::Int combo) {
    gd::Arr<gd::Dict<std::string, gd::PI32>> list = _combos(player);
    if (list.is_empty()) {
        return 0;
    }
    gd::PI32 moves = list[combo].at("moves");
    if (moves.is_empty()) {
        key_ring_reset(player);
        return 0;
    }
    s->put32(REPLAY + 8, moves.size());
    for (gd::Int i = 0; i < moves.size(); ++i) {
        s->put16(MARKS + 2 * i, moves[i]);
    }
    return moves.size();
}

// FUN_800B8764.
void PracticeMode::_guide_moves_reset() {
    put(0xA6, 1);
    put(0xA5, 0);
    _guide_moves(sb(COMBO_PLAYER), sb(COMBO_TYPE));
    s->put32(CHECKED + 4 * sb(COMBO_PLAYER), 0);
}

// FUN_800B7B08: the fighter's pad word logged into the ring (a repeat counts on).
void PracticeMode::_key_log(gd::Int player) {
    gd::Int head = s->s32(HEADS + 4 * player);
    gd::Int at = RING + 4 * head;
    gd::Int word = pads[player];
    gd::Int buttons = word & 0xF0F0;
    gd::Int entry = s->u32(at);
    if ((((entry & 0xFF) << 4) | (((entry >> 8) & 0xF) << 12)) == buttons) {
        gd::Int kept = entry & 0x8000FFFFLL;
        gd::Int count = ((Fx::w32((entry << 1) & 0xFFFFFFFFLL) >> 17) + 1) & 0x7FFF;
        gd::Int v = kept | (count << 16);
        if ((Fx::w32((v << 1) & 0xFFFFFFFFLL) >> 17) > 0x3FFE) {
            v = kept | 0x3FFF0000;
        }
        s->put32(at, v);
        return;
    }
    head += 1;
    head = head < 0 ? RING_WORDS - 1 : (head >= RING_WORDS ? 0 : head);
    at = RING + 4 * head;
    gd::Int side = g->fight->fighters[player]->side_flag != 0 ? 0x80000000LL : 0;
    s->put32(at, ((buttons >> 4) & 0xFF) | (((word >> 12) & 0xFF) << 8) | 0x10000 | side);
    s->put32(HEADS + 4 * player, head);
}

// FUN_800B7968: true when a new input comes after the ring has been idle for 30 frames.
bool PracticeMode::_ring_idle(gd::Int player) {
    gd::Ref<FighterState> f = g->fight->fighters[player];
    bool active = (gd::Int(g->sim->pads->held[player]) & 0xF0F0) != 0;
    if (f->cur_slot == f->pose_move->stance_slot) {
        guide_wait[player] -= 1;
    }
    if (!active && (f->move_row == nullptr || f->transition == Transition::HOLD_TURN || f->cur_slot == f->move_slot)) {
        guide_wait[player] -= 1;
    }
    guide_wait[player] = gd::maxi(guide_wait[player], 0);
    if (active) {
        gd::Int was = guide_wait[player];
        guide_wait[player] = GUIDE_WAIT_FRAMES;
        return was == 0;
    }
    return false;
}

// FUN_800B83A4: the guide's next scripted word for the player (false once the ring is played).
bool PracticeMode::_guide_play(gd::Int player) {
    gd::Ref<FighterState> f = g->fight->fighters[player];
    while (true) {
        gd::Int word = s->u32(REPLAY + 4);
        gd::Int kept = word & 0x8000FFFFLL;
        gd::Int count = (Fx::w32((word << 1) & 0xFFFFFFFFLL) >> 17) - 1;
        s->put32(REPLAY + 4, kept | ((count & 0x7FFF) << 16));
        if ((((count << 17) & 0xFFFFFFFFLL) & 0x80000000LL) == 0) {
            gd::Int direction = (s->u32(REPLAY + 4) >> 8) & 0xFF;
            if ((kept & 0x80000000LL) != 0) {
                if ((direction & 8) != 0) {
                    direction = (direction & 0xFFF7) | 2;
                } else if ((direction & 2) != 0) {
                    direction = (direction & 0xFFFD) | 8;
                }
            }
            f->script_input = (((s->u32(REPLAY + 4) & 0xFF) << 4) | (direction << 12)) & 0xFFFF;
            g->fight->input_mode[f->index] = 2;
            return true;
        }
        gd::Int pos = s->s32(REPLAY) + 1;
        s->put32(REPLAY, pos);
        gd::Int head = s->s32(HEADS + 4 * player);
        if (head < pos || pos >= RING_WORDS) {
            s->put32(REPLAY, 0);
            if (head < 0) {
                s->put32(REPLAY + 4, s->u32(REPLAY + 4) & 0x80000000LL);
            } else {
                s->put32(REPLAY + 4, s->u32(RING));
            }
            return false;
        }
        s->put32(REPLAY + 4, s->u32(RING + 4 * pos));
    }
    return false;
}

// FUN_800B8510: the move the player starts at its active frame, logged after the guide's moves.
void PracticeMode::_guide_move_log(gd::Int player) {
    gd::Ref<FighterState> f = g->fight->fighters[player];
    if (f->pose_move->active_first == 0 || f->pose_frame != f->pose_move->active_first) {
        return;
    }
    gd::Int n = s->s32(REPLAY + 8);
    s->put16(MARKS + 2 * n, f->cur_slot);
    n += 1;
    s->put32(REPLAY + 8, n >= RING_WORDS ? 0 : n);
}

// FUN_800B86F0: the combo player landed a clean hit, or is in a throw transition.
bool PracticeMode::_guide_hit() {
    gd::Ref<FighterState> f = g->fight->fighters[sb(COMBO_PLAYER)];
    if (f->hit_clean != 0) {
        return true;
    }
    return f->move_row != nullptr && f->transition == Transition::HOLD_TURN;
}

// ---- the attack data (FUN_800B3808)

void PracticeMode::_attack_data(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy) {
    gd::Ref<FightState> fight = g->fight;
    _dummy_control(me, dummy);
    gd::Int cp = sb(COMBO_PLAYER);
    if (b(GUIDE) == 0) {
        put(GUIDE_LENGTH, 0xF);
        put(GUIDE_PROMPT, 0);
        put(PROMPT, 1);
        put(PROMPT_TIMER, 0);
        if (_ring_idle(cp)) {
            key_ring_reset(cp);
        }
        _key_log(cp);
    } else {
        _guide_step(me, cp);
    }
    s->put16(SCRIPT_EDGE, me->script_input & (me->script_input ^ s->u16(SCRIPT_PREVIOUS)));
    s->put16(SCRIPT_PREVIOUS, me->script_input);
    if (b(GUIDE_PROMPT) != 0 && (sb(SUB) != 0 || b(GUIDE) == 0)) {
        _button_sounds(me->player_index, s->u16(SCRIPT_EDGE));
    }
    fight->counter_forced[dummy->player_index] = b(COUNTER_ATTACKS) != 0 ? 1 : 0;
    gd::Int acting = _acting(me, dummy) ? 1 : 0;
    put(ACTING + me->player_index, acting);
    put(ACTING + dummy->player_index, acting);
    _player_data(me, dummy);
    _player_data(dummy, me);
    for (gd::Int i = 0; i < 4; ++i) {
        gd::Int at = MARKERS + 8 * i + 2;
        s->put16(at, gd::maxi(s->s16(at) - 1, 0));
    }
    if (dummy->hit_clean != 0) {
        gd::Int k = b(MARKER_INDEX) + 1;
        if (k > 3) {
            k = 0;
        }
        put(MARKER_INDEX, k);
        marker_points[k] = dummy->hit_slots[dummy->best_hit_slot]->point.duplicate();
        s->put16(MARKERS + 8 * k + 2, 0x3C);
        s->put16(MARKERS + 8 * k, me->attack);
    }
}

// The COMBO TRAINING guide and the FREE page's recorded replay (FUN_800B3808's state machine).
void PracticeMode::_guide_step(const gd::Ref<FighterState>& me, gd::Int cp) {
    gd::Ref<FightState> fight = g->fight;
    switch (b(GUIDE_STATE)) {
        case GuideState::IDLE:
            if (sb(SUB) == 2) {
                put(GUIDE_PROMPT, 1);
                replay_reset(cp);
                _guide_moves_reset();
                put(GUIDE_LENGTH, load_guide(cp, sb(COMBO_TYPE)));
                if (b(GUIDE_LENGTH) == 0) {
                    put(GUIDE_LENGTH, 0xF);
                }
                put(GUIDE_STATE, GuideState::READY);
            } else if (sb(SUB) == 0 && b(GUIDE) != 0) {
                put(GUIDE_STATE, GuideState::READY);
            }
            break;
        case GuideState::READY:
            _guide_ready(cp);
            break;
        case GuideState::RECORD_WAIT:
        case GuideState::RECORDING:
            if (b(GUIDE_STATE) == GuideState::RECORD_WAIT) {
                if ((s->u16(MAPPED_PRESSED) & RECORD_KEYS) == 0) {
                    return;
                }
                put(GUIDE_STATE, GuideState::RECORDING);
            }
            _key_log(cp);
            if ((s->u16(PRESSED) & PadState::SELECT) == 0 && s->s32(HEADS + 4 * cp) != RING_WORDS - 1) {
                return;
            }
            _recording_end(cp);
            put(GUIDE_STATE, GuideState::READY);
            break;
        case GuideState::GUIDE_STAND: {
            gd::Ref<FighterState> f = fight->fighters[cp];
            if (!_standing_ok(f)) {
                return;
            }
            if (_ready_to_replay(f)) {
                put(GUIDE_STATE, GuideState::GUIDE_PLAY);
            }
            break;
        }
        case GuideState::GUIDE_PLAY:
            if (!_guide_play(cp)) {
                put(PROMPT_TIMER, 0x60);
                put(PROMPT, 3);
                put(GUIDE_STATE, GuideState::IDLE);
            } else if (_guide_hit()) {
                put(PROMPT, 2);
                put(GUIDE_STATE, GuideState::IDLE);
            } else {
                _guide_move_log(cp);
            }
            break;
        case GuideState::REPLAY:
            if (_guide_play(cp)) {
                _guide_move_log(cp);
            } else {
                put(GUIDE_STATE, GuideState::REPLAY_END);
            }
            break;
        case GuideState::REPLAY_END:
            if (me->cur_slot == 3 || me->cur_slot == me->pose_move->stance_slot) {
                put(GUIDE_PROMPT, 1);
                put(GUIDE_STATE, GuideState::IDLE);
                if ((s->u16(HELD) & PadState::SELECT) != 0) {
                    put(RESUMED_INPUT, 0);
                }
            } else {
                _guide_move_log(cp);
            }
            break;
        case GuideState::MOVE_END:
            if (me->pose_move->active_first == 0 || me->cur_transition == Transition::CONTINUE_FIXED) {
                put(GUIDE_STATE, GuideState::IDLE);
            }
            break;
        default:
            break;
    }
}

// READY: the prompt; in combo training Select plays the guide, in FREE Select replays the
// recorded input and down + Select records a new one.
void PracticeMode::_guide_ready(gd::Int cp) {
    gd::Ref<FightState> fight = g->fight;
    gd::Int t = b(PROMPT_TIMER);
    put(PROMPT_TIMER, t - 1);
    if (t == 0) {
        put(PROMPT_TIMER, 0);
        put(PROMPT, 2);
    }
    if (sb(SUB) == 2 && combo_count() != 0) {
        if ((s->u16(PRESSED) & PadState::SELECT) != 0) {
            FighterInput::clear(fight, fight->fighters[cp]);
            fight->input_mode[cp] = 2;
            put(PROMPT, 4);
            put(GUIDE_PROMPT, 2);
            put(GUIDE_STATE, GuideState::GUIDE_STAND);
            return;
        }
        if (b(PLAYER) == fight->tie_choice) {
            _button_sounds(cp, s->u16(MAPPED_PRESSED));
        }
    }
    if (sb(SUB) != 0 || b(GUIDE) == 0
            || ((s->u16(PRESSED) & PadState::SELECT) == 0 && b(RESUMED_INPUT) == 0)) {
        return;
    }
    if ((s->u16(HELD) & PadState::DOWN) == 0) {
        if ((s->u16(HELD) & PadState::HORIZONTAL) != 0) {
            put(RESUMED_INPUT, 1);
        }
        FighterInput::clear(fight, fight->fighters[cp]);
        replay_reset(cp);
        fight->input_mode[cp] = 2;
        put(GUIDE_PROMPT, 2);
        put(PROMPT, 1);
        put(GUIDE_STATE, GuideState::REPLAY);
    } else {
        key_ring_reset(cp);
        put(GUIDE_PROMPT, 2);
        put(PROMPT, 1);
        put(GUIDE_STATE, GuideState::RECORD_WAIT);
    }
}

// FUN_800B680C: the button sounds of the guide's replay.
void PracticeMode::_button_sounds(gd::Int player, gd::Int word) {
    for (gd::Int i = 0; i < PLAYER_SOUNDS.size(); i += 2) {
        if ((word & PLAYER_SOUNDS[i]) != 0) {
            g->sim->events->add(SimEvents::Kind::SOUND, -1, PLAYER_SOUNDS[i + 1], player);
        }
    }
}

// FUN_800B4818: the player is in an attack, or the dummy left its stance after a hit.
bool PracticeMode::_acting(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& o) {
    bool attacking = f->pose_move->active_first != 0 || (f->move_row != nullptr && f->move_row->active_first != 0);
    bool moved = o->cur_slot != o->pose_move->stance_slot && (o->was_hit_this_move != 0 || o->react_chain != 0);
    return attacking || moved;
}

// A player's combo counter, damage and labels (the hits the player takes).
void PracticeMode::_player_data(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& o) {
    gd::Int p = f->player_index;
    if (f->last_extra_damage != 0) {
        s->put16(pd(p, D_HITS), s->s16(pd(p, D_HITS)) + 1);
    }
    if (o->move_changed != 0) {
        s->put16(pd(p, D_HITS), 0);
    }
    if (o->throw_state < 1) {
        put(pd(p, D_HIT_ID), 0);
    }
    _throw_flag(p, o);
    _count_combo(p, f, o);
    _combo_markers(p);
    if (f->hit_clean != 0) {
        put(pd(p, D_COUNTER), f->counter_hit != 0 ? 1 : 0);
        put(pd(p, D_CLEAN), f->close_hit != 0 ? 1 : 0);
    }
    if (f->throw_state < 0) {
        put(pd(p, D_COUNTER), 0);
        put(pd(p, D_CLEAN), 0);
    }
    _damage(p, f);
}

// Whether the opponent's move is a throw (move flag 13, or one of a few slots of banks 4, 7 and
// 0xC), and whether its previous one was.
void PracticeMode::_throw_flag(gd::Int p, const gd::Ref<FighterState>& o) {
    if (o->move_changed == 0) {
        return;
    }
    put(pd(p, D_THROW_BEFORE), b(pd(p, D_THROW)));
    gd::Int throw_ = (o->pose_move->flags & MoveFlag::BUFFER_INPUT) != 0 ? 1 : 0;      // the overlay tests bit 13
    gd::Int slot = o->cur_slot & 0xFFFF;
    if (o->bank_type == BankType::YOSHIMITSU && gd::Arr<gd::Int>{0x12D, 0x131, 0x136, 0x484}.has(slot)) {
        throw_ = 1;
    }
    if (o->bank_type == BankType::XIAOYU && slot - 0x5A1 >= 0 && slot - 0x5A1 < 3) {
        throw_ = 1;
    }
    if (o->bank_type == BankType::BRYAN && slot - 0x832 >= 0 && slot - 0x832 < 3) {
        throw_ = 1;
    }
    put(pd(p, D_THROW), throw_);
}

// The combo counter: kept while the opponent reacts, counting hits and throw steps, up to 99.
void PracticeMode::_count_combo(gd::Int p, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& o) {
    bool no_throw = b(pd(p, D_THROW)) == 0 || b(pd(p, D_THROW_BEFORE)) == 0;
    if (f->hit_clean == 0 && f->last_extra_damage == 0) {
        if (no_throw) {
            if (f->was_hit_this_move == 0 && f->react_chain == 0) {
                put(pd(p, D_COMBO), 0);
            }
        } else if (o->whiffed != 0) {
            put(pd(p, D_COMBO), 0);
        }
    } else if (f->throw_state < 0) {
        if (f->last_extra_damage == 0) {
            put(pd(p, D_COMBO), b(pd(p, D_COMBO)) + 1);
        } else if (sb(pd(p, D_HIT_ID)) != o->throw_state) {
            if (!(o->bank_type == BankType::KING && o->cur_slot == 0xB82)) {
                put(pd(p, D_COMBO), b(pd(p, D_COMBO)) + 1);
            }
            put(pd(p, D_HIT_ID), o->throw_state);
        }
    } else if (f->react_chain == 0 && no_throw) {
        put(pd(p, D_COMBO), 1);
    } else {
        put(pd(p, D_COMBO), b(pd(p, D_COMBO)) + 1);
    }
    if (sb(pd(p, D_COMBO)) > 99) {
        put(pd(p, D_COMBO), 0);
        s->put16(pd(p, D_MARKER), 0x5A);
    }
    if (sb(pd(p, D_COMBO)) > 98) {
        put(pd(p, D_COMBO), 99);
    }
}

// A new best or an ended combo, and the marker's frames (90 on a new best or a multi-hit move).
void PracticeMode::_combo_markers(gd::Int p) {
    put(pd(p, D_NEW_BEST), 0);
    put(pd(p, D_ENDED), 0);
    gd::Int combo = sb(pd(p, D_COMBO));
    if (combo != sb(pd(p, D_PREVIOUS))) {
        if (sb(pd(p, D_PREVIOUS)) < combo) {
            put(pd(p, D_NEW_BEST), combo);
            put(pd(p, D_BEST), combo);
        }
        if (combo == 0) {
            put(pd(p, D_ENDED), b(pd(p, D_PREVIOUS)));
        }
    }
    if (combo >= 2 && b(pd(p, D_NEW_BEST)) != 0) {
        s->put16(pd(p, D_MARKER), 0x5A);
    }
    if (combo < 2 && s->s16(pd(p, D_HITS)) > 1) {
        s->put16(pd(p, D_MARKER), 0x5A);
    }
    if (s->s16(pd(p, D_MARKER)) > 0) {
        s->put16(pd(p, D_MARKER), s->s16(pd(p, D_MARKER)) - 1);
    }
    put(pd(p, D_PREVIOUS), b(pd(p, D_COMBO)));
}

// The hit's shown damage and share, the combo damage (capped at ±999) and the total.
void PracticeMode::_damage(gd::Int p, const gd::Ref<FighterState>& f) {
    gd::Ref<HitSlot> hs = f->hit_slots[f->best_hit_slot];
    gd::Int add = 0;
    if (f->got_hit == 0 && f->hit_clean == 0 && f->last_extra_damage == 0) {
        add = Fx::s16(hs->damage);
    } else {
        gd::Int shown = f->last_extra_damage;
        gd::Int share = shown;
        if (share == 0) {
            shown = 0;
            if (f->hit_clean != 0) {
                shown = Fx::s16(hs->base_damage);
                share = Fx::s16(hs->damage);
            }
        }
        s->put16(pd(p, D_HIT_DAMAGE), shown);
        s->put16(pd(p, D_HIT_SHARE), share);
        if (sb(pd(p, D_COMBO)) < 2 && s->s16(pd(p, D_HITS)) < 2) {
            s->put32(pd(p, D_COMBO_DAMAGE), share);
        } else {
            gd::Int v = s->s32(pd(p, D_COMBO_DAMAGE)) + share;
            s->put32(pd(p, D_COMBO_DAMAGE), v);
            s->put32(pd(p, D_SHOWN_DAMAGE), v);
        }
        if (s->s32(pd(p, D_COMBO_DAMAGE)) > 999) {
            s->put32(pd(p, D_COMBO_DAMAGE), 0);
            s->put32(pd(p, D_SHOWN_DAMAGE), 999);
        }
        if (s->s32(pd(p, D_COMBO_DAMAGE)) < -999) {
            s->put32(pd(p, D_COMBO_DAMAGE), 0);
            s->put32(pd(p, D_SHOWN_DAMAGE), -999);
        }
        add = f->last_extra_damage;
        if (add < 1) {
            add = Fx::s16(hs->damage);
        }
    }
    s->put32(pd(p, D_TOTAL), s->s32(pd(p, D_TOTAL)) + add);
}

// ---- the replay settings (FUN_800B8D50)

void PracticeMode::_replay_control(const gd::Ref<FighterState>& dummy, const gd::Ref<FighterState>& me) {
    gd::Ref<FightState> fight = g->fight;
    gd::Int d = PLAYER_DATA + 0x20 * dummy->player_index;
    if (b(d + D_COMBO) == 0 && b(d + D_LAST) != 0) {
        put(d + D_DROPPED, b(d + D_LAST));
    } else {
        put(d + D_DROPPED, 0);
    }
    put(d + D_LAST, b(d + D_COMBO));
    if (fight->replay->mode == ReplayState::DONE) {
        fight->replay->reset();
        g->sim->events->add(SimEvents::Kind::EFFECTS_CLEAR, -1);
        FighterSounds::script_stop(dummy);
        FighterSounds::script_stop(me);
        s->put16(REPLAY_FRAMES, 0);
        g->sim->camera->restart(dummy, me);
        s->put16(REPLAY_FRAMES, 0);
    }
    if (fight->replay_playing != 0) {
        return;
    }
    if (b(PAUSED) == 0) {
        if (sb(REPLAY_SETTING) == MANUAL) {
            s->put16(COMBO_FRAMES, 0);
        } else if (b(d + D_DROPPED) == 0 && b(d + D_COMBO) == 0 && b(ACTING + me->player_index) != 0) {
            s->put16(COMBO_FRAMES, me->pose_move->length);
        } else {
            s->put16(COMBO_FRAMES, s->u16(COMBO_FRAMES) + 1);
        }
        s->put16(REPLAY_FRAMES, gd::mini(s->u16(REPLAY_FRAMES) + 1, REPLAY_MAX));
    } else {
        s->put16(COMBO_FRAMES, 0);
    }
    if (s->u16(REPLAY_FRAMES) < REPLAY_MIN) {
        return;
    }
    bool start = false;
    switch (sb(REPLAY_SETTING)) {
        case 1:
            start = sb(d + D_ENDED) >= 4;
            break;
        case 2:
            start = sb(d + D_ENDED) >= 5;
            break;
        case 3:
            start = sb(d + D_ENDED) >= 6;
            break;
        case MANUAL:
            if ((s->u16(PRESSED) & PadState::SELECT) != 0) {
                s->put16(REPLAY_FRAMES, gd::mini(s->u16(REPLAY_FRAMES), 0x118));
                start = s->u16(REPLAY_FRAMES) >= REPLAY_MIN;
            }
            break;
        default:
            break;
    }
    if (!start) {
        return;
    }
    gd::Int frames = s->u16(REPLAY_FRAMES);
    if (sb(REPLAY_SETTING) != MANUAL && s->u16(COMBO_FRAMES) != 0) {
        frames = s->u16(COMBO_FRAMES) + 0x1E;
    }
    frames = gd::mini(frames, s->u16(REPLAY_FRAMES));
    frames = gd::clampi(frames, REPLAY_MIN, REPLAY_MAX);
    s->put16(REPLAY_FRAMES, 0);
    s->put16(COMBO_FRAMES, 0);
    s->put16(PRESSED, s->u16(PRESSED) & 0xFEFF);
    fight->replay->start(fight, frames - 2);
    g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
    g->sim->events->add(SimEvents::Kind::EFFECTS_CLEAR, -1);
    FighterSounds::script_stop(dummy);
    FighterSounds::script_stop(me);
}

// ---- the pause menu (practice_sim.menu_cursor, menu_input)

// FUN_800B4BA8: up/down on the repeat pad, wrapping at 0 and `last`.
gd::Int PracticeMode::_cursor_step(gd::Int cur, gd::Int last) {
    gd::Int rep = s->u16(REPEAT);
    if ((rep & PadState::DOWN) != 0) {
        cur += 1;
        g->sound(SOUND_MOVE);
    } else if ((rep & PadState::UP) != 0) {
        cur -= 1;
        g->sound(SOUND_MOVE);
    }
    gd::Int v = Fx::s8(cur);
    return v < 0 ? last : (last < v ? 0 : v);
}

// FUN_800B49C4.
void PracticeMode::_menu_cursor() {
    if (b(MODE_SELECT) != 0) {
        gd::Int old = sb(SUB);
        gd::Int new_ = _cursor_step(old, 4);   // GDScript `new` (a C++ keyword)
        put(SUB, new_);
        if (new_ == old) {
            return;
        }
        gd::Int me = b(PLAYER);
        switch (new_) {
            case 0:
                _puts(gd::Arr<gd::Int>{ATTACK_DATA, 1, GUIDE, 0, COMMAND_LIST, 0, SWAP, 0, COMBO_TYPE, 0, COMBO_PLAYER, me});
                replay_reset(me);
                key_ring_reset(b(PLAYER));
                put(PROMPT, 1);
                break;
            case 1:
                for (gd::Int at : gd::Arr<gd::Int>{COUNTER_ATTACKS, REPLAY_SETTING, GUIDE, COMMAND_LIST, SWAP, COMBO_TYPE}) {
                    put(at, 0);
                }
                put(COMBO_PLAYER, me);
                replay_reset(me);
                key_ring_reset(b(PLAYER));
                put(PROMPT, 1);
                break;
            case 2:
                put(COMBO_PLAYER, me);
                _puts(gd::Arr<gd::Int>{COUNTER_ATTACKS, 0, REPLAY_SETTING, 0, KEY_DISPLAY, 1, GUIDE, 0, COMMAND_LIST, 0,
                                       SWAP, 0, DUMMY, 0, COMBO_TYPE, 0});
                replay_reset(sb(COMBO_PLAYER));
                key_ring_reset(sb(COMBO_PLAYER));
                put(PROMPT, 2);
                break;
            default:
                break;
        }
        return;
    }
    gd::Int sub = sb(SUB);
    if (sub >= 0 && sub < 3) {
        gd::Int at = ITEM_ROW_CURSORS[sub];
        put(at, _cursor_step(sb(at), ITEM_ROW_LAST[sub]));
    }
}

// FUN_800B4C44.
void PracticeMode::_menu_input() {
    if (b(MODE_SELECT) != 0) {
        _mode_select_confirm();
    } else {
        _item_input();
    }
}

// FUN_800B4C8C.
void PracticeMode::_mode_select_confirm() {
    gd::Int sub = sb(SUB);
    if ((s->u16(PRESSED) & CHOOSE) == 0 || sub < 0 || sub > 4) {
        return;
    }
    if (sub == 3) {
        g->fight->round_state = REQUEST_SELECT;
        g->sound(SOUND_OK);
        return;
    }
    if (sub == 4) {
        g->fight->round_state = REQUEST_LEAVE;
        return;
    }
    _puts(gd::Arr<gd::Int>{MODE_SELECT, 0, CURSOR_FREE, 0, CURSOR_VS, 0, CURSOR_COMBO, 0, CHANGED, 0, MENU_DELAY, 3});
    if (sb(SUB) == 2) {
        _puts(gd::Arr<gd::Int>{COMBO_PLAYER, b(PLAYER), COMBO_TYPE, 0, GUIDE, 1, KEY_DISPLAY, 1, PROMPT, 1});
        key_ring_reset(sb(COMBO_PLAYER));
        replay_reset(sb(COMBO_PLAYER));
        load_guide(sb(COMBO_PLAYER), sb(COMBO_TYPE));
    }
    g->sound(SOUND_OK);
}

void PracticeMode::_changed() {
    put(CHANGED, 0);
    g->sound(SOUND_MOVE);
}

// Left or right flips a byte; true when a face button was pressed instead.
bool PracticeMode::_toggle(gd::Int at) {
    gd::Int p = s->u16(PRESSED);
    if ((p & CHOOSE) != 0) {
        return true;
    }
    if ((p & 0xA000) != 0) {
        put(at, (b(at) + 1) & 1);
        _changed();
    }
    return false;
}

// Right adds one, left takes one (both may apply), wrapping at 0 and `last`.
bool PracticeMode::_cycle(gd::Int at, gd::Int last) {
    gd::Int p = s->u16(PRESSED);
    if ((p & CHOOSE) != 0) {
        return true;
    }
    if ((p & 0x2000) != 0) {
        put(at, b(at) + 1);
        _changed();
    }
    if ((s->u16(PRESSED) & 0x8000) != 0) {
        put(at, b(at) - 1);
        _changed();
    }
    gd::Int v = sb(at);
    put(at, v < 0 ? last : (last < v ? 0 : v));
    return false;
}

void PracticeMode::_guide_changed() {
    key_ring_reset(sb(COMBO_PLAYER));
    replay_reset(sb(COMBO_PLAYER));
    load_guide(sb(COMBO_PLAYER), sb(COMBO_TYPE));
    _changed();
}

// The item under the cursor (FUN_800B4E4C, jump table 0x800B0A58).
gd::Int PracticeMode::item_under_cursor() {
    gd::Int sub = sb(SUB);
    if (sub < 0 || sub >= 3) {
        return 0;
    }
    return g->data->practice_rows[sub][sb(ITEM_ROW_CURSORS[sub])];
}

void PracticeMode::_item_input() {
    gd::Int item = item_under_cursor();
    gd::Int p = s->u16(PRESSED);
    bool back = false;
    switch (item) {
        case Item::RESUME:
            if ((p & CHOOSE) != 0) {
                resume();
            }
            break;
        case Item::ATTACK_DATA:
        case Item::COUNTER_ATTACKS:
        case Item::FREEZE_SIGNAL: {
            gd::Arr<gd::Int> fields{0, ATTACK_DATA, COUNTER_ATTACKS, FREEZE_SIGNAL};
            back = _toggle(fields[item]);
            break;
        }
        case Item::REPLAY:
            back = _cycle(REPLAY_SETTING, sb(SUB) == 2 ? 3 : 4);
            break;
        case Item::KEY_DISPLAY:
            if ((p & CHOOSE) != 0) {
                back = true;
            } else if ((p & PadState::HORIZONTAL) != 0) {
                put(KEY_DISPLAY, (b(KEY_DISPLAY) + 1) & 1);
                key_ring_reset(b(PLAYER));
                key_ring_reset(b(OTHER));
                _changed();
            }
            break;
        case Item::GUIDE:
            if ((p & CHOOSE) != 0) {
                back = true;
            } else if ((p & PadState::HORIZONTAL) != 0) {
                put(GUIDE, (b(GUIDE) + 1) & 1);
                _changed();
                key_ring_reset(0);
                key_ring_reset(1);
            }
            break;
        case Item::COMMAND_LIST:
            _command_list_item(p);
            break;
        case Item::CPU_DIFFICULTY:
            back = _cycle(CPU_DIFFICULTY, 2);
            break;
        case Item::CPU_LEVEL:
            back = _cycle(CPU_LEVEL, 9);
            break;
        case Item::CHARACTER_SELECT:
            if ((p & CHOOSE) != 0) {
                g->fight->round_state = REQUEST_SELECT;
                g->sound(SOUND_MOVE);
            }
            break;
        case Item::EXIT:
            if ((p & CHOOSE) != 0) {
                g->fight->round_state = REQUEST_LEAVE;
            }
            break;
        case Item::DUMMY:
            back = _cycle(DUMMY, 8);
            break;
        case Item::MODE_SELECT:
            if ((p & CHOOSE) != 0) {
                put(MODE_SELECT, 1);
                put(MENU_DELAY, 3);
                if (sb(SUB) == 2) {
                    key_ring_reset(sb(COMBO_PLAYER));
                    replay_reset(sb(COMBO_PLAYER));
                }
            }
            break;
        case Item::COMBO_PLAYER:
            if ((p & CHOOSE) != 0) {
                back = true;
            } else if ((p & PadState::HORIZONTAL) != 0) {
                put(COMBO_PLAYER, (b(COMBO_PLAYER) + 1) & 1);
                put(COMBO_TYPE, 0);
                _guide_changed();
            }
            break;
        case Item::COMBO_TYPE:
            back = _combo_type_item(p);
            break;
        default:
            break;
    }
    if (back && sb(SUB) >= 0 && sb(SUB) < 3) {
        put(ITEM_ROW_CURSORS[sb(SUB)], 0);
    }
}

// COMMAND LIST: a choice button opens or closes the move lists, which then take the input.
void PracticeMode::_command_list_item(gd::Int p) {
    if ((p & CHOOSE) != 0) {
        put(COMMAND_LIST, (b(COMMAND_LIST) + 1) & 1);
        put(MENU_DELAY, 3);
        // FUN_80048760's horizon band: covered by the remake's scene (MoveListScreen).
        if (b(COMMAND_LIST) != 0) {
            g->fight->pause_delay = 3;
        } else {
            for (gd::Int at : gd::Arr<gd::Int>{CHANGED, CURSOR_FREE, CURSOR_VS, CURSOR_COMBO}) {
                put(at, 0);
            }
        }
    }
    if (b(COMMAND_LIST) != 0) {
        // The player's list, and the dummy's while it is on CONTROLLER.
        gd::Int mask = gd::shl(1, b(PLAYER));
        if (sb(DUMMY_KIND) == 0) {
            mask |= gd::shl(1, b(OTHER));
        }
        MoveListScreen::practice_page(g->fight, mask, b(PLAYER), g->sim->pads, g->sim->events);
    }
}

// The combo type: left / right cycle through the character's combos; true to go back.
bool PracticeMode::_combo_type_item(gd::Int p) {
    if ((p & CHOOSE) != 0) {
        return true;
    }
    bool changed = false;
    if ((p & PadState::RIGHT) != 0) {
        changed = true;
        put(COMBO_TYPE, b(COMBO_TYPE) + 1);
    }
    if ((s->u16(PRESSED) & PadState::LEFT) != 0) {
        changed = true;
        put(COMBO_TYPE, b(COMBO_TYPE) - 1);
    }
    gd::Int v = sb(COMBO_TYPE);
    if (v < 0) {
        put(COMBO_TYPE, combo_count() - 1);
    } else if (combo_count() - 1 < v) {
        put(COMBO_TYPE, 0);
    }
    if (changed) {
        _guide_changed();
    }
    return false;
}

}  // namespace t3
