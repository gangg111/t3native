// remake/core/fighter/move_system.gd
// The move lifecycle (moves.md): MoveStartAll / MoveStartOrAdvance with every transition code,
// MoveStepAll / MoveStep (distances, hit reactions, KO collapse, throw pairing), MoveBranchStep
// with BranchFind, InputMatch and BranchCondition, and MoveBranchAll with the reversals.
//
// Written from the verified ports in tools/research/fight_sim.py (MoveStartOrAdvance, MoveBranchStep,
// BranchCondition, InputMatch, relative angles and LaunchTrajectory in fight_math.py) and the
// decompiled MoveStartAll, MoveStep, MoveReactionBranch, BranchFindReversal, HitSelectReaction,
// AirReactionKind, FighterCollapse and ThrowVictimStart.
#include "core/fighter/move_system.h"

#include "content/fight_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_math.h"
#include "core/combat/attack_records.h"
#include "core/combat/attack_word.h"
#include "core/combat/hit_slot.h"
#include "core/combat/state_bit.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/branch_condition.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_sounds.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/transition.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/math/fx.h"

namespace t3 {

MoveSystem::Cursor::Cursor(const gd::Arr<gd::PI32>& list, gd::Int index) {
    rows = list;
    i = index;
}

MoveSystem::MoveSystem(const gd::Ref<FightState>& fight_state) {
    fight = fight_state;
    t = fight_state->tables;
}

// angles

// FighterRelativeAngles (0x80043D2C).
void MoveSystem::relative_angles(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    if (f->fixed_facing == 0) {
        gd::Int target = FightMath::atan2_angle(opp->root_z - f->root_z, opp->root_x - f->root_x, t);
        f->target_dir = target;
        f->opp_to_self_dir = (target - 0x8000) & 0xFFFF;
        f->opp_heading = opp->heading & 0xFFFF;
    } else {
        gd::Int side = f->fixed_facing_side == 0 ? 0x4000 : -0x4000;
        f->opp_to_self_dir = (side - 0x8000) & 0xFFFF;
        f->opp_heading = (side - 0x8000) & 0xFFFF;
        f->target_dir = side & 0xFFFF;
    }
    f->heading_delta = (f->heading - f->target_dir) & 0xFFFF;
    f->facing_quadrant = FightMath::quadrant(f->heading_delta);
    f->opp_heading_delta = (f->opp_heading - f->opp_to_self_dir) & 0xFFFF;
    f->opp_quadrant = FightMath::quadrant(f->opp_heading_delta);
    f->rel_angle = FightMath::fold(f->heading_delta);
    f->opp_rel_angle = FightMath::fold(f->opp_heading_delta);
    if (f->dist > 0x200) {
        f->aim_dir = f->rel_angle < 0x4000 ? f->target_dir : (f->target_dir - 0x8000) & 0xFFFF;
        f->opp_aim_dir = f->opp_rel_angle < 0x4000 ? f->opp_to_self_dir : (f->opp_to_self_dir - 0x8000) & 0xFFFF;
    }
}

// MoveStartAll / MoveStartOrAdvance

// MoveStartAll (0x80045834): a pending throw (transition THROW) that reached its entry frame is
// resolved first (one of several by the frame counter), its victim starting with THROWN.
void MoveSystem::start_all() {
    gd::Arr<gd::Ref<FighterState>> throwers;
    for (const auto& f : fight->active()) {
        if (f->move_row != nullptr && f->entry_frame <= f->pose_frame && f->transition == Transition::THROW) {
            throwers.append(f);
        }
    }
    gd::Int first = 0;
    gd::Int second = 1;
    gd::Int rest = 2;                      // Tekken Force: the record outside the throw
    if (!throwers.is_empty()) {
        gd::Ref<FighterState> chosen = throwers[gd::mod(fight->frame_counter, throwers.size())];
        first = chosen->throw_partner;
        second = chosen->index;
        rest = 3 - (first + second);
    }
    gd::Ref<FighterState> f = fight->fighters[first];
    if (throwers.is_empty()) {
        start_or_advance(f, fight->fighters[f->cur_opp_index]);
        start_or_advance(fight->fighters[second], fight->fighters[fight->fighters[second]->cur_opp_index]);
    } else {
        throw_victim_start(f, fight->fighters[f->throw_partner]);
        start_or_advance(f, fight->fighters[f->throw_partner]);
        start_or_advance(fight->fighters[second], fight->fighters[fight->fighters[second]->throw_partner]);
    }
    if (fight->mode == GameMode::FORCE) {
        gd::Ref<FighterState> r = fight->fighters[rest];
        start_or_advance(r, fight->fighters[r->cur_opp_index]);
    }
}

// ThrowVictimStart (0x8002D9B0): the victim's move is the thrower's pending move's +0x32 entry.
void MoveSystem::throw_victim_start(const gd::Ref<FighterState>& victim, const gd::Ref<FighterState>& thrower) {
    gd::Int value = thrower->move_row->reaction;
    gd::Int slot = 0;
    if ((value & 0xF000) == 0) {
        slot = t->reactions[value][0];
    } else {
        slot = t->throw_victim_slots[value & 0xFFF];
    }
    victim->move_slot = slot;
    victim->move_row = fight->move_for_slot(victim, slot);
    victim->pose_frame = 1;
    victim->entry_frame = 1;
    victim->transition = FightMath::transition_remap(Transition::THROWN, victim->move_row, victim->entry_frame);
    victim->trans_bit7 = thrower->trans_bit7;
    thrower->trans_bit7 = 0;
    victim->trans_bit6 = 0;
}

// MoveStartOrAdvance (0x8002F600).
void MoveSystem::start_or_advance(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    if (f->active == 0) {
        return;
    }
    f->step_cooldown = gd::maxi(f->step_cooldown - 1, 0);
    if (!_pending_starts(f)) {
        _advance(f);
        return;
    }
    gd::Ref<MoveRow> move = f->move_row;
    if (f->branch_kind == 3) {
        f->entry_frame = 1;
    }
    _reset_for_move(f, opp, move);
    _load_move_words(f, move);
    gd::Int move_flags = move->flags;
    gd::Int pose_flags = f->pose_move->flags;
    f->crouch_move = (move_flags & MoveFlag::BUFFER_INPUT) != 0 ? 1 : 0;
    FighterInput::buffer_enable(f, (move_flags & MoveFlag::BUFFER_INPUT) != 0);
    if (f->cmd_read < f->cmd_count) {
        f->cmd_read += 1;
    }
    f->guarded_prev = f->guarded != 0 ? 1 : 0;
    if (f->got_hit == 0) {
        f->was_hit_this_move = 0;
        f->ballistic = 0;
        f->juggle_count = 0;
    }
    if (f->contact == 0) {
        f->contact_this_move = 0;
    }
    _count_react_chain(f, move_flags, pose_flags);
    f->in_reaction = f->hit_clean;
    if (f->got_hit != 0 && f->health != 0) {
        f->hit_freeze = f->hit_freeze_in;
    }
    if (f->branch_kind == 1 || f->branch_kind == 3 || move->anim_bank != f->pose_move->anim_bank
            || move->anim != f->pose_move->anim) {
        f->move_changed = 1;
    }
    gd::Int reanchor = (pose_flags & MoveFlag::REANCHOR) != 0 ? 1 : 0;
    if ((pose_flags & MoveFlag::POWER) != 0) {
        f->power_timer = 0x78;
    }
    if ((move_flags & MoveFlag::ALERT) != 0) {
        f->attack_alert = 9;
    }
    if (f->trans_bit7 != 0) {
        f->trans_bit7 = 0;
        f->heading = Fx::s16(f->heading - FightMath::HALF_TURN);
        f->facing = Fx::s16(f->facing - FightMath::HALF_TURN);
    }
    if (f->apply_end_turn != 0 && f->transition != Transition::THROWN) {
        f->heading = Fx::s16(f->heading + f->pose_move->end_turn);
        f->facing = f->heading;
    }
    relative_angles(f, opp);
    bool tail = _transition(f, opp, f->transition, reanchor);
    if (tail && reanchor != 0) {
        f->pos_x = f->root_x;
        f->pos_z = f->root_z;
    }
    _finish(f);
}

// The pending move starts at its entry frame (playing backwards: at or before it), or when the
// running move has run out.
bool MoveSystem::_pending_starts(const gd::Ref<FighterState>& f) {
    if (f->move_row == nullptr) {
        return false;
    }
    if (f->frame_step >= 1) {
        return !(f->pose_frame < f->entry_frame) || f->pose_move->length <= f->pose_frame;
    }
    if (f->frame_step < 0) {
        return f->pose_frame <= f->entry_frame || f->pose_frame < 1;
    }
    return true;
}

// The per-move state cleared at a move start.
void MoveSystem::_reset_for_move(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                                 const gd::Ref<MoveRow>& move) {
    f->prev_pose_frame = f->pose_frame;
    f->prev_slot = f->cur_slot;
    f->prev_move_unk10 = Fx::s16(move->stance_slot);
    f->hit_done[3] = 0;
    f->move_flag_ba = 0;
    f->skip_step_physics = 0;
    f->hit_done[2] = 0;
    f->air_phase = 0;
    f->slide_state = 0;
    f->track_mode = Tracking::NONE;
    f->hold_frames = -1;
    f->opp_start_x = opp->root_x;
    f->opp_start_z = opp->root_z;
    f->frame_step = 1;
    f->track_accum = 0;
    f->step_kind = 0;
    f->cond_flag_used = 0;
    f->hit_freeze = 0;
    f->hit_done[0] = 0;
    f->hit_done[1] = 0;
    f->hit_cooldown = 0;
}

// The move's damage, attack class, state, attack and guard words; the guard of a humans-only
// guard state is dropped for the CPU.
void MoveSystem::_load_move_words(const gd::Ref<FighterState>& f, const gd::Ref<MoveRow>& move) {
    if (f->damage_override == 0) {
        f->attack_class = move->attack_class();
        f->damage = f->attack_class == 1 ? 0 : move->damage();
    } else {
        f->damage = f->damage_override;
    }
    f->state = move->state;
    f->attack = move->attack;
    f->guard = f->state & StateBit::GUARDS;
    f->state_class = (f->state >> StateBit::CLASS_SHIFT) & 0x1F;
    f->attack_hi = f->attack >> 8;
    if (fight->mode != GameMode::PRACTICE) {
        f->human_guard = f->is_cpu == 0 ? 1 : 0;
    }
    if ((f->state & StateBit::HUMAN_GUARD) != 0 && f->human_guard == 0) {
        f->state &= ~gd::Int(StateBit::GUARDS) & 0xFFFFFFFFLL;
        f->guard &= ~gd::Int(StateBit::GUARDS) & 0xFFFF;
    }
}

// reactChain: consecutive reactions (and reaction-chain moves) counted, reset outside them.
void MoveSystem::_count_react_chain(const gd::Ref<FighterState>& f, gd::Int move_flags, gd::Int pose_flags) {
    bool chain = f->hit_clean != 0
        || ((pose_flags & MoveFlag::REACT_CHAIN) != 0 && (move_flags & MoveFlag::REACT_CHAIN) != 0)
        || (f->throw_state < 0 && (move_flags & MoveFlag::REACT_CHAIN) != 0);
    if (chain) {
        f->react_chain = Fx::s16(f->react_chain + 1);
    } else if (f->in_reaction == 0 && f->throw_state >= 0) {
        f->react_chain = 0;
    } else if (f->transition != Transition::STEP && f->transition != Transition::STEP_CROUCH) {
        if (f->transition == Transition::THROWN) {
            f->react_chain = Fx::s16(f->react_chain + 1);
        } else if ((f->move_slot & 0xFFFF) == 0x9A6) {
            // pass
        } else if ((f->move_slot & 0xFFFF) == 0xCC6) {
            f->react_chain = Fx::s16(f->react_chain + 1);
        } else {
            f->react_chain = 0;
        }
    }
}

void MoveSystem::_advance(const gd::Ref<FighterState>& f) {
    f->move_frame = Fx::s16(f->move_frame + 1);
    f->move_changed = 0;
    gd::Int freeze = f->hit_freeze;
    if (freeze > 0) {
        f->hit_freeze = freeze - 1;
        if (freeze != 1) {
            return;
        }
        f->hit_freeze = 0;
        f->pose_frame = Fx::s16(f->pose_frame + 1);
        f->root_frame = f->pose_frame;
        f->root_move = f->pose_move;
        return;
    }
    gd::Int hold = f->hold_frames;
    gd::Int hold_frame = f->pose_move->hold_frame;
    if (hold < 0) {
        if (f->ballistic == 0) {
            f->pose_frame = Fx::s16(f->pose_frame + f->frame_step);
            if (f->pose_frame < 1) {
                f->pose_frame = 1;
            }
            f->root_frame = f->pose_frame;
        } else if ((f->pose_frame & 0xFFFF) != hold_frame) {
            f->pose_frame = Fx::s16(f->pose_frame + 1);
            f->root_frame = f->pose_frame;
        }
        return;
    }
    if ((f->pose_frame & 0xFFFF) == hold_frame) {
        if (hold < 1) {
            if (hold != 0) {
                return;
            }
            f->pose_frame = Fx::s16(f->pose_frame + 1);
            f->root_frame = f->pose_frame;
            f->root_move = f->pose_move;
            return;
        }
        f->hold_frames = hold - 1;
    } else {
        f->pose_frame = Fx::s16(f->pose_frame + 1);
    }
    f->root_frame = Fx::s16(f->root_frame + 1);
}

void MoveSystem::_continue_frames(const gd::Ref<FighterState>& f) {
    if (FightMath::move_keeps_frame(f->move_row, f->branch_kind, f->pose_frame)) {
        f->pose_frame = Fx::s16(f->pose_frame + 1);
        f->root_frame = Fx::s16(f->root_frame + 1);
    } else {
        f->pose_frame = 1;
        f->root_frame = 1;
    }
    _clamp_to_length(f);
}

void MoveSystem::_clamp_to_length(const gd::Ref<FighterState>& f) {
    gd::Int n = f->move_row->length;
    if (n < f->pose_frame) {
        f->pose_frame = n;
        f->root_frame = n;
    }
}

void MoveSystem::_recover_mash(const gd::Ref<FighterState>& f) {
    gd::Int value = 0;
    if (!((f->pose_move->flags & MoveFlag::LAUNCH_ANGLE) != 0)) {
        value = f->last_damage + Fx::div_trunc(Fx::s16(f->recover_mash), 2);
    }
    f->recover_mash = gd::mini(Fx::s16(value), MAX_MASH);
    f->recover_frames = f->move_row->length;
}

void MoveSystem::_load_push(const gd::Ref<FighterState>& f, gd::Int index) {
    gd::PI32 entry = t->push_entry(index);
    f->push_frames = entry[0];
    f->push_speed = entry[1];
    f->push_table = entry.slice(2);
    f->push_table_pos = 0;
    f->push_table_frames = 8;
}

gd::Int MoveSystem::_turn_step(gd::Int target, gd::Int heading, gd::Int frames) {
    return Fx::s16(Fx::div_trunc(Fx::s16(target - heading), Fx::s16(frames)));
}

void MoveSystem::_timed_turn(const gd::Ref<FighterState>& f, gd::Int target) {
    gd::Int frames = f->move_row->active_first - 1;
    f->turn_frames = frames >= 1 ? frames : 1;
    f->turn_step = _turn_step(target, f->heading, f->turn_frames);
}

void MoveSystem::_keep_root_move(const gd::Ref<FighterState>& f) {
    f->root_frame = Fx::s16(f->root_frame + 1);
    gd::Int n = f->root_move->length;
    if (n < f->root_frame) {
        f->root_frame = n;
    }
}

void MoveSystem::_restart(const gd::Ref<FighterState>& f) {
    f->pose_frame = 1;
    f->root_frame = 1;
}

void MoveSystem::_anchor(const gd::Ref<FighterState>& f) {
    f->pos_x = f->root_x;
    f->pos_z = f->root_z;
}

void MoveSystem::_clear_turn(const gd::Ref<FighterState>& f) {
    f->turn_frames = 0;
    f->turn_step = 0;
}

// One transition case (moves.md#transition-codes); true when the common re-anchor tail runs.
bool MoveSystem::_transition(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int code,
                             gd::Int reanchor) {
    gd::Ref<MoveRow> move = f->move_row;
    switch (code) {
        case Transition::FACE_OPPONENT:
            _restart(f);
            f->throw_state = 0;
            f->root_move = move;
            f->heading = Fx::s16(f->target_dir);
            f->facing = f->heading;
            _anchor(f);
            return false;
        case Transition::CONTINUE_FACING:
        case Transition::CONTINUE:
        case Transition::CONTINUE_FIXED:
            if (code == Transition::CONTINUE_FACING) {
                f->facing = f->heading;
            }
            _continue_frames(f);
            f->track_mode = Tracking::TIMED;
            f->throw_state = 0;
            f->anchor_dirty = 1;
            f->root_move = move;
            _anchor(f);
            return false;
        case Transition::HOLD_TURN:
            _hold(f);
            f->track_mode = Tracking::TIMED;
            if (f->rel_angle <= FACING_LIMIT) {
                _timed_turn(f, f->target_dir);
            }
            f->throw_state = 0;
            return false;
        case Transition::HOLD_TRACK:
            _hold(f);
            f->track_mode = f->rel_angle < FACING_LIMIT ? Tracking::TARGET_SLOW : Tracking::AFTER_ACTIVE;
            _clear_turn(f);
            f->throw_state = 0;
            return false;
        case Transition::RESTART:
            _restart(f);
            f->root_move = move;
            f->facing = f->heading;
            if (reanchor != 0) {
                _anchor(f);
            }
            f->throw_state = 0;
            return false;
        case Transition::REVERSE:
            f->root_move = move;
            f->facing = f->heading;
            f->pose_frame = move->length;
            f->root_frame = move->length;
            if (reanchor != 0) {
                _anchor(f);
            }
            f->frame_step = -1;
            f->throw_state = 0;
            return false;
        case Transition::REWIND:
            f->pose_frame = gd::maxi(f->pose_frame - 1, 1);
            f->frame_step = -1;
            f->anchor_dirty = 1;
            f->throw_state = 0;
            f->root_frame = f->pose_frame;
            f->root_move = move;
            f->facing = f->heading;
            _anchor(f);
            return false;
        case Transition::TRACK_FAST:
            _restart(f);
            f->track_mode = Tracking::TARGET_FAST;
            f->throw_state = 0;
            _clear_turn(f);
            f->root_move = move;
            f->facing = Fx::s16(f->aim_dir);
            return true;
        case Transition::TURN:
        case Transition::TURN_AIM: {
            f->facing = code == Transition::TURN ? f->heading : Fx::s16(f->aim_dir);
            _restart(f);
            f->root_move = move;
            gd::Int frames = move->active_first == 0 ? gd::mini(0x10, f->pose_move->length) : move->active_first - 1;
            f->turn_frames = gd::maxi(frames, 1);
            f->aim_dir = (f->rel_angle & 0xFFFF) < FightMath::QUARTER_TURN ? f->target_dir
                : (f->target_dir - FightMath::HALF_TURN) & 0xFFFF;
            f->turn_step = _turn_step(f->aim_dir, f->heading, f->turn_frames);
            if (reanchor != 0) {
                _anchor(f);
            }
            f->track_mode = Tracking::TIMED;
            f->throw_state = 0;
            return false;
        }
        case Transition::TRACK_SLOW:
        case Transition::TRACK_HALF:
        case Transition::TRACK:
            _restart(f);
            f->root_move = move;
            if (f->rel_angle < FACING_LIMIT) {
                f->facing = Fx::s16(f->target_dir);
                f->track_mode = TRACK_MODES[code - Transition::TRACK_SLOW];
            } else {
                f->facing = Fx::s16(f->aim_dir);
                f->track_mode = Tracking::AFTER_ACTIVE;
            }
            f->throw_state = 0;
            _clear_turn(f);
            return true;
        case Transition::TRACK_AIM:
            _restart(f);
            f->track_mode = Tracking::AIM;
            f->throw_state = 0;
            _clear_turn(f);
            f->root_move = move;
            f->facing = Fx::s16(f->aim_dir);
            return true;
        case Transition::SLIDE:
        case Transition::SLIDE_CONTINUE:
        case Transition::SLIDE_TURNED:
            if (code == Transition::SLIDE) {
                _restart(f);
            } else {
                _continue_frames(f);
            }
            f->track_mode = Tracking::TIMED;
            f->root_move = move;
            f->facing = Fx::s16(f->target_dir);
            f->throw_state = 0;
            f->slide_state = 1;
            f->slide_step_x = 0;
            f->slide_step_z = 0;
            if (code == Transition::SLIDE_TURNED) {
                f->heading = Fx::s16(f->heading - FightMath::HALF_TURN);
            }
            _timed_turn(f, f->target_dir);
            return true;
        case Transition::SWAP:
            return false;
        case Transition::CONTINUE_TRACK_SLOW:
        case Transition::CONTINUE_TRACK:
            _continue_frames(f);
            f->root_move = move;
            if (f->rel_angle < FACING_LIMIT) {
                f->track_mode = code == Transition::CONTINUE_TRACK_SLOW ? Tracking::TARGET_SLOW : Tracking::TARGET;
            } else {
                f->track_mode = Tracking::AFTER_ACTIVE;
            }
            f->throw_state = 0;
            _clear_turn(f);
            f->anchor_dirty = 1;
            _anchor(f);
            return false;
        case Transition::CONTINUE_EASE:
        case Transition::CONTINUE_EASE_TURNING:
            f->throw_state = 0;
            _continue_frames(f);
            f->track_mode = Tracking::EASE;
            if (code == Transition::CONTINUE_EASE) {
                _clear_turn(f);
            }
            f->anchor_dirty = 1;
            f->root_move = move;
            _anchor(f);
            return false;
        case Transition::APPROACH:
            _restart(f);
            f->root_move = move;
            f->facing = f->heading;
            if (reanchor != 0) {
                _anchor(f);
            }
            f->track_mode = Tracking::APPROACH;
            f->throw_state = 0;
            return false;
        case Transition::EASE:
            _restart(f);
            f->throw_state = 0;
            f->track_mode = Tracking::EASE;
            f->root_move = move;
            f->facing = Fx::s16(f->aim_dir);
            return true;
        case Transition::STEP:
        case Transition::STEP_CROUCH:
            f->pose_frame = Fx::s16(f->pose_frame + 1);
            f->root_frame = Fx::s16(f->root_frame + 1);
            _clamp_to_length(f);
            f->root_move = move;
            f->facing = f->heading;
            f->step_kind = code == Transition::STEP ? 1 : 2;
            return false;
        case Transition::SIDE_STEP:
            _restart(f);
            f->track_mode = Tracking::SIDE_STEP;
            f->throw_state = 0;
            _clear_turn(f);
            f->facing = f->heading;
            f->root_move = move;
            if (reanchor != 0) {
                _anchor(f);
            }
            f->step_cooldown = SIDE_STEP_COOLDOWN;
            return false;
        case Transition::SIDE_STEP_CONTINUE:
            _continue_frames(f);
            f->track_mode = Tracking::SIDE_STEP;
            f->throw_state = 0;
            _clear_turn(f);
            f->facing = f->heading;
            f->root_move = move;
            return true;
        case Transition::LAUNCH:
        case Transition::HIT_AIR:
            _start_airborne(f, code == Transition::LAUNCH);
            return true;
        case Transition::THROW:
            _start_throw(f, opp);
            return false;
        case Transition::THROWN:
            _start_thrown(f, opp);
            return false;
        case Transition::HIT_GUARD:
        case Transition::HIT_COUNTER: {
            if (code == Transition::HIT_COUNTER) {
                _recover_mash(f);
            }
            gd::Int push = code == Transition::HIT_GUARD ? Reaction::PUSH_GUARD : Reaction::PUSH_COUNTER;
            _start_pushed(f, push, f->target_dir, Reaction::PUSH_ANGLE);
            f->track_mode = Tracking::EASE_BY_POSE;
            f->facing = Fx::s16(f->aim_dir);
            return true;
        }
        case Transition::HIT_DOWN:
            _recover_mash(f);
            _start_pushed(f, Reaction::PUSH_DOWN, f->target_dir, Reaction::PUSH_ANGLE_DOWN);
            f->recover_mash = 0;
            f->track_mode = Tracking::TIMED;
            f->facing = f->heading;
            return true;
        case Transition::HIT_SIDE_1:
        case Transition::HIT_SIDE_3:
        case Transition::HIT_OTHER:
        case Transition::HIT_FRONT:
        case Transition::HIT_BACK: {
            _recover_mash(f);
            gd::Int base = code == Transition::HIT_FRONT ? f->aim_dir : f->target_dir;
            _start_pushed(f, Reaction::PUSH_HIT, base, Reaction::PUSH_ANGLE);
            _push_repeat_count(f);
            f->track_mode = Tracking::TIMED;
            f->facing = f->heading;
            return true;
        }
        default:
            break;
    }
    _restart(f);
    f->root_move = move;
    f->heading = Fx::s16(f->target_dir);
    f->facing = f->heading;
    return true;
}

// Transitions HOLD_TURN and HOLD_TRACK: the pose restarts and holds for the branch window, the
// root continues on the old root move.
void MoveSystem::_hold(const gd::Ref<FighterState>& f) {
    f->pose_frame = 1;
    _keep_root_move(f);
    f->hold_frames = f->branch_window;
    f->facing = f->heading;
}

// Transitions LAUNCH and HIT_AIR: the ballistic flight from the entry frame (combat.md#launches-
// and-juggles); a launch goes into air kind 5, a juggle keeps the velocity of earlier hits.
void MoveSystem::_start_airborne(const gd::Ref<FighterState>& f, bool launch) {
    if (launch) {
        f->air_kind = LAUNCH_AIR_KIND;
        f->launch_armed = 1;
        f->pos_y = f->root_y;
        f->move_slot = t->air_kinds[LAUNCH_AIR_KIND][1];
        f->ground_offset = t->air_kinds[LAUNCH_AIR_KIND][0];
    } else {
        f->pos_y = f->root_y;
        if (f->juggle_count == 0) {
            f->air_vel_x = 0;
            f->air_vel_y = 0;
            f->air_vel_z = 0;
        }
    }
    _launch_trajectory(f);
    f->pose_frame = f->entry_frame;
    f->root_frame = f->entry_frame;
    f->air_phase = 1;
    f->ballistic = 1;
    f->throw_state = 0;
    f->root_move = f->move_row;
    f->juggle_count = Fx::s16(f->juggle_count + 1);
    _recover_mash(f);
    f->track_mode = Tracking::TIMED;
    if ((f->pose_move->flags & MoveFlag::LAUNCH_ANGLE) != 0) {
        f->facing = Fx::s16(f->aim_dir + f->reaction[Reaction::PUSH_ANGLE]);
    } else {
        f->facing = Fx::s16(f->aim_dir);
    }
    _clear_turn(f);
}

// Transition THROW: the thrower aligns with its victim (combat.md#throws).
void MoveSystem::_start_throw(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    _restart(f);
    f->root_move = f->move_row;
    if (f->throw_state == 0) {
        gd::Int target = f->target_dir;
        switch (f->opp_quadrant) {
            case 1:
                f->heading = Fx::s16(target - FightMath::QUARTER_TURN);
                break;
            case 2:
                f->heading = Fx::s16(target);
                break;
            case 3:
                f->heading = Fx::s16(target + FightMath::QUARTER_TURN);
                break;
            default:
                f->heading = Fx::s16(target - FightMath::HALF_TURN);
                break;
        }
        f->turn_frames = THROW_TURN_FRAMES;
        f->facing = f->heading;
        f->turn_step = Fx::s16(Fx::div_trunc(Fx::s16(opp->heading - f->heading), THROW_TURN_FRAMES));
        f->pos_x = opp->root_x;
        f->pos_z = opp->root_z;
    } else if (f->throw_state < 0 || (f->move_row->flags & MoveFlag::PARTNER) != 0) {
        f->heading = opp->facing;
        f->facing = opp->facing;
        f->pos_x = opp->root_x;
        f->pos_z = opp->root_z;
    }
    _stop_push(f);
    f->throw_state = f->throw_state < 0 ? 1 : Fx::s16(f->throw_state + 1);
}

// Transition THROWN: the victim turns to the thrower; the time spent down afterwards comes from the
// thrower's move (combat.md#knock-down-recovery).
void MoveSystem::_start_thrown(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Ref<MoveRow> move = f->move_row;
    if (f->throw_state >= 0) {
        gd::Ref<MoveRow> pose = f->pose_move;
        bool counter = !((Fx::s16(pose->damage_word) == 0 || pose->active_first <= f->pose_frame) && opp->power_timer == 0);
        f->attack_pending = counter ? 1 : 0;
    }
    _restart(f);
    f->hit_done[3] = 1;
    f->root_move = move;
    if (f->throw_state < 0) {
        if ((move->flags & MoveFlag::PARTNER) != 0) {
            f->facing = f->heading;
            _anchor(f);
        }
    } else {
        f->heading = Fx::s16(f->heading - FightMath::HALF_TURN);
        f->facing = f->heading;
        _anchor(f);
        FighterSounds::script_stop(f);
    }
    f->throw_state = f->throw_state < 1 ? Fx::s16(f->throw_state - 1) : -1;
    gd::Ref<MoveRow> other = opp->move_row;
    if (other->hold_frame == 0) {
        gd::Int diff = other->length - move->length;
        f->recover_frames = diff < 1 ? other->length : move->length + diff;
        f->recover_frames = gd::clampi(f->recover_frames, 0, MAX_THROWN_RECOVERY);
    } else {
        f->recover_frames = other->hold_frame + move->length;
    }
    gd::Int mash = Fx::s16(Fx::div_trunc(Fx::s16(other->damage_word) * 0x20, 0x2D) + 0x22);
    f->recover_mash = gd::clampi(mash, MIN_THROWN_MASH, MAX_MASH);
    _stop_push(f);
}

// The hit and guard reactions: restart pushed back along `base` + the record's angle + 180°
// (combat.md#push-back).
void MoveSystem::_start_pushed(const gd::Ref<FighterState>& f, gd::Int push_field, gd::Int base, gd::Int angle_field) {
    _load_push(f, f->reaction[push_field]);
    f->push_dir = Fx::s16(base + f->reaction[angle_field] - FightMath::HALF_TURN);
    _restart(f);
    f->throw_state = 0;
    f->root_move = f->move_row;
    _clear_turn(f);
}

void MoveSystem::_stop_push(const gd::Ref<FighterState>& f) {
    f->push_frames = 0;
    f->push_speed = 0;
    f->push_table_frames = 0;
}

// PushRepeatCount (0x8003159C).
void MoveSystem::_push_repeat_count(const gd::Ref<FighterState>& f) {
    gd::Int code = f->transition & 0xFFFF;
    if (code == Transition::HIT_SIDE_1 || code == Transition::HIT_SIDE_3 || code == Transition::HIT_BACK) {
        if (f->push_repeat_timer == 0) {
            f->push_repeat = 0;
        } else if ((f->push_repeat_trans & 0xFFFF) == code) {
            f->push_repeat = Fx::s16(f->push_repeat + 1);
        }
        f->push_repeat_trans = f->transition;
        f->push_repeat_timer = f->move_row->length + 10;
    }
}

void MoveSystem::_finish(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> move = f->move_row;
    f->move_row = nullptr;
    f->pose_move = move;
    f->cur_slot = f->move_slot;
    f->move_slot = -1;
    gd::Int segs = 0;
    if (move->active_first != 0) {
        gd::PBytes d = _descriptor(move);
        segs = d[2] != 0 ? 2 : (d[0] != 0 ? 1 : 0);
    }
    f->attack_seg_count = segs;
    f->move_frame = f->move_changed != 0 ? 1 : f->pose_frame;
    f->cur_transition = f->transition;
    f->transition = 0;
    f->trans_bit7 = 0;
    f->trans_bit6 = 0;
    f->apply_end_turn = 0;
    f->in_throw = f->throw_state != 0 ? 1 : 0;
    if (f->bank_type == BankType::GON && f->pose_frame == 1 && f->pose_move->air_first != 1) {
        f->pos_y = 0;
    }
}

// The first bytes of a move's attack descriptor (joints a0, b0, a1, b1, then two layout bytes).
gd::PBytes MoveSystem::_descriptor(const gd::Ref<MoveRow>& move) {
    return AttackRecords::header(move, t);
}

// LaunchTrajectory (0x8002F2C8): the launch of a hit or launching reaction (combat.md).
void MoveSystem::_launch_trajectory(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> move = f->move_row;
    gd::Int hold = move->hold_frame != 0 ? move->hold_frame : f->pose_move->length;
    gd::Int y = f->root_y;
    gd::Int ground = Fx::s16(f->ground_offset);
    gd::Int dmg = Fx::s16(f->last_damage);
    gd::Int jug = Fx::s16(f->juggle_count);
    gd::Int speed = dmg + (jug + 1) * 7 + 10 + (f->air_kind == 5 ? 10 : 0);
    f->air_speed = Fx::s16(gd::clampi(speed, 0, 100));
    gd::Int v = Fx::w32(dmg * 4 - (Fx::w32(f->vel_y + f->hit_dir_y) >> 3) - (jug * 7 - 40));
    gd::Int vc = gd::mini(v, 100);
    gd::Int sq = vc * vc;
    if (v < -100) {
        vc = -100;
        sq = 10000;
    }
    f->air_vel_y = Fx::s16(-vc);
    gd::Int sum = vc + CameraMath::isqrt(gd::absi(Fx::w32(sq + Fx::w32(ground + y) * 12)));
    gd::Int time = 0;
    if (fight->rules->fix_juggle_arc_overflow && sum < 0) {
        time = 0;
    } else {
        // The game divides the (possibly negative) sum as an unsigned int and keeps computing
        // with 32-bit wrap-around; high bounces depend on it (game bug #7).
        time = (sum & 0xFFFFFFFFLL) / 6;
    }
    gd::Int e = Fx::w32(-time);
    if (time == 0) {
        time = 1;
        e = -1;
    }
    e = Fx::w32(hold + e + 1);
    if (e < 1) {
        e = 1;
    }
    gd::Int entry = 0;
    if ((f->state & StateBit::DOWN) == 0 && e != 1) {
        time = (time - 1 + e) & 0xFFFFFFFFLL;
        entry = 1;
    } else if (jug == 0) {
        time = (time + 10) & 0xFFFFFFFFLL;
        entry = Fx::w32(hold - time + 1);
        if (entry < 1) {
            entry = 1;
        }
    } else {
        entry = e;
        bool same_anim = f->pose_move->anim_bank == move->anim_bank && f->pose_move->anim == move->anim;
        if (same_anim && gd::absi(Fx::s16(f->pose_frame) - e) < 10) {
            entry = Fx::s16(f->pose_frame) - 10;
            gd::Int back = -entry;
            if (entry < 1) {
                entry = 1;
                back = -1;
            }
            time = (time + e + back) & 0xFFFFFFFFLL;
        }
    }
    gd::Int n = Fx::w32(time + 1);
    gd::Int fall = Fx::w32(n * n * 6) >> 1;
    if (Fx::w32(Fx::w32(vc * n - fall) - y) < ground) {
        f->air_vel_y = Fx::s16(Fx::div_trunc(-Fx::w32(ground + y + fall), n));
    }
    f->push_table = t->launch_push;
    f->push_table_pos = 0;
    f->push_table_frames = 8;
    f->entry_frame = Fx::s16(entry);
    f->push_dir = Fx::s16(f->target_dir - 0x8000);
    if (f->air_kind == 1) {
        f->air_vel_y = 400;
        f->entry_frame = 6;
        f->push_frames = gd::maxi(move->length - 6, 0);
        f->push_speed = 0x28;
    }
    fight->air_frames[f->index] = n;
}

// MoveStepAll / MoveStep

// MoveStepAll (0x80045450): the fighters in the order of the frame counter's parity.
void MoveSystem::step_all() {
    if ((gd::Int(fight->frame_counter) & 1) == 0) {
        step(fight->fighters[1]);
        step(fight->fighters[0]);
    } else {
        step(fight->fighters[0]);
        step(fight->fighters[1]);
    }
    // The third record only while the level's enemies come (0x800B70E8 clear); Ghidra's
    // decompile drops that test.
    if (fight->mode == GameMode::FORCE && !fight->force->level_ended()) {
        step(fight->fighters[2]);
    }
}

// MoveStep (0x800454E0).
void MoveSystem::step(const gd::Ref<FighterState>& f) {
    gd::Ref<FighterState> opp = fight->opponent(f);
    relative_angles(f, opp);
    gd::Int a = f->index;
    gd::Int b = opp->index;
    if (a > 1) {
        a = gd::shl(1, a - 1);
    }
    if (b > 1) {
        b = gd::shl(1, b - 1);
    }
    f->dist = gd::Int(fight->pair_distance[a + b]) & 0xFFFFFFFFLL;
    gd::Int big = 0;
    if (f->char_id == Character::KUMA || f->char_id == Character::TRUE_OGRE) {
        big = 1;
    }
    if (opp->char_id == Character::KUMA || opp->char_id == Character::TRUE_OGRE) {
        big += 1;
    }
    f->dist_adj = (f->dist - big * 5 * 8) & 0xFFFFFFFFLL;
    if (f->got_hit != 0) {
        gd::Int kind = opp->attack_class;
        bool react = kind == 0 || (kind == 2 && f->in_air == 0 && (f->state & StateBit::DOWN) == 0);
        if (react) {
            _hit_select_reaction(f, opp);
            f->branch_kind = 1;
            if (f->throw_state > 0) {
                f->attack_class = 1;
            }
        }
    }
    if (fight->mode == GameMode::BALL && fight->ball->point_loser() == f->index + 1) {
        // Tekken Ball: the ball landing on this side knocks the fighter off its feet.
        f->in_air = 1;
        f->guarded = 0;
        f->hit_clean = 1;
        relative_angles(f, fight->fighters[TekkenBall::THIRD]);
        _hit_select_reaction(f, opp);
        f->attack_class = 1;
    }
    if (f->move_row == nullptr) {
        branch_step(f, opp);
    }
    if (f->health == 0 && f->hit_clean != 0) {
        gd::Ref<MoveRow> next = fight->move_for_slot(f, f->move_slot);
        if (fight->collapse_on_ko == 0) {
            if (next->stance_slot == STAND_SLOT || next->stance_slot == CROUCH_SLOT) {
                f->in_air = 1;
                f->guarded = 0;
                f->hit_clean = 1;
                f->guard = 0;
                _hit_select_reaction(f, opp);
                f->attack_class = 1;
            }
        } else if (next->stance_slot == STAND_SLOT || next->stance_slot == CROUCH_SLOT
                   || ((next->state & StateBit::DOWN) == 0 && next->air_first == 0)) {
            _collapse(f, opp);
            f->attack_class = 1;
        }
    }
    if (f->transition == Transition::THROW) {
        f->throw_partner = opp->index;
        opp->throw_partner = f->index;
        opp->in_throw = 1;
        f->in_throw = 1;
    }
}

// HitSelectReaction (0x8002EDF8): the defender's reaction move from its reaction record.
void MoveSystem::_hit_select_reaction(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& attacker) {
    gd::PI32 r = f->reaction;
    gd::Int crouch = (f->state & StateBit::STANDING) != 0 ? 0 : 1;     // the crouching slot follows the standing one
    gd::Int code = 0;
    if (f->guarded != 0) {
        code = Transition::HIT_GUARD;
        f->move_slot = r[Reaction::GUARD + crouch];
    } else if (f->in_air != 0 || (f->pose_move->state & StateBit::AIRBORNE) != 0) {
        code = Transition::HIT_AIR;
        _air_reaction_kind(f, attacker);
    } else if ((f->state & StateBit::DOWN_REVERSED) != 0) {
        code = Transition::HIT_DOWN;
        f->move_slot = r[Reaction::DOWN_OTHER];
    } else if ((f->state & StateBit::DOWN) != 0) {
        code = Transition::HIT_DOWN;
        f->move_slot = r[Reaction::DOWN];
    } else if (f->counter_hit != 0) {
        code = Transition::HIT_COUNTER;
        f->move_slot = r[Reaction::COUNTER + crouch];
    } else {
        switch (f->facing_quadrant) {
            case 0:
                code = Transition::HIT_FRONT;
                f->move_slot = r[Reaction::FRONT + crouch];
                break;
            case 1:
                code = Transition::HIT_SIDE_1;
                f->move_slot = r[Reaction::SIDE_1 + crouch];
                break;
            case 2:
                code = Transition::HIT_BACK;
                f->move_slot = r[Reaction::BACK + crouch];
                break;
            case 3:
                code = Transition::HIT_SIDE_3;
                f->move_slot = r[Reaction::SIDE_3 + crouch];
                break;
            default:
                break;
        }
    }
    if (code != 0) {
        // TransitionRemap works on the pending move, which is still the previous one here.
        f->transition = f->move_row != nullptr ? FightMath::transition_remap(code, f->move_row, f->entry_frame) : code;
    }
    f->trans_bit7 = 0;
    f->trans_bit6 = 0;
    f->entry_frame = f->pose_frame;
    f->move_row = fight->move_for_slot(f, f->move_slot);
}

// AirReactionKind (0x8002F0E8): the air kind (ground offset and air move) of a juggle hit.
void MoveSystem::_air_reaction_kind(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& attacker) {
    bool fresh = f->cur_slot != 0xD62;
    if (f->cur_slot == 0xD63 && f->pose_frame > 0x12) {
        fresh = false;
    }
    if (f->cur_slot == 0xD64 && f->pose_frame > 0x12) {
        fresh = false;
    }
    if (!fresh) {
        if (f->cur_slot != 0xD62) {
            gd::Int height = Fx::s16(f->root_y);
            if (f->root_y < 0) {
                if (0x3FF < -height) {
                    f->air_kind = 0;
                    _air_kind_rows(f);
                    return;
                }
            } else if (0x3FF < height) {
                f->air_kind = 0;
                _air_kind_rows(f);
                return;
            }
        }
        f->air_kind = 2;
        _air_kind_rows(f);
        return;
    }
    gd::Int special = 0;
    if (attacker->bank_type == BankType::JIN) {
        special = attacker->cur_slot == 0x241 ? 1 : 0;
    }
    if (attacker->bank_type == BankType::GUN_JACK && attacker->cur_slot == 0x14A) {
        special = 1;
        if (attacker->move_row != nullptr && (attacker->move_slot & 0xFFFF) - 0x848 < 2) {
            special = 2;
        }
    }
    if (special == 1 || (special != 2 && f->hit_dir_y > 0x6D)) {
        f->air_kind = 1;
    } else if (f->cur_slot != 0xD58 && f->facing_quadrant == 1) {
        f->air_kind = 4;
    } else if (f->cur_slot != 0xD58 && f->facing_quadrant == 3) {
        f->air_kind = 3;
    } else {
        f->air_kind = 0;
    }
    _air_kind_rows(f);
}

void MoveSystem::_air_kind_rows(const gd::Ref<FighterState>& f) {
    gd::PI32 row = t->air_kinds[f->air_kind];
    f->move_slot = row[1];
    f->ground_offset = row[0];
}

// FighterCollapse (0x8002DA80): a KO'd fighter falls with the collapse move 0xD66.
void MoveSystem::_collapse(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    f->move_slot = COLLAPSE_SLOT;
    f->move_row = fight->move_for_slot(f, COLLAPSE_SLOT);
    f->pose_frame = 1;
    f->entry_frame = 1;
    f->transition = FightMath::transition_remap(Transition::RESTART, f->move_row, f->entry_frame);
    f->trans_bit7 = 0;
    f->trans_bit6 = 0;
    f->guarded = 0;
    f->hit_clean = 1;
    f->guard = 0;
    start_or_advance(f, opp);
}

// branches

// MoveBranchStep (0x8002DC3C).
void MoveSystem::branch_step(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    f->branch_window = -1;
    if (f->move_row != nullptr || f->step_cooldown > 0x19) {
        return;
    }
    gd::Ref<Cursor> found = _branch_find(f, opp, gd::make<Cursor>(f->pose_move->bank->branches, f->pose_move->branches),
                                         f->pose_frame);
    gd::PI32 row = found->row();
    if (row[ROW_COMMAND] != END) {
        f->move_slot = Fx::s16(row[ROW_TARGET]);
        f->move_row = fight->move_for_slot(f, f->move_slot);
        f->branch_window = Fx::s16(row[ROW_LAST] - f->pose_frame);
        gd::Int flags = row[ROW_FLAGS];
        f->transition = _remap(f, flags & 0x3F);
        f->trans_bit7 = flags >> 7;
        f->trans_bit6 = (flags >> 6) & 1;
        f->entry_frame = f->frame_step < 0 ? gd::Int(row[ROW_LAST]) : gd::Int(row[ROW_ENTRY]);
        if ((f->entry_frame & 0xFFFF) == 0xFF) {
            f->entry_frame = f->pose_move->length;
        }
        f->branch_kind = 2;
        return;
    }
    gd::Int first = row[ROW_FIRST];
    gd::Int last = row[ROW_LAST];
    bool look = false;
    if (f->pose_frame >= first - 5 && f->pose_frame < first) {
        look = f->frame_step >= 1 && (gd::Int(f->in_history[f->in_hist_index]) & FighterInput::HIST_BUTTONS) != 0;
    }
    if (!look && first <= f->pose_frame && f->pose_frame <= last && f->frame_step > 0) {
        look = true;
    }
    if (!look && fight->mode == GameMode::BALL) {
        // Tekken Ball: some moves look ahead from a tuned frame on (FUN_800B0B54).
        gd::Int from = fight->ball->drift(f->player_index)[4];
        look = from != 0 && f->pose_frame >= from && f->frame_step >= 1;
    }
    if (look) {
        gd::Ref<MoveRow> target = fight->move_for_slot(f, row[ROW_TARGET]);
        gd::Int heading = f->heading;
        f->heading = Fx::s16(heading + f->pose_move->end_turn);
        relative_angles(f, opp);
        gd::Ref<Cursor> ahead = _branch_find(f, opp, gd::make<Cursor>(target->bank->branches, target->branches), 1);
        f->heading = heading;
        relative_angles(f, opp);
        gd::PI32 hit = ahead->row();
        if (hit[ROW_COMMAND] != END) {
            f->move_slot = Fx::s16(hit[ROW_TARGET]);
            f->move_row = fight->move_for_slot(f, f->move_slot);
            f->entry_frame = first;
            f->trans_bit6 = (row[ROW_FLAGS] >> 6) & 1;
            f->branch_window = Fx::s16(hit[ROW_LAST] - f->pose_frame);
            gd::Int flags = hit[ROW_FLAGS];
            f->transition = _remap(f, flags & 0x3F);
            f->apply_end_turn = 1;
            f->branch_kind = 3;
            f->trans_bit7 = flags >> 7;
            return;
        }
    }
    gd::Int frame = f->pose_frame + f->frame_step;
    if (f->pose_move->length < frame || frame < 1) {
        f->entry_frame = f->pose_frame;
        f->move_slot = Fx::s16(row[ROW_TARGET]);
        f->move_row = fight->move_for_slot(f, f->move_slot);
        gd::Int flags = row[ROW_FLAGS];
        f->transition = _remap(f, flags & 0x3F);
        f->trans_bit7 = flags >> 7;
        f->apply_end_turn = 1;
        f->branch_kind = 4;
        f->trans_bit6 = (flags >> 6) & 1;
    }
}

gd::Int MoveSystem::_remap(const gd::Ref<FighterState>& f, gd::Int code) {
    return FightMath::transition_remap(code, f->move_row, f->entry_frame);
}

// BranchFind (0x8002E078): the first matching row of a list, else its terminator.
gd::Ref<MoveSystem::Cursor> MoveSystem::_branch_find(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                                                     const gd::Ref<Cursor>& start, gd::Int frame) {
    gd::Int direction = f->in_dir;
    gd::Int pressed = f->in_pressed;
    fight->situation[f->index] = 0;
    gd::Arr<gd::PI32> list = start->rows;
    gd::Int i = start->i;
    gd::Arr<gd::PI32> ret_list = list;
    gd::Int ret = 0;
    while (true) {
        gd::Arr<gd::PI32> match_list = list;
        gd::Int match_i = -1;
        while (true) {
            gd::Int cmd = list[i][ROW_COMMAND];
            if (cmd == END) {
                match_list = list;
                match_i = i;
                break;
            }
            if (cmd == RETURN) {
                list = ret_list;
                i = ret + 1;
                continue;
            }
            gd::Arr<gd::PI32> row_list = list;
            gd::Int row_i = i;
            if (cmd == CALL) {
                ret_list = list;
                ret = i;
                row_list = t->common_branches;
                row_i = list[i][ROW_TARGET];
            }
            gd::PI32 row = row_list[row_i];
            if (_input_match(f, row, direction, pressed) && row[ROW_FIRST] <= frame && frame <= row[ROW_LAST]
                    && _restriction_ok(f, opp, row[ROW_RESTRICTION]) && row[ROW_CONDITION] < BranchCondition::REVERSAL_HIT
                    && (row[ROW_CONDITION] == 0 || condition(row, f, opp))) {
                match_list = row_list;
                match_i = row_i;
                break;
            }
            list = row_list;
            i = row_i + 1;
        }
        gd::Int flag = match_list[match_i][ROW_FLAGS];
        if (flag == Transition::TO_TERMINATOR) {
            while (match_list[match_i][ROW_COMMAND] != END) {
                match_i += 1;
            }
            flag = match_list[match_i][ROW_FLAGS];
        }
        bool at_end = match_list[match_i][ROW_COMMAND] == END;
        bool skip_step = flag == Transition::SIDE_STEP && !at_end && f->step_cooldown != 0;
        bool skip_throw = fight->mode == GameMode::FORCE && match_list[match_i][ROW_FLAGS] == Transition::THROW && !at_end
            && f->in_throw == 0 && opp->in_throw != 0;
        if (!(skip_step || skip_throw)) {
            return gd::make<Cursor>(match_list, match_i);
        }
        list = match_list;
        i = match_i + 1;
    }
    return nullptr;
}

// practice.ovl FUN_800B3520 (the FREEZE SIGNAL): a branch of the running move could be taken
// now (input aside), or, near the move's end, a branch of its default continuation.
bool MoveSystem::can_act(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Ref<Cursor> found = _can_act_find(f, opp, gd::make<Cursor>(f->pose_move->bank->branches, f->pose_move->branches), false);
    gd::PI32 row = found->row();
    if (row[ROW_COMMAND] != END) {
        return true;
    }
    gd::Int first = row[ROW_FIRST];
    gd::Int frame = f->pose_frame;
    if (((first - 5 <= frame && frame < first) || (frame >= first && frame <= row[ROW_LAST])) && f->frame_step > 0) {
        gd::Ref<MoveRow> target = fight->move_for_slot(f, row[ROW_TARGET]);
        gd::Ref<Cursor> ahead = _can_act_find(f, opp, gd::make<Cursor>(target->bank->branches, target->branches), true);
        return ahead->row()[ROW_COMMAND] != END;
    }
    return false;
}

// The branch search of FUN_800B3520: window, restriction and condition (no input); for the
// continuation (`ahead`) rows open from the first frame.
gd::Ref<MoveSystem::Cursor> MoveSystem::_can_act_find(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                                                      const gd::Ref<Cursor>& start, bool ahead) {
    gd::Arr<gd::PI32> list = start->rows;
    gd::Int i = start->i;
    gd::Arr<gd::PI32> ret_list = list;
    gd::Int ret = 0;
    while (true) {
        gd::Int cmd = list[i][ROW_COMMAND];
        if (cmd == END) {
            return gd::make<Cursor>(list, i);
        }
        if (cmd == RETURN) {
            list = ret_list;
            i = ret + 1;
            continue;
        }
        gd::Arr<gd::PI32> row_list = list;
        gd::Int row_i = i;
        if (cmd == CALL) {
            ret_list = list;
            ret = i;
            row_list = t->common_branches;
            row_i = list[i][ROW_TARGET];
        }
        gd::PI32 row = row_list[row_i];
        bool in_window = ahead ? (row[ROW_FIRST] < 2 && row[ROW_LAST] != 0)
                               : (row[ROW_FIRST] <= f->pose_frame && f->pose_frame <= row[ROW_LAST]);
        gd::Int c = row[ROW_CONDITION];
        if (in_window && _restriction_ok(f, opp, row[ROW_RESTRICTION]) && c < 0x45
                && !(row[ROW_COMMAND] == 0x10 && c == 0 && row[ROW_FLAGS] == 2)
                && ((!ahead && c - 0x3A >= 0 && c - 0x3A < 3) || c == 0 || condition(row, f, opp))) {
            return gd::make<Cursor>(row_list, row_i);
        }
        list = row_list;
        i = row_i + 1;
    }
    return nullptr;
}

bool MoveSystem::_restriction_ok(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int value) {
    if (value == 0) {
        return true;
    }
    if (value < 0x18) {
        return f->bank_type == value - 1;
    }
    if (value < 0x2F) {
        return value - 0x18 != f->bank_type;
    }
    if (value < 0x46) {
        return opp->bank_type == value - 0x2F;
    }
    if (value < 0x5D) {
        return f->char_id == value - 0x46;
    }
    return value - 0x5D != f->char_id;
}

// InputMatch (0x8002CE7C).
bool MoveSystem::_input_match(const gd::Ref<FighterState>& f, const gd::PI32& row, gd::Int direction, gd::Int pressed) {
    gd::Int cmd = row[ROW_COMMAND];
    if (cmd < 0xC000) {
        if ((cmd & direction) == 0 && (cmd & 0x3FE0) != 0) {
            return false;
        }
        if ((cmd & 0x1F) == 0x10) {
            return true;
        }
        if ((cmd & 0x10) != 0) {
            return (cmd & 0xF & pressed) != 0;
        }
        gd::Int mask = cmd & 0xF;
        if (mask != 0 || pressed != 0) {
            if ((mask & pressed) == 0) {
                return false;
            }
            return mask == (f->in_held & mask);
        }
        return true;
    }
    if (cmd < 0xC00E) {
        if (cmd == 0xC001) {
            return _double_tap(f, 6);
        }
        if (cmd == 0xC002) {
            return _double_tap(f, 4);
        }
        return false;
    }
    if (cmd < 0xC7FF) {
        if (cmd - 0xC00E < 0x3F) {
            return _sequence_match(f, t->sequences_a[cmd - 0xC00E]);
        }
        return false;
    }
    if (cmd - 0xC7FF < 0x29) {
        return _sequence_match(f, t->sequences_b[cmd - 0xC7FF]);
    }
    return false;
}

bool MoveSystem::_buttons_match(gd::Int spec, gd::Int pressed, gd::Int held) {
    if ((spec & 0x1F) == 0x10) {
        return true;
    }
    if ((spec & 0x10) != 0) {
        return (pressed & spec & 0xF) != 0;
    }
    gd::Int mask = spec & 0xF;
    if (mask == 0 && pressed == 0) {
        return true;
    }
    return (pressed & mask) != 0 && mask == (held & mask);
}

gd::Int MoveSystem::_prev(gd::Int i) {
    return i > 0 ? i - 1 : HISTORY - 1;
}

// FUN_8002CB08 / FUN_8002CBF0: forward (6) or back (4) double tap within 20 frames.
bool MoveSystem::_double_tap(const gd::Ref<FighterState>& f, gd::Int direction) {
    gd::PI32 h = f->in_history;
    gd::Int i = f->in_hist_index;
    if ((gd::Int(h[i]) & 0xF) != direction) {
        return false;
    }
    gd::Int j = _prev(i);
    if ((gd::Int(h[j]) & 0xF) != 5) {
        return false;
    }
    gd::Int budget = 0x13;
    gd::Int k = j;
    while (true) {
        k = _prev(j);
        gd::Int d = gd::Int(h[k]) & 0xF;
        if (d != 5) {
            if (d != direction) {
                return false;
            }
            break;
        }
        budget -= 1;
        j = k;
        if (budget <= 0) {
            break;
        }
    }
    if (budget <= 0) {
        return false;
    }
    while (true) {
        gd::Int m = _prev(k);
        if ((gd::Int(h[m]) & 0xF) != direction) {
            break;
        }
        budget -= 1;
        k = m;
        if (budget <= 0) {
            break;
        }
    }
    return budget > 0;
}

// InputSequenceMatch (0x8002CCD8).
bool MoveSystem::_sequence_match(const gd::Ref<FighterState>& f, const gd::PI32& seq) {
    gd::Int window = seq[0];
    gd::Int k = seq.size() - 2;          // index of the last step (steps start at 1)
    gd::Int last = k >= 0 ? gd::Int(seq[k + 1]) : 0;
    gd::Int idx = f->in_hist_index & 0xFFFFFFFFLL;
    gd::Int want = 0;
    gd::Int pos = 0;
    if ((last & 0xF) == last) {
        pos = idx < 0x3B ? idx + 1 : 0;
        want = last;
    } else {
        if ((last & FighterInput::HIST_DIRECTION) != (gd::Int(f->in_history[idx]) & FighterInput::HIST_DIRECTION)) {
            return false;
        }
        if (!_buttons_match(last >> 8, f->in_pressed, f->in_held)) {
            return false;
        }
        k -= 1;
        if (k < 0) {
            return true;
        }
        want = seq[k + 1];
        pos = idx;
    }
    while (true) {
        if (window < 1) {
            return false;
        }
        pos = pos > 0 ? pos - 1 : 0x3B;
        gd::Int entry = f->in_history[pos];
        bool hit = false;
        if ((want & FighterInput::HIST_DIRECTION) == (entry & FighterInput::HIST_DIRECTION)) {
            gd::Int spec = want >> 8;
            if ((spec & 0x1F) == 0x10) {
                hit = true;
            } else if ((spec & 0x10) != 0) {
                hit = (spec & (entry >> 4)) != 0;
            } else {
                hit = (spec & 0xF) == (entry >> 4);
            }
        }
        if (!hit) {
            if ((entry >> 4) != 0) {
                return false;
            }
        } else {
            k -= 1;
            if (k < 0) {
                return true;
            }
            want = seq[k + 1];
        }
        window -= 1;
    }
    return false;
}

// SituationClassify (0x800313B0): the throw situation code, cached per player.
void MoveSystem::_situation_classify(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Int code = -1;
    if (f->contact != 0 && opp->in_air == 0 && opp->about_to_hit == 0) {
        gd::Int q = f->opp_quadrant;
        if (f->rel_angle < 0x2AAA) {
            gd::Int s = opp->state;
            gd::PI32 table;
            if ((s & 0x80) != 0) {
                table = (s & 0x200) == 0 ? gd::PI32{10, 12, 11, 13} : gd::PI32{14, 16, 15, 17};
            } else if ((s & 0x40) != 0) {
                table = gd::PI32{2, 4, 3, 5};
            } else if ((s & 0x20) != 0) {
                table = gd::PI32{6, 8, 7, 9};
            }
            if (!table.is_empty() && q >= 0 && q <= 3) {
                code = table[q];
            }
        } else if (f->rel_angle > 0x5555 && (opp->state & StateBit::STANDING) != 0) {
            code = q == 0 ? 0x12 : (q == 2 ? 0x13 : -1);
        }
    }
    fight->situation[f->index] = code;
}

// BranchCondition (0x8002E310), types 0x00 to 0x44.
bool MoveSystem::condition(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Int kind = row[ROW_CONDITION];
    gd::Int param = row[ROW_PARAMETER];
    if (kind == BranchCondition::ALWAYS) {
        return true;
    }
    if (kind == BranchCondition::CONTACT) {
        return f->contact != 0;
    }
    if (kind >= BranchCondition::SITUATION_FIRST && kind <= BranchCondition::SITUATION_5_9) {
        if (fight->situation[f->index] == 0) {
            _situation_classify(f, opp);
        }
        gd::Int sit = fight->situation[f->index];
        if (kind <= BranchCondition::SITUATION_LAST) {
            return f->dist_adj <= param && kind == sit;
        }
        if (param < f->dist_adj) {
            return false;
        }
        switch (kind) {
            case BranchCondition::SITUATION_2_6: return sit == 2 || sit == 6;
            case BranchCondition::SITUATION_3_7: return sit == 3 || sit == 7;
            case BranchCondition::SITUATION_4_8: return sit == 4 || sit == 8;
            default: return sit == 5 || sit == 9;
        }
    }
    bool opp_down = (opp->state & StateBit::DOWN) != 0;
    bool turned_away = f->rel_angle > FightMath::QUARTER_TURN;
    switch (kind) {
        case BranchCondition::NEAR: return f->dist <= param;
        case BranchCondition::FAR: return f->dist >= param;
        case BranchCondition::HIT: return f->contact != 0 && (f->attack & opp->guard) == 0;
        case BranchCondition::OPP_GUARDED: return opp->guarded != 0;
        case BranchCondition::WHIFFED: return f->whiffed != 0;
        case BranchCondition::OPP_NOT_THROWING: return opp->transition != Transition::THROW;
        case BranchCondition::OPP_ATTACKING: return opp->damage != 0;
        case BranchCondition::OPP_ATTACKING_STANDING: return opp->damage != 0 && (opp->state & StateBit::POSTURE) == StateBit::STANDING;
        case BranchCondition::OPP_ATTACKING_CROUCHING: return opp->damage != 0 && (opp->state & StateBit::POSTURE) == StateBit::CROUCHING;
        case BranchCondition::OPP_NOT_ATTACKING: return opp->damage == 0;
        case BranchCondition::OPP_STANDING: return (opp->state & (StateBit::AIRBORNE | StateBit::POSTURE)) == StateBit::STANDING;
        case BranchCondition::OPP_CROUCHING: return (opp->state & (StateBit::AIRBORNE | StateBit::POSTURE)) == StateBit::CROUCHING;
        case BranchCondition::TURNED_AWAY: return turned_away;
        case BranchCondition::TURNED_SIDE_A: return ((f->heading_delta - 0x4E38) & 0xFFFF) < 0x31C8;
        case BranchCondition::TURNED_SIDE_B: {
            gd::Int delta = f->heading_delta & 0xFFFF;
            return delta >= FightMath::HALF_TURN && delta <= 0xB1C6;
        }
        case BranchCondition::FACING_QUADRANT_0: return f->facing_quadrant == 0;
        case BranchCondition::FACING_QUADRANT_1: return f->facing_quadrant == 1;
        case BranchCondition::FACING_QUADRANT_3: return f->facing_quadrant == 3;
        case BranchCondition::FACING_QUADRANT_2: return f->facing_quadrant == 2;
        case BranchCondition::OPP_QUADRANT_0: return f->opp_quadrant == 0;
        case BranchCondition::OPP_QUADRANT_1: return f->opp_quadrant == 1;
        case BranchCondition::OPP_QUADRANT_3: return f->opp_quadrant == 3;
        case BranchCondition::OPP_QUADRANT_2: return f->opp_quadrant == 2;
        case BranchCondition::NEVER: return false;
        case BranchCondition::HIGH_ATTACK_COMING:
            return opp->attack == AttackWord::HIGH && opp->pose_frame <= opp->pose_move->active_last
                && f->dist < HIGH_ATTACK_RANGE;
        case BranchCondition::RECOVERED: return f->recover_mash == 0 && f->recover_frames == 0;
        case BranchCondition::RECOVERING: return f->recover_mash != 0 || f->recover_frames != 0;
        case BranchCondition::BODY_CONTACT: return f->body_contact != 0;
        case BranchCondition::BODY_CONTACT_GROUNDED: return f->body_contact != 0 && opp->in_air == 0 && !opp_down;
        case BranchCondition::OPP_DOWN: return opp_down;
        case BranchCondition::OPP_NOT_DOWN: return !opp_down;
        case BranchCondition::OPP_DOWN_NEAR: return opp_down && opp->pose_move->air_first == 0 && f->dist <= param;
        case BranchCondition::OPP_DOWN_TURNED_AWAY: return opp_down && f->rel_angle >= FightMath::QUARTER_TURN;
        case BranchCondition::OPP_COUNTER_HIT: return opp->counter_hit != 0 && opp->in_air == 0;
        case BranchCondition::TAP_LP:
            f->cond_flag_used = 1;
            return f->tap_lp != 0;
        case BranchCondition::TAP_RP:
            f->cond_flag_used = 1;
            return f->tap_rp != 0;
        case BranchCondition::TAP_BOTH:
            f->cond_flag_used = 1;
            return f->tap_both != 0;
        case BranchCondition::KO: return f->health == 0;
        case BranchCondition::SIDE_CLEAR: return f->side_flag == 0;
        case BranchCondition::SIDE_SET: return f->side_flag != 0;
        case BranchCondition::SIDE_CLEAR_TURNED_AWAY: return f->side_flag == 0 && turned_away;
        case BranchCondition::SIDE_SET_TURNED_AWAY: return f->side_flag != 0 && turned_away;
        case BranchCondition::NOT_LAUNCHED: return f->launch_armed == 0 && f->health != 0;
        case BranchCondition::POWER: return f->power_timer != 0;
        case BranchCondition::NO_POWER: return f->power_timer == 0;
        default:
            break;
    }
    return false;
}

// reversals

// MoveBranchAll (0x8004539C): MoveReactionBranch for both fighters by frame parity.
void MoveSystem::branch_all() {
    gd::Arr<gd::Int> order{1, 0};
    if ((gd::Int(fight->frame_counter) & 1) != 0) {
        order = gd::Arr<gd::Int>{0, 1};
    }
    if (fight->mode == GameMode::FORCE) {
        order.append(2);
    }
    for (gd::Int i : order) {
        gd::Ref<FighterState> f = fight->fighters[i];
        reaction_branch(f, fight->opponent(f));
    }
}

// MoveReactionBranch (0x8002DB08): a hit fighter's running move may reverse or parry the hit.
void MoveSystem::reaction_branch(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    if (opp->char_id == Character::GON) {
        return;
    }
    f->damage_override = 0;
    if (f->got_hit == 0) {
        return;
    }
    gd::Ref<Cursor> found = _find_reversal(f, opp);
    if (found == nullptr) {
        return;
    }
    gd::PI32 row = found->row();
    f->move_slot = Fx::s16(row[ROW_TARGET]);
    f->move_row = fight->move_for_slot(f, f->move_slot);
    f->entry_frame = row[ROW_ENTRY];
    f->was_hit_this_move = 0;
    f->got_hit = 0;
    f->guarded = 0;
    f->hit_clean = 0;
    f->hit_cooldown = 0;
    f->branch_window = Fx::s16(row[ROW_LAST] - f->pose_frame);
    opp->contact = 0;
    opp->hit_done[0] = 0;
    opp->hit_done[1] = 0;
    opp->hit_done[2] = 0;
    opp->contact_this_move = 0;
    f->transition = _remap(f, row[ROW_FLAGS] & 0x3F);
    f->trans_bit7 = 0;
    f->trans_bit6 = 0;
    for (const auto& s : f->hit_slots) {
        s->used = 0;
    }
    f->damage_override = Fx::s16(Fx::div_trunc(Fx::s16(opp->damage), 2) + 0x19);
}

// BranchFindReversal (0x8002E95C): the first row of the running move's list with a reversal
// condition (0x45 to 0x4B) that matches, or null.
gd::Ref<MoveSystem::Cursor> MoveSystem::_find_reversal(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Int frame = f->pose_frame;
    gd::Int direction = f->in_dir;
    gd::Int pressed = f->in_pressed;
    gd::Arr<gd::PI32> list = f->pose_move->bank->branches;
    gd::Int i = f->pose_move->branches;
    gd::Arr<gd::PI32> ret_list = list;
    gd::Int ret = 0;
    while (true) {
        gd::Int cmd = list[i][ROW_COMMAND];
        if (cmd == END) {
            return nullptr;
        }
        if (cmd == RETURN) {
            list = ret_list;
            i = ret + 1;
            continue;
        }
        gd::Arr<gd::PI32> row_list = list;
        gd::Int row_i = i;
        if (cmd == CALL) {
            ret_list = list;
            ret = i;
            row_list = t->common_branches;
            row_i = list[i][ROW_TARGET];
        }
        gd::PI32 row = row_list[row_i];
        if (row[ROW_FIRST] <= frame && frame <= row[ROW_LAST] && _input_match(f, row, direction, pressed)
                && _restriction_ok(f, opp, row[ROW_RESTRICTION]) && row[ROW_CONDITION] >= BranchCondition::REVERSAL_HIT
                && _reversal_condition(row, f, opp)) {
            return gd::make<Cursor>(row_list, row_i);
        }
        list = row_list;
        i = row_i + 1;
    }
    return nullptr;
}

// ReversalCondition (0x8002EB24), types 0x45 to 0x4B.
bool MoveSystem::_reversal_condition(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Int kind = row[ROW_CONDITION];
    gd::Int param = row[ROW_PARAMETER];
    bool facing_each_other = Fx::s16(f->rel_angle) < FACING_EACH_OTHER && Fx::s16(opp->rel_angle) < FACING_EACH_OTHER;
    switch (kind) {
        case BranchCondition::REVERSAL_HIT:
            return f->got_hit != 0 && facing_each_other;
        case BranchCondition::REVERSAL_HIGH:
        case BranchCondition::REVERSAL_MID:
        case BranchCondition::REVERSAL_LOW:
        case BranchCondition::REVERSAL_HIGH_MID: {
            if ((opp->pose_move->flags & MoveFlag::ALERT) != 0) {
                return false;
            }
            if (!facing_each_other) {
                return false;
            }
            gd::Int level = Fx::s16(opp->attack);
            bool ok = false;
            switch (kind) {
                case BranchCondition::REVERSAL_HIGH: ok = level == AttackWord::HIGH; break;
                case BranchCondition::REVERSAL_MID: ok = level == AttackWord::MID; break;
                case BranchCondition::REVERSAL_LOW: ok = level == AttackWord::LOW; break;
                default: ok = level == AttackWord::HIGH || level == AttackWord::MID; break;
            }
            return ok && AttackRecords::uses_joint(opp, param, t);
        }
        case BranchCondition::REVERSAL_SLOT:
            return param == Fx::s16(opp->cur_slot) && facing_each_other;
        case BranchCondition::REVERSAL_ANIM:
            return facing_each_other && _anim_in_list(opp->pose_move, param);
        default:
            break;
    }
    return false;
}

// MoveInSlotList (the list `param` of 0x80095AB8): whether the attacker's animation tag is in it.
bool MoveSystem::_anim_in_list(const gd::Ref<MoveRow>& move, gd::Int list) {
    return list < t->reversal_lists.size() ? t->reversal_lists[list].has(move->anim_bank->anim_tag(move->anim)) : false;
}

}  // namespace t3
