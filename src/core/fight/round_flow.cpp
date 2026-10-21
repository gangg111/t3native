// remake/core/fight/round_flow.gd
// The round state machine (FUN_8003CDD8, fight-frame.md#round-flow) of a VS match:
//
// | State | |
// |---:|---|
// | 0 | Intro: 150 frames in round 1, 90 later, then input on. |
// | 1 | Fight: the timer runs until a KO or the time is up; then the result. |
// | 2 | Result shown (240 frames after a KO, 90 after a time out), losers on scripted input. |
// | 3, 4 | The replay (after a KO), then the win poses and the round-end camera. Tekken Ball
// |   | shows the deciding point's last 30 frames (twice, six times after an iron ball's lethal
// |   | hit) from the result, and skips state 4. |
// | 5, 6 | Win poses until they end (or 300 frames); Start skips. |
// | 9 | The round is over (FightMain starts the next round or ends the match). |
//
// Tekken Force: a won level (the player's round won) goes to state 8, STAGE CLEAR!! until the
// tally is done and Start is pressed or 300 frames pass; state 7 (the level runner's area
// change) returns to the fight.
#include "core/fight/round_flow.h"

#include "content/fight_tables.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_phase.h"
#include "core/combat/state_bit.h"
#include "core/effects/effect_objects.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/replay_state.h"
#include "core/fight/round_state.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/fighter_animation.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_sounds.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_system.h"
#include "core/fighter/transition.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

RoundFlow::RoundFlow(const gd::Ref<FightState>& fight_state) {
    fight = fight_state;
}

// FUN_8003EE94: a requested replay starts (ReplayStart(0)).
void RoundFlow::replay_request_check(const gd::Ref<FightSimulation>& sim) {
    if (fight->replay_request != 0) {
        fight->replay->start(fight, 0);
        sim->events->add(SimEvents::Kind::EFFECTS_CLEAR, -1);
    }
    fight->replay_request = 0;
}

// ReplayUpdate (0x800323CC): non-zero while the replay plays.
gd::Int RoundFlow::replay_update(const gd::Ref<FightSimulation>& sim) {
    return fight->replay->update(fight, sim);
}

// FUN_8003CDD8 for one frame.
void RoundFlow::step(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<FighterState> f0 = fight->fighters[0];
    gd::Ref<FighterState> f1 = fight->fighters[1];
    if (fight->round_state > RoundState::FIGHT) {
        fight->round_counter += 1;
    }
    switch (fight->round_state) {
        case RoundState::INTRO: {
            fight->round_frame += 1;
            gd::Int wait = fight->rounds_played == 1 ? INTRO_ROUND1 : INTRO_LATER;
            if (fight->round_frame < wait) {
                return;
            }
            if (fight->mode != GameMode::PRACTICE) {
                sim->set_input_mode(1);
            }
            fight->round_state = RoundState::FIGHT;
            break;
        }
        case RoundState::FIGHT:
            fight->round_frame += 1;
            if (fight->mode == GameMode::FORCE) {
                fight->challenger_lock = 1;
            }
            fight->no_damage = _round_over() ? 1 : 0;
            if (fight->no_damage == 0) {
                if (fight->timer_stopped == 0) {
                    fight->timer -= 1;
                }
                fight->fight_clock += 1;
            } else {
                _result(f0, f1);
            }
            break;
        case RoundState::RESULT:
            _result_step(sim, f0, f1);
            break;
        case RoundState::REPLAY:
            _ball_result(sim, f0, f1);
            break;
        case RoundState::REPLAY_DONE:
            _replay_done(sim, f0, f1);
            break;
        case RoundState::WIN_POSES:
            _win_state(sim);
            break;
        case RoundState::WIN_WAIT:
            _win_wait(sim, f0, f1);
            break;
        case RoundState::AREA_CHANGE:
            fight->round_state = RoundState::FIGHT;
            break;
        case RoundState::STAGE_CLEAR:
            _stage_clear(sim);
            break;
        case RoundState::END:
            _round_end();
            break;
        default:
            break;
    }
}

// RESULT: the losers on scripted input until the result display ends; then the replay (Tekken
// Force: the round's end, or STAGE CLEAR!! for a won level).
void RoundFlow::_result_step(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    _scripted_input(f0);
    _scripted_input(f1);
    if (fight->mode == GameMode::FORCE) {
        _scripted_input(fight->fighters[2]);
    }
    if (fight->mode == GameMode::BALL) {
        _ball_replays(sim, f0, f1);
    }
    if ((fight->result_flags & (TIME_UP | PERFECT_FLAGS)) != 0 && fight->round_counter == RESULT_CAMERA_TIME) {
        fight->round_end_done = 1;
    }
    if (fight->round_end_done == 0) {
        return;
    }
    fight->input_mode[0] = 0;
    fight->input_mode[1] = 0;
    fight->input_mode[2] = 0;
    FighterSounds::script_stop(f0);
    FighterSounds::script_stop(f1);
    sim->events->add(SimEvents::Kind::VOICE_OFF, -1, 0);
    if (fight->mode == GameMode::FORCE) {
        if (fight->force->player()->round_won != 1) {
            fight->round_state = RoundState::END;
            return;
        }
        sim->events->add(SimEvents::Kind::MUSIC_VOLUME, -1, 0, STAGE_CLEAR_FADE);
        fight->round_state = RoundState::STAGE_CLEAR;
        fight->round_counter = 0;
        return;
    }
    fight->round_state = RoundState::REPLAY;
    _ball_result(sim, f0, f1);
}

// REPLAY_DONE: after the replay, the win poses and the round-end camera, or (skipped) the round's
// end.
void RoundFlow::_replay_done(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (fight->replay->mode != ReplayState::DONE) {
        return;
    }
    fight->blend_hold = 2;
    sim->events->add(SimEvents::Kind::EFFECTS_CLEAR, -1);
    FighterSounds::script_stop(f0);
    FighterSounds::script_stop(f1);
    if (fight->match_result != 0 || (start_pressed(sim) == false && fight->replay_skip == 0)) {
        _win_poses(sim, f0, f1);
        if ((fight->result_flags & TIME_UP) == 0) {
            sim->animation->animate(f0, sim->fighter_view, fight->blend_enabled != 0, true);
            sim->animation->animate(f1, sim->fighter_view, fight->blend_enabled != 0, true);
        }
        _result_camera(sim, f0, f1);
        if (fight->camera_phase == CameraPhase::LOSER_0 || fight->camera_phase == CameraPhase::LOSER_1) {
            _lose_poses(sim, f0, f1);
        }
        _clear_blends();
        sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
        fight->round_state = RoundState::WIN_POSES;
        _win_state(sim);
    } else {
        sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
        _round_end();
    }
}

// STAGE_CLEAR (Tekken Force): until the tally is done and Start is pressed, or the frames run out.
void RoundFlow::_stage_clear(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<FighterState> p = fight->force->player();
    if (fight->round_counter < STAGE_CLEAR_FRAMES) {
        if (start_pressed(sim) && fight->force->gu32(TekkenForce::TALLY_DONE) != 0) {
            fight->round_state = RoundState::END;
        }
    } else {
        fight->round_state = RoundState::END;
    }
    fight->input_mode[p->index] = 2;
    _hold_input(p);
}

// State RESULT of Tekken Ball: 16 frames after the result the deciding point's replay starts, with
// the loser camera; each finished replay starts the next until RESULT of them have played.
void RoundFlow::_ball_replays(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Ref<ModeRegion> region = fight->region;
    if (fight->round_counter == BALL_REPLAY_DELAY) {
        region->ctx_put32(TekkenBall::LAST_POINT, 1);
        fight->hud_shown = 0;
        fight->replay_request = 1;
        gd::Ref<FighterState> loser = f0->round_won < 0 ? f0 : f1;
        sim->camera->loser_start(loser, loser->health != 0);
        fight->camera_phase = loser == f0 ? CameraPhase::LOSER_0 : CameraPhase::LOSER_1;
        region->ctx_put8(TekkenBall::SERVE_SIDE, loser == f0 ? 0 : 1);
    } else if (region->ctx_s32(TekkenBall::LAST_POINT) > 0) {
        if (fight->replay->mode != ReplayState::DONE) {
            fight->round_counter -= 1;
            return;
        }
        sim->events->add(SimEvents::Kind::EFFECTS_CLEAR, -1);
        FighterSounds::script_stop(f0);
        FighterSounds::script_stop(f1);
        gd::Int shown = region->ctx_s32(TekkenBall::LAST_POINT);
        if (shown < region->ctx_s32(TekkenBall::RESULT)) {
            fight->replay_request = 1;
            EffectObjects::reset(fight);
            fight->round_counter -= 1;
            region->ctx_put32(TekkenBall::LAST_POINT, shown + 1);
            fight->camera_phase = f0->round_won < 0 ? CameraPhase::WINNER_0 : CameraPhase::WINNER_1;
        } else {
            fight->hud_shown = 1;
        }
    }
}

// State 3: other modes go on to the replay (state 4); Tekken Ball, whose replays are shown, to
// the win poses (a CPU's defeated opponent keeps a sliver of health).
void RoundFlow::_ball_result(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (fight->mode != GameMode::BALL) {
        fight->round_state = RoundState::REPLAY_DONE;
        fight->replay_request = 1;
        return;
    }
    fight->camera_phase = CameraPhase::FIGHT;
    if (f0->round_won < 0 && f1->is_cpu != 0) {
        f0->health = 1;
    }
    if (f1->round_won < 0 && f0->is_cpu != 0) {
        f1->health = 1;
    }
    _win_poses(sim, f0, f1);
    _result_camera(sim, f0, f1);
    if (fight->camera_phase == CameraPhase::LOSER_0 || fight->camera_phase == CameraPhase::LOSER_1) {
        _lose_poses(sim, f0, f1);
    }
    _clear_blends();
    fight->round_state = RoundState::WIN_POSES;
    fight->region->ctx_put32(TekkenBall::LAST_POINT, fight->region->ctx_s32(TekkenBall::LAST_POINT) + 1);
    _win_state(sim);
}

// States 5 → 6: the win poses start (FUN_8003CE64 case 5 falls into 6).
void RoundFlow::_win_state(const gd::Ref<FightSimulation>& sim) {
    fight->round_end_hold = WIN_HOLD;
    fight->win_pose_done = gd::PI32{0, 0};
    fight->round_counter = 0;
    fight->round_end_done = 0;
    fight->round_state += 1;
    if (fight->carry_health != 0) {
        for (const auto& f : fight->active()) {
            f->health = f->carried_health;
        }
    }
    _win_wait(sim, fight->fighters[0], fight->fighters[1]);
}

// State 6: the win poses and the round-end camera until they end, the hold runs out or Start.
void RoundFlow::_win_wait(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    fight->blend_hold = 2;
    switch (fight->camera_phase) {
        case CameraPhase::FIGHT:
            if (fight->round_counter == WIN_CAMERA_END) {
                fight->round_end_done = 1;
            }
            break;
        case CameraPhase::LOSER_0:
        case CameraPhase::LOSER_1:
            if (fight->round_counter == (fight->mode == GameMode::BALL ? BALL_LOSER_CAMERA_END : LOSER_CAMERA_END)) {
                fight->round_end_done = 1;
            }
            break;
        case CameraPhase::WINNER:
            if (f0->win_pose < 1 || f0->pose_move->length - 1 <= f0->pose_frame) {
                fight->win_pose_done[0] = 1;
            }
            if (f1->win_pose < 1 || f1->pose_move->length - 1 <= f1->pose_frame) {
                fight->win_pose_done[1] = 1;
            }
            if (fight->win_pose_done[0] != 0 && fight->win_pose_done[1] != 0) {
                fight->round_end_done = 1;
            }
            break;
        default:
            break;
    }
    fight->round_end_hold = gd::maxi(fight->round_end_hold - 1, 0);
    if (start_pressed(sim)) {
        sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
    }
    if (fight->round_end_done != 0 || fight->round_end_hold < 1
            || (fight->match_result == 0 && fight->replay_skip != 0 && fight->round_counter > 1)
            || start_pressed(sim)) {
        _round_end();
    }
}

// FUN_8003E608: a fighter is out of health or the time is up. Tekken Force watches the player
// while the enemies come, the first two records in the boss fight (0x800B70E8 set).
bool RoundFlow::_round_over() {
    if (fight->mode == GameMode::FORCE) {
        if (!fight->force->level_ended()) {
            return fight->force->player()->health < 1 || fight->timer < 1;
        }
        return fight->fighters[0]->health < 1 || fight->fighters[1]->health < 1 || fight->timer < 1;
    }
    return fight->fighters[0]->health == 0 || fight->fighters[1]->health == 0 || fight->timer < 1;
}

// The round's result (state 1 → 2).
void RoundFlow::_result(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    // FUN_8003E6D0: health left in 1/4096 of the maximum, rounded up.
    for (const auto& f : gd::Arr<gd::Ref<FighterState>>{f0, f1}) {
        gd::Int unit = f->health_max >> 12;
        f->health_left = Fx::s16(unit != 0 ? Fx::div_trunc(f->health + unit - 1, unit) : 0);
    }
    _result_flags(f0, f1);
    _round_wins(f0, f1);
    _carry_health(f0, f1);
    if (fight->match_result != 0 && fight->mode == GameMode::ARCADE
            && (gd::shr(fight->region->human_mask, fight->match_loser) & 1) != 0) {
        fight->challenger_lock = 1;
    }
    _lose_markers(f0, f1);
    fight->win_pose_done = gd::PI32{0, 0};
    if ((fight->result_flags & TIME_UP) == 0) {
        fight->round_end_hold = RESULT_KO_HOLD;
    } else {
        fight->round_end_hold = RESULT_TIME_HOLD;
        fight->input_mode[2] = 0;
        fight->input_mode[1] = 0;
        fight->input_mode[0] = 0;
    }
    fight->round_state = RoundState::RESULT;
    fight->round_end_done = 0;
}

// FUN_80051AD4: the health the next fight starts with. Team battle's winner and survival's
// winning human recover part of it (by the health left, by the fight's time); other modes keep it.
void RoundFlow::_carry_health(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<FighterState> f = i == 0 ? f0 : f1;
        gd::Int v = f->health;
        switch (gd::Int(fight->mode)) {
            case GameMode::TEAM:
                if (i == fight->match_winner && v != 0) {
                    gd::Int eighths = gd::mini(gd::div(((v << 3) & 0xFFFFFFFFLL), f->health_max), 7);
                    v = _recovered(f, fight->tables->recovery_team[eighths]);
                }
                break;
            case GameMode::SURVIVAL:
                if (i == fight->match_winner && i == fight->region->human_index && v != 0) {
                    gd::Int step = 7;
                    for (gd::Int k = 0; k < RECOVERY_CLOCK.size(); ++k) {
                        if (fight->fight_clock < RECOVERY_CLOCK[k]) {
                            step = k;
                            break;
                        }
                    }
                    v = _recovered(f, fight->tables->recovery_survival[step]);
                }
                break;
            default:
                break;
        }
        f->carried_health = v;
    }
}

gd::Int RoundFlow::_recovered(const gd::Ref<FighterState>& f, gd::Int share) {
    return gd::mini((f->health_max >> 8) * share + f->health, f->health_max);
}

// Game state 9 (and every frame in it): after the first round the human wins against Ogre in
// arcade or time attack, the Ogre scene is requested (OgreSceneCheck 0x800514EC, round state 10).
void RoundFlow::_round_end() {
    fight->round_state = RoundState::END;
    gd::Ref<ModeRegion> region = fight->region;
    if ((fight->mode == GameMode::ARCADE || fight->mode == GameMode::TIME_ATTACK) && region->fight_index == 9 && region->human_count == 1
            && region->ctx32(OGRE_SCENE) == 0 && fight->challenger_lock == 0
            && fight->fighters[region->cpu_index]->round_wins < fight->rounds_to_win
            && fight->fighters[region->human_index]->round_wins > 0) {
        region->ctx_put32(OGRE_SCENE, 1);
        fight->round_state = RoundState::OGRE_SCENE;
    }
}

// FUN_8003E70C: time up, draw, double KO, KO, perfect and great flags.
void RoundFlow::_result_flags(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int time_up = fight->timer < 1 ? 1 : 0;
    gd::Int a = f0->health_left & 0xFFFF;
    gd::Int b = f1->health_left & 0xFFFF;
    fight->result_flags = time_up;
    if (a == b) {
        fight->result_flags = time_up | (a == 0 ? DOUBLE_KO : DRAW);
        return;
    }
    gd::Int winner_left = b < a ? a : b;
    gd::Int loser_left = b < a ? b : a;
    if (loser_left == 0) {
        fight->result_flags = time_up | KO;
        if (winner_left == FULL_HEALTH) {
            fight->result_flags = time_up | PERFECT;
        } else if (winner_left < GREAT_HEALTH) {
            fight->result_flags = time_up | GREAT;
        }
    }
}

// FUN_8003E7C4: the round's winner (+0x46 = 1, the loser −1) and the round wins; a draw counts
// for both (and costs a round of the maximum), the match ends when a fighter has enough wins
// or the last round is played (FUN_8003EA98).
void RoundFlow::_round_wins(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int a = f0->health_left & 0xFFFF;
    gd::Int b = f1->health_left & 0xFFFF;
    if (b < a) {
        f0->round_won = 1;
        f1->round_won = -1;
        f0->round_wins += 1;
    } else if (a < b) {
        f0->round_won = -1;
        f1->round_won = 1;
        f1->round_wins += 1;
    } else {
        fight->max_rounds -= 1;
        gd::Ref<FighterState> first = f0;
        gd::Ref<FighterState> second = f1;
        if (f0->round_wins <= f1->round_wins) {
            if (f1->round_wins > f0->round_wins || _tie_breaker() != 0) {
                first = f1;
                second = f0;
            }
        }
        first->round_wins += 1;
        second->round_wins += 1;
        first->round_won = -1;
        second->round_won = -1;
        if (fight->draw_wins == 0) {
            if (fight->rounds_to_win <= first->round_wins) {
                first->round_won = 1;
            }
            if (fight->rounds_to_win <= second->round_wins) {
                second->round_wins -= 1;
            }
        } else if (fight->rounds_to_win <= first->round_wins && second->round_wins < fight->rounds_to_win) {
            first->round_won = 1;
        }
    }
    if (fight->max_rounds <= fight->rounds_played || fight->rounds_to_win <= f0->round_wins
            || fight->rounds_to_win <= f1->round_wins) {
        _match_result(f0, f1);
    }
}

// The draw tie breaker by the human mask (1: player 2 first; 3: the stored choice 0x800AE406).
gd::Int RoundFlow::_tie_breaker() {
    switch (fight->human_mask) {
        case 1:
            return 1;
        case 3:
            return fight->tie_choice;
        default:
            break;
    }
    return 0;
}

// FUN_8003EA98: the match result from the round wins.
void RoundFlow::_match_result(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (f0->round_wins > f1->round_wins) {
        fight->match_winner = 0;
        fight->match_loser = 1;
        fight->match_result = 1;
    } else if (f0->round_wins < f1->round_wins) {
        fight->match_winner = 1;
        fight->match_loser = 0;
        fight->match_result = 2;
    } else {
        fight->match_winner = _tie_breaker();
        fight->match_loser = (fight->match_winner + 1) & 1;
        fight->match_result = 3;
    }
}

// FUN_8003EBF4: after a time out the losers' win-pose field takes the −1 of the result.
void RoundFlow::_lose_markers(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if ((fight->result_flags & TIME_UP) == 0) {
        return;
    }
    if (f0->round_won == f1->round_won) {
        f0->win_pose = 0;
        f1->win_pose = 0;
        return;
    }
    if (f0->round_won < 0) {
        f0->win_pose = f0->round_won;
    }
    if (f1->round_won < 0) {
        f1->win_pose = f1->round_won;
    }
}

// FUN_8003D7A8: a KO'd fighter or the loser stands still (scripted input 0); the winner of a KO
// keeps the pad unless it is CPU-controlled; the time-out winner holds its guard or crouch.
void RoundFlow::_scripted_input(const gd::Ref<FighterState>& f) {
    if (f->health == 0 || f->round_won < 0) {
        fight->input_mode[f->index] = 2;
        f->script_input = 0;
    } else if ((fight->result_flags & TIME_UP) == 0 && f->is_cpu == 0) {
        fight->input_mode[f->index] = 1;
    } else {
        fight->input_mode[f->index] = 2;
        _hold_input(f);
    }
}

// FUN_8003D860: the scripted word that keeps the fighter's stance (down while crouching). A
// stance slot empty in both banks has no row (bug #61, not reproduced): no hold.
void RoundFlow::_hold_input(const gd::Ref<FighterState>& f) {
    if (f->bank_type != BankType::DOCTOR_B) {
        gd::Ref<MoveRow> row = f->move_row;
        if (row == nullptr) {
            row = fight->move_for_slot(f, f->pose_move->stance_slot);
        }
        if (row != nullptr && (row->state & (StateBit::DOWN_REVERSED | StateBit::DOWN)) != 0) {
            f->script_input = 0x1000;
            return;
        }
    }
    f->script_input = 0;
}

// FUN_8003EB60: the round winner's win pose (1 + frame parity; the physical buttons held
// choose one).
void RoundFlow::_win_poses(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int parity = fight->frame_counter & 1;
    gd::Ref<FighterState> winner = f0;
    gd::Ref<FighterState> loser = f1;
    if (f0->round_won < 1) {
        if (f1->round_won < 1) {
            return;
        }
        winner = f1;
        loser = f0;
    }
    winner->win_pose = parity + 1;
    winner->blend_mode = FighterAnimation::Blend::NONE;
    winner->blend_mode_b = 0;
    fight->blend_hold = 0x14;
    _start_win_pose(sim, winner, loser, parity);
}

// FighterStartWinPose (0x8002D6B0).
void RoundFlow::_start_win_pose(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f,
                                const gd::Ref<FighterState>& opp, gd::Int button) {
    gd::Int held = sim->pads->physical[f->index];
    if ((held & PadState::CIRCLE) != 0) {
        button = 3;
    }
    if ((held & PadState::CROSS) != 0) {
        button = 2;
    }
    if ((held & PadState::TRIANGLE) != 0) {
        button = 1;
    }
    if ((held & PadState::SQUARE) != 0) {
        button = 0;
    }
    switch (button) {
        case 0:
            f->move_slot = WIN_POSE_FIRST;
            break;
        case 1:
            f->move_slot = WIN_POSE_FIRST + 1;
            break;
        case 2:
            f->move_slot = WIN_POSE_FIRST + 2;
            break;
        case 3:
            f->move_slot = WIN_POSE_FIRST + 3;
            break;
        default:
            break;
    }
    if (f->costume_slot == SPECIAL_WIN_COSTUME && f->move_slot == WIN_POSE_FIRST) {
        // The game picks by the video frame count (0x800AE6E0).
        switch (fight->vblank % 3) {
            case 1:
                f->move_slot = WIN_POSE_FIRST + 2;
                break;
            case 2:
                f->move_slot = WIN_POSE_FIRST + 3;
                break;
            default:
                f->move_slot = WIN_POSE_FIRST + 1;
                break;
        }
    }
    _start_move(sim, f, opp, f->move_slot, true);
}

// FUN_8002D8C0 / FUN_8002D844: the loser's pose of a match-deciding round (down or standing).
void RoundFlow::_lose_poses(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    // [[f0, f1], [f1, f0]]: (fighter, opponent) pairs.
    for (const auto& pair : gd::Arr<gd::Arr<gd::Ref<FighterState>>>{{f0, f1}, {f1, f0}}) {
        gd::Ref<FighterState> f = pair[0];
        gd::Ref<FighterState> opp = pair[1];
        if (f->round_won < 0) {
            f->move_slot = f->health == 0 ? LOSE_POSE_DOWN : LOSE_POSE_UP;
            _start_move(sim, f, opp, f->move_slot, false);
        }
    }
}

// A move started from its first frame with transition 6 (`grounded`: on the floor first).
void RoundFlow::_start_move(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f,
                            const gd::Ref<FighterState>& opp, gd::Int slot, bool grounded) {
    gd::Ref<MoveRow> row = fight->move_for_slot(f, slot);
    f->pose_move = row;
    f->root_move = row;
    f->move_row = row;
    f->pose_frame = 1;
    f->entry_frame = 1;
    f->transition = FightMath::transition_remap(Transition::RESTART, f->move_row, f->entry_frame);
    f->trans_bit7 = 0;
    f->trans_bit6 = 0;
    if (grounded) {
        f->pos_y = 0;
    }
    sim->moves->start_or_advance(f, opp);
}

// FUN_8003ECF4: the round-end camera phase (winner 3/4, loser 6/7, none 2) and whose sounds
// stay on (FUN_8002BBE0). The loser camera only follows a match-deciding round a CPU won.
void RoundFlow::_result_camera(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (fight->mode == GameMode::DEMO) {
        f0->active = 1;
        f1->active = 1;
        return;
    }
    bool deciding = !((f0->round_wins < fight->rounds_to_win && f1->round_wins < fight->rounds_to_win)
        || fight->team_round == 1);
    if (deciding) {
        if (f0->is_cpu == 0 && f0->win_pose > 0) {
            fight->camera_phase = CameraPhase::WINNER_0;
            _sounds_of(f0);
            return;
        }
        if (!(f1->is_cpu == 0 && f1->win_pose > 0)) {
            if (f0->win_pose > 0) {
                sim->camera->loser_start(f1, f1->health != 0);
                fight->camera_phase = CameraPhase::LOSER_1;
                _sounds_of(f1);
                return;
            }
            if (f1->win_pose < 1) {
                fight->camera_phase = CameraPhase::FIGHT;
                f0->active = 1;
                f1->active = 1;
                return;
            }
            sim->camera->loser_start(f0, f0->health != 0);
            fight->camera_phase = CameraPhase::LOSER_0;
            _sounds_of(f0);
            return;
        }
    }
    if (f0->win_pose < 1) {
        fight->camera_phase = CameraPhase::WINNER_1;
        if (f1->win_pose > 0) {
            _sounds_of(f1);
            return;
        }
        fight->camera_phase = CameraPhase::FIGHT;
        f0->active = 1;
        f1->active = 1;
        return;
    }
    fight->camera_phase = CameraPhase::WINNER_0;
    _sounds_of(f0);
}

// FUN_8002BBE0: only this fighter's sounds play (the `active` bytes).
void RoundFlow::_sounds_of(const gd::Ref<FighterState>& f) {
    for (const auto& other : fight->fighters) {
        other->active = 0;
    }
    f->active = 1;
}

// FUN_8002C23C: every fighter's blend state cleared.
void RoundFlow::_clear_blends() {
    for (const auto& f : fight->active()) {
        f->blend_frames = 0;
        f->blend_counter = 0;
        f->blend_weight = 0;
        f->blend_root_delta = gd::PI32{0, 0, 0};
    }
}

// FUN_8002BC28 & 0x800: a human pressed Start (Tekken Force: the player's pad).
bool RoundFlow::start_pressed(const gd::Ref<FightSimulation>& sim) {
    gd::Int word = 0;
    if (fight->mode == GameMode::FORCE) {
        return (sim->pads->physical_pressed[fight->force->human()] & PadState::START) != 0;
    }
    switch (fight->human_mask) {
        case 1:
            word = sim->pads->physical_pressed[0];
            break;
        case 2:
            word = sim->pads->physical_pressed[1];
            break;
        case 3:
            word = sim->pads->physical_pressed[0] | sim->pads->physical_pressed[1];
            break;
        default:
            break;
    }
    return (word & PadState::START) != 0;
}

}  // namespace t3
