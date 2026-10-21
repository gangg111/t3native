// remake/core/ai/ai_decision.gd
// AiUpdate (0x80059B78; ai.md#per-frame-behaviour), written from the verified port
// `tools/research/ai_update.py`: the CPU fighter's held and newly pressed pad words for one frame.
// The steps run in the game's order until one produces the frame's input; the bookkeeping at
// the end always runs.
#include "core/ai/ai_decision.h"

#include "content/ai_tables.h"
#include "content/fight_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/ai/ai_record.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_math.h"
#include "core/combat/attack_records.h"
#include "core/combat/attack_word.h"
#include "core/combat/state_bit.h"
#include "core/combat/state_word.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/branch_condition.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/move_system.h"
#include "core/force/tekken_force.h"
#include "core/game/byte_block.h"
#include "core/game/game_mode.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"
#include "platform/pad_state.h"

namespace t3 {

AiDecision::AiDecision(const gd::Ref<CpuOpponent>& opponent) {
    ai = opponent;
    fight = ai->fight;
    t = ai->t;
}

// AiUpdate for CPU fighter `fighter`: (held pad, newly pressed pad).
gd::PI32 AiDecision::update(const gd::Ref<FighterState>& fighter) {
    rec = ai->records[fighter->ai_slot];
    f = fighter;
    o = rec->target;
    ai->ai_opp = o;
    ai->set_ai_self(f);
    if (fight->mode == GameMode::BALL) {
        _ball_project();
    }
    _decide();
    return _finish();
}

void AiDecision::_decide() {
    _situation();
    if (_fixed_input()) {
        return;
    }
    _track();
    if (_throws() || _stance()) {
        return;
    }
    if (f->move_row != nullptr) {
        return;
    }
    if (_crouch_and_actions()) {
        return;
    }
    if (ai->call_hook(rec, false) || ai->call_hook(rec, true)) {
        return;
    }
    if (_hold_guard() || _new_move() || _follow_through() || _counter()) {
        return;
    }
    if (_react(_reaction_kind())) {
        return;
    }
    _expire_lists();
    if (_avoid_air_attack() || _attack()) {
        return;
    }
    _move();
}

bool AiDecision::_draw(gd::Int word_index) {
    return (ai->lcg_next() & 0xFFF) < rec->word(word_index);
}

// ---- bookkeeping

void AiDecision::_situation() {
    rec->pressed_move = nullptr;
    gd::Int frame = Fx::s16(f->pose_frame);
    gd::Int prev = rec->pose_frame_seen;
    rec->pose_frame_seen = frame;
    if (frame != prev) {
        rec->frame_count = Fx::s16(rec->frame_count + 1);
    }
    gd::Ref<MoveRow> smv = f->pose_move;
    gd::Ref<MoveRow> omv = o->pose_move;
    rec->own_state = smv->state;
    rec->own_word = smv->attack_word;
    rec->own_attack = smv->attack;
    rec->opp_state = omv->state;
    rec->opp_word = omv->attack_word;
    rec->opp_attack = omv->attack;
    rec->distance = Fx::w32(f->dist);
    gd::Int sit = Fx::s16(f->rel_angle) > FightMath::QUARTER_TURN ? CpuOpponent::SIT_TURNED_AWAY : 0;
    rec->angle = Fx::s16(f->rel_angle);
    gd::Int heading = f->heading_delta & 0xFFFF;
    if (((heading - 0x4E38) & 0xFFFFFFFFLL) < 0x31C8) {
        sit |= CpuOpponent::SIT_SIDE_A;
    }
    if (((heading + FightMath::HALF_TURN) & 0xFFFF) < 0x31C7) {
        sit |= CpuOpponent::SIT_SIDE_B;
    }
    if (Fx::s16(o->attack) == AttackWord::HIGH && Fx::s16(o->pose_frame) <= omv->active_last
            && (gd::Int(fight->fighter_distance) & 0xFFFFFFFFLL) < MoveSystem::HIGH_ATTACK_RANGE) {
        sit |= CpuOpponent::SIT_HIGH_COMING;
    }
    if (Fx::s16(f->facing_quadrant) == 0) {
        sit |= CpuOpponent::SIT_FACING;
    }
    rec->opp_gon_special = 0;
    rec->opp_breath = 0;
    rec->situation = sit;
    gd::Int first_byte = AttackRecords::header(omv, fight->tables)[0];
    if (o->bank_type == BankType::OGRE && first_byte == 0x18) {
        rec->opp_breath = 1;
    } else if (o->bank_type == BankType::GON && first_byte == 0x1A) {
        rec->opp_gon_special = 1;
        rec->opp_word |= 0x10000;
    }
}

// Pending script steps, the disabled input modes and the alternate-frame rule.
bool AiDecision::_fixed_input() {
    if (rec->script_pending()) {
        gd::Int step = rec->script_steps[rec->script_at];
        rec->script_at += 1;
        if (rec->script_at >= rec->script_steps.size()) {
            rec->script_at = -1;
        }
        rec->pad = ai->pad_from_step(step);
        return true;
    }
    gd::Int mode = Fx::s16(rec->input_mode);
    if (mode == 0) {
        rec->pad = 0;
        return true;
    }
    if (mode == 1) {
        rec->pad = (rec->own_state & StateBit::DOWN) != 0 ? PAD_UP : 0;      // state bit 2 (down): get up
        return true;
    }
    gd::Int slot = rec->slot & 0xFFFF;
    if ((ai->multi != 0 && (gd::Int(fight->frame_counter) & slot & 1) != 0)
            || (fight->mode == GameMode::FORCE && ai->force_timers[2 + slot] > 0)) {
        rec->pad = rec->prev_pad & PadState::DIRECTIONS;
        return true;
    }
    return false;
}

// Per-frame bookkeeping before the decision: windows, distance bands, guard needs.
void AiDecision::_track() {
    om = o->pose_move;
    sm = f->pose_move;
    my_first = sm->active_first;
    my_last = sm->active_last;
    my_frame = Fx::s16(f->pose_frame);
    op_first = om->active_first;
    op_last = om->active_last;
    op_frame = Fx::s16(o->pose_frame);
    rec->filter_no_lows = false;
    if ((sm->flags & MoveFlag::BUFFER_INPUT) == 0) {
        rec->no_strings = 0;
    }
    gd::Int parried = rec->parried;
    gd::Int throw_next = rec->throw_next;
    rec->parried = 0;
    rec->throw_next = 0;
    rec->punish = parried;
    rec->throw_chance = throw_next;
    if (o->health <= Fx::div_trunc(o->health_max, 10)) {
        for (gd::Int w : gd::Arr<gd::Int>{AiRecord::Param::FILTER_POSTURE, AiRecord::Param::FILTER_FAST,
                                          AiRecord::Param::PUNISH, AiRecord::Param::PUNISH_CAP}) {
            rec->params[w] = 0;
        }
    }
    gd::Int x = ai->lcg_next();
    if (rec->word(AiRecord::Param::FOCUS) <= (x & 0xFFF) && rec->angle > 0x1FFF && Fx::s16(f->opp_rel_angle) < 0x6001
            && rec->reach_band < 3) {
        rec->punish = 1;
    }
    rec->move_changed = (sm != rec->collected_move || my_frame < rec->move_frame_seen) ? 1 : 0;
    // +0x204 compares with the words +0x1D8 / +0x206, which nothing writes: always changed.
    rec->opp_move_changed = 1;
    rec->move_frames = Fx::s16(rec->move_frames + 1);
    if (rec->collected_move != sm) {
        rec->collected_move = sm;
        rec->move_frames = 0;
    }
    rec->move_frame_seen = my_frame;
    if ((rec->own_state & 0x400000) != 0) {
        CpuOpponent::reset_approach(rec);
    }
    if (my_first == 0) {
        if ((rec->own_word & HINT_MIDDLE) == 0 && o->was_hit_this_move == 0) {
            rec->attacks_in_row = 0;
        }
    } else if (my_first == my_frame) {
        rec->attacks_in_row = Fx::s16(rec->attacks_in_row + 1);
    }
    if (op_first == 0) {
        if ((rec->opp_word & HINT_MIDDLE) == 0 && f->was_hit_this_move == 0) {
            rec->opp_string = -1;
        }
    } else if (op_first == op_frame) {
        rec->opp_string = Fx::s16(rec->opp_string + 1);
        if (rec->opp_string > 10) {
            rec->opp_string = 10;
        }
    }
    _distance_bands();
    _opponent_attack();
    _guard_state();
}

void AiDecision::_distance_bands() {
    gd::Int d = rec->distance;
    if (fight->mode == GameMode::BALL) {
        gd::Int b = _ball_distance();
        if (b < d) {
            d = b;
        }
    }
    gd::Int prev = rec->prev_distance;
    rec->prev_distance = d;
    gd::Int e = d - gd::Int(rec->opp_bands[5]) - 0x80;
    rec->distance_change = Fx::w32(d - prev);
    if (e < rec->own_bands[0]) {
        bool pushed = false;
        for (const gd::Ref<FighterState>& fighter : gd::Arr<gd::Ref<FighterState>>{f, o}) {
            if (fighter->body_push_x != 0 || fighter->body_push_y != 0 || fighter->body_push_z != 0) {
                pushed = true;
            }
        }
        if (!pushed) {
            rec->band = 1;
            if (rec->attacking == 0) {
                rec->wait = Fx::s16(rec->wait - 1);
            }
        } else {
            rec->band = 0;
            if (rec->attacking == 0) {
                rec->wait = Fx::s16(rec->wait - 2);
            }
        }
        rec->far_count = 0;
    } else if (e < rec->own_bands[1]) {
        rec->band = 2;
        rec->far_count = 0;
    } else if (e < rec->own_bands[2]) {
        rec->band = 3;
        rec->far_count = 0;
    } else {
        gd::Int band = e < rec->own_bands[3] ? 4 : 5;
        gd::Int extra = band == 4 ? 2 : 3;
        rec->band = band;
        gd::Int r = fight->rng->next();
        gd::Int v = ((r + fight->ai_rng) & 0x7FFF) * extra;
        ai->lcg_next();
        rec->far_count = Fx::s16(rec->far_count + band + (v >> 15));
    }
    if (rec->attacking == 0 || o->in_air != 0) {
        rec->wait = Fx::s16(rec->wait - 1);
    }
    gd::Int g = Fx::w32(rec->distance - gd::Int(rec->own_bands[5]));
    if (rec->distance_change < 0) {
        g = Fx::w32(g + rec->distance_change);
    }
    gd::Int reach = 1;
    gd::PI32 margins{0x80, 0x100, 0x200, 0x280};
    for (gd::Int k = 0; k < 4; ++k) {
        if (g < gd::Int(rec->opp_bands[k]) + gd::Int(margins[k])) {
            break;
        }
        reach += 1;
    }
    rec->reach_band = reach;
}

// How long until the opponent's attack is active (+0x20A) and whether it is coming (+0x20C).
void AiDecision::_opponent_attack() {
    if (op_first != 0 || (rec->opp_state & 0x80000) == 0) {
        rec->opp_unblockable = 0;
    } else {
        rec->opp_unblockable = Fx::s16(rec->opp_unblockable + 1);
        if (om->stance_slot == Fx::s16(o->cur_slot)) {
            rec->opp_unblockable = 0;
        }
    }
    gd::Int coming = (rec->opp_unblockable > 0 || (op_first != 0 && op_frame <= op_last + 1)) ? 1 : 0;
    rec->opp_coming = coming;
    if (coming == 0 || op_first - 3 <= op_frame) {
        rec->announced_frames = 0;
    } else {
        rec->announced_frames = Fx::w32(rec->announced_frames + 1);
    }
    if (my_first == 0 || op_frame <= op_last + 1) {
        rec->passed_frames = 0;
    } else {
        rec->passed_frames = Fx::w32(rec->passed_frames + 1);
    }
    if (rec->opp_coming == 0) {
        rec->guard_delay = -1;
    } else if (rec->guard_delay > 0) {
        rec->guard_delay -= 1;
    }
    if (rec->opp_coming == 0 && (rec->opp_state & 0x200000) != 0) {
        rec->opp_coming = 1;
    }
    if ((rec->opp_state & 0x80000) != 0) {
        rec->opp_charging = Fx::s16(rec->opp_charging + 1);
    } else {
        rec->opp_charging = 0;
    }
    gd::Int grab = ((rec->opp_word & GRAB) != 0 && (op_first == 0 || op_frame <= op_last)) ? 1 : 0;
    rec->opp_grabs = grab;
    if (rec->opp_coming == 0 && grab == 0) {
        rec->opp_until = 999;
        rec->evade_reason = 0;
    } else if (op_first == 0) {
        gd::PI32 first_row = om->bank->branches[om->branches];
        rec->opp_until = Fx::s16(first_row[ROW_LAST] - op_frame + 1);
    } else {
        gd::Int value = 0x3E5;
        if (op_frame <= op_last) {
            rec->opp_until = Fx::s16(op_first - op_frame);
            value = Fx::s16(op_first - op_frame) < 0x65 ? -1 : 0x3E6;
        }
        if (value >= 0) {
            rec->opp_grabs = 0;
            rec->opp_coming = 0;
            rec->opp_until = value;
        }
    }
}

// +0x6A: the CPU already holds the guard that stops the opponent's attack level.
gd::Int AiDecision::_needs_guard() {
    gd::Int posture = rec->own_state & 0xFFFF;
    if (rec->distance >= 0xB87 || rec->vs_stance != 0) {
        return 0;
    }
    gd::Int attack = rec->opp_attack;
    if (rec->opp_grabs != 0) {
        return attack == AttackWord::MID ? 0 : ((posture == StateWord::CROUCH || posture == StateWord::GUARD_LOW) ? 1 : 0);
    }
    if (rec->opp_coming == 0 || rec->angle > 0x3000) {
        return 0;
    }
    // `accepted`: GDScript untyped Array of state words.
    gd::Arr<gd::Int> accepted;
    switch (attack) {
        case AttackWord::HIGH:
            accepted = gd::Arr<gd::Int>{StateWord::CROUCH, StateWord::GUARD_LOW, StateWord::GUARD_HIGH};
            break;
        case AttackWord::MID:
            accepted = gd::Arr<gd::Int>{StateWord::GUARD_HIGH};
            break;
        case AttackWord::LOW:
            accepted = gd::Arr<gd::Int>{StateWord::GUARD_LOW};
            break;
        case AttackWord::MID_2:
            accepted = gd::Arr<gd::Int>{StateWord::GUARD_LOW, StateWord::GUARD_HIGH};
            break;
        case AttackWord::UNBLOCKABLE_2:
            accepted = gd::Arr<gd::Int>{StateWord::GUARD_LOW, StateWord::CROUCH};
            break;
        default:
            break;
    }
    return accepted.has(posture) ? 1 : 0;
}

void AiDecision::_guard_state() {
    rec->opp_high = o->body->joints[2]->t[1] < -0xC1B ? 1 : 0;
    if (Fx::s16(f->opp_rel_angle) > 0x3000
            || ((rec->opp_state & 0x200000) == 0 && rec->opp_attack != AttackWord::UNBLOCKABLE
                && rec->opp_attack != AttackWord::UNBLOCKABLE_2)) {
        rec->evasion = 0;
    }
    if (rec->whiff_timer >= 0 && rec->reach_band < 2 && rec->word(AiRecord::Param::FOCUS) <= (ai->lcg_next() & 0xFFF)) {
        rec->evade_reason = 1;
    }
    gd::Int down = 0;
    if ((rec->opp_state & (StateBit::DOWN_REVERSED | StateBit::DOWN)) != 0 && (rec->opp_state & 0xFFFF) != StateWord::LAUNCHED) {
        down = o->in_air == 0 ? 1 : 0;
    }
    rec->opp_down = down;
    rec->vs_stance = (o->bank_type == BankType::YOSHIMITSU && Fx::s16(o->cur_slot) == CpuOpponent::YOSHIMITSU_STANCE) ? 1 : 0;
    if (rec->vs_stance != 0) {
        rec->opp_string = -1;
    }
    rec->guard_holds = _needs_guard();
    if ((rec->own_state & StateBit::CROUCHING) == 0) {
        rec->crouch_timer = -1;
    } else if (rec->crouching > 0) {
        rec->crouch_timer = Fx::s16(rec->crouch_timer - 1);
    } else if ((sm->flags & MoveFlag::LAUNCH_ANGLE) == 0) {
        gd::Int x0 = ai->lcg_next();
        gd::Int x1 = ai->lcg_next();
        gd::Int x2 = ai->lcg_next();
        rec->crouch_timer = (x0 & 7) + (x1 & 0xF) + 0x14 + (x2 & 7);
    }
    rec->attacking = my_first != 0 ? 1 : 0;
    gd::Int open = 0;
    if ((sm->state & 0x200000) != 0 && (sm->attack_word & HINT_SPECIAL) != 0) {
        open = my_frame <= my_last ? 1 : 0;
    }
    rec->special_open = open;
}

// ---- throws and stances

// Throw escapes (the CPU is thrown, throwState < 0) and throw follow-ups (it throws).
bool AiDecision::_throws() {
    gd::Int state = Fx::s16(f->throw_state);
    if (state == 0) {
        rec->throw_move = nullptr;
        rec->throw_steps = 0;
        rec->throw_press_frame = -1;
        rec->throw_row = gd::PI32();
        return false;
    }
    CpuOpponent::reset_approach(rec);
    gd::Ref<MoveRow> move = f->pose_move;
    if (state < 1) {
        if (move == rec->throw_move) {
            rec->pad = 0;
            return true;
        }
        rec->throw_steps = Fx::s16(rec->throw_steps - 1);
        rec->throw_move = move;
        gd::Int slot = Fx::s16(o->cur_slot);
        gd::Int bank = o->bank_type;
        bool forced = FORCED_ESCAPES.has(slot) || (slot == 0x172 && (bank == BankType::JIN || bank == BankType::PAUL))
            || (bank == BankType::KING && slot == 0x152);
        if (!forced && (ai->lcg_next() & 0xFFF) >= rec->word(AiRecord::Param::THROW_ESCAPE)) {
            return true;
        }
        gd::Arr<gd::PI32> rows = ai->list_rows(move).slice(0, THROW_ROWS);
        gd::Int press = 0;
        if (!rows.is_empty()) {
            gd::PI32 row = rows[ai->random_below(rows.size())];
            press = _escape_press(row);
        }
        if (press != 0) {
            ai->press_command(rec, press, nullptr);
        } else {
            rec->pad = 0;
        }
        return true;
    }
    if (move != rec->throw_move && rec->throw_steps >= 0 && rec->throw_steps < rec->word(AiRecord::Param::THROW_STEPS) - 1
            && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::THROW_CONTINUE)) {
        gd::Arr<gd::PI32> follow = _throw_follow_ups(move);
        if (follow.is_empty()) {
            rec->throw_steps = -1;
            rec->throw_press_frame = -1;
            rec->throw_row = gd::PI32();
        } else {
            rec->throw_steps = Fx::s16(rec->throw_steps + 1);
            rec->throw_move = f->pose_move;
            gd::PI32 chosen = follow[ai->random_below(follow.size())];
            rec->throw_press_frame = chosen[ROW_FIRST] + 1;
            rec->throw_row = chosen;
        }
    }
    if (my_frame != rec->throw_press_frame || rec->throw_row.is_empty()) {
        rec->pad = 0;
        return true;
    }
    ai->press_command(rec, rec->throw_row[ROW_COMMAND], ai->slot_move(f->ai_slot, rec->throw_row));
    return true;
}

// The command pressed to escape with a row: 1 or 2 for the latched tap conditions, else the
// row's command.
gd::Int AiDecision::_escape_press(const gd::PI32& row) {
    switch (gd::Int(row[ROW_CONDITION])) {
        case BranchCondition::TAP_LP:
        case BranchCondition::TAP_BOTH:
            return 1;
        case BranchCondition::TAP_RP:
            return 2;
        default:
            break;
    }
    return Fx::s16(row[ROW_COMMAND]);
}

// Up to six rows of the running throw the CPU may continue with; a few slots are tied to banks.
gd::Arr<gd::PI32> AiDecision::_throw_follow_ups(const gd::Ref<MoveRow>& move) {
    gd::Int bank = f->bank_type;
    gd::Arr<gd::PI32> found;
    for (const gd::PI32& row : ai->list_rows(move)) {
        if (!CpuOpponent::restriction_ok(f, o, row[ROW_RESTRICTION])) {
            continue;
        }
        gd::Int u = ai->lcg_next() & 0x70;
        gd::Int slot = Fx::s16(row[ROW_TARGET]);
        bool ok = true;
        if (slot == 0x3A7) {
            ok = false;
        } else if (slot == 0xD41) {
            ok = bank != BankType::YOSHIMITSU || u != 0;
        } else if (slot == 0xD24 || slot == 0xD32) {
            ok = (bank == BankType::PAUL || bank == BankType::HEIHACHI) ? u == 0 : true;
        } else if (slot == 0xDE2) {
            ok = !(bank == BankType::PAUL && u != 0);
        } else if (slot == 0xD25 || slot == 0xD34) {
            ok = bank != BankType::PAUL && bank != BankType::HEIHACHI;
        }
        if (ok && (row[ROW_CONDITION] == 0 || ai->moves->condition(row, f, o))) {
            found.append(row);
            if (found.size() > 5) {
                return found;
            }
        }
    }
    return found;
}

gd::Int AiDecision::_collect(bool current_only) {
    rec->marked_count = 0;
    rec->collected_move = f->pose_move;
    gd::Int n = ai->collect_candidates(rec, current_only);
    rec->candidate_count = Fx::s16(n);
    return Fx::s16(n);
}

// Stance moves (state 0x4C02, no active window): with probability word 49 execute a candidate
// whose branch row flags are 0x1C or 0x1D.
bool AiDecision::_stance() {
    if ((rec->own_state & 0xFFFF) != StateWord::LAUNCHED || f->pose_move->active_first != 0) {
        return false;
    }
    CpuOpponent::reset_approach(rec);
    if (_collect(true) > 0 && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::STANCE)) {
        gd::Int marked = ai->mark_candidates(rec, [=](const gd::Ref<MoveRow>& /*_m*/, const gd::PI32& br) -> bool {
            return ((br[ROW_FLAGS] - 0x1C) & 0xFFFFFFFFLL) < 2;
        });
        if (marked > 0) {
            ai->execute_candidate(rec);
            return true;
        }
    }
    rec->pad = 0;
    return true;
}

// ---- neutral actions

// A grounded CPU in a neutral state (+0x224 bits 2 or 10): crouch-cancel rows (RECOVERED)
// or a random action from a mask chosen by the opponent's attack timing.
bool AiDecision::_crouch_and_actions() {
    if (f->in_air != 0 || (rec->own_state & (StateBit::AIRBORNE | StateBit::DOWN)) == 0) {
        rec->neutral_action = 0;
        return false;
    }
    bool cancel = false;
    for (const gd::PI32& row : ai->list_rows(f->pose_move)) {
        if (row[ROW_CONDITION] == BranchCondition::RECOVERED) {
            cancel = true;
            break;
        }
    }
    if (cancel) {
        gd::Int v = Fx::s16(rec->cancel_wait - 1);
        rec->neutral_action = 1;
        rec->cancel_wait = v;
        if (v >= 0) {
            rec->pad = 0;
            return true;
        }
        gd::Int wait = rec->param_byte(fight->ai_rng & 3);
        ai->lcg_next();
        rec->pad = 0x10;
        rec->cancel_wait = wait;
        return true;
    }
    if (_collect(true) < 1) {
        rec->pad = 0;
        return true;
    }
    rec->neutral_action = 1;
    rec->approach_hold = 0x28;
    rec->direction_hold = -1;
    rec->approach_kind = 0;
    rec->approach = 0;
    rec->move_block = 0x10;
    if (rec->opp_gon_special != 0 && (f->state & StateBit::DOWN_REVERSED) != 0) {
        rec->pad = PAD_BACK;
        return true;
    }
    if ((((rec->opp_state & 0x200000) != 0 && op_first == 0)
            || (rec->opp_attack == AttackWord::UNBLOCKABLE && (rec->opp_word & HINT_SAFE) == 0 && rec->opp_until < 0x1D))
            && op_frame < op_last) {
        rec->pad = 0;
        return true;
    }
    gd::Int mask = _action_mask();
    if (mask == 0) {
        rec->pad = 0;
        return true;
    }
    gd::Int action = 0;
    while (true) {
        gd::Int r = fight->rng->next();
        gd::Int u = (r + ai->lcg_next()) & 0x7FFF;
        action = t->actions[(u * 6) >> 15];
        if ((mask & action) != 0) {
            break;
        }
    }
    switch (action) {
        case 1:
            rec->pad = PAD_UP;
            return true;
        case 2:
            rec->pad = (rec->frame_count & 7) != 0 ? 0x80 : 0x4080;
            return true;
        case 4:
            rec->pad = f->bank_type == BankType::DOCTOR_B ? PAD_FORWARD : PAD_BACK;
            return true;
        case 8:
            ai->filter_attacks(rec);
            break;
        case 0x10:
            ai->mark_candidates(rec, [=](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
                return (m->attack_word & HINT_SAFE) != 0 || (m->attack_word & 0xFFFF) == AttackWord::LOW;
            });
            break;
        case 0x20:
            ai->mark_candidates(rec, _side_step_or_special);
            break;
        default:
            break;
    }
    if (rec->marked_count > 0) {
        ai->execute_candidate(rec);
    }
    return true;
}

gd::Int AiDecision::_action_mask() {
    gd::Int band = rec->band;
    gd::Int closing = rec->reach_band;
    if (op_first == 0) {
        if (band > 2) {
            rec->neutral_action = 0;
            return closing >= 4 ? 7 : 0x3F;
        }
        if ((rec->frame_count & 1) == 0) {
            return 0;
        }
        rec->neutral_action = 0;
        if (rec->wait < 0 && (ai->lcg_next() & 0x3F) == 0) {
            return 0x38;
        }
        return 7;
    }
    if (rec->opp_until < 0xC && op_frame <= op_last) {
        if (rec->opp_attack == AttackWord::LOW || (rec->opp_word & HINT_SAFE) != 0) {
            if ((rec->frame_count & 1) == 0) {
                return 0;
            }
            rec->neutral_action = 0;
            gd::Int x = fight->ai_rng;
            gd::Int x1 = (5 * x + 3) & 0xFFFFFFFFLL;
            if ((x & 2) != 0) {
                fight->ai_rng = x1;
                return 0x38;
            }
            if (om->air_first == 0 && rec->angle <= FightMath::QUARTER_TURN && (rec->opp_state & 0x200000) == 0) {
                fight->ai_rng = x1;
                return 4;
            }
            fight->ai_rng = (5 * x1 + 3) & 0xFFFFFFFFLL;
            return (x1 & 0x3F) == 0 ? 4 : 2;
        }
        gd::Int mask = 1;
        if (closing < 3 && (closing < 2 || rec->distance_change < 0xB)) {
            mask = (ai->lcg_next() & 0xF) != 0 ? 0x3F : 0;
        }
        rec->neutral_action = 0;
        return mask;
    }
    if (band < 2) {
        rec->neutral_action = 0;
        return (rec->opp_state & StateBit::STANDING) != 0 ? 0x38 : 0x28;
    }
    return 0;
}

bool AiDecision::_side_step_or_special(const gd::Ref<MoveRow>& m, const gd::PI32& br) {
    return br[ROW_COMMAND] == CpuOpponent::SIDE_STEP
        || ((m->state & 0xA0000) == 0x80000 && (m->state & 0x600000) == 0);
}

// ---- guard, new moves, follow-through

// After the opponent whiffs, a draw marks a punish chance (+0x8F); otherwise the direction is
// held while the guard state (+0x6A) says so.
bool AiDecision::_hold_guard() {
    if (o->whiffed != 0) {
        gd::Int threshold = rec->word(AiRecord::Param::PUNISH)
            + Fx::w32(rec->opp_string * (fight->ai_difficulty & 0xFFFF) * 30);
        if ((ai->lcg_next() & 0xFFF) < threshold) {
            rec->punish = 1;
            return false;
        }
    }
    if ((rec->guard_delay >= 0 || op_first == 0 || op_first - 0x1E < op_frame) && rec->guard_holds != 0) {
        rec->pad = rec->prev_pad & PadState::DIRECTIONS;
        return true;
    }
    return false;
}

// The move a row leads to, or null when its slot is empty in both fighters' banks (bug #61: the
// original reads a garbage row there; the remake treats the row as leading nowhere).
gd::Ref<MoveRow> AiDecision::_self_target(const gd::PI32& row) {
    return ai->slot_move(f->ai_slot, row);
}

// Rows of a move's branch list that pass the restriction and `accept` (called with the row and
// the move it leads to), up to `limit`.
gd::Arr<gd::PI32> AiDecision::_gather(const gd::Ref<MoveRow>& move, gd::Int limit,
                                      const std::function<bool(const gd::PI32&, const gd::Ref<MoveRow>&)>& accept) {
    gd::Arr<gd::PI32> found;
    for (const gd::PI32& row : ai->list_rows(move)) {
        gd::Ref<MoveRow> target = _self_target(row);
        if (target != nullptr && CpuOpponent::restriction_ok(f, o, row[ROW_RESTRICTION])
                && accept(row, target)) {
            found.append(row);
            if (found.size() >= limit) {
                return found;
            }
        }
    }
    return found;
}

// As _gather, with the restriction tested after `pre` (the game's order of tests).
gd::Arr<gd::PI32> AiDecision::_gather_ordered(const gd::Ref<MoveRow>& move, gd::Int limit,
                                              const std::function<bool(const gd::PI32&)>& pre,
                                              const std::function<bool(const gd::PI32&, const gd::Ref<MoveRow>&)>& accept) {
    gd::Arr<gd::PI32> found;
    for (const gd::PI32& row : ai->list_rows(move)) {
        gd::Ref<MoveRow> target = _self_target(row);
        if (target != nullptr && pre(row)
                && CpuOpponent::restriction_ok(f, o, row[ROW_RESTRICTION]) && accept(row, target)) {
            found.append(row);
            if (found.size() >= limit) {
                return found;
            }
        }
    }
    return found;
}

// When the CPU's move changed (+0x28), the follow-up lists: string continuations, planned
// follow-ups in reach, or rows that open on frame 1 of an attack; the direction is held while a
// list is ready. Otherwise the candidates are collected, and after 220 frames in one move any
// of them is executed.
bool AiDecision::_new_move() {
    rec->marked_count = 0;
    rec->candidate_count = 0;
    bool done = false;
    if (rec->move_changed != 0) {
        rec->collected_move = f->pose_move;
        rec->move_frame_seen = Fx::s16(f->pose_frame);
        gd::Ref<MoveRow> move = rec->collected_move;
        if (rec->strings_on >= 1) {
            gd::Arr<gd::PI32> found = _gather(move, 4, [=](const gd::PI32& /*_row*/, const gd::Ref<MoveRow>& target) -> bool {
                return (target->flags & MoveFlag::BUFFER_INPUT) != 0;
            });
            rec->strings = found;
            rec->strings_count = found.size();
            done = true;
            if (!found.is_empty()) {
                rec->pad = rec->prev_pad & PadState::DIRECTIONS;
            } else {
                rec->strings_count = 0;
                rec->strings_on = 0;
            }
        } else if (rec->planned_on >= 1) {
            gd::Arr<gd::PI32> found = _planned_follow_ups(move);
            rec->planned = found;
            rec->planned_count = found.size();
            done = true;
            if (!found.is_empty()) {
                rec->pad = rec->prev_pad & PadState::DIRECTIONS;
            } else {
                rec->planned_count = 0;
                rec->planned_on = 0;
            }
        } else if (rec->special_open == 0) {
            if (f->pose_move->active_first == 0) {
                rec->links_count = 0;
                rec->planned_count = 0;
                rec->strings_count = 0;
                rec->links_on = 0;
                rec->planned_on = 0;
                rec->strings_on = 0;
                rec->throw_list = 0;
            } else {
                gd::Arr<gd::PI32> found;
                if (rec->link_skip == 0) {
                    rec->link_last = 0;
                    gd::Int focus = rec->word(AiRecord::Param::FOCUS);
                    found = _gather_ordered(move, 10, [=](const gd::PI32& row) -> bool {
                        return row[ROW_FIRST] == 1 && gd::Int(row[ROW_LAST]) == gd::Int(row[ROW_ENTRY]);
                    },
                    [=, this](const gd::PI32& /*_row*/, const gd::Ref<MoveRow>& target) -> bool {
                        return !((target->flags & MoveFlag::BUFFER_INPUT) != 0 && (ai->lcg_next() & 0xFFF) < focus);
                    });
                    rec->links = found;
                    for (const gd::PI32& row : found) {
                        gd::Int last = row[ROW_LAST];
                        if (last < 0x3C && rec->link_last < last) {
                            rec->link_last = last;
                        }
                    }
                    rec->links_count = found.size();
                } else {
                    rec->link_skip = 0;
                }
                done = true;
                if (found.is_empty()) {
                    rec->links_count = 0;
                    rec->links_on = 0;
                } else {
                    rec->pad = rec->prev_pad & PadState::DIRECTIONS;
                    rec->links_on = Fx::s16(rec->links_on + 1);
                }
            }
        }
    }
    rec->move_frame_seen = Fx::s16(f->pose_frame);
    if (!done) {
        gd::Int n = Fx::s16(ai->collect_candidates(rec, false));
        rec->candidate_count = n;
        if (rec->move_frames > 0xDC) {
            gd::Int marked = 0;
            for (gd::Int k = 0; k < n; ++k) {
                if (ai->cand_mark[k] == 0) {
                    ai->cand_mark[k] = 1;
                    marked += 1;
                }
            }
            rec->marked_count = Fx::s16(rec->marked_count + marked);
            if (marked > 0) {
                ai->execute_candidate(rec);
                done = true;
            }
        }
    }
    if (!done && fight->mode == GameMode::BALL && _ball_behind()) {
        done = true;
    }
    return done;
}

// The +0xDC list: rows in reach whose target is usable; AI-excluded targets pass only while
// +0x208 is clear, and plain ground moves with the hint only after a draw above word 11.
gd::Arr<gd::PI32> AiDecision::_planned_follow_ups(const gd::Ref<MoveRow>& move) {
    gd::Int down = rec->opp_down;
    gd::Int threshold = rec->word(AiRecord::Param::THROW_LIST);
    ai->lcg_next();
    gd::Int reach_mask = t->band_reach[rec->band];
    return _gather(move, 48, [=, this](const gd::PI32& /*_row*/, const gd::Ref<MoveRow>& m) -> bool {
        if (!((down == 0 || (m->attack_word & HINT_NEVER) == 0) && (m->state & StateBit::NOT_AI_ATTACK) == 0)) {
            return false;
        }
        gd::Int w = m->attack_word;
        if ((w & HINT_SPECIAL) == 0 && (m->state & 0x80000) == 0) {
            if ((w & HINT_NEVER) == 0) {
                return false;
            }
            if ((ai->lcg_next() & 0x7FFF) <= threshold) {
                return false;
            }
        }
        gd::Int r = w & 0xFFFF0000LL;
        if ((w & HINT_REACH) == 0) {
            r |= HINT_CLOSE | HINT_MIDDLE;
        }
        return (r & reach_mask) != 0;
    });
}

// During a move usable in the air or as a special (+0x52), any open candidate is executed when
// the opponent's attack comes later than this move's, the band is above 2 or the CPU faces
// away; this ends the decision either way.
bool AiDecision::_follow_through() {
    if (!(rec->special_open != 0 && rec->candidate_count != 0 && rec->opp_breath == 0)) {
        return false;
    }
    if (rec->opp_until < f->pose_move->active_first - Fx::s16(f->pose_frame) || rec->band > 2
            || Fx::s16(f->facing_quadrant) == 2) {
        if (ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& /*_m*/, const gd::PI32& /*_br*/) -> bool { return true; }) > 0
                && ai->execute_candidate(rec) >= 0) {
            rec->wait = -1;
        }
    }
    return true;
}

// The opponent's attack is coming: attack first (word 19), duck or step back (word 21) or
// side-step (word 18).
bool AiDecision::_counter() {
    if (!(rec->opp_coming != 0 && rec->opp_string >= 0 && rec->reach_band < 4)) {
        return false;
    }
    if (rec->punish != 0 || (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::COUNTER)) {
        gd::Int v = rec->opp_until;
        if (1 < v && v < 0x1E) {
            gd::Int k = ai->filter_quick_attacks(rec);
            if (k > 0 && ai->execute_candidate(rec) >= 0) {
                return true;
            }
        }
    }
    if (rec->attacking == 0) {
        gd::Int attack = Fx::s16(rec->opp_word);
        if (((attack != 0 && attack != AttackWord::UNBLOCKABLE) || rec->opp_unblockable > 0)
                && (rec->evade_reason != 0 || (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::DUCK))) {
            rec->pad = _duck_or_back();
            return true;
        }
    }
    if ((ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::SIDE_STEP)) {
        bool ok = false;
        if ((((rec->opp_until & 0xFFFF) - 9) & 0xFFFFFFFFLL) < 3 && rec->angle < FightMath::QUARTER_TURN) {
            ok = (rec->own_state & 0x400000) == 0;
        }
        if (ok && rec->reach_band > 1) {
            ai->side_step(rec, -1);
            rec->wait = Fx::s16(rec->wait - 10);
            return true;
        }
    }
    return false;
}

// ---- reactions

// The opponent's move reach bits against the band mask of `band`.
bool AiDecision::_reach_ok(gd::Int band) {
    gd::Int w = om->attack_word;
    gd::Int r = w & 0xFFFF0000LL;
    if ((w & HINT_REACH) == 0) {
        r |= HINT_CLOSE | HINT_MIDDLE;
    }
    return (r & gd::Int(t->band_reach[band])) != 0;
}

bool AiDecision::_can_counter() {
    return rec->opp_coming != 0 && rec->guard_holds == 0 && Fx::s16(f->opp_rel_angle) < 0x5000
        && rec->distance < 0xB87 && _reach_ok(rec->reach_band);
}

// The reaction to the opponent's attack (jump table 0x8002317C): 0 none, 1 escape attempt,
// 2 guard or evade, 3 evasion, 4 side step, 5-8 bank-specific counters, 9 a neutral move.
gd::Int AiDecision::_reaction_kind() {
    if (rec->opp_string >= 0 || rec->vs_stance != 0) {
        return 0;
    }
    if (rec->guard_delay == 0 && rec->attacking == 0) {
        return 2;
    }
    if (rec->evasion == 0 && !(rec->opp_coming != 0 && rec->reach_band < 4)) {
        return 0;
    }
    if ((om->active_first != 0 && om->active_last < op_frame) || Fx::s16(f->opp_quadrant) == 2) {
        rec->evasion = 2;
    }
    if (rec->evasion == 0 && (rec->opp_attack == AttackWord::UNBLOCKABLE || rec->opp_attack == AttackWord::UNBLOCKABLE_2
                              || (rec->opp_state & 0x200000) != 0)
            && _draw(AiRecord::Param::HOOKS)) {
        rec->evasion = 1;
    }
    if (rec->guard_suspend < 0 && rec->evasion != 2) {
        if (rec->evasion != 0) {
            return 3;
        }
    }
    gd::Int bank = f->bank_type;
    bool grounded = rec->own_attack == 0 && f->in_air == 0;
    gd::Int until = rec->opp_until & 0xFFFF;
    gd::Ref<AiTables::Bank> bank_record = t->bank(bank);
    if (((until - 8) & 0xFFFFFFFFLL) < 2 && grounded && bank_record->counter != 0
            && (om->flags & MoveFlag::ALERT) == 0 && o->in_air == 0) {
        if (_can_counter() && _draw(AiRecord::Param::BANK_COUNTER)) {
            return 5;
        }
    }
    if (bank == BankType::JIN && 0x15 < rec->opp_until && rec->opp_until < 0x1B && grounded) {
        if (_can_counter() && _draw(AiRecord::Param::BANK_COUNTER)) {
            return 6;
        }
    }
    if (((until - 4) & 0xFFFFFFFFLL) < 7 && grounded && o->in_air == 0) {
        if (_can_counter() && _draw(AiRecord::Param::BANK_COUNTER)) {
            gd::Int attack = rec->opp_attack;
            if (attack == AttackWord::HIGH || attack == AttackWord::MID || attack == AttackWord::UNBLOCKABLE
                    || attack == AttackWord::UNBLOCKABLE_2) {
                if (!bank_record->parry_high.is_empty()) {
                    return 7;
                }
            }
            if (attack == AttackWord::LOW) {
                if (!bank_record->parry_low.is_empty()) {
                    return 8;
                }
            }
        }
    }
    if ((rec->opp_word & GRAB) != 0) {
        gd::Int slot = Fx::s16(o->cur_slot);
        gd::Int obank = o->bank_type;
        if (FORCED_ESCAPES.has(slot) || (slot == 0x172 && (obank == BankType::JIN || obank == BankType::PAUL))
                || (obank == BankType::KING && slot == 0x152)) {
            if (_draw(AiRecord::Param::HOOKS)) {
                return 1;
            }
        }
        if (rec->opp_until <= 0x1D && _draw(AiRecord::Param::GRAB_GUARD)) {
            rec->evade_reason = 2;
            return 2;
        }
    }
    gd::Int attack_word = Fx::s16(rec->opp_word);
    if ((attack_word != 0 && attack_word != AttackWord::UNBLOCKABLE) || rec->opp_unblockable > 0) {
        if (rec->guard_holds != 0) {
            return 0;
        }
        if (rec->attacking != 0) {
            return 0;
        }
        if (rec->evade_reason != 0) {
            return 2;
        }
        gd::Int x = ai->lcg_next();
        if ((x & 0xFFF) < rec->word(AiRecord::Param::GUARD)
                || (rec->opp_unblockable > 0x1E && _draw(AiRecord::Param::GUARD_CAP))) {
            rec->evade_reason = 5;
            return 2;
        }
    }
    if (rec->guard_delay > 8 && (ai->lcg_next() & 0x1F) == 0) {
        return 1;
    }
    if (rec->adaptive != 0 && (ai->lcg_next() & 0xF) == 0) {
        return 1;
    }
    if (learned(rec->history, o->pose_move, ai->fight->rules->fix_ai_damage_history) && rec->vs_stance == 0
            && _draw(AiRecord::Param::PUNISH_CAP)) {
        return 1;
    }
    if (rec->vs_stance == 0 && _draw(AiRecord::Param::PUNISH)) {
        return _low_parry_or_escape();
    }
    if (rec->opp_unblockable >= 0x1F && _draw(AiRecord::Param::PUNISH_CAP)) {
        return _low_parry_or_escape();
    }
    bool ok = false;
    if ((((rec->opp_until & 0xFFFF) - 9) & 0xFFFFFFFFLL) < 3 && rec->angle < FightMath::QUARTER_TURN) {
        ok = (rec->own_state & 0x400000) == 0;
    }
    if (!ok || rec->reach_band != 2) {
        return 0;
    }
    if ((rec->own_state & StateBit::CROUCHING) != 0 || f->in_air != 0 || rec->attacking != 0
            || rec->opp_attack != AttackWord::HIGH) {
        return 0;
    }
    return _draw(AiRecord::Param::SIDE_STEP) ? 4 : 0;
}

// Kind 1, or kind 9 on a 1-in-64 draw against the attack word 0x412.
gd::Int AiDecision::_low_parry_or_escape() {
    if (rec->opp_attack != AttackWord::HIGH) {
        return 1;
    }
    return (ai->lcg_next() & 0x3F) == 0 ? 9 : 1;
}

// LAB_8005D7A4: with `execute`, runs a marked candidate (and restarts the approach timers when
// one was pressed); the reaction then ends the decision.
bool AiDecision::_execute_marked(gd::Int count, bool execute) {
    if (!execute) {
        return false;
    }
    if (count > 0 && ai->execute_candidate(rec) >= 0) {
        rec->approach_hold = 0x28;
        rec->approach_kind = 0;
        rec->approach = 0;
        rec->direction_hold = -1;
    }
    return true;
}

bool AiDecision::_react(gd::Int kind) {
    switch (kind) {
        case 0:
            return false;
        case 9: {
            gd::Int n = ai->mark_neutral(rec);
            if (n > 0) {
                rec->parried = 1;
                rec->wait = -1;
            }
            return _execute_marked(n, n > 0);
        }
        case 1: {
            gd::Int attack = rec->opp_attack;
            gd::Int n = 0;
            if (rec->opp_coming == 0) {
                n = ai->filter_fast_attacks(rec);
            } else if (attack == AttackWord::UNBLOCKABLE || attack == AttackWord::UNBLOCKABLE_2
                       || (rec->opp_state & 0x200001) != 0) {
                n = ai->filter_quick_attacks_no_high(rec);
            } else {
                n = ai->filter_quick_attacks(rec);
            }
            if (attack == AttackWord::HIGH) {
                n +=ai->mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
                    return m->active_first != 0
                        && (m->state & (StateBit::NOT_AI_ATTACK | StateBit::CROUCHING)) == StateBit::CROUCHING
                        && ai->reach_ok(rec, m);
                });
            }
            return _execute_marked(n, n > 0);
        }
        case 2: {
            gd::Int n = ai->filter_defence(rec);
            if (n > 0) {
                return _execute_marked(n, true);
            }
            gd::Int pad = 0;
            if (Fx::s16(f->facing_quadrant) == 0) {
                pad = rec->opp_attack == AttackWord::MID ? PAD_BACK : PAD_DOWN_BACK;
                CpuOpponent::reset_approach(rec);
            }
            rec->pad = pad;
            return true;
        }
        case 3:
            return _evade();
        case 4:
            ai->side_step(rec, -1);
            rec->wait = Fx::s16(rec->wait - 10);
            return true;
        case 5: {
            gd::Int n = ai->mark_candidates(rec, [=](const gd::Ref<MoveRow>& /*_m*/, const gd::PI32& br) -> bool {
                gd::Int slot = br[ROW_TARGET] & 0xFFFF;
                return slot == 0x1EE || slot == 0x6A3;
            });
            return _execute_marked(n, n > 0);
        }
        case 6: {
            gd::Int n = ai->mark_candidates(rec, [=](const gd::Ref<MoveRow>& /*_m*/, const gd::PI32& br) -> bool {
                return (br[ROW_TARGET] & 0xFFFF) == 0x22C;
            });
            return _execute_marked(n, n > 0);
        }
        default:
            break;
    }
    gd::Ref<AiTables::Bank> bank_record = t->bank(f->bank_type);
    ai->start_script(rec, kind == 7 ? bank_record->parry_high : bank_record->parry_low);
    return true;
}

// FUN_800595E4: restarts the approach timers; forward on frames with +0x02 bit 0 clear.
gd::Int AiDecision::_pad_back_on_even() {
    CpuOpponent::reset_approach(rec);
    return (rec->frame_count & 1) == 0 ? PAD_FORWARD : 0;
}

// The evasion states in +0x94: 1 pick, 3 retreat, 4 back off, 5 side step, 6 keep out (no path
// sets 6, bug #22).
bool AiDecision::_evade() {
    if (rec->opp_high != 0) {
        return false;
    }
    gd::Int dist = rec->distance;
    gd::Int band = rec->band;
    if (rec->evasion == 1) {
        if (band < 3 && (ai->lcg_next() & 0xFFF) >= rec->word(AiRecord::Param::FOCUS)) {
            rec->evasion = 3;
        } else {
            rec->evasion = t->evasion_states[ai->random_below(7)];
        }
    }
    if (o->bank_type == BankType::YOSHIMITSU) {
        dist -= 0x12C;
    }
    gd::Int state = rec->evasion;
    if (state != 1 && state != 2 && dist >= 0xB87) {
        rec->evasion = 0;
        rec->pad = 0;
        return true;
    }
    gd::Int until = rec->opp_until;
    gd::Int facing = Fx::s16(f->opp_rel_angle);
    switch (state) {
        case 3:
            CpuOpponent::reset_approach(rec);
            if (o->pose_move->active_first != 0 && o->pose_move->active_last < Fx::s16(o->pose_frame)) {
                return false;
            }
            if (until >= 0x1D) {
                if (band >= 3) {
                    rec->pad = _pad_back_on_even();
                    rec->wait = ai->lcg_next() & 0xF;
                    return true;
                }
                return ai->filter_attacks_no_special(rec) <= 0;
            }
            if (until < 9) {
                rec->pad = facing < FightMath::QUARTER_TURN ? PAD_BACK : 0;
                return true;
            }
            if (band < 3) {
                if (ai->filter_attacks_no_special(rec) <= 0) {
                    return true;
                }
                rec->wait = -1;
                return false;
            }
            if (facing < FightMath::QUARTER_TURN) {
                rec->evasion = 4;
                rec->pad = PAD_BACK;
                return true;
            }
            return false;
        case 4:
            CpuOpponent::reset_approach(rec);
            if (facing > FightMath::QUARTER_TURN && rec->reach_band >= 2) {
                rec->pad = PAD_FORWARD;
                return true;
            }
            if (until >= 9 && band < 2) {
                if (ai->filter_attacks_no_special(rec) <= 0) {
                    return true;
                }
                rec->wait = -1;
                return false;
            }
            ai->start_script(rec, t->back_dash);
            return true;
        case 5:
            if ((rec->own_state & 0x400000) != 0 || (ai->lcg_next() & 7) != 0 || until < 7) {
                rec->pad = PAD_FORWARD;
                return false;
            }
            if (facing >= FightMath::QUARTER_TURN) {
                rec->evasion = 3;
            }
            ai->side_step(rec, -1);
            return true;
        case 6:
            CpuOpponent::reset_approach(rec);
            if (until >= 0x1D) {
                rec->pad = rec->reach_band > 0 ? PAD_BACK : 0;
                return true;
            }
            if (rec->reach_band >= 2 || until < 8) {
                ai->start_script(rec, t->back_dash);
                return true;
            }
            rec->pad = 0x3000;
            rec->direction_hold = 0x14;
            return true;
        default:
            break;
    }
    return false;
}

bool AiDecision::_open_now(const gd::PI32& row) {
    return row[ROW_FIRST] <= Fx::s16(f->pose_frame) && Fx::s16(f->pose_frame) <= row[ROW_LAST]
        && CpuOpponent::restriction_ok(f, o, row[ROW_RESTRICTION]) && ai->cond_ok(row, f, o, rec->situation);
}

// The follow-up lists are tested for their open rows and dropped: the code that would press
// one counts them in a register that is never incremented (bug #10). The tests still run, with
// the side effects of their branch conditions.
void AiDecision::_expire_lists() {
    if (rec->strings_on > 0) {
        for (gd::Int i = 0; i < rec->strings_count; ++i) {
            _open_now(rec->strings[i]);
        }
        rec->strings_on = 0;
        rec->strings_count = 0;
        rec->no_strings = 1;
    }
    if (rec->planned_on > 0) {
        for (gd::Int i = 0; i < rec->planned_count; ++i) {
            _open_now(rec->planned[i]);
        }
        rec->planned_on = 0;
        rec->planned_count = 0;
    }
    if (rec->links_on >= 0) {
        for (gd::Int i = 0; i < rec->links_count; ++i) {
            _open_now(rec->links[i]);
        }
        rec->links_on = 0;
        rec->links_count = 0;
    }
}

// Crouch-back (back against mids) when facing the opponent, else nothing;
// restarts the approach timers.
gd::Int AiDecision::_duck_or_back() {
    if (Fx::s16(f->facing_quadrant) != 0) {
        return 0;
    }
    rec->approach_hold = 0x28;
    rec->direction_hold = -1;
    rec->approach_kind = 0;
    rec->approach = 0;
    return rec->opp_attack == AttackWord::MID ? PAD_BACK : PAD_DOWN_BACK;
}

// Outside its own attack the CPU backs off from airborne attacks at close range, turns towards
// the opponent, or walks in (bank 7 moves differently).
bool AiDecision::_avoid_air_attack() {
    if (rec->attacking != 0) {
        return false;
    }
    gd::Ref<MoveRow> omv = o->pose_move;
    gd::Int angle = rec->angle;
    gd::Int closing = rec->reach_band;
    bool a = omv->air_first == 0 || closing > 1 || rec->word(AiRecord::Param::AIR_RETREAT) <= (ai->lcg_next() & 0xFFF);
    if (!a) {
        bool b = false;
        gd::Int pad = 0;
        if (angle >= FightMath::QUARTER_TURN || rec->distance_change > -0xB) {
            if (omv->air_last - Fx::s16(o->pose_frame) < 0x15 || angle <= FightMath::QUARTER_TURN) {
                b = true;
            } else {
                pad = PAD_FORWARD;
            }
        } else {
            pad = PAD_DOWN_FORWARD;
        }
        if (!b) {
            rec->pad = pad;
            return true;
        }
    }
    if (angle < 0x6001 || closing > 1) {
        if (angle <= 0x1000) {
            return false;
        }
        if ((ai->lcg_next() & 0x7FFF) >= angle) {
            return false;
        }
        if ((ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::FOCUS)) {
            return false;
        }
        rec->approach = 8;
        rec->approach_kind = 0;
        rec->approach_hold = 0x30;
        if (f->bank_type == BankType::XIAOYU) {
            rec->pad = angle >= FightMath::QUARTER_TURN ? PAD_BACK : PAD_FORWARD;
            return true;
        }
        gd::Int pad2 = 0;
        if (rec->opp_coming == 0 || (ai->lcg_next() & 7) != 0) {
            pad2 = PAD_FORWARD;
            if (closing < 3) {
                pad2 = (rec->frame_count & 1) != 0 ? PAD_BACK : PAD_FORWARD;
            }
        } else {
            pad2 = _duck_or_back();
        }
        rec->pad = pad2;
        return true;
    }
    if (o->in_air != 0) {
        rec->pad = 0;
    } else if (rec->opp_down != 0) {
        rec->pad = (rec->frame_count & 1) != 0 ? (PAD_DOWN | PadState::CROSS) : (PAD_DOWN | PadState::CIRCLE);
    } else {
        if ((ai->lcg_next() & 0xFFF) >= rec->word(AiRecord::Param::FOCUS)) {
            rec->wait = -2;
            return false;
        }
        if (f->bank_type == BankType::XIAOYU) {
            rec->pad = angle >= FightMath::QUARTER_TURN ? PAD_BACK : PAD_FORWARD;
        } else {
            rec->pad = PAD_FORWARD;
        }
    }
    return true;
}

// ---- attacks

// Picks and executes an attack; true ends the decision.
bool AiDecision::_attack() {
    if (fight->mode == GameMode::FORCE) {
        // Tekken Force: no attack while held off, and one not facing the player attacks only
        // by chance.
        gd::Int slot = rec->slot & 0xFFFF;
        if (ai->force_timers[slot] >= 1) {
            return false;
        }
        if (fight->force->player()->cur_opp_index != f->index) {
            if ((ai->lcg_next() & 0xFFF) < gd::Int(t->force_words[rec->force_words + 2])) {
                return false;
            }
        }
    }
    if (rec->crouch_attack_hold >= 1) {
        rec->crouch_attack_hold -= 1;
        rec->pad = rec->prev_pad & PadState::DIRECTIONS;
        return true;
    }
    if (rec->neutral_cooldown >= 5) {
        rec->pad = rec->prev_pad & PadState::DIRECTIONS;
        return true;
    }
    gd::Int band = rec->band;
    if (!(band < 4 && rec->opp_high == 0 && rec->attacks_in_row < rec->attack_limit)) {
        return false;
    }
    if (band > 2 && ai->random_below(12) != 0) {
        return false;
    }
    if (band > 1 && ai->random_below(8) == 0) {
        return false;
    }
    if (rec->approach_kind != 0) {
        if (rec->guard_delay < 1 && (rec->opp_coming == 0 || rec->reach_band > 3)) {
            if ((rec->opp_state & StateBit::DOWN) != 0 && ai->random_below(0x1000) < 0x100) {
                return false;
            }
        } else {
            rec->pad = PAD_BACK;
            return true;
        }
    }
    gd::Int n = _choose();
    if (n != DIRECT) {
        n = _last_resort(n);
        if (n < 0) {
            CpuOpponent::reset_approach(rec);
            return true;
        }
    }
    gd::Int pick = ai->execute_candidate(rec);
    if (pick < 0) {
        return false;
    }
    rec->approach_hold = 0x28;
    rec->direction_hold = -1;
    rec->approach_kind = 0;
    rec->approach = 0;
    gd::Ref<MoveRow> target = ai->cand_target[pick];
    if (Fx::s16(fight->attract) == 0 && (target->flags & MoveFlag::BUFFER_INPUT) != 0) {
        rec->strings_on = 1;
    } else if (((rec->own_state & 0x80000) != 0 && (ai->lcg_next() & 0x3F) < 0x20)
               || ((target->state & 0x80000) != 0 && (ai->lcg_next() & 0x3F) < 0x30)) {
        rec->planned_on = 1;
    }
    gd::Ref<MoveRow> pressed = rec->pressed_move;
    gd::Int state = pressed != nullptr ? Fx::s16(pressed->state) : 0;
    if (state == StateWord::CROUCH || state == 0x2829) {
        rec->crouch_attack_hold = 0xC;
    }
    if (target->active_first == 0 && (target->state & 0x80000) == 0) {
        return true;
    }
    rec->last_attack = target;
    return true;
}

// With nothing marked, far away and at 10 % health, a 1-in-4 draw and word 16 mark the
// desperation moves (move +0x24 bit 16).
gd::Int AiDecision::_last_resort(gd::Int n) {
    if (n == 0 && rec->reach_band > 2 && Fx::s16(f->power_timer) == 0
            && f->health <= Fx::div_trunc(f->health_max, 10)
            && (ai->lcg_next() & 3) == 0 && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::PUNISH)) {
        n = ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return (m->flags & MoveFlag::POWER) != 0;
        });
    }
    return n;
}

// Marks the candidates to execute: their count (−1: input already set), or DIRECT.
gd::Int AiDecision::_choose() {
    gd::Int wait = rec->wait;
    gd::Int threshold = rec->word(AiRecord::Param::THROW_TRY);
    if (rec->no_strings != 0) {
        gd::Int removed = 0;
        for (gd::Int k = 0; k < rec->candidate_count; ++k) {
            gd::Int mark = ai->cand_mark[k];
            if (mark >= 0 && (ai->cand_target[k]->flags & MoveFlag::BUFFER_INPUT) != 0) {
                if (mark > 0) {
                    removed += 1;
                }
                ai->cand_mark[k] = -1;
            }
        }
        rec->marked_count = Fx::s16(rec->marked_count - removed);
    }
    if (rec->announced_frames >= 0x1A && rec->guard_delay < 0 && rec->guard_holds == 0) {
        gd::Int delay = rec->opp_until - ((fight->ai_rng & 7) + 4);
        ai->lcg_next();
        rec->guard_delay = gd::maxi(delay, 3);
    }
    if (rec->guard_delay >= 1) {
        return 0;
    }
    gd::Int focus = rec->word(AiRecord::Param::FOCUS);
    if (rec->punish != 0 || rec->throw_chance != 0
            || (rec->announced_frames > 0x19 && Fx::div_trunc(focus, 2) <= (ai->lcg_next() & 0xFFF))
            || (((rec->passed_frames - 1) & 0xFFFFFFFFLL) < 0x13 && focus <= (ai->lcg_next() & 0xFFF))) {
        wait = -1;
    }
    gd::Ref<MoveRow> omv = o->pose_move;
    gd::Int band = rec->band;
    if ((ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::AIR_RETREAT) && o->in_air != 0
            && Fx::s16(rec->opp_state) != StateWord::LAUNCHED) {
        gd::Int last = omv->air_last;
        gd::Int first = omv->air_first;
        gd::Int frame = Fx::s16(o->pose_frame);
        if (first + Fx::div_trunc(last - first, 2) <= frame && frame <= last && rec->angle <= FightMath::QUARTER_TURN
                && band == 2) {
            rec->wait = -1;
        }
    }
    if (Fx::s16(o->cur_slot) == 6 && band < 3 && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::PUNISH)) {
        rec->wait = Fx::s16(rec->wait - 2);
    }
    if ((rec->opp_state & (StateBit::AIRBORNE | StateBit::DOWN_REVERSED | StateBit::DOWN | StateBit::CROUCHING)) != 0
            || o->in_air != 0 || o->about_to_hit != 0 || o->bank_type == BankType::GON || rec->opp_until < 10) {
        threshold = 0;
        rec->throw_chance = 0;
    }
    if (fight->mode == GameMode::BALL && _ball_ahead()) {
        return ai->filter_attacks(rec);
    }
    if (rec->opp_down != 0 && rec->vs_stance == 0) {
        return _vs_grounded(wait);
    }
    if (Fx::s16(rec->opp_state) == StateWord::LAUNCHED && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::VS_LAUNCHED)) {
        return _vs_stance();
    }
    if (rec->air_cooldown >= 0) {
        gd::Int n = ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return m->active_first != 0 && ((m->state & 0x80000) != 0 || (m->attack_word & HINT_SPECIAL) != 0)
                && (m->state & StateBit::NOT_AI_ATTACK) == 0;
        });
        if (n > 0) {
            rec->air_cooldown = -1;
        }
        return n;
    }
    if (rec->opp_unblockable != 0) {
        if (rec->punish == 0 && rec->word(AiRecord::Param::PUNISH) <= (ai->lcg_next() & 0xFFF)) {
            if (wait < 1) {
                return (rec->opp_state & StateBit::CROUCHING) != 0 ? ai->filter_attacks_no_special(rec) : ai->filter_attacks(rec);
            }
            return 0;
        }
        return ai->filter_side_steps_then_quick(rec);
    }
    if ((rec->opp_coming != 0 || rec->opp_grabs != 0) && rec->vs_stance == 0) {
        if (rec->punish != 0 || (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::PUNISH)) {
            return ai->filter_side_steps_then_quick(rec);
        }
        if (wait < 1) {
            return (rec->opp_state & StateBit::CROUCHING) != 0 ? ai->filter_attacks_no_special(rec) : ai->filter_attacks(rec);
        }
        return 0;
    }
    if (wait < 1) {
        if (rec->neutral_cooldown < 0) {
            return _approach_attack(threshold);
        }
        gd::Int n2 = ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return (m->state & 0xA0000) == 0x80000 && m->air_first == 0;
        });
        rec->neutral_cooldown = -1;
        return n2;
    }
    if (band > 2 || (rec->own_state & StateBit::CROUCHING) != 0 || (ai->lcg_next() & 0xFFF) > 3) {
        return 0;
    }
    if (ai->mark_neutral(rec) < 1) {
        return 0;
    }
    return DIRECT;
}

bool AiDecision::_low_or_hint(const gd::Ref<MoveRow>& m, const gd::Arr<gd::Int>& extra) {
    gd::Int w = m->attack_word;
    return m->active_first < 0x51 && ((w & HINT_SAFE) != 0 || extra.has(w & 0xFFFF)) && (w & HINT_NEVER) == 0;
}

// The opponent is down (+0x208).
gd::Int AiDecision::_vs_grounded(gd::Int wait) {
    if (rec->attacking != 0) {
        return 0;
    }
    gd::Int x = ai->lcg_next();
    if (rec->word(AiRecord::Param::FOCUS) <= (x & 0xFFF)) {
        wait = -1;
    }
    if (!(rec->angle <= FightMath::QUARTER_TURN || (ai->lcg_next() & 0xF) != 0)) {
        return ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return (m->state & StateBit::NOT_AI_ATTACK) != 0;
        });
    }
    if (wait >= 0) {
        return 0;
    }
    x = ai->lcg_next();
    gd::Int u = x & 0xFFF;
    if (u < 0x334) {
        return ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return m->bank->branches[m->branches][ROW_CONDITION] == BranchCondition::OPP_DOWN
                && (m->attack_word & HINT_NEVER) == 0;
        });
    }
    if (u < 0x3AF) {
        ai->side_step(rec, x & 1);
        return -1;
    }
    if (u < 0x6E2) {
        return ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return _low_or_hint(m, gd::Arr<gd::Int>{AttackWord::LOW_2});
        });
    }
    if (u < 0x75D && (rec->own_state & StateBit::CROUCHING) == 0) {
        return ai->mark_neutral(rec);
    }
    if (u < 0x829) {
        return ai->mark_command(rec, CpuOpponent::CROUCH_DASH);
    }
    if (((rec->band - 2) & 0xFFFFFFFFLL) < 2 && u < 0x9C3) {
        ai->start_script(rec, t->wake_up);
        return -1;
    }
    return ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        return _low_or_hint(m, gd::Arr<gd::Int>{AttackWord::LOW_2, AttackWord::LOW});
    });
}

bool AiDecision::_hint_ground(const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) {
    return (m->attack_word & HINT_SAFE) != 0 && m->active_first == 0 && m->air_first == 0;
}

// The opponent is in a stance (state 0x4C02), after a draw against word 30.
gd::Int AiDecision::_vs_stance() {
    gd::Int band = rec->band;
    gd::Int n = 0;
    if (band < 3) {
        if (band > 1) {
            gd::Int x = ai->lcg_next();
            if ((x & 0xF) == 0) {
                n = ai->mark_command(rec, CpuOpponent::SIDE_STEP);
            } else {
                n = ai->mark_candidates(rec, _hint_ground);
            }
        }
    } else {
        bool side = true;
        if (rec->distance_change < 0x33) {
            gd::Int x2 = ai->lcg_next();
            if ((x2 & 7) == 0) {
                n = ai->mark_candidates(rec, _hint_ground);
                side = false;
            }
        }
        if (side) {
            n = ai->mark_command(rec, CpuOpponent::SIDE_STEP);
            if (n > 0) {
                rec->approach_kind = 2;
                rec->approach = 0xFA;
                rec->approach_hold = 0x136;
            }
        }
    }
    if (band < 4 && rec->opp_high == 0 && rec->word(AiRecord::Param::FOCUS) <= (ai->lcg_next() & 0xFFF)) {
        rec->wait = -1;
        if (o->in_air == 0 && (ai->lcg_next() & 0x3F) < 0x20 && !rec->filter_no_lows) {
            n += ai->filter_punish_level_no_high(rec);
        } else {
            n += ai->filter_punish_level(rec);
        }
    }
    return n;
}

// Waiting is over and nothing urgent: throws, bank setups, neutral moves, side steps or the
// regular attack filters.
gd::Int AiDecision::_approach_attack(gd::Int threshold) {
    gd::Int band = rec->band;
    if ((rec->throw_chance != 0 || (ai->lcg_next() & 0xFFF) < threshold) && band < 2 && o->bank_type != BankType::GON) {
        gd::Int x = ai->lcg_next();
        gd::Int n = 0;
        std::function<bool(const gd::Ref<MoveRow>&, const gd::PI32&)> throw_rows =
            [](const gd::Ref<MoveRow>& m, const gd::PI32& br) -> bool {
                return (m->attack_word & HINT_NEVER) != 0 && (br[ROW_COMMAND] & 0xC200) != 0x200;
            };
        std::function<bool(const gd::Ref<MoveRow>&, const gd::PI32&)> plain_throws =
            [](const gd::Ref<MoveRow>& m, const gd::PI32& br) -> bool {
                return (m->attack_word & HINT_NEVER) != 0 && (br[ROW_COMMAND] & 0xFFE0) == 0;
            };
        if ((x & 0xFFF) < rec->word(AiRecord::Param::THROW_LIST)) {
            n = ai->mark_candidates(rec, throw_rows);
            if (n < 1) {
                n = ai->mark_candidates(rec, plain_throws);
            } else {
                rec->throw_list = 1;
            }
        } else {
            n = ai->mark_candidates(rec, plain_throws);
        }
        if (n > 0) {
            return n;
        }
    }
    gd::Arr<gd::Ref<AiTables::Setup>> setups = t->bank(f->bank_type)->setups;
    if (rec->setup_cooldown < 0 && !setups.is_empty() && rec->attacking == 0 && (rec->own_state & StateBit::CROUCHING) == 0
            && Fx::s16(f->cur_slot) != setups[0]->slot && rec->angle < 0x2001 && f->in_air == 0 && band < 3
            && rec->opp_down == 0) {
        gd::Int u = ai->lcg_next() & 0xFFF;
        for (const gd::Ref<AiTables::Setup>& setup : setups) {
            if (u <= setup->chance) {
                if (ai->branch_open_to(rec, setup->slot)) {
                    ai->start_script(rec, setup->steps);
                    rec->planned_on = 1;
                    rec->setup_cooldown = (ai->lcg_next() & 0xF) + 0x1C;
                    return -1;
                }
                break;
            }
        }
    }
    if (band < 3 && f->in_air == 0 && (rec->own_state & StateBit::CROUCHING) == 0 && (ai->lcg_next() & 0x3F) < 0xE) {
        return ai->mark_neutral(rec);
    }
    gd::Int x3 = ai->lcg_next();
    if ((x3 & 0x3F) < 0x20) {
        if ((ai->lcg_next() & 1) != 0) {
            return ai->mark_candidates(rec, _side_step_or_special);
        }
        return ai->filter_punish_level(rec);
    }
    if (rec->neutral_cooldown < 0 && rec->air_cooldown < 0 && (rec->own_state & StateBit::CROUCHING) == 0 && f->in_air == 0
            && (ai->lcg_next() & 0xFFF) < 0x200) {
        gd::Int neutral = ai->mark_neutral(rec);
        if (neutral > 0) {
            rec->neutral_cooldown = 0x12;
            rec->wait = -1;
            return neutral;
        }
    }
    gd::Int n2 = ai->mark_candidates(rec, _side_step_or_special);
    if ((ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::FILTER_POSTURE)) {
        return n2 + ai->filter_vs_posture(rec);
    }
    if ((ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::FILTER_FAST)) {
        return n2 + ai->filter_fast_attacks(rec);
    }
    if ((rec->opp_state & StateBit::CROUCHING) != 0 && (ai->lcg_next() & 0xFFF) >= rec->word(AiRecord::Param::FOCUS)) {
        return n2 + ai->filter_attacks_no_special(rec);
    }
    if ((ai->lcg_next() & 0x3F) < 10) {
        return n2 + ai->filter_plain_attacks(rec);
    }
    if (f->in_air == 0 && rec->air_cooldown < 0 && ai->random_below(0x1000) < 0x200) {
        gd::Int air = ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
            return m->air_first != 0 && (m->state & StateBit::NOT_AI_ATTACK) == 0
                && ((m->state & 0x80000) != 0 || (m->attack_word & HINT_SPECIAL) != 0);
        });
        if (air > 0) {
            rec->air_cooldown = 4;
            gd::Int a = ai->random_below(4);
            gd::Int b = ai->random_below(4);
            gd::Int d = ai->random_below(4);
            rec->air_cooldown = a + b + d + 1;
            return air;
        }
    }
    if (ai->random_below(0x1000) < 10) {
        return n2 + ai->mark_candidates(rec, [](const gd::Ref<MoveRow>& m, const gd::PI32& br) -> bool {
            gd::Int slot = br[ROW_TARGET] & 0xFFFF;
            return m->active_first == 0 && m->air_first == 0 && (m->state & StateBit::NOT_AI_ATTACK) == 0
                && slot != 0x1EE && slot != 0x6A3;
        });
    }
    return n2 + ai->filter_attacks(rec);
}

// ---- movement

void AiDecision::_move() {
    if (_movement() == 0) {
        if (rec->crouch_timer >= 0) {
            rec->pad = PAD_DOWN;
        } else if (rec->direction_hold >= 0) {
            rec->pad = rec->prev_pad & PadState::DIRECTIONS;
        } else {
            rec->pad = 0;
        }
    }
}

gd::Int AiDecision::_movement() {
    if (rec->move_block >= 0) {
        return 0;
    }
    if (rec->approach < 0) {
        if (rec->attacking == 0 && rec->crouch_timer < 0 && rec->direction_hold < 0 && rec->air_cooldown < 0) {
            return _pick_movement();
        }
        return 0;
    }
    if ((rec->opp_breath == 0 || rec->distance > 7000) && rec->band > 0) {
        gd::Int x = fight->ai_rng;
        gd::Int x1 = (5 * x + 3) & 0xFFFFFFFFLL;
        if (rec->word(AiRecord::Param::FOCUS) <= (x & 0xFFF)) {
            bool unsafe = false;
            if (rec->opp_coming != 0 && rec->guard_holds == 0 && Fx::s16(f->opp_rel_angle) < 0x5000) {
                if (rec->distance < 0xB87) {
                    gd::Int w = o->pose_move->attack_word;
                    gd::Int r = w & 0xFFFF0000LL;
                    if ((w & HINT_REACH) == 0) {
                        r |= HINT_CLOSE | HINT_MIDDLE;
                    }
                    unsafe = (r & gd::Int(t->approach_reach[rec->reach_band])) != 0;
                }
            }
            fight->ai_rng = x1;
            if (unsafe) {
                CpuOpponent::reset_approach(rec);
                return _pick_movement();
            }
        }
        fight->ai_rng = x1;
        if (rec->approach_kind == 2) {
            gd::Int pad = PAD_FORWARD;
            if (rec->reach_band < 3 && (ai->lcg_next() & 0xFFF) < 0x20) {
                CpuOpponent::reset_approach(rec);
                pad = PAD_BACK;
            }
            rec->pad = pad;
            return 1;
        }
        if (rec->wait < 0x24 && rec->reach_band == 2 && (ai->lcg_next() & 0xF) == 0) {
            if (ai->mark_command(rec, CpuOpponent::SIDE_STEP) > 0 && ai->execute_candidate(rec) >= 0) {
                rec->approach_kind = 0;
                rec->approach = 0;
                rec->approach_hold = 0x28;
                return 1;
            }
        }
        rec->pad = rec->prev_pad & PadState::DIRECTIONS;
        return 1;
    }
    CpuOpponent::reset_approach(rec);
    return _pick_movement();
}

// A random movement by distance band (or, while waiting long, by reach band).
gd::Int AiDecision::_pick_movement() {
    if (rec->approach_hold >= 0 || rec->direction_hold >= 0) {
        return 0;
    }
    gd::Int v = ai->random_below(0x1000);
    gd::Int choice = 0;
    if (rec->opp_breath == 0) {
        if (rec->wait < 0x24) {
            choice = _by_distance(v);
        } else {
            choice = _by_closing(v);
            if ((choice == 3 || choice == 6) && rec->word(AiRecord::Param::KEEP_RETREAT) <= (ai->lcg_next() & 0xFFF)) {
                choice = 0;
            }
        }
        if ((choice == 3 || choice == 6) && ((rec->opp_state & 0x80000) != 0 || rec->opp_attack != 0)
                && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::FOCUS)) {
            choice = 0;
        }
    }
    return _do_movement(choice);
}

gd::Int AiDecision::_by_distance(gd::Int v) {
    switch (rec->band) {
        case 0:
        case 1:
            return v < 0x14 ? 3 : 0;
        case 2:
            if (v < 0x7A) {
                return 5;
            }
            if (v < 0x1EB) {
                return 1;
            }
            if (v < 0x214) {
                return 3;
            }
            return v < Fx::div_trunc(rec->word(AiRecord::Param::SIDE_STEP) << 12, 1000) ? 8 : 0;
        case 3:
            return v < 0x666 ? 2 : (v < 0xB33 ? 1 : 0);
        case 4:
            return v < 0xCC ? 2 : (v < 0x599 ? 4 : 1);
        case 5:
            if (rec->far_count < 0x2D1 && rec->opp_down == 0 && v > 0x11D) {
                return 1;
            }
            return 4;
        default:
            break;
    }
    return 0;
}

gd::Int AiDecision::_by_closing(gd::Int v) {
    switch (rec->reach_band) {
        case 0:
        case 1:
            return v < 0x199 ? 6 : (v < 0x333 ? 3 : 0);
        case 2:
            if (v <= 0x27) {
                return 2;
            }
            return v < 0x170 ? 3 : 0;
        case 3:
            if (v < 0x1EB) {
                return 2;
            }
            if (v < 0x214) {
                return 5;
            }
            if (v < 0x218) {
                return 3;
            }
            if (v < 0x22D) {
                return 1;
            }
            return (v < 0x241 && (rec->own_state & StateBit::STANDING) != 0) ? 8 : 0;
        case 4:
            if (v < 0x80) {
                return 2;
            }
            if (v < 0x90) {
                return 5;
            }
            if (v < 0xC0) {
                return 8;
            }
            return v < 0x3C0 ? 1 : 0;
        case 5:
            if (rec->far_count < 0x2D1 && rec->opp_down == 0 && v > 0x50) {
                if (v < 0xCC) {
                    return 2;
                }
                if (v < 0x400) {
                    return 1;
                }
                return v < 0x5EB ? 5 : 0;
            }
            return 4;
        default:
            break;
    }
    return 0;
}

// The movements: 1 side step or walk in, 2 walk in, 3 step back, 4 side step with a long
// approach or dash in, 5 dash in, 6 crouch dash or step back, 8 side step then a candidate.
gd::Int AiDecision::_do_movement(gd::Int choice) {
    if (choice == 0) {
        return 0;
    }
    if (choice == 1) {
        if (ai->mark_command(rec, CpuOpponent::SIDE_STEP) >= 1) {
            rec->approach_kind = 0;
            rec->approach = 0;
            rec->approach_hold = 0x28;
            return ai->execute_candidate(rec) >= 0 ? 1 : 0;
        }
        choice = 2;
    }
    if (choice == 4) {
        if (ai->mark_command(rec, CpuOpponent::SIDE_STEP) >= 1) {
            rec->approach_kind = 2;
            rec->approach = 0xFA;
            rec->approach_hold = 0x136;
            return ai->execute_candidate(rec) >= 0 ? 1 : 0;
        }
        choice = 5;
    }
    if (choice == 6) {
        if (ai->mark_command(rec, CpuOpponent::CROUCH_DASH) >= 1) {
            rec->approach_hold = 0x3C;
            return ai->execute_candidate(rec) >= 0 ? 1 : 0;
        }
        choice = 3;
    }
    if (choice == 2) {
        // GDScript `short` / `long` are C++ keywords: `short_` / `long_`.
        gd::Int q = 0;
        gd::Int short_ = 0;
        gd::Int long_ = 0;
        if (rec->band >= 0 && rec->band < 3) {
            q = ai->random_below(8);
            short_ = q + 4;
            long_ = q + 0x2C;
        } else if (rec->band == 3) {
            q = ai->random_below(10);
            short_ = q + 10;
            long_ = q + 0x32;
        } else {
            q = ai->random_below(0xF);
            short_ = q + 0xF;
            long_ = q + 0x37;
        }
        rec->approach_kind = 0;
        rec->approach = short_;
        rec->approach_hold = long_;
        rec->pad = rec->crouching > 0 ? PAD_DOWN_FORWARD : PAD_FORWARD;
        return 1;
    }
    if (choice == 3) {
        gd::Int q = ai->random_below(5);
        rec->approach = q + 0x14;
        rec->approach_kind = 0;
        rec->approach_hold = q + 0x3C;
        rec->pad = rec->crouching > 0 ? PAD_DOWN_BACK : PAD_BACK;
        return 1;
    }
    if (choice == 5) {
        gd::Int q = ai->random_below(0x14);
        rec->approach = q + 0x28;
        rec->approach_kind = 0;
        rec->approach_hold = q + 0x50;
        rec->pad = rec->crouching > 0 ? PAD_DOWN_FORWARD : PAD_FORWARD;
        return 1;
    }
    if (choice == 8 && (rec->reach_band > 2 || rec->opp_until > 10)) {
        ai->side_step(rec, -1);
    }
    return ai->execute_candidate(rec) >= 0 ? 1 : 0;
}

// ---- the end of the frame

// Timers, adaptive difficulty, guard memory and the pad output.
gd::PI32 AiDecision::_finish() {
    gd::Int state = rec->own_state;
    gd::Ref<MoveRow> target = rec->pressed_move;
    rec->crouching = (state & (StateBit::DOWN_REVERSED | StateBit::DOWN | StateBit::CROUCHING)) == StateBit::CROUCHING ? 1 : 0;
    if (target != nullptr && rec->direction_hold < 0 && target->air_first != 0) {
        rec->direction_hold = target->air_first;
    }
    rec->direction_hold = _dec(rec->direction_hold);
    rec->approach = _dec(rec->approach);
    rec->approach_hold = _dec(rec->approach_hold);
    rec->whiff_timer = _dec(rec->whiff_timer);
    rec->air_cooldown = _dec(rec->air_cooldown);
    rec->neutral_cooldown = _dec(rec->neutral_cooldown);
    rec->setup_cooldown = _dec(rec->setup_cooldown);
    rec->move_block = _dec(rec->move_block);
    rec->guard_suspend = _dec(rec->guard_suspend);
    if (rec->reload_timer == 0) {
        ai->load_params(rec, -1, -1, -1);
    }
    rec->reload_timer = _dec(rec->reload_timer);
    if (fight->mode == GameMode::FORCE) {
        gd::Int slot = rec->slot & 0xFFFF;
        for (gd::Int base : gd::Arr<gd::Int>{0, 2}) {
            if (ai->force_timers[base + slot] >= 0) {
                ai->force_timers[base + slot] -= 1;
            }
        }
    }
    gd::Int posture = state & 0xFFFF;
    rec->coming_seen = rec->opp_coming;
    bool guarding = posture == StateWord::GUARD_HIGH || posture == StateWord::GUARD_LOW;
    if (rec->guarding == 0) {
        rec->guarding = guarding ? 1 : 0;
        rec->guard_memory = 0;
    } else {
        rec->guarding = guarding ? 1 : 0;
        if (!guarding && rec->guard_memory != 0) {
            rec->guard_memory = 0;
        }
    }
    if (f->got_hit != 0) {
        CpuOpponent::reset_approach(rec);
        rec->planned_on = 0;
        rec->strings_on = 0;
        rec->links_on = 0;
        rec->throw_list = 0;
    }
    if (f->whiffed != 0 && rec->reach_band < 3) {
        rec->whiff_timer = 0x1E;
    }
    if (o->whiffed != 0 && rec->band < 2 && (ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::PUNISH)) {
        rec->wait = Fx::s16(rec->wait - 3);
    }
    if (f->health < rec->health_seen) {
        _learn_from_damage();
    }
    rec->health_seen = f->health;
    if (f->got_hit != 0 && fight->attract == 0) {
        rec->approach_hold = 0x28;
        rec->direction_hold = -1;
        rec->approach_kind = 0;
        rec->approach = 0;
        _raise_capped(AiRecord::Param::GUARD, 10, AiRecord::Param::GUARD_CAP);
        _raise_capped(AiRecord::Param::PUNISH, 10, AiRecord::Param::PUNISH_CAP);
        if (rec->opp_string > 0) {
            _raise_capped(AiRecord::Param::DUCK, 0x28, AiRecord::Param::DUCK_CAP);
            _raise_capped(AiRecord::Param::COUNTER, 0x14, AiRecord::Param::COUNTER_CAP);
        }
    }
    gd::Int pad = rec->pad & 0xFFFF;
    gd::Int pressed = pad & (pad ^ rec->prev_pad);
    rec->prev_pad = pad;
    return gd::PI32{pad, pressed};
}

void AiDecision::_raise_capped(gd::Int word_index, gd::Int step, gd::Int cap) {
    rec->params[word_index] = Fx::s16(rec->params[word_index] + step);
    if (gd::Int(rec->params[cap]) < gd::Int(rec->params[word_index])) {
        rec->params[word_index] = rec->params[cap];
    }
}

// Whether the opponent's move is one that hurt the CPU before: the game compares only history slot
// 0 (bug #23); `every_entry` (RuleSet.fix_ai_damage_history) compares the whole history.
bool AiDecision::learned(const gd::Arr<gd::Ref<MoveRow>>& history, const gd::Ref<MoveRow>& move, bool every_entry) {
    return every_entry ? history.has(move) : move == history[0];
}

// The CPU lost health: after a draw against word 35 the wait shortens and, in one-on-one play,
// the opponent's move goes into an 8-entry history; a move already there twice switches to
// level 9 for 600 frames (the adaptive mode).
void AiDecision::_learn_from_damage() {
    if ((ai->lcg_next() & 0xFFF) < rec->word(AiRecord::Param::FOCUS)) {
        return;
    }
    rec->wait = Fx::s16(rec->wait - 3);
    gd::Ref<MoveRow> move = o->pose_move;
    if (ai->multi != 0) {
        return;
    }
    rec->adaptive = 0;
    gd::Int seen = 0;
    for (gd::Int k = 0; k < AiRecord::HISTORY; ++k) {
        if (rec->history[k] == move) {
            seen += 1;
        }
    }
    if (seen > 1) {
        if (rec->reload_timer < 1) {
            ai->load_params(rec, 0, 9, -1);
        }
        rec->reload_timer = 600;
        rec->adaptive = 1;
    }
    gd::Int k2 = rec->history_at;
    rec->history[k2] = move;
    rec->history_at = (k2 + 1) & 7;
}

// ---- Tekken Ball (volley.ovl)

// FUN_800B4958: both fighters and the ball projected onto the line through the fighters. The
// line's z intercept divides by the fighters' x difference unchecked (bug #24: with equal x the
// R3000's quotient ±1 gives one frame of wrong positions; with the fix the line keeps its values).
void AiDecision::_ball_project() {
    gd::Ref<TekkenBall> tb = fight->ball;
    gd::PI32 p = tb->position();
    gd::Ref<FighterState> f0 = fight->fighters[0];
    gd::Ref<FighterState> f1 = fight->fighters[1];
    gd::Int dx = Fx::w32(f1->root_x - f0->root_x);
    gd::Int dz = Fx::w32(f1->root_z - f0->root_z);
    if (dx == 0 && fight->rules->fix_ball_cpu) {
        return;
    }
    gd::Int angle = CameraMath::atan2_units4096(dx, dz, fight->tables->camera);
    gd::Int c = FightMath::cos12(-angle, fight->tables);
    gd::Int s = FightMath::sin12(-angle, fight->tables);
    gd::Int k = Fx::w32(f0->root_z - TekkenBall::_div(Fx::w32(dz * f0->root_x), dx));
    gd::Int p0 = p[0];
    gd::Int p1 = p[1];
    gd::Int p2 = p[2];
    gd::PI32 line{Fx::trunc12(Fx::w32(f0->root_x * c - Fx::w32(f0->root_z - k) * s)),
                  Fx::trunc12(Fx::w32(f1->root_x * c - Fx::w32(f1->root_z - k) * s)),
                  Fx::trunc12(Fx::w32(p0 * c - Fx::w32(p2 - k) * s)), p1,
                  Fx::trunc12(Fx::w32(p0 * s + Fx::w32(p2 - k) * c))};
    for (gd::Int i = 0; i < 5; ++i) {
        tb->v->put32(BALL_LINE + 4 * i - TekkenBall::BASE, line[i]);
    }
}

gd::Int AiDecision::_ball_line(gd::Int i) {
    return fight->ball->v->s32(BALL_LINE + 4 * i - TekkenBall::BASE);
}

// FUN_800B4B08: the CPU's distance to the ball on the line.
gd::Int AiDecision::_ball_distance() {
    gd::Int d = Fx::w32(_ball_line(f->player_index) - _ball_line(2));
    gd::Int lateral = _ball_line(4);
    return CameraMath::isqrt(Fx::w32(d * d + lateral * lateral) & 0xFFFFFFFFLL);
}

// FUN_800B4B64: the ball is behind the CPU on the line: it back-dashes to get under it.
bool AiDecision::_ball_behind() {
    gd::Int ball = _ball_line(2);
    bool behind = rec->slot == 0 ? ball < _ball_line(0) : _ball_line(1) < ball;
    if (behind) {
        ai->start_script(rec, t->back_dash);
    }
    return behind;
}

// FUN_800B4BE4: the ball is ahead of the CPU: for player 1 more than 2,000 units ahead, for
// player 2 ahead but closer than 2,000 units (bug #3; the fix mirrors player 1's test).
bool AiDecision::_ball_ahead() {
    gd::Int ball = _ball_line(2);
    if (rec->slot == 0) {
        gd::Int p = _ball_line(0);
        return p < ball && p + BALL_AHEAD < ball;
    }
    gd::Int q = _ball_line(1);
    if (fight->rules->fix_ball_cpu) {
        return ball < q && ball < q - BALL_AHEAD;
    }
    return ball < q && q - BALL_AHEAD < ball;
}

}  // namespace t3
