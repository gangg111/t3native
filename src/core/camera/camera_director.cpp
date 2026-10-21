// remake/core/camera/camera_director.gd
// The fight camera (camera.md): `CameraDirector` (0x800633C0) with its five camera sources,
// the two-fighter framing, the round intro, throw presets and camera streams, hit cameras and
// the round-end cameras. Integer port of the game's code (the framing and blending follow the
// verified `tools/research/camera_sim.py`); the state is the director's globals, named after the
// documentation with the address in the comment.
//
// Game bugs 1, 2, 6, 53, 54 and 55 are not reproduced (remake-plan.md#original-bugs).
//
// The demonstration fight (mode 6, or any mode while the attract flag is set) has its own
// camera (FUN_800693E8). Tekken Ball frames the ball too (a third target, at pitch and yaw 0);
// Tekken Force has its own side-scrolling camera (force.ovl, TekkenForce).
//
// The intro counter and the winner style persist across matches, as in the game: GameFlow keeps
// one director for the whole session (the flow traces compare both).
#include "core/camera/camera_director.h"

#include "content/camera_tables.h"
#include "content/fight_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_math.h"
#include "core/camera/camera_phase.h"
#include "core/camera/camera_reel.h"
#include "core/camera/camera_view.h"
#include "core/combat/hit_slot.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/transition.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"

namespace t3 {

CameraDirector::CameraDirector(const gd::Ref<FightState>& fight_state) {
    view = gd::make<CameraView>(0, 0, 0, 0, 0, 500);
    fight = fight_state;
    t = fight->tables;
    ct = t->camera;
    for (gd::Int i = 0; i < SOURCES; ++i) {
        sources.append(gd::PI32{0, 0, 0, 0, 0, 0});
        source_state.append(gd::make<SourceState>());
    }
    for (gd::Int i = 0; i < 2; ++i) {
        targets.append(gd::PI32{0, 0, 0, 0});
        prev_targets.append(gd::PI32{0, 0, 0, 0});
    }
}

// ==== director

// CameraDirector for the fight frame; `fight.camera_phase` is g_cameraPhase.
void CameraDirector::step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    skip_side = -1;
    if (f0->bank_type == BankType::YOSHIMITSU && _camera_byte(f0) == CAMERA_BYTE_SKIP)
        skip_side = 0;
    else if (f1->bank_type == BankType::YOSHIMITSU && _camera_byte(f1) == CAMERA_BYTE_SKIP)
        skip_side = 1;
    if (fight->attract != 0 || fight->mode == GameMode::DEMO) {
        _attract_step(f0, f1);
        return;
    }
    if (fight->mode == GameMode::FORCE) {
        _force_step(f0, f1);
        return;
    }
    switch (fight->camera_phase) {
        case CameraPhase::RESET:
        case CameraPhase::INTRO:
            if (fight->camera_phase == CameraPhase::RESET) {
                reset();
                _init_sources(f0, f1);
                _init_sources(f0, f1);
                stream = -1;
                _intro_start(f0, f1);
                cam_script = -1;
                stream_frame = -1;
                stream = -1;
                fight->camera_phase = CameraPhase::INTRO;
            }
            if (enabled != 0 && _intro_frame(f0, f1)) fight->camera_phase = CameraPhase::FIGHT;
            break;
        case CameraPhase::FIGHT:
            if (enabled != 0) _fight_frame(f0, f1);
            break;
        case CameraPhase::WINNER_0:
        case CameraPhase::WINNER_1:
            fight->shake_script = -1;
            _winner_start(fight->camera_phase == CameraPhase::WINNER_0 ? f0 : f1);
            fight->camera_phase = CameraPhase::WINNER;
            _winner_step();
            break;
        case CameraPhase::WINNER:
            _winner_step();
            break;
        case CameraPhase::LOSER_0:
        case CameraPhase::LOSER_1: {
            fight->shake_script = -1;
            gd::Ref<FighterState> f = fight->camera_phase == CameraPhase::LOSER_0 ? f0 : f1;
            _loser_step(f, f->health == 0);
            break;
        }
        default:
            break;
    }
}

// The demonstration fight's branch of CameraDirector: set up once (CameraReset, FUN_80069388,
// FUN_800661C0, FUN_800669CC, FUN_800699F0), then FUN_800693E8 every frame.
void CameraDirector::_attract_step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (attract_state == 0) {
        reset();
        _attract_next_style(f0, f1);
        _init_sources(f0, f1);
        stream = 0;
        replay_on = 0;
        attract_state = 1;
    } else if (attract_state != 1) {
        attract_state = 0;
        return;
    }
    _attract_frame(f0, f1);
}

// Tekken Force's branch of CameraDirector: set up once (CameraReset, FUN_80069388, FUN_800661C0,
// FUN_800669CC, FUN_800699F0, then force.ovl's FUN_800B545C; phase 2), then the side-scrolling
// camera (FUN_800B5504) as source 0 every frame.
void CameraDirector::_force_step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (fight->camera_phase == CameraPhase::RESET) {
        reset();
        _attract_next_style(f0, f1);
        _init_sources(f0, f1);
        stream = 0;
        replay_on = 0;
        fight->force->camera_setup(view);
        fight->camera_phase = CameraPhase::FIGHT;
    } else if (fight->camera_phase != CameraPhase::FIGHT) {
        return;
    }
    sources[SOURCE_FIGHT] = fight->force->camera_frame(view);
    _use_source(SOURCE_FIGHT);
}

// FUN_80063364: the camera starts again (practice after its replay): CameraReset, the style
// counter and the sources, and the replay's own state.
void CameraDirector::restart(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    reset();
    _attract_next_style(f0, f1);
    _init_sources(f0, f1);
    stream = 0;
    replay_on = 0;
}

// FUN_80069388: the style of the counter (modulo 4) and the sources set up again.
void CameraDirector::_attract_next_style(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    attract_choice = Fx::s16(_c_rem4(attract_choice));
    attract_style = ct->attract_styles[attract_choice];
    _init_sources(f0, f1);
}

gd::Int CameraDirector::_c_rem4(gd::Int v) { return v - Fx::div_trunc(v, 4) * 4; }

// FUN_800693E8: four styles of 720 frames each: the fight camera (10), an orbit (20, 40) and a
// circling camera (30); each style's first frame starts it.
void CameraDirector::_attract_frame(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    track(f0, f1, 0);
    bool started = false;
    switch (Fx::s16(attract_style - 10)) {
        case 0:
            _camera_fight(f0, f1, 1, DISTANCE);
            started = true;
            break;
        case 1:
            _camera_fight(f0, f1, 0, DISTANCE);
            break;
        case 10:
            _orbit(f0, -1);
            started = true;
            break;
        case 0xB:
        case 0x1F:
            _orbit(f0, 0);
            break;
        case 0x14:
            _circle(f0, f1, -1);
            started = true;
            break;
        case 0x15:
            _circle(f0, f1, 0);
            break;
        case 0x1E:
            _orbit(f0, -2);
            started = true;
            break;
        default:
            _attract_next_style(f0, f1);
            break;
    }
    if (started) {
        attract_timer = 0x2D0;
        attract_style = (attract_style + 1) & 0xFFFF;
    }
    attract_timer -= 1;
    if (attract_timer < 0) {
        attract_choice = Fx::s16(attract_choice + 1);
        attract_style = ct->attract_styles[_c_rem4(attract_choice)];
    }
    _use_source(SOURCE_FIGHT);
}

// FUN_80069038: source 0 orbiting behind fighter 0 (start with `kind` < 0: ~kind is the orbit
// kind, 0 level, 1 rising and sweeping sideways), looking at the fighters' midpoint.
void CameraDirector::_orbit(const gd::Ref<FighterState>& f, gd::Int kind) {
    gd::Int x0 = targets[0][0];
    gd::Int z0 = targets[0][2];
    if (kind < 0) {
        orbit_kind = ~kind;
        orbit_yaw = Fx::s16(f->facing);
        orbit_frame = 0;
    }
    gd::Int height = 0;
    gd::Int sweep = 0;
    if (orbit_kind == 0) {
        height = -0x62A;
    } else if (orbit_kind == 1 || orbit_kind == 2) {
        gd::Int sway = FightMath::sin12(Fx::div_trunc(orbit_frame * 0x800, 0x2D0) & 0xFFF, t) * 100;
        height = Fx::trunc12(sway) - 0x5AA;
        sweep = Fx::div_trunc(orbit_frame * 8000, 0x2D0) - 4000;
    }
    orbit_frame += 1;
    if (f->hit_done[3] == 0)
        orbit_yaw = Fx::s16(f->facing) >= 0 ? 0x400 - Fx::div_trunc(Fx::s16(f->facing), 16)
                                            : 0x400 - ((Fx::s16(f->facing) + 0xF) >> 4);
    gd::Int k = _index64(orbit_yaw);
    gd::Int c = FightMath::cos12(k, t);
    gd::Int sn = FightMath::sin12(k, t);
    gd::Int x = x0 - Fx::trunc12(c * 0x11F5) + Fx::trunc12(sweep * sn);
    gd::Int z = z0 - Fx::trunc12(sn * 0x11F5) - Fx::trunc12(sweep * c);
    gd::Int yaw = CameraMath::atan2_units4096(Fx::div_trunc(targets[1][0] + x0, 2) - x,
                                              Fx::div_trunc(targets[1][2] + z0, 2) - z, ct);
    gd::Int k2 = _index64((yaw - 0x400) * -0x40);
    gd::Int along = Fx::trunc12(FightMath::sin12(k2, t) * (x0 - x) + FightMath::cos12(k2, t) * (z0 - z));
    gd::Int pitch = CameraMath::atan2_units4096(along, -height, ct);
    sources[SOURCE_FIGHT] = gd::PI32{x, height, z, Fx::s16(pitch) * 64 - 0x1B00, (yaw - 0x400) * 0x40, 500};
}

// The table index of an angle as the code forms it: (v rounded toward zero to 64ths) & 0xFFF.
gd::Int CameraDirector::_index64(gd::Int v) {
    if (v < 0) v += 0x3F;
    return (v >> 6) & 0xFFF;
}

// FUN_80068E90: source 0 circling the fighters at a random pitch and direction (start with
// `restart` < 0), framed at 1.1 times the fight camera's distance.
void CameraDirector::_circle(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart) {
    (void)f0;
    (void)f1;
    if (restart < 0) {
        circle_pitch = ct->attract_pitches[fight->rng->next() & 7];
        circle_yaw = (fight->rng->next() & 0xFFF) << 6;
        circle_speed = ct->attract_speeds[fight->rng->next() & 1];
    }
    cam_script = -1;
    circle_yaw = (circle_yaw + circle_speed) & 0x3FFFF;
    _frame_fighters(circle_pitch, circle_yaw, 1, 0x17F2, SOURCE_FIGHT);
}

// CameraReset (0x80062CF8).
void CameraDirector::reset() {
    view->pitch = 0;
    view->yaw = 0;
    view->x = 0;
    view->y = -0x4B0;
    view->z = 0;
    for (gd::Int i = 0; i < SOURCES; ++i) {
        weights[i] = 0;
        prev_weights[i] = 0;
    }
    weights[0] = Fx::ONE;
    state = Cinematic::IDLE;
    wait = -1;
    throw_player = -1;
    count_total = -1;
    count = -1;
    hit_total = -1;
    hit_count = -1;
    skip_side = -1;
    stream_restart = 0;
    enabled = 1;
    intro_timer = 0;
    cinematic = (fight->mode < GameMode::DEMO || fight->mode > GameMode::FORCE) ? 1 : 0;
}

// The round start (FUN_8002AB68): FUN_800661C0, then FUN_8006867C(0) (the intro offset of the
// counter as it is, the fight camera placed from scratch and kept as the intro's base).
void CameraDirector::round_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    _init_sources(f0, f1);
    intro_index = intro_counter % 10;
    intro_length = fight->rounds_played == 1 ? INTRO_ROUND1 : INTRO_LATER;
    intro_timer = intro_length;
    _camera_fight(f0, f1, 1, DISTANCE);
    intro_base = sources[SOURCE_FIGHT].duplicate();
}

// FUN_800661C0: the projection plane, both target sets, cleared source states and the
// start positions of sources 0 and 1.
void CameraDirector::_init_sources(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    h = 500;
    h_min = 500;
    h_max = 500;
    stream_rescale = 1;
    view->h = 500;
    track(f0, f1, 0);
    track(f0, f1, 0);
    for (gd::Int i = 0; i < SOURCES; ++i) source_state[i] = gd::make<SourceState>();
    for (gd::Int i = 0; i < 2; ++i) sources[i] = gd::PI32{0, -0x5AA, -0x1F40, 0, 0, h};
}

// The round intro (phase 1): returns true when it has faded into the fight camera.
bool CameraDirector::_intro_frame(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    track(f0, f1, 0);
    cam_script = -1;
    stream_frame = -1;
    stream = -1;
    gd::Int w = _intro_step();
    weights[0] = Fx::s16(Fx::ONE - w);
    weights[2] = Fx::s16(w);
    if (weights[0] > 0) _camera_fight(f0, f1, prev_weights[0] < 1 ? 1 : 0, DISTANCE);
    _blend();
    if (weights[2] > 0) return false;
    state = Cinematic::IDLE;
    return true;
}

// Phase 2: the fight camera with throw, stream and hit cameras (camera.md#cinematic-cameras).
void CameraDirector::_fight_frame(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    track(f0, f1, 0);
    if ((throw_player == 0 && f0->throw_state == 0) || (throw_player == 1 && f1->throw_state == 0))
        throw_player = -1;
    switch (state) {
        case Cinematic::IDLE:
            _cinematic_start(f0, f1);
            break;
        case Cinematic::PRESET:
            weights[1] = Fx::ONE;
            weights[2] = 0;
            weights[0] = 0;
            if (_thrower_done(f0, f1)) {
                count = Fx::s16(_blend_out(f0, f1));
                state = Cinematic::PRESET_OUT;
                count_total = count;
            }
            break;
        case Cinematic::PRESET_IN:
            if (wait < 0) {
                weights[0] = Fx::s16(_div(count << 12, count_total));
                weights[1] = Fx::s16(Fx::ONE - weights[0]);
                if (_thrower_done(f0, f1)) {
                    count = Fx::s16(_blend_out(f0, f1));
                    count_total = count;
                    if (weights[1] == 0)
                        state = Cinematic::IDLE;
                    else if (weights[0] == 0)
                        state = Cinematic::PRESET_OUT;
                    else
                        _transition_start();
                } else {
                    count = Fx::s16(count - 1);
                    if (count < 0) state = Cinematic::PRESET;
                }
            } else if (_camera_byte(f0) == CAMERA_BYTE_CUT) {
                _cancel();
            } else {
                wait = Fx::s16(wait - 1);
                if (_camera_byte(f1) == CAMERA_BYTE_CUT) _cancel();
            }
            break;
        case Cinematic::PRESET_OUT: {
            gd::Int c = count;
            count = Fx::s16(count - 1);
            weights[1] = Fx::s16(_div(c << 12, count_total));
            weights[0] = Fx::s16(Fx::ONE - weights[1]);
            if (count < 0) {
                count_total = -1;
                count = -1;
                state = Cinematic::IDLE;
            }
            break;
        }
        case Cinematic::TRANSITION: {
            gd::Int c = count;
            count = Fx::s16(count - 1);
            weights[4] = Fx::s16(_div(c << 12, count_total));
            weights[0] = Fx::s16(Fx::ONE - weights[4]);
            if (count < 0) state = Cinematic::IDLE;
            break;
        }
        case Cinematic::STREAM:
            if (!_stream_frame_state(f0, f1)) return;
            break;
        case Cinematic::HIT:
            weights[1] = Fx::ONE;
            weights[2] = 0;
            weights[0] = 0;
            hit_count = Fx::s16(hit_count - 1);
            if (hit_count < 1) {
                hit_total = -1;
                state = Cinematic::IDLE;
                weights[0] = Fx::ONE;
                weights[2] = 0;
                weights[1] = 0;
                count_total = -1;
                count = -1;
            }
            break;
        default:
            reset();
            break;
    }
    if (weights[1] > 0) {
        if (hit_total < 0)
            _script_camera(f0, f1, prev_weights[1] < 1 ? 1 : 0, DISTANCE, thrower);
        else
            _hit_camera(f0, f1, prev_weights[1] < 1 ? 1 : 0, DISTANCE);
    }
    if (weights[0] > 0) _camera_fight(f0, f1, prev_weights[0] < 1 ? 1 : 0, DISTANCE);
    _blend();
}

// State IDLE: a running throw starts its preset or stream camera (CameraThrowStart); otherwise
// CameraHitChoose may start a hit camera.
void CameraDirector::_cinematic_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    cam_script = -1;
    count = -1;
    hit_total = -1;
    if (cinematic == 0) return;
    if (throw_player < 0 && (f0->throw_state > 0 || f1->throw_state > 0)) {
        thrower = f0->throw_state < 1 ? 1 : 0;
        side = _screen_order() ? 0 : 1;
        if (f0->throw_state < 1) side = 1 - side;
        hit_total = -1;
        throw_player = thrower;
        gd::Int r = _throw_start(f0, f1, thrower, side);
        if (r < 0) {
            cam_script = -1;
            return;
        }
        if (script_kind < 0) {
            state = Cinematic::STREAM;
            wait = -1;
        } else {
            _preset_start(f0, f1);
            wait = 0x10;
            state = Cinematic::PRESET_IN;
        }
        count = r;
        count_total = r;
        return;
    }
    gd::Int r = _hit_choose(f0, f1);
    if (r < 0) return;
    thrower = r;
    side = _screen_order() ? 0 : 1;
    if (r != 0) side = 1 - side;
    script_kind = 1;
    r = hit_script->blend_in;
    if (r >= 0 && script_kind >= 0) {
        state = Cinematic::HIT;
        hit_count = hit_script->frames - hit_script->blend_out;
        hit_total = hit_count;
    }
    count = r;
    count_total = r;
}

// State STREAM: the camera stream. Returns false when the director stops for this frame.
bool CameraDirector::_stream_frame_state(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int restart = 0;
    if (stream_restart == 0) {
        if (wait == -1) {
            restart = 1;
            wait = -2;
        }
    } else {
        wait = -3;
        stream_restart = 0;
    }
    gd::Int r = _script_camera(f0, f1, restart, DISTANCE, thrower);
    if (r < 0) return false;
    weights[1] = 0;
    weights[0] = Fx::s16(Fx::div_trunc(r << 12, STREAM_WEIGHT_FULL));
    weights[2] = Fx::s16(Fx::ONE - weights[0]);
    if (_camera_byte(f0) == CAMERA_BYTE_CUT || _camera_byte(f1) == CAMERA_BYTE_CUT) {
        _blend_out(f0, f1);
        count_total = 8;
        count = 8;
        if (r == STREAM_WEIGHT_FULL) {
            state = Cinematic::IDLE;
        } else {
            _transition_start();
            weights[2] = 0;
        }
        wait = -1;
        cam_script = -1;
    } else if ((thrower == 0 && f0->throw_state < 0) || (thrower == 1 && f1->throw_state < 0)) {
        count_total = 0x23;
        count = 0x23;
        _transition_start();
        weights[2] = 0;
        wait = -1;
        cam_script = -1;
    } else if (wait == -2 && r == 0) {
        wait = -3;
    } else if (wait == -3 && r == STREAM_WEIGHT_FULL) {
        count = Fx::s16(_blend_out(f0, f1));
        state = Cinematic::IDLE;
        wait = -1;
        count_total = count;
        stream = -1;
    }
    return true;
}

// A preset is cancelled by a move camera byte 9 while it waits to blend in.
void CameraDirector::_cancel() {
    cam_script = -1;
    wait = -1;
    state = Cinematic::IDLE;
}

// State 5: source 4 freezes the blended view and fades into the fight camera.
void CameraDirector::_transition_start() {
    weights[4] = Fx::ONE;
    weights[1] = 0;
    weights[0] = 0;
    state = Cinematic::TRANSITION;
    sources[SOURCE_TRANSITION] = sources[SOURCE_OUTPUT].duplicate();
}

// The side flag's screen order: fighter 0 left of fighter 1. On equal screen positions the
// game compares a third target's screen x with the previous frame's first one (bug #53); the
// remake compares the previous frame's positions.
bool CameraDirector::_screen_order() {
    if (gd::Int(targets[0][3]) == gd::Int(targets[1][3]))
        return gd::Int(prev_targets[0][3]) < gd::Int(prev_targets[1][3]);
    return gd::Int(targets[0][3]) < gd::Int(targets[1][3]);
}

gd::Int CameraDirector::_camera_byte(const gd::Ref<FighterState>& f) { return f->pose_move->flags & 0xFF; }

// ==== throw cameras

// CameraThrowStart (0x80067988): the thrower's camera; its blend-in length, 0 for a stream,
// -1 for none.
gd::Int CameraDirector::_throw_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1,
                                     gd::Int thrower_index, gd::Int side_flag) {
    gd::Ref<FighterState> thr = thrower_index == 0 ? f0 : f1;
    if (cam_script != -1) {
        reset();
        return -1;
    }
    return _choose_for(thr, side_flag, thrower_index);
}

// The common part of CameraThrowStart and FUN_80067B14.
gd::Int CameraDirector::_choose_for(const gd::Ref<FighterState>& thr, gd::Int side_flag, gd::Int player) {
    cam_script = _choose(_camera_byte(thr), side_flag, player);
    if (cam_script == -1) return -1;
    script_kind = _kind_of(cam_script);
    if (script_kind < 0) {
        switch_mode = 0;
        script_move = thr->pose_move;
        return 0;
    }
    if (script_kind > 0) {
        script_move = thr->pose_move;
        gd::Int blend_in = ct->presets[script_kind - 1][0];
        switch_mode = blend_in < 2 ? 1 : 0;
        return blend_in;
    }
    return -1;
}

// CameraChoose (0x80067548): the script for a camera id (-1 none); sets the choice index.
// A random number is drawn even when the id has no camera.
gd::Int CameraDirector::_choose(gd::Int id, gd::Int side_flag, gd::Int player) {
    gd::Int r = fight->rng->next();
    if (id < 1) id = 1;
    if (fight->mode == GameMode::PRACTICE || (id - REJECTED_IDS >= 0 && id - REJECTED_IDS < REJECTED_COUNT))
        return -1;
    gd::Arr<gd::PI32> entries = ct->choices;
    gd::Int k = id + 1;
    if (id >= FIRST_BANK_ID) {
        entries = fight->fighters[player]->bank->camera_choices;
        k = id - FIRST_BANK_ID + 1;
    }
    gd::Int index = 0;
    while ((entries[k][1] & 0xFFFF) < (r & 0xFFF)) {
        k += 1;
        index += 1;
    }
    mirror = 0;
    gd::Int value = entries[k][2];
    if (entries[k][0] != 1 && side_flag != 0) {
        gd::PI32 nxt = entries[k + 1];
        if (nxt[0] == 0 || (entries[k][1] & 0xFFFF) < (nxt[1] & 0xFFFF))
            mirror = 1;
        else
            value = nxt[2];
    }
    if (value < 0 && value != -1) value = STREAM_BASE | player << 16 | (value & 0xFFFF);
    choice_index = index;
    return value;
}

// The preset index + 1 of a script, -1 for a stream, 0 for anything else (FUN_800677C8).
gd::Int CameraDirector::_kind_of(gd::Int value) {
    if (value >= 0 && value < PRESET_SCRIPTS) return ct->preset_index[value];
    return value >= STREAM_BASE ? -1 : 0;
}

// FUN_800664A0: a preset's random pitch and yaw offset around the fighters' axis.
void CameraDirector::_preset_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int yaw = _axis_yaw(f0, f1);
    gd::Vector2i p = _preset_random(script_kind - 1);
    if (side != 0) p.y = -gd::Int(p.y);
    preset_yaw = yaw + gd::Int(p.y);
    preset_pitch = p.x;
}

// FUN_80067DE4: (pitch << 5, yaw offset << 6) of preset `index`, one random pick each.
gd::Vector2i CameraDirector::_preset_random(gd::Int index) {
    gd::PI32 preset = ct->presets[index];
    gd::Int pitch = gd::Int(preset[2 + (fight->rng->next() & 3)]) << 5;
    gd::Int yaw = gd::Int(preset[6 + (fight->rng->next() & 3)]) << 6;
    return gd::Vector2i(pitch, yaw);
}

// FUN_80066430: the direction between the targets in 18-bit units, turned by 180 degrees when
// fighter 1 is left of fighter 0 on screen.
gd::Int CameraDirector::_axis_yaw(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int axis = CameraMath::atan2_units4096(targets[1][0] - targets[0][0], targets[1][2] - targets[0][2], ct);
    if (f1->screen_x <= f0->screen_x) axis = (axis + 0x800) & 0xFFF;
    return axis << 6;
}

// CameraThrowEnds (0x80067818) for the running thrower (as the director calls it).
bool CameraDirector::_thrower_done(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (thrower == 0) return _throw_ends(f0, f1);
    if (thrower == 1) return _throw_ends(f1, f0);
    return false;
}

bool CameraDirector::_throw_ends(const gd::Ref<FighterState>& thr, const gd::Ref<FighterState>& vic) {
    if (thr->throw_state < 1 || vic->throw_state >= 0) return true;
    if (_camera_byte(thr) == CAMERA_BYTE_CUT) return true;
    if (switch_mode == 1) return false;
    if ((vic->pose_move->state & 0x400000) != 0) return true;
    gd::Int thrower_left = _event_distance(thr, 0xB00);
    gd::Int victim_left = _event_distance(vic, 0xC00);
    if (thrower_left < 0 && victim_left < 0) return false;
    gd::Int left = thrower_left <= victim_left ? victim_left : thrower_left;
    return left == 0 && thr->transition != Transition::THROW;
}

// Frames from the fighter's pose frame to the last event `kind` (high byte) of its move.
gd::Int CameraDirector::_event_distance(const gd::Ref<FighterState>& f, gd::Int kind) {
    gd::Ref<MoveRow> move = f->pose_move;
    gd::Int left = -1;
    if (move->events < 0) return left;
    gd::PI32 events = move->bank->events;
    gd::Int i = move->events;
    while (events[i] != 0) {
        if ((events[i + 1] & 0xFF00) == kind) left = events[i] - f->pose_frame;
        i += 2;
    }
    return left;
}

// FUN_80067C8C: the blend-out length of the running script, 5 for a cut when the preset
// camera is already close to the fight camera's direction.
gd::Int CameraDirector::_blend_out(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int v = ct->outside_blend_out;
    if (cam_script >= 0 && cam_script < PRESET_SCRIPTS) v = ct->presets[gd::Int(ct->preset_index[cam_script]) - 1][1];
    if (v < 2) {
        gd::PI32 s = sources[SOURCE_SCRIPT];
        if (_within(s[3], 0xE37)) {
            gd::Int axis = CameraMath::atan2_units4096(targets[1][0] - targets[0][0], targets[1][2] - targets[0][2], ct);
            if (f1->screen_x <= f0->screen_x) axis = (axis + 0x800) & 0xFFF;
            if (_within(_s18(s[4] - axis * 0x40), 0xE37)) v = 5;
        }
    }
    return v;
}

// `v + limit < 2 * limit + 1` as the game's unsigned test: -limit <= v <= limit.
bool CameraDirector::_within(gd::Int v, gd::Int limit) { return ((v + limit) & 0xFFFFFFFFLL) < 2 * limit + 1; }

// FUN_80066568: source 1 for a preset (or the stream step for a stream). Returns the
// stream's weight, 0 otherwise.
gd::Int CameraDirector::_script_camera(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1,
                                       gd::Int restart, gd::Int distance, gd::Int thrower_index) {
    gd::Ref<FighterState> thr = thrower_index == 0 ? f0 : f1;
    gd::Ref<FighterState> vic = thrower_index == 0 ? f1 : f0;
    if (script_kind < 0) return _stream_step(restart != 0 ? cam_script : 0, thr, vic);
    if (script_kind < 1) {
        reset();
        return 0;
    }
    _frame_fighters(preset_pitch, preset_yaw, restart, Fx::div_trunc(distance * 0x72, 128), SOURCE_SCRIPT);
    if (restart != 0 || thr->pose_move == script_move) return 0;
    if (thr->throw_state > 0 && _camera_byte(thr) != CAMERA_BYTE_KEEP) {
        bool to_stream = thr->bank_type == BankType::PAUL && _camera_byte(thr) == CAMERA_BYTE_STREAM;
        script_move = thr->pose_move;
        if (to_stream) _switch_camera(side, thr, vic, true);
    }
    return 0;
}

// FUN_800670E4: a stream (re)start or the switch at the thrower's next move, then the
// stream step (FUN_800669D8). `restart_script` 0: no restart. -1 when the switch went to preset
// framing.
gd::Int CameraDirector::_stream_step(gd::Int restart_script, const gd::Ref<FighterState>& thr,
                                     const gd::Ref<FighterState>& vic) {
    if (restart_script == 0) {
        gd::Ref<MoveRow> move = thr->pose_move;
        if (move != script_move && thr->throw_state > 0 && _camera_byte(thr) != CAMERA_BYTE_KEEP) {
            script_move = move;
            if (_camera_byte(thr) < FIRST_BANK_ID) {
                // NormalClip of (thrower root, midpoint of the roots, stream eye) in x/z. The game
                // passes the points' stack addresses instead of their values (bug #54), so it
                // always picks the same side; the remake uses the values.
                gd::Int ax = Fx::s16(thr->root_x);
                gd::Int az = Fx::s16(thr->root_z);
                gd::Int bx = Fx::s16(Fx::div_trunc(thr->root_x + vic->root_x, 2));
                gd::Int bz = Fx::s16(Fx::div_trunc(thr->root_z + vic->root_z, 2));
                gd::Int cx = stream_sample[0];
                gd::Int cz = stream_sample[2];
                gd::Int clip = Fx::w32(ax * bz + bx * cz + cx * az - ax * cz - bx * az - cx * bz);
                _switch_camera(clip < 0 ? 1 : 0, thr, vic, false);
                return -1;
            }
            cam_script = _bank_script(thr);
            script_kind = _kind_of(cam_script);
            _stream_start(cam_script);
            _stream_sample();
            stream_frame = 0;
        }
    } else {
        stream_frame = 0;
        gd::Int yaw_mode = _stream_start(restart_script);
        _stream_sample();
        gd::Int k = Fx::div_trunc(vic->heading, 16);
        gd::Int yaw = 0;
        switch (yaw_mode) {
            case 0:
            case 4:
                yaw = 0x400 - k;
                break;
            case 1:
                yaw = 0x800 - k;
                break;
            case 2:
                yaw = -k;
                break;
            case 3:
            case 5:
                yaw = -(k + 0x400);
                break;
            default:
                break;
        }
        preset_yaw = yaw & 0xFFF;
        stream_origin = gd::PI32{vic->pos_x, vic->pos_y, vic->pos_z};
    }
    if (script_kind < 0) return _stream_view();
    reset();
    // The game returns its caller's $s3 here, 1 (bug #55); the remake returns as the switch does.
    return -1;
}

// FUN_800676CC: the same choice index in the list of the thrower's new camera id (>= 0x2B).
gd::Int CameraDirector::_bank_script(const gd::Ref<FighterState>& thr) {
    gd::Arr<gd::PI32> entries = thr->bank->camera_choices;
    gd::Int k = _camera_byte(thr) - FIRST_BANK_ID + 1 + choice_index;
    mirror = 0;
    gd::Int value = entries[k][2];
    if (entries[k][0] != 1 && side != 0) {
        gd::PI32 nxt = entries[k + 1];
        if (nxt[0] == 0 || (entries[k][1] & 0xFFFF) < (nxt[1] & 0xFFFF))
            mirror = 1;
        else
            value = nxt[2];
    }
    if (value < 0 && value != -1) value = STREAM_BASE | thr->player_index << 16 | (value & 0xFFFF);
    return value;
}

// FUN_8006312C / FUN_8006320C: the camera of the thrower's next move of a multi-part throw.
// `start_stream` (FUN_8006320C) also starts a stream around the victim.
void CameraDirector::_switch_camera(gd::Int side_flag, const gd::Ref<FighterState>& thr,
                                    const gd::Ref<FighterState>& vic, bool start_stream) {
    side = side_flag;
    hit_total = -1;
    throw_player = thrower;
    gd::Int r = -1;
    if (cam_script == -1)
        reset();
    else
        r = _choose_for(thr, side_flag, thr->player_index);
    if (r < 0) {
        cam_script = -1;
    } else if (script_kind < 0) {
        if (start_stream) {
            preset_yaw = (0x400 - Fx::div_trunc(vic->heading, 16)) & 0xFFF;
            stream_origin = gd::PI32{vic->root_x, vic->pos_y, vic->root_z};
            stream_frame = 0;
            _stream_start(cam_script);
            _stream_sample();
            stream_restart = 1;
        }
        state = Cinematic::STREAM;
        count = r;
        count_total = r;
    } else {
        state = Cinematic::PRESET;
        count_total = 1;
        count = 1;
        stream = -1;
    }
    wait = -1;
    weights[1] = Fx::ONE;
    weights[0] = 0;
    weights[2] = 0;
    switch_mode = 1;
}

// ==== camera streams

// FUN_800666E0 with a script: selects the stream; returns its yaw mode.
gd::Int CameraDirector::_stream_start(gd::Int value) {
    stream = value;
    stream_frame = 0;
    gd::Ref<MotionBank> bank = _stream_bank(value);
    gd::Int offset = value & 0xFFFF;
    stream_frames = bank->camera_frames(offset);
    return bank->camera_streams.at(offset).z;
}

gd::Ref<MotionBank> CameraDirector::_stream_bank(gd::Int value) { return fight->fighters[(value >> 16) & 0xFF]->bank; }

// The Ogre scene's camera set-up (FUN_800B4528's first frame, modes.md#match-flow): CameraReset,
// FUN_80069388, FUN_800661C0, then the reel of Ogre's camera id 0x4B (FUN_800673E4(2)): the
// stream state cleared, source 2 at full weight, and the reel's first step at once.
void CameraDirector::ogre_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    reset();
    _attract_next_style(f0, f1);
    _init_sources(f0, f1);
    reel_stream = _choose(OGRE_CAMERA_ID, 0, f0->bank_type == OGRE_BANK_TYPE ? 0 : 1);
    script_kind = -1;
    weights[SOURCE_STREAM] = Fx::ONE;
    stream_frame = 0;
    enabled = 0;
    stream_origin = gd::PI32{0, 0, 0};
    preset_yaw = 0;
    mirror = 0;
    weights[SOURCE_FIGHT] = 0;
    weights[SOURCE_SCRIPT] = 0;
    stream_rescale = 1;
    // FUN_800666E0 selecting the stream returns its frame count + 1, less one: every sample shows.
    reel_left = Fx::s16(_stream_bank(reel_stream)->camera_frames(reel_stream & 0xFFFF));
    reel_frame = 0;
    _reel_take();
    replay_on = 0;  // FUN_800699F0
    reel_step();
}

// FUN_800673E4(0) on the Ogre reel: FUN_80066D00 makes source 2 of the current sample (halved,
// rescaled to the projection plane) and takes the next, then CameraUseSource(2). True on the step
// that shows the stream's last sample: the director takes over from the next frame.
bool CameraDirector::reel_step() {
    if (script_kind >= 0) return true;
    preset_yaw = 0;
    sources[SOURCE_STREAM] = CameraReel::source_of(reel_sample, h, ct);
    _reel_take();
    reel_left = Fx::s16(reel_left - 1);
    _use_source(SOURCE_STREAM);
    return reel_left <= 0;
}

void CameraDirector::_reel_take() {
    reel_sample = _stream_bank(reel_stream)->camera_sample(reel_stream & 0xFFFF, reel_frame);
    reel_frame += 1;
}

// FUN_800666E0 sampling: the next frame into `stream_sample` (positions * 20/32, weight / 8
// clamped to 0...320 and 320 from the last frame on, H 550).
// Past the last frame the game decodes beyond the stream; those samples only ever feed an
// unused source (the weight is then 320), so the remake repeats the last frame.
void CameraDirector::_stream_sample() {
    gd::Int index = stream_frame;
    stream_frame = Fx::s16(stream_frame + 1);
    gd::PI32 raw = _stream_bank(stream)->camera_sample(stream & 0xFFFF, index);
    gd::Int w = Fx::div_trunc(raw[6], 8);
    if (w < 0)
        w = 0;
    else if (w > STREAM_WEIGHT_FULL || stream_frames <= stream_frame)
        w = STREAM_WEIGHT_FULL;
    for (gd::Int i = 0; i < 6; ++i) stream_sample[i] = Fx::s16(Fx::div_trunc(raw[i] * 0x14, 32));
    stream_sample[6] = w;
    stream_sample[7] = STREAM_H;
}

// FUN_800669D8: source 2 from the current sample (placed around the stream origin, turned by
// the stream yaw, mirrored for the other side, rescaled to the projection plane), then the
// next sample. Returns the used sample's weight.
gd::Int CameraDirector::_stream_view() {
    gd::PI32 s = stream_sample;
    gd::Int sample_h = s[7];
    gd::Int weight = s[6];
    gd::Int ex = s[0];
    gd::Int ey = s[1];
    gd::Int ez = s[2];
    gd::Int tx = s[3];
    gd::Int ty = s[4];
    gd::Int tz = s[5];
    if (mirror != 0) {
        ez = Fx::s16(-gd::Int(s[2]));
        tz = Fx::s16(-gd::Int(s[5]));
    }
    gd::Int sn = FightMath::sin12(preset_yaw & 0xFFF, t);
    gd::Int cs = FightMath::cos12(preset_yaw & 0xFFF, t);
    gd::Int plane = sample_h;
    if (stream_rescale != 0) {
        plane = h;
        ey = Fx::s16(ty + Fx::s16(Fx::div_trunc((s[1] - ty) * plane, sample_h)));
        ex = Fx::s16(tx + Fx::s16(Fx::div_trunc((s[0] - tx) * plane, sample_h)));
        ez = Fx::s16(tz + Fx::s16(Fx::div_trunc((ez - tz) * plane, sample_h)));
    }
    gd::PI32 eye{stream_origin[0] + Fx::trunc12(cs * ex - sn * ez), stream_origin[1] + ey,
                 stream_origin[2] + Fx::trunc12(sn * ex + cs * ez)};
    gd::PI32 target{stream_origin[0] + Fx::trunc12(cs * tx - sn * tz), stream_origin[1] + ty,
                    stream_origin[2] + Fx::trunc12(sn * tx + cs * tz)};
    sources[SOURCE_STREAM] = _look_source(eye, target, plane, sources[SOURCE_STREAM][5]);
    _stream_sample();
    return weight;
}

// ==== hit cameras

// CameraHitChoose (0x80067FD4): the index of the fighter whose clean hit gets a hit camera,
// -1 for none.
gd::Int CameraDirector::_hit_choose(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (f0->hit_clean != 0 && f1->hit_clean != 0) return -1;
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<FighterState> d = i == 0 ? f0 : f1;
        gd::Ref<FighterState> a = i == 0 ? f1 : f0;
        if (d->hit_clean == 0) continue;
        gd::Int rating = 0;
        if (d->ko != 0 && d->last_damage > 0x1B)
            rating = 3;
        else if (d->counter_hit != 0)
            rating = 2;
        else if (d->close_hit != 0)
            rating = 1;
        for (const gd::PI32& row : ct->hit_rows) {
            if (rating != 3 && (a->bank_type != row[0] || a->cur_slot != row[1])) continue;
            gd::Int r = fight->rng->next() & 0xFFF;
            // Weights are cumulative thresholds ending at 0x1000 (FUN_80067ECC), so a record
            // is always chosen.
            for (const gd::PI32& record : ct->hit_lists[gd::Int(row[2]) - HIT_LIST_BASE]) {
                if (r <= record[0]) {
                    gd::Ref<CameraTables::HitScript> chosen = ct->hit_scripts[gd::Int(record[1 + rating])];
                    if (chosen->kind == -1) return -1;
                    hit_script = chosen;
                    hit_point = d->hit_slots[d->best_hit_slot]->point.duplicate();
                    return i;
                }
            }
            return -1;
        }
    }
    hit_script = nullptr;
    return -1;
}

// FUN_80068244: source 1 for a hit camera.
void CameraDirector::_hit_camera(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart,
                                 gd::Int distance) {
    gd::Ref<CameraTables::HitScript> s = hit_script;
    if (s->kind != 0 && s->kind != 1) {
        reset();
        return;
    }
    if (restart != 0) {
        gd::Int axis = _axis_yaw(f0, f1);
        gd::Int pitch = gd::Int(s->pitches[fight->rng->next() & 3]) << 6;
        gd::Int pick = fight->rng->next() & 3;
        gd::Int yaw_offset = gd::Int(s->yaws[pick]) * (side != 0 ? -0x40 : 0x40);
        preset_yaw = axis + yaw_offset;
        preset_pitch = pitch;
    }
    if (s->kind == 0) {
        _frame_fighters(preset_pitch, preset_yaw, restart, Fx::div_trunc(distance * 0x72, 128), SOURCE_SCRIPT);
        return;
    }
    gd::Int k = _div(hit_count << 12, hit_total) - 0x400;
    if (k < 1) k = 0;
    gd::Int dist = Fx::trunc12((s->start_distance - s->end_distance) * k);
    dist = Fx::div_trunc((dist + s->end_distance) * h, HIT_DISTANCE_H);
    gd::Int sp = FightMath::sin12(_index18(preset_pitch), t);
    gd::Int cp = FightMath::cos12(_index18(preset_pitch), t);
    gd::Int yi = (CameraMath::sar6(preset_yaw) + 0x400) & 0xFFF;
    gd::PI32 eye{hit_point[0] - Fx::trunc12(Fx::trunc12(dist * FightMath::cos12(yi, t)) * cp),
                 hit_point[1] - Fx::trunc12(dist * sp),
                 hit_point[2] - Fx::trunc12(Fx::trunc12(dist * FightMath::sin12(yi, t)) * cp)};
    sources[SOURCE_SCRIPT] = _look_source(eye, hit_point, h, sources[SOURCE_SCRIPT][5]);
}

// ==== round intro

// FUN_8006867C(1): the next intro offset and length; the fight camera placed from scratch.
void CameraDirector::_intro_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    intro_counter += 1;
    intro_index = intro_counter % 10;
    intro_length = fight->rounds_played == 1 ? INTRO_ROUND1 : INTRO_LATER;
    intro_timer = intro_length;
    _camera_fight(f0, f1, 1, DISTANCE);
    intro_base = sources[SOURCE_FIGHT].duplicate();
}

// FUN_8006879C: source 2 of the intro frame; returns its weight. The eye is the fight
// camera's plus the offset eased from 2x to 0, looking at the fight camera's x and y at z 0.
gd::Int CameraDirector::_intro_step() {
    gd::PI32 offset = fight->mode == GameMode::BALL ? ct->intro_offset_ball : ct->intro_offsets[intro_index];
    gd::Int k = FightMath::sin12((Fx::div_trunc(intro_timer << 10, intro_length) + 0xC00) & 0xFFF, t) + Fx::ONE;
    gd::PI32 eye{intro_base[0] + Fx::trunc12(offset[0] * k), intro_base[1] + Fx::trunc12(offset[1] * k),
                 intro_base[2] + Fx::trunc12(offset[2] * k)};
    gd::PI32 target{intro_base[0], intro_base[1], 0};
    gd::PI32 src = sources[SOURCE_FIGHT];
    sources[SOURCE_STREAM] = _look_source(eye, target, src[5], src[5]);
    intro_timer = Fx::s16(intro_timer - 1);
    return intro_timer > 0x10 ? Fx::ONE : intro_timer << 8;
}

// ==== round end

// FUN_80068998: the winner camera of a round that does not end the match; the two styles
// alternate.
void CameraDirector::_winner_start(const gd::Ref<FighterState>& f) {
    enabled = 0;
    winner_style = (winner_style + 1) & 1;
    round_end_fighter = f;
    if (winner_style == 0)
        _winner_a_start(f);
    else
        _winner_b_start(f);
}

void CameraDirector::_winner_step() {
    enabled = 0;
    if (winner_style == 0)
        _winner_a_step(round_end_fighter);
    else
        _winner_b_step(round_end_fighter);
}

// FUN_800466F8: style A starts 3953 units away at a random angle 60-120 degrees off the facing.
void CameraDirector::_winner_a_start(const gd::Ref<FighterState>& f) {
    gd::Int r = _camera_random();
    gd::Int a = (((r * 0x2AB) >> 16) + 0x2AB - Fx::div_trunc(f->facing, 16)) & 0xFFF;
    gd::PI32 eye{f->root_x + Fx::trunc12(FightMath::cos12(a, t) * 0xF71), -0x352,
                 f->root_z + Fx::trunc12(FightMath::sin12(a, t) * 0xF71)};
    winner_a = _winner_view(f, eye, winner_a);
}

// FUN_80046854: the eye approaches by 8 units per frame while farther than 2513; the view
// turns with 1/32 smoothing.
void CameraDirector::_winner_a_step(const gd::Ref<FighterState>& f) {
    gd::Int dx = f->root_x - winner_a[5];
    gd::Int dy = f->root_y - winner_a[6];
    gd::Int dz = f->root_z - winner_a[7];
    gd::Int d = FightMath::sqrt_table(Fx::w32(dx * dx + dy * dy + dz * dz), t);
    if (d - 0x9D1 > 0) {
        winner_a[5] += Fx::div_trunc(dx * 8, d);
        winner_a[6] += Fx::div_trunc(dy * 8, d);
        winner_a[7] += Fx::div_trunc(dz * 8, d);
    }
    gd::PI32 aim = _winner_view(f, winner_a.slice(5, 8), gd::PI32{0, 0, 0, 0, 0, 0, 0, 0, 0});
    winner_a[0] += Fx::div_trunc(_s18(aim[0] - winner_a[0]), 32);
    winner_a[1] += Fx::div_trunc(_s18(aim[1] - winner_a[1]), 32);
    _use_round_end_view(winner_a, true);
}

// FUN_80046A38: style B picks a lateral offset, a forward distance and a height, and a side.
void CameraDirector::_winner_b_start(const gd::Ref<FighterState>& f) {
    gd::Int lateral = ct->winner_lateral[(_camera_random() * 3) >> 16];
    gd::Int forward = ct->winner_forward[(_camera_random() * 3) >> 16];
    gd::Int height = ct->winner_height[(_camera_random() * 3) >> 16];
    winner_b[6] = height;
    if (height < -0x79D) forward = ct->winner_forward[2];
    gd::Int a = (0x400 - Fx::div_trunc(f->facing, 16)) & 0xFFF;
    gd::Int sn = FightMath::sin12(a, t);
    gd::Int cs = FightMath::cos12(a, t);
    winner_drift[0] = Fx::s16(Fx::div_trunc(-cs, 512));
    winner_drift[1] = Fx::s16(Fx::div_trunc(-sn, 512));
    gd::Int fx = forward * sn;
    gd::Int fz = forward * -cs;
    if (_camera_random() < 0x8000) {
        fx = -fx;
        fz = -fz;
    }
    winner_b[5] = f->root_x + Fx::trunc12(lateral * cs + fx);
    winner_b[7] = f->root_z + Fx::trunc12(lateral * sn + fz);
    winner_b = _winner_view(f, winner_b.slice(5, 8), winner_b);
}

// FUN_80046CB8: the eye drifts backwards along the facing, is pushed out to 2308 units, yaw
// follows with 1/32 and pitch with 1/16 smoothing, and the eye sinks when looking up.
void CameraDirector::_winner_b_step(const gd::Ref<FighterState>& f) {
    winner_b[5] += winner_drift[0];
    winner_b[7] += winner_drift[1];
    gd::Int dx = f->root_x - winner_b[5];
    gd::Int dy = f->root_y - winner_b[6];
    gd::Int dz = f->root_z - winner_b[7];
    gd::Int push = 0x904 - FightMath::sqrt_table(Fx::w32(dx * dx + dy * dy + dz * dz), t);
    if (push > 0) {
        gd::Int flat = FightMath::sqrt_table(Fx::w32(dx * dx + dz * dz), t);
        winner_b[5] -= Fx::div_trunc(_div(push * dx, flat), 16);
        winner_b[7] -= Fx::div_trunc(_div(push * dz, flat), 16);
    }
    gd::PI32 aim = _winner_view(f, winner_b.slice(5, 8), gd::PI32{0, 0, 0, 0, 0, 0, 0, 0, 0});
    winner_b[0] += Fx::div_trunc(_s18(aim[0] - winner_b[0]), 32);
    winner_b[1] += Fx::div_trunc(_s18(aim[1] - winner_b[1]), 16);
    if (winner_b[0] > 0) {
        gd::Int floor_y = Fx::trunc12(FightMath::sin12(_index18(winner_b[0]), t) * -0x1D4C);
        if (floor_y < winner_b[6]) winner_b[6] += Fx::div_trunc(floor_y - winner_b[6], 16);
    }
    _use_round_end_view(winner_b, true);
}

// FUN_80046508: the view from `eye` at the winner's root, its pitch limited by the head and
// the floor, the eye pushed out to 5800 units when closer (into `out`).
gd::PI32 CameraDirector::_winner_view(const gd::Ref<FighterState>& f, const gd::PI32& eye, const gd::PI32& out) {
    gd::PI32 root{f->root_x, f->root_y, f->root_z};
    gd::PI32 v = _aim(eye, root);
    out[0] = v[0];
    out[1] = v[1];
    out[2] = 0;
    out[5] = eye[0];
    out[6] = eye[1];
    out[7] = eye[2];
    gd::PI32 head = _aim(eye, gd::PI32{f->root_x, f->body->joints[2]->t[1] - 0x122, f->root_z});
    out[0] = gd::mini(out[0], head[0] + 0xFA4);
    gd::PI32 ground = _aim(eye, gd::PI32{f->root_x, 0, f->root_z});
    out[0] = gd::mini(out[0], ground[0] - 0x1B05);
    gd::Int ex = out[5] - f->root_x;
    gd::Int ez = out[7] - f->root_z;
    if (Fx::w32(ex * ex + ez * ez) < 0x2014E40) {
        gd::Int i = _index18(out[1]);
        out[5] = f->root_x + Fx::trunc12(FightMath::sin12(i, t) * 0x16A8);
        out[7] = f->root_z - Fx::trunc12(FightMath::cos12(i, t) * 0x16A8);
    }
    return out;
}

// FUN_80046284: pitch (signed) and yaw - 90 degrees of the eye-to-target vector, in 18-bit units.
gd::PI32 CameraDirector::_aim(const gd::PI32& eye, const gd::PI32& target) {
    gd::PI32 a = _aim4096(eye, target);
    return gd::PI32{gd::Int(a[0]) << 6, gd::Int(a[1]) << 6};
}

// FUN_80046180: the same in 4096 units.
gd::PI32 CameraDirector::_aim4096(const gd::PI32& eye, const gd::PI32& target) {
    gd::Int dx = target[0] - eye[0];
    gd::Int dz = target[2] - eye[2];
    gd::Int flat = CameraMath::isqrt(Fx::w32(dx * dx + dz * dz));
    gd::Int pitch = CameraMath::atan2_units4096(flat, target[1] - eye[1], ct) & 0xFFF;
    if ((pitch & 0x800) != 0) pitch -= 0x1000;
    return gd::PI32{pitch, CameraMath::atan2_units4096(dx, dz, ct) - 0x400};
}

// FUN_800463A4 (18-bit angles) / FUN_80046458 (4096 units): a round-end view becomes the
// camera. The projection plane is left as it is.
void CameraDirector::_use_round_end_view(const gd::PI32& v, bool angles18) {
    view->pitch = angles18 ? CameraMath::sar6(v[0]) : gd::Int(v[0]);
    view->yaw = angles18 ? CameraMath::sar6(v[1]) : gd::Int(v[1]);
    view->x = v[5];
    view->y = v[6];
    view->z = v[7];
}

// FUN_80068A44: the loser camera of the match-deciding round, set up at the result (by the
// round flow). `alive`: time out (the loser keeps health); otherwise the overhead KO view.
void CameraDirector::loser_start(const gd::Ref<FighterState>& f, bool alive) {
    weights[0] = Fx::ONE;
    weights[1] = 0;
    weights[2] = 0;
    if (!alive) {
        loser_height = 0;
        loser_yaw = (fight->rng->next() & 0x7F) - 0x40;
        return;
    }
    gd::Int a = (((_camera_random() * 0x401) >> 16) + 0x200 - Fx::div_trunc(f->heading, 16)) & 0xFFF;
    gd::Int reach = f->bank_type == BankType::KUMA ? 0x19C4 : 0xF8A;
    loser[0] = f->root_x + Fx::trunc12(FightMath::cos12(a, t) * reach);
    loser[2] = f->root_z + Fx::trunc12(FightMath::sin12(a, t) * reach);
    loser[1] = -0x5DC;
    loser[3] = f->root_x;
    loser[4] = -0x591;
    loser[5] = f->root_z;
    loser[6] = 0;
}

// FUN_80068C44: the loser camera each frame. `down`: the loser's health is 0.
void CameraDirector::_loser_step(const gd::Ref<FighterState>& f, bool down) {
    enabled = 0;
    if (!down) {
        gd::Int dx = loser[3] - loser[0];
        gd::Int dz = loser[5] - loser[2];
        gd::Int d = FightMath::sqrt_table(Fx::w32(dx * dx + dz * dz), t);
        gd::Int left = d - (f->bank_type == BankType::KUMA ? 0x188A : 0xE50);
        if (left > 0) {
            loser[0] += _div(dx * 2, d);
            loser[2] += _div(dz * 2, d);
        }
        loser[3] += _smooth6(f->root_x - loser[3]);
        loser[4] += _smooth6(f->body->joints[2]->t[1] - loser[4]);
        loser[5] += _smooth6(f->root_z - loser[5]);
        gd::PI32 a = _aim4096(gd::PI32{loser[0], loser[1], loser[2]}, gd::PI32{loser[3], loser[4], loser[5]});
        view->pitch = a[0];
        view->yaw = a[1];
        view->x = loser[0];
        view->y = loser[1];
        view->z = loser[2];
        return;
    }
    if (fight->mode != GameMode::BALL) overhead = 1;
    view->pitch = 0x400;
    view->yaw = loser_yaw + 0x800 - Fx::div_trunc(f->heading, 16);
    view->x = f->root_x;
    view->y = -0x1EAD - loser_height;
    view->z = f->root_z;
}

// `(v * 6) / 128` rounded toward zero.
gd::Int CameraDirector::_smooth6(gd::Int v) { return Fx::div_trunc(v * 6, 128); }

// One step of the camera generator 0x800A95F4; its upper half.
gd::Int CameraDirector::_camera_random() {
    fight->camera_rng = ((fight->camera_rng + 1) * 0x10DCD) & 0xFFFFFFFFLL;
    return fight->camera_rng >> 16;
}

// ==== replay camera

// FUN_80069824 at the replay's start: an orbit from a random side (two styles alternate).
void CameraDirector::replay_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    reset();
    _init_sources(f0, f1);
    replay_track = -1;
    replay_on = 1;
    weights[0] = 0;
    weights[1] = Fx::ONE;
    weights[2] = 0;
    replay_reset = 1;
    replay_distance = 0x2B1A;
    replay_pitch = 0;
    replay_style = (replay_style + 1) & 1;
    gd::Int axis = CameraMath::atan2_units4096(targets[1][0] - targets[0][0], targets[1][2] - targets[0][2], ct);
    replay_yaw = Fx::s16(axis << 6);
    fight->rng->next();
    if (replay_style == 0) {
        replay_yaw = Fx::s16(replay_yaw - 0x71C7 + _ai_random(0xE38E));
        gd::Int a = (_ai_random(0x1E) + 0x19) * 0x40000;
        replay_swing = Fx::s16(Fx::div_trunc(a, 360));
        gd::Int b = (_ai_random(0x1E) + 0x19) * 0x40000;
        replay_swing_to = Fx::s16(-Fx::div_trunc(b, 360));
        if ((fight->rng->next() & 1) != 0) {
            replay_swing = Fx::s16(-replay_swing);
            replay_swing_to = Fx::s16(-replay_swing_to);
        }
    } else {
        gd::Int s = _ai_random(0x15555);
        replay_swing_to = 0;
        replay_swing = 0;
        replay_yaw = Fx::s16(replay_yaw + 0x5556 + s);
    }
    replay_step(f0, f1);
}

// FUN_800569D0: a random value below `n` from rand() and the AI generator 0x800AE168.
gd::Int CameraDirector::_ai_random(gd::Int n) {
    gd::Int r = fight->rng->next();
    gd::Int v = Fx::w32(n * ((r + fight->ai_rng) & 0x7FFF));
    fight->ai_rng = (fight->ai_rng * 5 + 3) & 0xFFFFFFFFLL;
    if (v < 0) v += 0x7FFF;
    return v >> 15;
}

// FUN_800696FC: the replay camera each frame: source 1 framing both fighters from the orbit
// yaw, swinging and closing in slowly.
void CameraDirector::replay_step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    if (replay_on == 0) return;
    _replay_targets(f0, f1);
    _frame_fighters(replay_pitch, replay_swing + replay_yaw, replay_reset, replay_distance, SOURCE_SCRIPT);
    gd::Int d = _s18(replay_swing_to - replay_swing);
    replay_reset = 0;
    replay_swing = Fx::s16(replay_swing + Fx::div_trunc(d, 128));
    replay_distance = Fx::s16(replay_distance + Fx::div_trunc((0x113D - replay_distance) * 6, 256));
    _use_source(SOURCE_SCRIPT);
    enabled = 0;
}

// FUN_800695EC: the replay's targets, the roots (at head height at most), taken every fourth
// frame while bank type 4 tracks on odd frames only ('S' camera byte).
void CameraDirector::_replay_targets(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1) {
    gd::Int skip = -1;
    if (f0->bank_type == BankType::YOSHIMITSU && _camera_byte(f0) == CAMERA_BYTE_SKIP)
        skip = 0;
    else if (f1->bank_type == BankType::YOSHIMITSU && _camera_byte(f1) != CAMERA_BYTE_SKIP)
        skip = -1;
    else if (f1->bank_type == BankType::YOSHIMITSU)
        skip = 1;
    if (skip < 0) {
        replay_track = -1;
    } else {
        gd::Int was = replay_track;
        replay_track = Fx::s16(replay_track + 1);
        if (was < 0) replay_track = 0;
    }
    for (gd::Int k = 0; k < 2; ++k) {
        gd::Ref<FighterState> f = k == 0 ? f0 : f1;
        if (replay_track < 0 || (replay_track & 3) == 0) {
            replay_targets[k] = f->root_x;
            replay_targets[4 + k] = f->root_z;
            replay_targets[2 + k] = gd::mini(f->root_y, f->body->joints[2]->t[1]);
            targets[k][0] = replay_targets[k];
            targets[k][1] = replay_targets[2 + k];
            targets[k][2] = replay_targets[4 + k];
        }
    }
}

// ==== fight camera

// CameraTrackFighters (0x80064944): one target per fighter (the move anchor, or the root while
// launched, in throws or for move flag bit 29; head height - 0x122).
void CameraDirector::track(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int force_root) {
    if (f0->throw_state != 0 || f1->throw_state != 0) force_root = 1;
    for (gd::Int k = 0; k < 2; ++k) {
        gd::Ref<FighterState> f = k == 0 ? f0 : f1;
        prev_targets[k] = targets[k].duplicate();
        if (skip_side != k || (f->pose_frame & 1) != 0) {
            gd::PI32 tg = targets[k];
            if (!((f->pose_move->flags & MoveFlag::TRACK_ROOT) != 0) && f->throw_state == 0 && force_root == 0) {
                tg[0] = f->pos_x;
                tg[2] = f->pos_z;
            } else {
                tg[0] = f->root_x;
                tg[2] = f->root_z;
            }
            tg[3] = f->screen_x;
            tg[1] = Fx::w32(f->body->joints[2]->t[1] - 0x122);
            if (f->cur_slot == 2 && f->bank_type == BankType::YOSHIMITSU) tg[1] = -0x6B8;
        }
    }
    gd::PI32 a = targets[0];
    gd::PI32 b = targets[1];
    if (gd::Int(a[0]) == gd::Int(b[0]) && gd::Int(a[2]) == gd::Int(b[2])) {
        b[0] = a[0] + 1;
        b[2] = a[2] + 1;
        a[0] -= 1;
        a[2] -= 1;
    }
}

// CameraFight (0x8006636C): source 0 from the yaw spring and the framing.
void CameraDirector::_camera_fight(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart,
                                   gd::Int distance) {
    gd::Vector2i spring = _yaw_spring(f0, f1, restart, SOURCE_FIGHT);
    gd::Int pitch = source_state[SOURCE_FIGHT]->pitch;
    gd::Int yaw = spring.y;
    if (fight->mode == GameMode::BALL) {
        pitch = 0;
        yaw = 0;
    }
    _frame_fighters(pitch, yaw, spring.x, distance, SOURCE_FIGHT);
}

// CameraYawSpring (0x80064AF8): (reset for the framing, yaw).
gd::Vector2i CameraDirector::_yaw_spring(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1,
                                         gd::Int restart, gd::Int src) {
    gd::Ref<SourceState> st = source_state[src];
    gd::Int cam_yaw = sources[src][4];
    gd::Int axis = CameraMath::atan2_units4096(targets[1][0] - targets[0][0], targets[1][2] - targets[0][2], ct);
    if (f1->screen_x <= f0->screen_x) axis = (axis + 0x800) & 0xFFF;
    gd::Int err = _s18(Fx::w32(axis * 0x40 - cam_yaw));
    gd::Int sign = gd::signi(err);
    err = gd::absi(err);
    if (restart != 0) {
        st->angle = 0;
        st->speed = 0;
        st->pitch_target = 0;
        st->pitch = 0;
        st->pitch_step = 0;
        st->cleared_a = 0;
        st->cleared_b = 0;
        return gd::Vector2i(restart, Fx::w32(axis * 0x40));
    }
    if (src != 0 || err > 0x1AAAA || fight->fighter_distance < 200) {
        st->cleared_a = 0;
        st->cleared_b = 0;
        return gd::Vector2i(0, cam_yaw);
    }
    gd::Int offset = _swing(st, f0, f1, err, sign);
    _pitch_follow(st, err);
    return gd::Vector2i(0, Fx::w32(cam_yaw + offset));
}

// The yaw swing of the fight source: a kick when the camera falls behind the fighters' axis, then
// a damped spring within +-SWING_CAP (narrower while a move's camera byte asks for it); returns
// the yaw offset.
gd::Int CameraDirector::_swing(const gd::Ref<SourceState>& st, const gd::Ref<FighterState>& f0,
                               const gd::Ref<FighterState>& f1, gd::Int err, gd::Int sign) {
    gd::Int phase = st->phase;
    gd::Int frames = st->frames;
    gd::Int angle = st->angle;
    gd::Int speed = st->speed;
    gd::Int offset = 0;
    if (phase == 0) {
        frames = err < 0xE38 ? 0 : Fx::s16(frames + 1);
        if (frames > 0) {
            phase = 1;
            frames = 0;
            speed = Fx::w32(speed + sign * SWING_KICK);
        }
    } else if (phase == 1) {
        if (err < 0x38E4) {
            angle = Fx::w32(angle - _sar(Fx::w32(angle * 0xE), 7));
            speed = Fx::w32(speed - _half(speed) + sign * 0x300);
            if (err < 0xE39) {
                speed = Fx::w32(speed - _half(speed));
                frames = Fx::s16(frames + 1);
                if (frames >= 0x20) {
                    angle = 0;
                    phase = 0;
                    frames = 0;
                }
            } else {
                frames = 0;
            }
        } else {
            speed = Fx::w32(speed + sign * SWING_KICK);
        }
        angle = Fx::w32(angle + speed);
        gd::Int limit = _swing_limit(_camera_byte(f0), _camera_byte(f1));
        gd::Int hi = SWING_CAP;
        gd::Int lo = -SWING_CAP;
        if (limit >= 1) {
            hi = _sar(limit * SWING_CAP, 8);
            lo = _sar(-limit * SWING_CAP, 8);
        }
        if (hi < angle) {
            angle = hi;
            speed = Fx::w32(speed - SWING_KICK);
        } else if (angle < lo) {
            angle = lo;
            speed = Fx::w32(speed + SWING_KICK);
        }
        offset = _sar(angle, 6);
    }
    st->phase = phase;
    st->frames = frames;
    st->angle = angle;
    st->speed = speed;
    return offset;
}

// The swing limit (in 1/256 of SWING_CAP) asked for by either fighter's move camera byte, or -1.
gd::Int CameraDirector::_swing_limit(gd::Int b0, gd::Int b1) {
    for (const gd::Arr<gd::Int>& entry : SWING_LIMITS) {
        if (b0 == entry[0] || b1 == entry[0]) return entry[1];
    }
    return -1;
}

// The pitch target grows with the yaw error; the fight source's pitch follows it while it moves.
void CameraDirector::_pitch_follow(const gd::Ref<SourceState>& st, gd::Int err) {
    if (err < 0x38E3) {
        st->pitch_target = 0;
    } else {
        gd::Int q = _sar(Fx::div_trunc(Fx::w32((err - 0x38E3) * 0x1000), 0xC71D), 8);
        st->pitch_target = Fx::trunc12(Fx::w32(FightMath::sin12(q, t) * 0x44440));
    }
    gd::Ref<SourceState> s0 = source_state[SOURCE_FIGHT];
    if (s0->moved == 0) {
        s0->pitch_step = 0;
    } else {
        gd::Int step = gd::clampi(_sar(Fx::w32(s0->pitch_target - s0->pitch), 4), -0x100, 0x100);
        s0->pitch_step = step;
        s0->pitch = Fx::w32(s0->pitch + step);
    }
    s0->error = err;
}

// CameraFrameFighters (0x800650F8): source `src` framing both targets (camera.md#fight-camera).
void CameraDirector::_frame_fighters(gd::Int pitch, gd::Int yaw, gd::Int restart, gd::Int distance, gd::Int src) {
    gd::Ref<Framing> fr = gd::make<Framing>();
    fr->pitch = Fx::w32(pitch);
    fr->yaw = Fx::w32(yaw);
    fr->distance = Fx::w32(distance);
    if (fr->distance < 0x321) fr->distance = MIN_DISTANCE;
    fr->restart = restart;
    fr->src = src;
    fr->cam_y = sources[src][1];
    fr->sp = FightMath::sin12(_index18(-fr->pitch), t);
    fr->cp = FightMath::cos12(_index18(-fr->pitch), t);
    fr->sy = FightMath::sin12(_index18(-fr->yaw), t);
    fr->cy = FightMath::cos12(_index18(-fr->yaw), t);
    _orbit_centre(fr);
    _project_targets(fr);
    _fit_horizontal(fr);
    gd::Int vertical = _fit_vertical(fr);
    _place_source(fr, vertical);
}

// The point the camera orbits: the targets' midpoint, offset by the camera's current position
// turned into the new yaw (or placed at `distance` on a restart).
void CameraDirector::_orbit_centre(const gd::Ref<Framing>& fr) {
    gd::PI32 rec = sources[fr->src];
    fr->mx = _div(targets[0][0] + targets[1][0], 2);
    fr->mz = _div(targets[0][2] + targets[1][2], 2);
    gd::Int ox = 0;
    gd::Int oz = 0;
    if (fr->restart == 0) {
        gd::Int i = _index18(_s18(fr->yaw - rec[4]));
        gd::Int s_ = FightMath::sin12(i, t);
        gd::Int c_ = FightMath::cos12(i, t);
        gd::Int dx = Fx::w32(rec[0] - fr->mx);
        gd::Int dz = Fx::w32(rec[2] - fr->mz);
        ox = Fx::trunc12(c_ * dx - s_ * dz);
        oz = Fx::trunc12(s_ * dx + c_ * dz);
    } else {
        gd::Int i = _index18(fr->yaw + 0x30000);
        ox = Fx::trunc12(fr->distance * FightMath::cos12(i, t));
        oz = Fx::trunc12(fr->distance * FightMath::sin12(i, t));
    }
    fr->mx = Fx::w32(fr->mx + ox);
    fr->mz = Fx::w32(fr->mz + oz);
}

// Both targets (and the ball) in view space, the ground point below their middle, and the depth
// correction that puts the camera `distance` behind it.
void CameraDirector::_project_targets(const gd::Ref<Framing>& fr) {
    for (gd::Int k = 0; k < 2; ++k) {
        gd::Int tx = Fx::w32(targets[k][0] - fr->mx);
        gd::Int tz = Fx::w32(targets[k][2] - fr->mz);
        gd::Int ty = Fx::w32(targets[k][1] - fr->cam_y);
        gd::Int a1 = Fx::trunc12(fr->sy * tx + fr->cy * tz);
        fr->u[k] = Fx::trunc12(fr->cy * tx - fr->sy * tz);
        fr->z[k] = Fx::trunc12(fr->cp * a1 - fr->sp * ty);
        fr->v[k] = Fx::trunc12(fr->sp * a1 + fr->cp * ty);
        fr->depth[k] = a1;
    }
    // Tekken Ball: the ball (FUN_800B4948) is a third target; it keeps its raw depth, without the
    // distance correction below.
    if (fight->mode == GameMode::BALL) {
        gd::PI32 p = fight->ball->position();
        gd::Int tx = Fx::w32(p[0] - fr->mx);
        gd::Int tz = Fx::w32(p[2] - fr->mz);
        gd::Int ty = Fx::w32(p[1] - fr->cam_y);
        gd::Int a1 = Fx::trunc12(fr->sy * tx + fr->cy * tz);
        fr->ball = gd::PI32{Fx::trunc12(fr->cy * tx - fr->sy * tz), Fx::trunc12(fr->cp * a1 - fr->sp * ty),
                            Fx::trunc12(fr->sp * a1 + fr->cp * ty), p[1]};
    }
    gd::Int m = _div(fr->depth[0] + fr->depth[1], 2);
    gd::Int up = Fx::w32(-0x1C2 - fr->cam_y);
    gd::Int e10 = Fx::trunc12(fr->cp * m - fr->sp * up);
    fr->e2 = Fx::trunc12(Fx::w32(fr->sp * m + fr->cp * up));
    gd::Int d = fr->distance - e10;
    fr->depth[2] = e10 + d;
    fr->back = -d;
    fr->z[0] += d;
    fr->z[1] += d;
}

// Moves the camera back until both targets fit across the screen with a margin, and sideways to
// centre them (state `lateral` of the source settles it over frames).
void CameraDirector::_fit_horizontal(const gd::Ref<Framing>& fr) {
    gd::Ref<SourceState> st = source_state[fr->src];
    if (!fr->ball.is_empty()) {
        // A ball beyond the outer fighter replaces it, with a further 150-unit margin.
        gd::Int lo_k = gd::Int(fr->u[0]) < gd::Int(fr->u[1]) ? 0 : 1;
        gd::Int hi_k = 1 - lo_k;
        if (gd::Int(fr->ball[0]) < gd::Int(fr->u[lo_k])) {
            fr->u[lo_k] = fr->ball[0] - MARGIN;
            fr->z[lo_k] = fr->ball[1];
        } else if (gd::Int(fr->u[hi_k]) < gd::Int(fr->ball[0])) {
            fr->u[hi_k] = fr->ball[0] + MARGIN;
            fr->z[hi_k] = fr->ball[1];
        }
    }
    gd::Int lo = 0;
    gd::Int hi = 1;
    gd::Int left_px = 0;
    gd::Int edge = 0;
    gd::Int zr = 0;
    if (gd::Int(fr->u[1]) <= gd::Int(fr->u[0])) {
        left_px = _div(Fx::w32((fr->u[1] - MARGIN) * h), fr->z[1]);
        edge = fr->u[0] + MARGIN;
        fr->u[1] -= MARGIN;
        fr->u[0] = edge;
        zr = fr->z[0];
        lo = 1;
        hi = 0;
    } else {
        left_px = _div(Fx::w32((fr->u[0] - MARGIN) * h), fr->z[0]);
        edge = fr->u[1] + MARGIN;
        fr->u[0] -= MARGIN;
        fr->u[1] = edge;
        zr = fr->z[1];
    }
    gd::Int right_px = _div(Fx::w32(edge * h), zr);
    gd::Int backoff = 0;
    if (right_px - left_px < SPREAD_LIMIT) {
        bool centred = false;
        if (fr->restart == 0) {
            gd::Int a7 = _div(fr->z[hi] * 0x30, h);
            gd::Int a13 = _div(fr->z[lo] * -0x30, h);
            if (a7 - (fr->u[hi] - fr->u[lo]) < a13) {
                centred = true;
            } else {
                a13 = fr->u[lo] - a13;
                if (left_px > -0x31) {
                    if (right_px >= 0x31) fr->lateral += _div8(fr->u[hi] - a7);
                } else {
                    fr->lateral += _div8(a13);
                }
            }
        } else {
            centred = true;
        }
        if (centred)
            fr->lateral = _div8(fr->u[lo] + _div(Fx::w32((fr->u[hi] - fr->u[lo]) * fr->z[lo]), fr->z[lo] + fr->z[hi]));
        if (fr->restart != 0 && centred) {
            st->lateral = 0;
            return;
        }
        backoff = _settle(st->lateral);
    } else {
        gd::Int e10b = _div(Fx::w32(h * (fr->u[hi] - fr->u[lo]) + (fr->z[hi] - fr->z[lo]) * -0x8C), 0x118);
        backoff = fr->z[lo] - e10b;
        if (fr->restart == 0) {
            gd::Int s = st->lateral;
            if (s < backoff)
                backoff = s + ((backoff - s) >> 5);
            else
                backoff = s - _div8(s - backoff);
        }
        fr->lateral = _div8(fr->u[lo] - _div(Fx::w32(e10b * -0x8C), h));
    }
    st->lateral = backoff;
    fr->back += backoff;
    fr->depth[2] -= backoff;
    fr->z[0] -= backoff;
    fr->z[1] -= backoff;
}

// Raises the camera so the higher target (or the ball) stays below the top of the screen (state
// `depth` of the source eases it); returns the camera's vertical offset.
gd::Int CameraDirector::_fit_vertical(const gd::Ref<Framing>& fr) {
    gd::Ref<SourceState> st = source_state[fr->src];
    gd::Int top = 100;
    gd::Int ty_world = targets[1][1];
    gd::Int top_v = fr->v[1];
    gd::Int top_z = fr->z[1];
    if (_div(Fx::w32(fr->v[0] * h), fr->z[0]) < _div(Fx::w32(fr->v[1] * h), fr->z[1])) {
        top_v = fr->v[0];
        top_z = fr->z[0];
        ty_world = targets[0][1];
    }
    gd::Int height = Fx::trunc12(ty_world * fr->cp);
    if (!fr->ball.is_empty() && fr->ball[2] < top_v) {
        top_v = fr->ball[2];
        top_z = fr->ball[1];
        height = Fx::trunc12(Fx::w32(fr->ball[3] * fr->cp));
    }
    if (fr->src == SOURCE_SCRIPT && fr->pitch < 0) top = 0x80;
    _script_lift(fr, height);
    fr->scale = Fx::trunc12(fr->cp * (top - _div(Fx::w32(h * 0x1C2), fr->distance)));
    gd::Int want = _div(Fx::w32(Fx::w32((top_z - fr->depth[2]) * -0x70) - Fx::w32(h * (top_v - fr->e2))), fr->scale + 0x70)
        - fr->depth[2];
    if (want < 0) want = 0;
    if (fr->restart == 0) {
        gd::Int cur = st->depth;
        if (want < cur) {
            gd::Int step = cur - want;
            gd::Int big = step * 16;
            if (step > 0x4B0) {
                step = 0x4B0;
                big = 0x4B00;
            }
            gd::Int q = Fx::w32((big - step) * 8);
            q = (q < 0 ? q + 0x7F : q) >> 7;
            st->depth_step = q - step;
            want = cur + (q - step);
        } else if (want > cur) {
            gd::Int step = gd::mini(want - cur, 800);
            st->depth_step = _div8(step);
            want = cur + _div8(step);
        }
    } else {
        want = 0;
        st->depth_step = 0;
    }
    st->depth = want;
    if (want > 0) {
        fr->back -= want;
        fr->depth[2] += want;
    }
    gd::Int vertical_scale = fr->scale;
    if (fr->src == SOURCE_FIGHT) {
        gd::Int q = _div(Fx::w32(Fx::trunc12(Fx::w32(want * fr->cp)) * h), fr->distance);
        vertical_scale += (q < 0 ? q + 0xF : q) >> 4;
    }
    return fr->e2 - _div(Fx::w32(vertical_scale * fr->depth[2]), h) + fr->lift;
}

// Script cameras looking down lift with the target's height, at most 0x1C a frame.
void CameraDirector::_script_lift(const gd::Ref<Framing>& fr, gd::Int height) {
    gd::Int g = height_term;
    gd::Int new_g = g;
    if (fr->src == SOURCE_SCRIPT && fr->pitch > 0) {
        gd::Int lift = 0;
        if (height > -1001) lift = _div(Fx::w32((height + 1000) * 0x170), 0x280);
        if (fr->restart == 0) {
            if (g < lift && lift - g > 0x1C)
                lift = g + 0x1C;
            else if (lift < g && g - lift > 0x1C)
                lift = g - 0x1C;
        }
        new_g = lift;
        fr->lift = lift;
    }
    height_term = new_g;
}

// The source's new position and angles from the offsets around the orbit centre; unless it
// restarts, the angles turn by the wrapped difference.
void CameraDirector::_place_source(const gd::Ref<Framing>& fr, gd::Int vertical) {
    gd::PI32 rec = sources[fr->src];
    gd::Int cam_x = rec[0];
    gd::Int cam_z = rec[2];
    gd::Int cam_p = rec[3];
    gd::Int cam_yw = rec[4];
    gd::Int sp2 = FightMath::sin12(_index18(fr->pitch), t);
    gd::Int cp2 = FightMath::cos12(_index18(fr->pitch), t);
    gd::Int sy2 = FightMath::sin12(_index18(fr->yaw), t);
    gd::Int cy2 = FightMath::cos12(_index18(fr->yaw), t);
    gd::Int r2 = Fx::trunc12(fr->back * cp2 - vertical * sp2);
    gd::Int r12 = Fx::trunc12(fr->back * sp2 + vertical * cp2);
    gd::Int xa = Fx::w32(fr->lateral * cy2 + r2 * -sy2);
    gd::Int za = Fx::w32(fr->lateral * sy2 + r2 * cy2);
    gd::Int new_x = Fx::w32(fr->mx + Fx::trunc12(xa));
    gd::Int new_z = Fx::w32(fr->mz + Fx::trunc12(za));
    if (fr->restart == 0) {
        gd::Int dx = Fx::w32(new_x - cam_x);
        gd::Int dz = Fx::w32(new_z - cam_z);
        if (fr->src == SOURCE_FIGHT) source_state[fr->src]->moved = Fx::w32(dx * dx + dz * dz) < 0x3E9 ? 0 : 1;
        cam_p = Fx::w32(cam_p + _s18(fr->pitch - cam_p));
        cam_yw = Fx::w32(cam_yw + _s18(fr->yaw - cam_yw));
    } else {
        cam_p = fr->pitch;
        cam_yw = fr->yaw;
    }
    gd::Int cam_y = Fx::w32(fr->cam_y + r12);
    sources[fr->src] = gd::PI32{new_x, cam_y, new_z, cam_p, cam_yw, rec[5]};
}

// ==== blending

// CameraBlend (0x80062E5C): the positive-weight sources into source 3 and the view.
void CameraDirector::_blend() {
    gd::Int mask = 0;
    for (gd::Int i = 0; i < SOURCES; ++i) {
        if (weights[i] > 0) mask |= gd::shl(1, i);
    }
    gd::Int w0 = weights[0];
    switch (mask) {
        case 1:
        case 2:
        case 4:
        case 0x10:
            _use_source(_bit_index(mask));
            break;
        case 3:
        case 5:
            _blend_pair(sources[0], sources[mask == 3 ? 1 : 2], w0);
            break;
        case 0x11: {
            gd::Int e = FightMath::sin12((Fx::div_trunc(w0, 2) - 0x400) & 0xFFF, t);
            _blend_pair(sources[0], sources[4], Fx::div_trunc(e, 2) + 0x800);
            break;
        }
        default:
            reset();
            break;
    }
    for (gd::Int i = 0; i < SOURCES; ++i) prev_weights[i] = weights[i];
}

gd::Int CameraDirector::_bit_index(gd::Int mask) {
    gd::Int i = 0;
    while (mask > 1) {
        mask >>= 1;
        i += 1;
    }
    return i;
}

// CameraUseSource (0x80064368): source i becomes source 3 and the view.
void CameraDirector::_use_source(gd::Int i) {
    gd::PI32 rec = sources[i];
    sources[SOURCE_OUTPUT] = rec.duplicate();
    view->x = rec[0];
    view->y = rec[1];
    view->z = rec[2];
    view->pitch = CameraMath::sar6(rec[3]);
    view->yaw = CameraMath::sar6(rec[4]);
    view->h = gd::clampi(rec[5], h_min, h_max);
}

// CameraLookPoint (0x80063FE8): the mean distance r to the targets and the point r along
// the view. Bug #1 is not reproduced: the distance to target 1 uses target 1's z.
CameraDirector::LookPoint CameraDirector::_look_point(const gd::PI32& rec) {
    gd::Int sp = FightMath::sin12(_index18(rec[3]), t);
    gd::Int cp = FightMath::cos12(_index18(rec[3]), t);
    gd::Int i = _index18(Fx::w32(rec[4] + 0x10000));
    gd::Int sy = FightMath::sin12(i, t);
    gd::Int cy = FightMath::cos12(i, t);
    gd::Int d0 = _distance(targets[0], rec);
    gd::Int d1 = _distance(targets[1], rec);
    gd::Int r = ((d0 + d1) & 0xFFFFFFFFLL) >> 1;
    gd::PI32 look{Fx::w32(rec[0] + Fx::trunc12(Fx::w32(Fx::trunc12(Fx::w32(r * cy)) * cp))),
                  Fx::w32(rec[1] + Fx::trunc12(Fx::w32(r * sp))),
                  Fx::w32(rec[2] + Fx::trunc12(Fx::w32(Fx::trunc12(Fx::w32(r * sy)) * cp)))};
    LookPoint out;
    out.r = r;
    out.look = look;
    return out;
}

gd::Int CameraDirector::_distance(const gd::PI32& target, const gd::PI32& rec) {
    gd::Int dx = target[0] - rec[0];
    gd::Int dy = target[1] - rec[1];
    gd::Int dz = target[2] - rec[2];
    return CameraMath::isqrt(Fx::w32(Fx::w32(dx * dx) + Fx::w32(dy * dy) + Fx::w32(dz * dz)));
}

// CameraBlendPair (0x80064458): `a` and `b` (weight w of `a`) blended around their look
// points into source 3 and the view. Bug #6: a non-positive H keeps source 3's H.
void CameraDirector::_blend_pair(const gd::PI32& a, const gd::PI32& b, gd::Int w) {
    gd::Int iw = Fx::ONE - w;
    LookPoint la = _look_point(a);
    LookPoint lb = _look_point(b);
    gd::Int ra = la.r;
    gd::Int rb = lb.r;
    gd::Int r = Fx::trunc12(Fx::w32(ra * w + rb * iw));
    gd::PI32 pa = la.look;
    gd::PI32 pb = lb.look;
    gd::PI32 look{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) look[k] = Fx::trunc12(Fx::w32(pa[k] * w + pb[k] * iw));
    gd::Int pitch = Fx::w32(a[3] + Fx::trunc12(Fx::w32(_s18(b[3] - a[3]) * iw)));
    gd::Int yaw = Fx::w32(a[4] + Fx::trunc12(Fx::w32(_s18(b[4] - a[4]) * iw)));
    gd::Int sp = FightMath::sin12(_index18(pitch), t);
    gd::Int cp = FightMath::cos12(_index18(pitch), t);
    gd::Int yi = (CameraMath::sar6(yaw) + 0x400) & 0xFFF;
    gd::Int ys = FightMath::sin12(yi, t);
    gd::Int yc = FightMath::cos12(yi, t);
    gd::PI32 pos{Fx::w32(look[0] - Fx::trunc12(Fx::w32(Fx::trunc12(Fx::w32(r * yc)) * cp))),
                 Fx::w32(look[1] - Fx::trunc12(Fx::w32(r * sp))),
                 Fx::w32(look[2] - Fx::trunc12(Fx::w32(Fx::trunc12(Fx::w32(r * ys)) * cp)))};
    gd::Int bh = Fx::trunc12(Fx::w32(a[5] * w + b[5] * iw));
    gd::Int old_h = sources[SOURCE_OUTPUT][5];
    sources[SOURCE_OUTPUT] = _look_source(pos, look, bh, old_h);
    _use_source(SOURCE_OUTPUT);
}

// FUN_800641D4: a source at `eye` looking at `target` with projection plane `plane` (the
// previous value `keep` when not positive).
gd::PI32 CameraDirector::_look_source(const gd::PI32& eye, const gd::PI32& target, gd::Int plane, gd::Int keep) {
    gd::Int dx = target[0] - eye[0];
    gd::Int dz = target[2] - eye[2];
    gd::Int flat = CameraMath::isqrt(Fx::w32(dx * dx + dz * dz));
    gd::Int pitch = Fx::s16(CameraMath::atan2_units4096(flat, target[1] - eye[1], ct)) << 6;
    gd::Int yaw = Fx::s16(CameraMath::atan2_units4096(dz, -dx, ct)) << 6;
    return gd::PI32{eye[0], eye[1], eye[2], pitch, yaw, plane > 0 ? plane : keep};
}

// ==== arithmetic

// An 18-bit angle as a 4096-entry table index: `(a [+ 0x3F if negative]) >> 6 & 0xFFF`.
gd::Int CameraDirector::_index18(gd::Int a) {
    a = Fx::w32(a);
    return ((a < 0 ? a + 0x3F : a) >> 6) & 0xFFF;
}

gd::Int CameraDirector::_s18(gd::Int v) {
    v &= 0x3FFFF;
    return (v & 0x20000) != 0 ? v - 0x40000 : v;
}

// C division; a zero divisor is taken as 1 (bug #2 is not reproduced).
gd::Int CameraDirector::_div(gd::Int a, gd::Int b) { return Fx::div_trunc(Fx::w32(a), b == 0 ? 1 : Fx::w32(b)); }

gd::Int CameraDirector::_div8(gd::Int v) { return (v < 0 ? v + 7 : v) >> 3; }

gd::Int CameraDirector::_half(gd::Int v) { return (v + (v < 0 ? 1 : 0)) >> 1; }

gd::Int CameraDirector::_sar(gd::Int v, gd::Int n) { return gd::shr(v < 0 ? v + gd::shl(1, n) - 1 : v, n); }

gd::Int CameraDirector::_settle(gd::Int s) {
    if (s < 0) return s - ((s + 0x1F) >> 5);
    return s > 0 ? s - (s >> 3) : 0;
}

}  // namespace t3
