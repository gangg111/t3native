// remake/core/fighter/fighter_physics.gd
// FighterMovePhysics (0x8003F330): slides, the move's step displacement, launches, push-back,
// timed turns and tracking, and the per-frame bookkeeping (moves.md#displacement-and-slides,
// moves.md#facing-and-tracking, moves.md#per-frame-bookkeeping-in-fightermovephysics).
// Written from the verified port `move_physics` in tools/research/fight_sim.py.
// Tekken Ball drifts the anchor of some moves (the tuning of FUN_800B0B54).
// Tekken Force's thrown bodies and power moves hit whoever they touch (forced hits).
#include "core/fighter/fighter_physics.h"

#include "content/fight_tables.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_math.h"
#include "core/camera/camera_shake.h"
#include "core/combat/attack_records.h"
#include "core/combat/state_bit.h"
#include "core/combat/state_word.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_state.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_hands.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/move_system.h"
#include "core/fighter/tracking.h"
#include "core/game/game_mode.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

FighterPhysics::FighterPhysics(const gd::Ref<FightState>& fight_state, const gd::Ref<MoveSystem>& move_system) {
    fight = fight_state;
    t = fight_state->tables;
    moves = move_system;
}

// (length · sin / 0x7FFF, length · cos / 0x7FFF) in 32-bit C arithmetic.
gd::Vector2i FighterPhysics::polar(gd::Int length, gd::Int angle) {
    gd::Int x = Fx::div_trunc(Fx::w32(length * FightMath::sin_q15(angle, t)), 0x7FFF);
    gd::Int y = Fx::div_trunc(Fx::w32(length * FightMath::cos_q15(angle, t)), 0x7FFF);
    return gd::Vector2i(x, y);
}

gd::Int FighterPhysics::clamp_symmetric(gd::Int v, gd::Int limit) {
    v = Fx::s16(v);
    if (v < 0) {
        return v < -limit ? Fx::s16(-limit) : v;
    }
    return limit < v ? Fx::s16(limit) : v;
}

gd::Int FighterPhysics::track_budget(gd::Int step, gd::Int accum, gd::Int limit) {
    gd::Int a = gd::absi(Fx::s16(accum));
    step = Fx::s16(step);
    if (limit < a + gd::absi(step)) {
        gd::Int rest = Fx::s16(limit - a);
        return step < 0 ? -rest : rest;
    }
    return 0;
}

gd::Int FighterPhysics::_degrees(gd::Int d) {
    return Fx::div_trunc(d * 0xFFFF, 0x168);
}

gd::Int FighterPhysics::_towards(gd::Int target, gd::Int current, gd::Int frames) {
    return Fx::s16(Fx::div_trunc(Fx::s16(target - current), frames));
}

// FighterMovePhysics for one fighter; effects outside the record go to `events`.
void FighterPhysics::update(const gd::Ref<FighterState>& f, const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> opp = fight->opponent(f);
    f->landed_a = 0;
    f->landed_b = 0;
    f->about_to_hit = 0;
    moves->relative_angles(f, opp);
    _slide(f);
    if (f->ballistic == 0 || (f->hit_freeze != 0 && opp->bank_type == BankType::OGRE)) {
        _step_displacement(f);
    } else {
        _flight(f, events);
    }
    if (f->char_id == Character::GON && f->in_reaction != 0 && f->juggle_count == 0) {
        if (f->in_air == 0) {
            f->pos_y = 0;
        } else {
            gd::Int y = Fx::w32(f->root_y + 0x80);
            if (y < 0) {
                f->pos_y = y >> 1;
            }
        }
    }
    if (fight->mode == GameMode::BALL) {
        _ball_drift(f);
    }
    if (f->in_air == 0) {
        f->launch_armed = 0;
    }
    if (f->turn_frames != 0) {
        f->turn_frames = Fx::s16(f->turn_frames - 1);
        f->heading = Fx::s16(f->heading + f->turn_step);
        if (f->throw_state == 1) {
            f->facing = f->heading;
        }
    }
    if (f->slide_to_point == 1) {
        _slide_to_point(f);
    }
    _push(f);
    _tracking(f, opp);
    _bookkeeping(f, opp, events);
}

// Slides (transitions SLIDE*): from the root move's air window on, the distance to the opponent
// (less 500, at most 0xA0C) is spread over the window; at its end the anchor snaps to the root.
void FighterPhysics::_slide(const gd::Ref<FighterState>& f) {
    if (f->slide_state == 1) {
        gd::Ref<MoveRow> root = f->root_move;
        if (root->air_first <= f->pose_frame) {
            gd::Int frames = root->air_last - f->pose_frame;
            if (frames == 0) {
                frames = 1;
            }
            f->slide_state = 2;
            gd::Int d = gd::Int(fight->fighter_distance) & 0xFFFFFFFFLL;
            gd::Int total = d > 0xC00 ? 0xA0C : Fx::w32(d - 500);
            gd::Vector2i s = polar(Fx::s16(Fx::div_trunc(total, frames)), f->target_dir);
            f->slide_step_x = s.x;
            f->slide_step_z = s.y;
            f->air_phase = 2;
        }
    } else if (f->slide_state == 2) {
        f->pos_z = Fx::w32(f->pos_z + f->slide_step_z);
        f->pos_x = Fx::w32(f->pos_x + f->slide_step_x);
        if (f->root_move->air_last <= f->pose_frame) {
            f->slide_state = 0;
            f->slide_step_x = 0;
            f->slide_step_z = 0;
            f->pos_x = f->root_x;
            f->pos_z = f->root_z;
        }
    }
}

// The move's step displacement inside its air window: step / 256 units a frame against the
// facing, through the per-player 24.8 accumulators.
void FighterPhysics::_step_displacement(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> pose = f->pose_move;
    gd::Int first = pose->air_first;
    gd::Int step = pose->step;
    if (first == 0 || f->pose_frame < first - 1 || pose->air_last < f->pose_frame || step == 0) {
        return;
    }
    gd::Int i = f->index;
    gd::Int old_x = f->pos_x;
    gd::Int old_z = f->pos_z;
    gd::Vector2i d = polar(step, f->facing);
    fight->step_accum_x[i] = Fx::w32(gd::Int(fight->step_accum_x[i]) + d.x);
    fight->step_accum_z[i] = Fx::w32(gd::Int(fight->step_accum_z[i]) + d.y);
    f->pos_x = Fx::w32(f->pos_x - (gd::Int(fight->step_accum_x[i]) >> 8));
    f->pos_z = Fx::w32(f->pos_z - (gd::Int(fight->step_accum_z[i]) >> 8));
    fight->step_accum_x[i] = Fx::w32(gd::Int(fight->step_accum_x[i]) + Fx::w32(Fx::w32(f->pos_x - old_x) * 0x100));
    fight->step_accum_z[i] = Fx::w32(gd::Int(fight->step_accum_z[i]) + Fx::w32(Fx::w32(f->pos_z - old_z) * 0x100));
}

// Ballistic flight (combat.md#launches-and-juggles) until the body lands.
void FighterPhysics::_flight(const gd::Ref<FighterState>& f, const gd::Ref<SimEvents>& events) {
    f->air_vel_y = Fx::s16(f->air_vel_y + 6);
    f->pos_y = Fx::w32(f->pos_y + f->air_vel_y);
    f->pos_x = Fx::w32(f->pos_x + f->air_vel_x);
    f->pos_z = Fx::w32(f->pos_z + f->air_vel_z);
    gd::Vector2i v = polar(f->air_speed, f->target_dir + FightMath::HALF_TURN);
    f->air_vel_x = Fx::s16(v.x);
    f->air_vel_z = Fx::s16(v.y);
    if (f->air_vel_y > 0 && -f->ground_offset <= f->pos_y) {
        if (f->air_kind == LANDING_SHAKE) {
            CameraShake::start(fight, 1, events);
        }
        f->pos_z = f->root_z;
        f->pos_y = 0;
        f->pos_x = f->root_x;
        if (f->air_kind == MoveSystem::LAUNCH_AIR_KIND) {
            f->landed_a = 1;
        } else {
            f->landed_b = 1;
        }
        f->ballistic = 0;
        f->air_phase = 2;
    }
}

// Slide to point: the anchor is set back along the direction from the target, and the push-back
// carries it there over 16 frames.
void FighterPhysics::_slide_to_point(const gd::Ref<FighterState>& f) {
    f->slide_to_point = 0;
    gd::Int dz = Fx::w32(f->root_z - f->slide_target_z);
    gd::Int dx = Fx::w32(f->root_x - f->slide_target_x);
    f->push_dir = Fx::s16(FightMath::atan2_angle(dz, dx, t));
    f->push_frames = 0x10;
    gd::Int dist = CameraMath::isqrt(Fx::w32(dx * dx + dz * dz));
    f->push_speed = Fx::s16(Fx::div_trunc(dist, f->push_frames));
    gd::Vector2i o = polar(dist, f->push_dir + FightMath::HALF_TURN);
    f->pos_x = Fx::w32(f->root_x - o.x);
    f->pos_z = Fx::w32(f->root_z - o.y);
}

// Push-back along pushDir: the push speed, the push table, and the repeated-hit bonus.
void FighterPhysics::_push(const gd::Ref<FighterState>& f) {
    gd::Int push = 0;
    if (f->push_frames > 0) {
        push = f->push_speed;
        f->push_frames -= 1;
    }
    if (f->push_table_frames > 0) {
        f->push_table_frames -= 1;
        push += gd::Int(f->push_table[f->push_table_pos]);
        f->push_table_pos += 1;
        if (f->push_repeat > 0) {
            push += f->push_repeat * 0x28 + 10;
        }
    }
    if (push != 0) {
        gd::Vector2i p = polar(push, f->push_dir);
        f->pos_x = Fx::w32(f->pos_x + p.x);
        f->pos_z = Fx::w32(f->pos_z + p.y);
    }
}

// The per-frame bookkeeping after tracking (moves.md#per-frame-bookkeeping-in-fightermovephysics).
void FighterPhysics::_bookkeeping(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                                  const gd::Ref<SimEvents>& events) {
    if (f->attack_alert > 0) {
        f->attack_alert -= 1;
    }
    if (f->hit_cooldown > 0) {
        f->hit_cooldown -= 1;
    }
    gd::Ref<MoveRow> pose = f->pose_move;
    if (f->pose_frame < pose->active_first || pose->active_last < f->pose_frame) {
        f->active_segs = 0;
    } else {
        f->active_segs = gd::Int(AttackRecords::header(pose, t)[2]) != 0 ? 2 : 1;
    }
    f->forced_hit = 0;
    if (fight->mode == GameMode::FORCE) {
        _forced_hit(f);
    }
    if (f->juggle_count != 0 || (pose->air_first <= f->pose_frame && f->pose_frame <= pose->air_last)) {
        f->in_air = 1;
    } else {
        f->in_air = 0;
    }
    if (f->pose_frame == pose->air_last && f->hold_frames == -1 && f->pose_frame != 1 && f->air_phase != 2) {
        if (f->was_hit_this_move == 0 || f->air_kind == MoveSystem::LAUNCH_AIR_KIND
                || (f->state_class != 9 && (f->state & StateBit::DOWN) == 0)) {
            f->landed_a = 1;
        } else {
            f->landed_b = 1;
        }
    }
    _recovery(f);
    _about_to_hit(f, opp);
    _guard_state(f);
    if (f->state_class == 9) {                       // FUN_80040C04
        gd::Int first = f->pose_move->air_first;
        if (first != 0 && first <= f->pose_frame) {
            f->state |= 4;
        }
    }
    _face_shape(f);
    f->ogre_freeze = f->hit_freeze != 0 && opp->bank_type == BankType::OGRE ? 1 : 0;
    f->air_free = f->in_reaction == 0 && f->in_air != 0 ? 1 : 0;
    if (f->hit_clean == 0 && opp->hit_clean == 0) {
        if (f->power_timer != 0) {
            f->power_timer -= 1;
            f->guard &= 0xFFE7;
            if (f->power_timer < 0) {
                f->power_timer = 0;
            }
        }
    } else {
        f->power_timer = 0;
    }
    if (f->in_air != 0) {
        f->push_repeat_timer = 0;
    }
    if (f->push_repeat_timer < 1) {
        f->push_repeat_timer = 0;
    } else {
        f->push_repeat_timer -= 1;
    }
    if (f->push_repeat_timer == 0) {
        f->push_repeat = 0;
    }
    if (fight->mode == GameMode::FORCE) {
        f->no_look_at = f->is_cpu == 0 && f->fixed_facing == 0 ? 0 : 1;
    }
    if (f->active != 0 && f->landed_b != 0) {
        events->add_at(SimEvents::Kind::LANDING_DUST, f->index, gd::PI32{f->root_x, f->root_y, f->root_z});
        CameraShake::start(fight, 0, events);
    }
}

// Knock-down recovery (combat.md#knock-down-recovery): the counters run down, faster with
// button presses.
void FighterPhysics::_recovery(const gd::Ref<FighterState>& f) {
    if (f->recover_frames != 0) {
        f->recover_frames = Fx::s16(f->recover_frames - 1);
        if (f->recover_frames < 0 || f->hit_clean != 0) {
            f->recover_frames = 0;
        }
    }
    if (f->recover_frames < 0x1F && f->recover_mash != 0) {
        gd::Int entry = f->in_history[f->in_hist_index];
        gd::Int mash = (entry & FighterInput::HIST_RP) != 0 ? 3 : ((entry & FighterInput::HIST_BUTTONS) != 0 ? 1 : 0);
        f->recover_mash = Fx::s16(f->recover_mash - 1 - mash);
        if (f->recover_mash < 0) {
            f->recover_mash = 0;
        }
    }
}

// aboutToHit: a damage move up to 3 frames before its active window, flag 10, or Gon against
// anyone else.
void FighterPhysics::_about_to_hit(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Ref<MoveRow> pose = f->pose_move;
    if (f->damage != 0) {
        gd::Int active = pose->active_first;
        gd::Int low = active >= 3 ? active - 3 : 1;
        if (low <= f->pose_frame && f->pose_frame <= active) {
            f->about_to_hit = 1;
        }
    }
    if ((pose->flags & MoveFlag::ABOUT_TO_HIT) != 0) {
        f->about_to_hit = 1;
    }
    if (f->char_id == Character::GON && opp->char_id != Character::GON) {
        f->about_to_hit = 1;
    }
}

// The hand and face shape: hit, or the attack shape before an attack's active window and at the
// start of a non-damage move during the fight.
void FighterPhysics::_face_shape(const gd::Ref<FighterState>& f) {
    if (f->hit_clean != 0) {
        f->hands->command(3, t->face_shape(t->face_hit, f->costume_slot), 10, f->char_id);
    } else if (f->skip_step_physics == 0) {
        if (f->move_changed != 0 && f->damage == 0 && f->in_reaction == 0
                && (fight->round_state == RoundState::FIGHT || fight->round_state == RoundState::RESULT)) {
            f->hands->command(3, t->face_shape(t->face_attack, f->costume_slot), 10, f->char_id);
        }
        if (f->damage != 0 && f->pose_move->active_first - f->pose_frame < 8) {
            f->hands->command(3, t->face_shape(t->face_attack, f->costume_slot), 10, f->char_id);
        }
    }
}

// Tekken Force: a CPU body thrown fast enough, or the player's power move (which costs health),
// hits any enemy it touches (Combat.hit_test).
void FighterPhysics::_forced_hit(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> pose = f->pose_move;
    if (f->is_cpu != 0) {
        if (f->throw_state < 0 && FORCED_THROWN_FRAME < f->pose_frame && f->pose_frame < pose->length) {
            gd::Int speed = CameraMath::isqrt(f->vel_x * f->vel_x + f->vel_y * f->vel_y + f->vel_z * f->vel_z);
            if (speed > FORCED_THROWN_SPEED) {
                f->forced_hit = 1;
                f->attack_hi = FORCED_ATTACK_HI;
                f->attack = FORCED_ATTACK;
                f->damage = (speed >> 4) + FORCED_DAMAGE;
            }
        }
    } else if ((pose->flags & MoveFlag::POWER) != 0) {
        f->forced_hit = 1;
        f->attack_hi = FORCED_ATTACK_HI;
        f->attack = FORCED_ATTACK;
        f->damage = FORCED_DAMAGE;
        if (f->move_changed != 0 && f->health > POWER_MOVE_COST) {
            f->health -= POWER_MOVE_COST;
        }
    }
}

void FighterPhysics::_set_guard_state(const gd::Ref<FighterState>& f, gd::Int value) {
    f->state = value & 0xFFFFFFFFLL;
    f->guard = value & 0x18;
}

// The guard stance from the stick while the move allows it (flag 0x4000 or a side step).
void FighterPhysics::_guard_state(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> pose = f->pose_move;
    gd::Int direction = gd::Int(f->in_history[f->in_hist_index]) & FighterInput::HIST_DIRECTION;
    if (!((pose->flags & MoveFlag::STICK_GUARD) != 0)) {
        if (f->step_kind != 0) {
            if (direction == 4) {
                _set_guard_state(f, StateWord::GUARD_HIGH);
            } else if (direction == 1) {
                _set_guard_state(f, StateWord::GUARD_LOW);
            } else {
                _set_guard_state(f, Fx::s16(pose->state));
                if ((f->state & StateBit::HUMAN_GUARD) != 0 && f->human_guard == 0) {
                    f->state &= 0xFFFFFFE7LL;
                }
            }
        }
    } else if (f->human_guard == 0) {
        if (direction == 4) {
            _set_guard_state(f, StateWord::GUARD_HIGH);
        } else if (direction == 1) {
            _set_guard_state(f, StateWord::GUARD_LOW);
        }
    } else if (direction == 4 || direction == 5) {
        _set_guard_state(f, StateWord::GUARD_HIGH);
    } else if (direction == 1 || direction == 2) {
        _set_guard_state(f, StateWord::GUARD_LOW);
    }
    f->guard = f->in_air == 0 && f->rel_angle < 0x4000 ? (f->state & StateBit::GUARDS) : 0;
}

void FighterPhysics::_track_step(const gd::Ref<FighterState>& f, gd::Int step, gd::Int budget, bool set_facing) {
    gd::Int over = track_budget(step, f->track_accum, budget);
    if (over != 0) {
        f->track_mode = Tracking::AFTER_ACTIVE;
        step = over;
    }
    gd::Int accum = f->track_accum;
    f->heading = Fx::s16(f->heading + step);
    if (set_facing) {
        f->facing = f->heading;
    }
    f->track_accum = Fx::s16(accum + step);
    if (f->pose_move->active_first <= f->pose_frame) {
        f->track_mode = Tracking::AFTER_ACTIVE;
    }
}

void FighterPhysics::_timed_turn(const gd::Ref<FighterState>& f) {
    f->turn_frames = Fx::s16(f->pose_move->active_first - (f->pose_frame & 0xFFFF));
    if (f->turn_frames < 1) {
        f->turn_frames = 1;
    }
    f->turn_step = clamp_symmetric(_towards(f->target_dir, f->heading, f->turn_frames), 0x9F4);
    f->track_mode = Tracking::TIMED_ALERT;
}

// Heading tracking by the transition's tracking mode (moves.md#facing-and-tracking).
void FighterPhysics::_tracking(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Ref<MoveRow> pose = f->pose_move;
    gd::Int mode = f->track_mode;
    gd::Int remaining = Fx::s16(pose->active_first + 1 - (f->pose_frame & 0xFFFF));
    if (remaining < 1) {
        remaining = 1;
    }
    bool alert = opp->attack_alert != 0 && f->attack_seg_count != 0;
    switch (mode) {
        case Tracking::NONE:
            f->heading = f->facing;
            break;
        case Tracking::AIM: {
            gd::Int step = clamp_symmetric(_towards(f->aim_dir, f->heading, remaining), 0x1555);
            _track_step(f, step, 0x7FFF, true);
            break;
        }
        case Tracking::TARGET:
        case Tracking::TARGET_HALF: {
            if (alert && f->move_frame < 9) {
                _timed_turn(f);
                return;
            }
            gd::Int degrees = f->in_air != 0 ? 1 : (f->move_frame > 7 ? 0xE : 3);
            gd::Int limit = _degrees(degrees);
            limit = mode == Tracking::TARGET_HALF ? Fx::s16(Fx::w32(limit << 16) >> 17) : Fx::s16(limit);
            _track_step(f, clamp_symmetric(_towards(f->target_dir, f->heading, remaining), limit), 0x5555);
            break;
        }
        case Tracking::TARGET_SLOW:
            if (f->turn_frames == 0 && !alert) {
                gd::Int degrees = f->in_air != 0 ? 1 : (f->move_frame > 7 ? 3 : 2);
                gd::Int step = clamp_symmetric(_towards(f->target_dir, f->heading, remaining), Fx::s16(_degrees(degrees)));
                _track_step(f, step, 0xE38);
            }
            break;
        case Tracking::TARGET_FAST:
            if (!alert || f->move_frame > 8) {
                gd::Int degrees = f->in_air != 0 ? 4 : (f->move_frame > 7 ? 100 : 3);
                gd::Int step = clamp_symmetric(_towards(f->target_dir, f->heading, remaining), Fx::s16(_degrees(degrees)));
                _track_step(f, step, 0x9554);
            } else {
                _timed_turn(f);
            }
            break;
        case Tracking::AFTER_ACTIVE:
            if (f->damage == 0 || pose->air_first == 0 || f->in_air != 0 || f->pose_frame < pose->active_last) {
                gd::Int frame = f->pose_frame;
                gd::Int active = pose->active_first;
                if (active < frame && (frame - active) % 5 == 0 && f->rel_angle < 0x5556) {
                    gd::Int length = pose->length;
                    f->turn_frames = Fx::s16(length - frame < 6 ? 5 : length - frame);
                    if (f->turn_frames < 0) {
                        f->turn_frames = 0;
                    } else {
                        f->turn_step = clamp_symmetric(_towards(f->aim_dir, f->heading, f->turn_frames), 0x222);
                    }
                }
            } else {
                f->turn_frames = 0;
                f->turn_step = 0;
            }
            break;
        case Tracking::EASE_BY_POSE: {
            gd::Int end = pose->air_last != 0 ? pose->air_last : pose->length;
            if (f->pose_frame <= end) {
                gd::Int delta = Fx::s16(Fx::div_trunc(Fx::s16(f->aim_dir - f->heading) * f->pose_frame, end != 0 ? end : 1));
                f->heading = Fx::s16(f->heading + clamp_symmetric(delta, 0x38E));
            }
            break;
        }
        case Tracking::EASE: {
            gd::Int frames = gd::mini(f->move_frame, 8);
            gd::Int delta = Fx::s16(Fx::div_trunc(Fx::s16(f->aim_dir - f->heading) * frames, 8));
            f->heading = Fx::s16(f->heading + clamp_symmetric(delta, 0x71C));
            break;
        }
        case Tracking::APPROACH:
            if ((gd::Int(fight->fighter_distance) & 0xFFFFFFFFLL) > 0x4AF) {
                gd::Int aim = FightMath::atan2_angle(Fx::w32(f->opp_start_z - f->root_z), Fx::w32(f->opp_start_x - f->root_x), t);
                gd::Int diff = ((f->facing & 0xFFFF) - aim) & 0xFFFFFFFFLL;
                if ((diff & 0x8000) != 0) {
                    diff = ~diff;
                }
                if ((diff & 0xFFFF) > 0x3FFF) {
                    aim -= 0x8000;
                }
                gd::Int delta = Fx::s16(Fx::div_trunc(Fx::s16(aim - (f->facing & 0xFFFF)) * f->pose_frame, pose->length));
                f->facing = Fx::s16(f->facing + clamp_symmetric(delta, 0x5B0));
                f->anchor_dirty = 1;
                f->heading = f->facing;
            }
            break;
        case Tracking::SIDE_STEP: {
            gd::Int length = pose->length;
            if (f->pose_frame <= length) {
                gd::Int side = 0;
                if (f->char_id == 7 && f->facing_quadrant == 2) {
                    side = f->root_dx < 0 ? 0x71C : -0x71C;
                } else {
                    side = f->root_dx < 0 ? -0x11C7 : 0x11C7;
                }
                f->anchor_dirty = 1;
                f->facing = Fx::s16(f->aim_dir + Fx::s16(Fx::div_trunc(side * f->pose_frame, length)));
                f->heading = f->facing;
            }
            break;
        }
        default:
            break;
    }
}

// Tekken Ball: within the tuned frames of the move the anchor drifts at the tuned speed along the
// tuned angle from the opponent's direction.
void FighterPhysics::_ball_drift(const gd::Ref<FighterState>& f) {
    gd::PI32 d = fight->ball->drift(f->player_index);
    gd::Int speed = d[1];
    if (speed == 0 || f->pose_frame < d[2] || f->pose_frame > d[3]) {
        return;
    }
    gd::Int angle = f->target_dir + Fx::s16(d[0]);
    f->pos_x = Fx::w32(f->pos_x + Fx::div_trunc(speed * FightMath::sin_q15(angle, t), 0x7FFF));
    f->pos_z = Fx::w32(f->pos_z + Fx::div_trunc(speed * FightMath::cos_q15(angle, t), 0x7FFF));
}

}  // namespace t3
