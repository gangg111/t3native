// remake/core/ball/tekken_ball.gd
// Tekken Ball (volley.ovl, mode 7; modes.md#tekken-ball), written from the verified port
// tools/research/ball_sim.py: the ball object, its physics and touches (FUN_800B16A4 with the attack
// and body tests FUN_80048030 / FUN_800481AC), the render kind and squash (FUN_800B2F14), the
// fight-start, round and frame hooks FightFrame calls (FUN_800B0C90, FUN_800B0DD8, FUN_800B0E14),
// the move tuning after MoveStartAll (FUN_800B0B54), and the power gauges and popup texts.
#include "core/ball/tekken_ball.h"

#include "content/fight_tables.h"
#include "content/game_ram.h"
#include "content/move_row.h"
#include "core/camera/camera_math.h"
#include "core/camera/camera_shake.h"
#include "core/combat/attack_records.h"
#include "core/combat/combat.h"
#include "core/combat/hit_slot.h"
#include "core/effects/effect_objects.h"
#include "core/effects/effect_pool.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/pause_menu.h"
#include "core/fight/replay_state.h"
#include "core/fight/round_state.h"
#include "core/fight/rule_set.h"
#include "core/fight/vibration.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/game/byte_block.h"
#include "core/game/mode_region.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

TekkenBall::TekkenBall(const gd::Ref<FightState>& fight_state) {
    v = gd::make<ByteBlock>(SIZE);
    b = gd::make<ByteBlock>(BALL_SIZE);
    _fight = fight_state;
    t = fight_state->tables;
}

// The mode's overlay image: volley.ovl's initial data (the ball types, the sphere, the hit volume).
void TekkenBall::load_overlay(const gd::Ref<GameRam>& ram) {
    v->bytes = ram->bytes(BASE, SIZE);
}

// ==== byte access by game address

gd::Int TekkenBall::_v16(gd::Int a) {
    return v->s16(a - BASE);
}

gd::Int TekkenBall::_vu16(gd::Int a) {
    return v->u16(a - BASE);
}

gd::Int TekkenBall::_v32(gd::Int a) {
    return v->s32(a - BASE);
}

void TekkenBall::_vput16(gd::Int a, gd::Int x) {
    v->put16(a - BASE, x);
}

void TekkenBall::_vput32(gd::Int a, gd::Int x) {
    v->put32(a - BASE, x);
}

// 0x800B6ADE: the half-width of the court's neutral zone (the side lines).
gd::Int TekkenBall::neutral_zone() {
    return _vu16(NEUTRAL);
}

// 0x800B6B54: the ball's last hit was a charged one (HitApply takes the attacker's reaction).
bool TekkenBall::charged_hit() {
    return v->u8(CHARGED_HIT - BASE) != 0;
}

// 1 + the index of the player who lost the point this frame, 0 for none (MoveStep knocks that
// fighter down).
gd::Int TekkenBall::point_loser() {
    return _v32(POINT);
}

gd::Int TekkenBall::state() {
    return b->s16(B_STATE);
}

gd::PI32 TekkenBall::position() {
    return gd::PI32{b->s32(B_POS), b->s32(B_POS + 4), b->s32(B_POS + 8)};
}

// ==== hooks

// FUN_800B0B24: a player's move tuning cleared.
void TekkenBall::drift_clear(gd::Int player) {
    gd::Int r = DRIFT + 6 * player;
    _vput16(r, 0);
    for (gd::Int k = 2; k < 6; ++k) {
        v->put8(r + k - BASE, 0);
    }
}

// FUN_800B0C90 (the round start in mode 7): the ball type's constants, the ball, the players at
// x = ∓0x1400 facing each other, and two points per round.
void TekkenBall::fight_start(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<ModeRegion> region = fight()->region;
    gd::Int table = BALL_TABLE + 24 * region->ctx8(BALL_TYPE);
    _vput16(SPIKE_UNIT, _vu16(table));
    _vput16(SPEED_UNIT, _vu16(table + 4));
    _vput16(GRAVITY, _vu16(table + 8));
    _vput16(GAIN, _vu16(table + 0x10));
    _vput16(NEUTRAL, _vu16(table + 0x14));
    ball_init();
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<FighterState> f = fight()->fighters[i];
        f->pos_x = i == 0 ? -0x1400 : 0x1400;
        f->pos_z = 0;
        f->facing = i == 0 ? 0x4000 : 0xC000;
        sim->place(f, fight()->fighters[1 - i], false);
    }
    popups.clear();
    v->put32(POPUPS - BASE, 0);
    v->put32(POPUP_NEXT - BASE, 0);
    for (gd::Int i = 0; i < 8; ++i) {
        v->put8(POPUP_SLOTS + 0x20 * i + 0x1F - BASE, 0);
    }
    for (gd::Int g = 0; g < 2; ++g) {
        gd::Int r = GAUGES + 0x10 * g;
        _vput32(r, 0);
        for (gd::Int off : gd::Arr<gd::Int>{4, 6, 8}) {
            _vput16(r + off, 0);
        }
    }
    region->ctx_put32(LAST_POINT, 0);
    region->ctx_put32(RESULT, 2);
    v->put8(GLOWS - BASE, 0);
    _vput32(TOUCH_SOUND_WAIT, 0);
}

// FUN_800B0DD8: the court's matrix and a new ball (the select screen's preview).
void TekkenBall::round_reset() {
    _vput32(0x800B6AA8LL, 0);
    for (gd::Int i = 0; i < 0x20; i += 4) {
        _vput32(COURT + i, IDENTITY_WORDS.has(i) ? 0x1000 : 0);
    }
    ball_init();
}

// FUN_800B5B6C: the ball on the select screen: no side has chosen yet, the preview spins at the
// centre (render kind 10).
void TekkenBall::preview() {
    fight()->region->ctx_put8(CHOOSER, 0);
    round_reset();
    b->put32(B_GRAVITY, 0x220);
    b->put16(B_DIR, 1);
    b->put8(B_KIND, 10);
    b->put32(B_POS, 0);
    b->put32(B_POS + 4, 0);
    b->put32(B_POS + 8, 0);
    b->put32(B_VEL, 0);
}

// FUN_800B569C: the ball type chosen by the first side to finish (`pressed`: its pad): left /
// right spin the ball to the previous / next type (it slides out and back in), a face button
// takes the type once the ball rests (1), Select backs out (−1); 0 while choosing.
gd::Int TekkenBall::side_select(gd::Int pressed, const gd::Ref<SimEvents>& events) {
    gd::Ref<ModeRegion> region = fight()->region;
    region->ctx_put8(CHOOSER, 3);
    b->put16(B_ANGLES + 2, b->u16(B_ANGLES + 2) + 8);
    b->put16(B_ANGLES, b->u16(B_ANGLES) + 1);
    gd::Int x = b->s32(B_POS) + ((b->s32(B_VEL) - b->s32(B_POS)) >> 3);
    b->put32(B_POS, x);
    b->put16(B_ANGLES + 4, b->u16(B_ANGLES + 4) + 2);
    if (x < -0x7FF && b->s16(B_DIR) == -1) {
        gd::Int ty = region->ctx8(BALL_TYPE) + 1;
        region->ctx_put8(BALL_TYPE, ty > 2 ? 0 : ty);
        b->put32(B_POS, 0x800);
        b->put32(B_VEL, 0);
    }
    if (b->s32(B_POS) > 0x7FF && b->s16(B_DIR) == 1) {
        gd::Int ty = region->ctx8(BALL_TYPE);
        region->ctx_put8(BALL_TYPE, ty == 0 ? 2 : ty - 1);
        b->put32(B_POS, -0x800);
        b->put32(B_VEL, 0);
    }
    if ((pressed & PadState::LEFT) != 0) {
        b->put32(B_VEL, -0x1000);
        b->put16(B_DIR, -1);
        _sound(0x55F5, events);
    }
    if ((pressed & PadState::RIGHT) != 0) {
        b->put32(B_VEL, 0x1000);
        b->put16(B_DIR, 1);
        _sound(0x55F5, events);
    }
    if (gd::absi(b->s32(B_POS) - b->s32(B_VEL)) > 0x3F) {
        return 0;
    }
    gd::Int result = 0;
    if ((pressed & PadState::SELECT) != 0) {
        result = -1;
    }
    if ((pressed & PadState::FACE_BUTTONS) != 0) {
        _sound(0x50F4, events);
        result = 1;
    }
    return result;
}

// FUN_800B0FDC: the ball at the start of a round (and after a point).
void TekkenBall::ball_init() {
    b->put16(B_STATE, 0);
    b->put16(2, 0);
    b->put8(B_TIMER, fight()->rounds_played == 1 ? 100 : 40);
    b->put32(0x44, 0);
    for (gd::Int i = 0; i < 0x20; i += 4) {
        b->put32(0x48 + i, IDENTITY_WORDS.has(i) ? 0x1000 : 0);
    }
    gd::Int side = fight()->region->ctx8(SERVE_SIDE);
    b->put32(B_POS, side != 0 ? 0xD00 : -0xD00);
    b->put32(B_OWNER, FighterState::address(side));
    b->put32(B_POS + 4, -0x500);
    b->put16(B_SPIN, 0x20);
    b->put16(B_SPIN + 2, 0x10);
    b->put16(B_SPIN + 4, 4);
    for (gd::Int off : gd::Arr<gd::Int>{0x70, 0xA8, 0xAC, 0x7C, 0x80, 0x78, 0x90, 0x8C, 0x88}) {
        b->put32(off, 0);
    }
    for (gd::Int off : gd::Arr<gd::Int>{0x13C, 0x13E, 0x140, 0x9C, 0x9A, 0x98, 0xB0, 0xB2, 0x14C, 0x14A, 0x148, 0x150}) {
        b->put16(off, 0);
    }
    b->put8(B_LOCK, 0);
    b->put32(B_TARGET, FighterState::address(1 - side));
    b->put32(B_GRAVITY, _v16(GRAVITY));
    b->put8(B_REHITS, 0);
    b->put16(B_RADIUS, 0x260);
    b->put32(0xC8, 0x5A400);
    b->put32(0xB4, BALL_VERTICES);
    for (gd::Int i = 0; i < 3; ++i) {
        b->put32(B_CENTRE + 4 * i, b->u32(B_POS + 4 * i));
    }
    _hit_volume();
    _hit_volume();
    gd::Int zone = _vu16(NEUTRAL);
    gd::Int z = -0x1F40;
    for (gd::Int i = 0; i < 9; ++i) {
        // Untyped `[[CENTRE_BAND, 0x64], [SIDE_LINES, zone]]`: (base address, half-width) pairs.
        for (const gd::Arr<gd::Int>& pair : gd::Arr<gd::Arr<gd::Int>>{gd::Arr<gd::Int>{CENTRE_BAND, 0x64},
                                                                      gd::Arr<gd::Int>{SIDE_LINES, zone}}) {
            gd::Int base = pair[0];
            gd::Int x = pair[1];
            for (gd::Int k = 0; k < 2; ++k) {
                gd::Int at = base + 16 * i + 8 * k;
                _vput16(at, k == 0 ? -x : x);
                _vput16(at + 2, 0);
                _vput16(at + 4, z);
            }
        }
        z += 0x600;
    }
}

// FUN_800B0B54 (after MoveStartAll): the Tekken Ball tuning of the running move slot
// (curSlot − 0x18): slots 0x18/0x1A drift (0x8000, 30, 3, 9); 0x1E/0x20 (0x8000, 150, 5, 12,
// look-ahead from frame 18); 0x1F/0x21 only the look-ahead frame 18.
void TekkenBall::move_tuning(const gd::Ref<FighterState>& f) {
    gd::Int r = DRIFT + 6 * f->player_index;
    drift_clear(f->player_index);
    gd::Int k = Fx::s16(f->cur_slot - 0x18);
    if (k < 0 || k >= 10) {
        return;
    }
    switch (k) {
        case 0:
        case 2:
            _vput16(r, 0x8000);
            v->put8(r + 2 - BASE, 0x1E);
            v->put8(r + 3 - BASE, 3);
            v->put8(r + 4 - BASE, 9);
            break;
        case 6:
        case 8:
            _vput16(r, 0x8000);
            v->put8(r + 2 - BASE, 0x96);
            v->put8(r + 3 - BASE, 5);
            v->put8(r + 4 - BASE, 0xC);
            v->put8(r + 5 - BASE, 0x12);
            break;
        case 7:
        case 9:
            v->put8(r + 5 - BASE, 0x12);
            break;
        default:
            break;
    }
}

// A player's tuning record (the move system reads the look-ahead frame, the physics the drift).
gd::PI32 TekkenBall::drift(gd::Int player) {
    gd::Int r = DRIFT + 6 * player - BASE;
    return gd::PI32{v->u16(r), v->u8(r + 2), v->u8(r + 3), v->u8(r + 4), v->u8(r + 5)};
}

// FUN_800B0E14 (after MoveBranchAll, and on the frames the fight only draws or replays): the
// ball's physics unless the fight is frozen, then the gauges and popups.
void TekkenBall::frame(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> f_a = fight()->fighters[0];
    gd::Ref<FighterState> f_b = fight()->fighters[1];
    if (fight()->round_state == RoundState::FIGHT) {
        fight()->region->ctx_put32(RESULT, 2);
    }
    if (_v32(TOUCH_SOUND_WAIT) > 0) {
        _vput32(TOUCH_SOUND_WAIT, _v32(TOUCH_SOUND_WAIT) - 1);
    }
    if (fight()->freeze == 0 && fight()->paused_player == 0) {
        fight()->replay->ball(shared_from_this());
        if (fight()->replay_playing == 0) {
            update(sim, f_a, f_b, events);
            after_update();
        } else if ((b->u32(0x144) & 0xFFFF0000LL) == 0x05020000) {
            _sound(SOUND_BOUNCE, events);
        }
    }
    if (state() != 0) {
        _draw_effects();
    }
    bool hidden = fight()->pause_page == PauseMenu::PAGE_COMMAND && fight()->region->human_count != 1;
    if (!hidden) {
        gd::Int charge = b->s16(B_CHARGE);
        gd::Int highlight = charge != 0 ? gd::shl(1, FighterState::index_at(b->u32(B_OWNER))) : 0;
        _gauges(b->s16(B_POWER), 100, b->s16(B_POWER + 2), highlight);
        _popups_step(fight()->paused_player != 0 || fight()->freeze != 0);
    }
}

// ==== physics (FUN_800B16A4)

void TekkenBall::update(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f_a,
                        const gd::Ref<FighterState>& f_b, const gd::Ref<SimEvents>& events) {
    gd::Int keep = b->u8(B_LOCK) != 0 ? b->u8(B_REHIT) : 0;
    b->put8(B_REHIT, keep);
    v->put8(CHARGED_HIT - BASE, 0);
    _vput32(POINT, 0);
    gd::Int st = state();
    switch (st) {
        case 0: {
            gd::Int n = (b->u8(B_TIMER) - 1) & 0xFF;
            b->put8(B_TIMER, n);
            if (n == 0) {
                b->put16(B_STATE, 8);
                b->put8(B_TIMER, 0x20);
                _sound(SOUND_SERVE, events);
            }
            return;
        }
        case 1:
        case 8: {
            gd::Int n = (b->u8(B_TIMER) - 1) & 0xFF;
            b->put8(B_TIMER, n);
            if (n != 0) {
                return;
            }
            b->put16(B_STATE, st == 8 ? 9 : 2);
            b->put32(B_SPEED, 0);
            b->put8(B_LOCK, 0);
            b->put16(B_DIR, 0);
            b->put16(B_HOMING, 0);
            gd::Int x0 = gd::mini(f_a->root_x, 0);
            gd::Int x1 = gd::maxi(f_b->root_x, 0);
            gd::Int z0 = f_a->root_z;
            gd::Int dx = Fx::w32(x1 - x0);
            gd::Int dz = Fx::w32(f_b->root_z - z0);
            if (dx != 0) {
                b->put32(B_POS + 8, z0 + _div(Fx::w32((b->s32(B_POS) - x0) * dz), dx));
            } else {
                b->put32(B_POS, x0 + _div(0, dz));
            }
            return;
        }
        case 6: {
            b->put16(B_CHARGE, 0);
            b->put8(B_REHITS, 0);
            gd::Int n = (b->u8(B_TIMER) - 1) & 0xFF;
            b->put8(B_TIMER, n);
            if (n != 0) {
                return;
            }
            b->put16(B_STATE, 1);
            b->put8(B_TIMER, 0x20);
            b->put32(B_POS, 0);
            b->put32(B_POS + 4, -0xE40);
            for (gd::Int off : gd::Arr<gd::Int>{0x90, 0x88, 0x80, 0x7C, 0x78}) {
                b->put32(off, 0);
            }
            for (gd::Int off : gd::Arr<gd::Int>{0xA4, 0xA2, 0xA0}) {
                b->put16(off, 0);
            }
            b->put32(B_GRAVITY, _v16(GRAVITY) >> 1);
            b->put32(B_POS + 8, Fx::w32(f_a->root_z + f_b->root_z) >> 1);
            _hit_volume();
            _hit_volume();
            if (fight()->no_damage == 0) {
                _sound(SOUND_RESET, events);
            }
            return;
        }
        case 7: {
            b->put16(B_CHARGE, 0);
            b->put8(B_REHITS, 0);
            gd::Int n = (b->u8(B_TIMER) - 1) & 0xFF;
            b->put8(B_TIMER, n);
            if (n == 0) {
                b->put16(B_STATE, 6);
                b->put8(B_TIMER, 0x10);
            }
            return;
        }
        default:
            break;
    }
    // Bug #44 (not reproduced): the frames without touch tests use no hit mask.
    gd::Int mask = 0;
    gd::Int g = _v16(GRAVITY);
    gd::Int y = b->s32(B_POS + 4);
    if (st == 4 && y < SPIKE_CEILING) {
        b->put32(B_POS + 4, SPIKE_CEILING);
        b->put32(B_GRAVITY, g);
        b->put16(B_STATE, 2);
        b->put32(B_TARGET, b->u32(B_SPIKE_TARGET));
        b->put32(B_SPEED, b->s16(B_SPIKE_SPEED));
        b->put32(B_VEL + 4, Fx::div_trunc(b->s16(B_SPIKE_SPEED) * 2, 3) + 0x1000);
    } else if (st != 4 && y < CEILING) {
        b->put32(B_POS + 4, CEILING);
        b->put32(B_VEL + 4, 0);
        b->put32(B_GRAVITY, g);
    } else {
        mask = _floor_and_touches(sim, f_a, f_b, mask, events);
    }
    _tail(mask);
}

gd::Int TekkenBall::_floor_and_touches(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f_a,
                                       const gd::Ref<FighterState>& f_b, gd::Int mask,
                                       const gd::Ref<SimEvents>& events) {
    gd::Int g = _v16(GRAVITY);
    gd::Int x = b->s32(B_POS);
    if (b->s32(B_POS + 4) >= FLOOR_Y || gd::absi(x) > COURT_HALF) {
        b->put32(B_POS + 4, FLOOR_Y);
        b->put32(B_GRAVITY, g);
        gd::Int vy = b->s32(B_VEL + 4);
        if (vy > 0xD00) {
            _sound(SOUND_BOUNCE, events);
        }
        if (fight()->region->ctx8(BALL_TYPE) == 2) {
            if (vy > 0x3000) {
                CameraShake::start(fight(), 1, events);
            } else if (vy > 0x1800) {
                CameraShake::start(fight(), 0, events);
            }
        }
        x = b->s32(B_POS);
        if (gd::absi(x) <= _v16(NEUTRAL)) {
            b->put16(B_STATE, 2);
            b->put32(B_VEL + 4, Fx::w32(-b->s32(B_VEL + 4)) >> 1);
            for (gd::Int off : gd::Arr<gd::Int>{0xA0, 0xA2, 0xA4}) {
                b->put16(off, b->s16(off) >> 1);
            }
        } else {
            b->put32(B_VEL + 4, 0);
            b->put32(B_SPEED, 0);
            if (fight()->no_damage != 0 || state() == 5) {
                b->put16(B_STATE, 6);
                b->put8(B_TIMER, 0x10);
                return mask;
            }
            b->put16(x < 0 ? B_POWER : B_POWER + 2, 0);
            gd::Ref<FighterState> loser = x < 0 ? f_a : f_b;
            gd::Int hp = Fx::w32(loser->health - POINT_DAMAGE);
            loser->health = hp;
            loser->last_attacker = (loser->index + 1) & 1;
            if (hp < 0) {
                loser->health = 0;
            }
            b->put16(B_STATE, 7);
            b->put8(B_TIMER, 5);
            _vput32(POINT, fight()->throw_count != 0 ? 0 : loser->index + 1);
            gd::Ref<FighterState> third = fight()->fighters[THIRD];
            third->root_x = b->s32(B_POS);
            third->root_z = b->s32(B_POS + 8);
            return mask;
        }
    }
    if (b->s32(B_POS + 4) < -0x189) {
        _vput32(IDLE, 0);
    } else {
        gd::Int n = _v32(IDLE) + 1;
        _vput32(IDLE, n);
        if (n >= IDLE_FRAMES) {
            b->put16(B_STATE, 6);
            b->put8(B_TIMER, 0x10);
            return mask;
        }
    }
    if (b->s16(B_HOMING) == 0) {
        b->put32(B_SPEED, gd::maxi(Fx::w32(b->s32(B_SPEED) - 0x20), 0));
    }
    if (b->u8(B_LOCK) != 0) {
        b->put8(B_LOCK, b->u8(B_LOCK) - 1);
        return mask;
    }
    return _touches(sim, f_a, f_b, events);
}

void TekkenBall::_tail(gd::Int mask) {
    gd::Int angle = b->u16(B_DIR) & 0xFFF;
    gd::Int cos = FightMath::cos12(angle, t);
    gd::Int sin = FightMath::sin12(angle, t);
    gd::Int speed = b->s32(B_SPEED);
    b->put32(B_VEL, Fx::w32(cos * speed) >> 12);
    b->put32(B_VEL + 8, Fx::w32(sin * speed) >> 12);
    if (state() == 9) {
        // The serve: the ball bobs until someone touches it or 600 frames pass.
        gd::Int n = b->u8(B_TIMER);
        gd::Int s = FightMath::sin12(((n << 6) & 0x1FC0) >> 1, t) & 0xFFFF;
        b->put8(B_TIMER, n + 1);
        b->put32(B_POS + 4, (Fx::s16(s) >> 4) - 0x500);
        if (fight()->round_frame >= 600) {
            b->put16(B_STATE, 2);
            b->put32(B_VEL + 4, 0);
        }
    } else {
        for (gd::Int i = 0; i < 3; ++i) {
            b->put32(B_POS + 4 * i, b->s32(B_POS + 4 * i) + (b->s32(B_VEL + 4 * i) >> 8));
        }
    }
    _hit_volume();
    b->put32(B_VEL + 4, b->s32(B_VEL + 4) + b->s32(B_GRAVITY));
    if (mask != 0 && state() != 3) {
        angle = b->u16(B_DIR) & 0xFFF;
        cos = FightMath::cos12(angle, t);
        sin = FightMath::sin12(angle, t);
        speed = b->s32(B_SPEED);
        gd::Int spin = gd::shr(Fx::w32(cos * speed), b->s16(B_HOMING) != 0 ? 0x11 : 0x13);
        b->put16(B_SPIN + 4, spin);
        b->put16(B_SPIN, Fx::s16(spin) >> 2);
        b->put16(B_SPIN + 2, Fx::w32(sin * speed) >> 0x13);
    }
    for (gd::Int i = 0; i < 3; ++i) {
        b->put16(B_ANGLES + 2 * i, b->u16(B_ANGLES + 2 * i) + b->u16(B_SPIN + 2 * i));
    }
    gd::Int z_old = b->s32(B_POS + 8);
    gd::Int mid = Fx::w32(fight()->fighters[0]->root_z + fight()->fighters[1]->root_z) >> 1;
    if (Fx::w32(z_old - mid) > 0x80) {
        b->put32(B_POS + 8, mid + 0x80);
    }
    if (Fx::w32(b->s32(B_POS + 8) - mid) < -0x80) {
        b->put32(B_POS + 8, mid - 0x80);
    }
    b->put16(B_ANGLES, b->u16(B_ANGLES) + b->u16(B_POS + 8) - (z_old & 0xFFFF));
    gd::Ref<FighterState> target = fight()->fighters[FighterState::index_at(b->u32(B_TARGET))];
    gd::Int tx = fight()->fighters[target->index]->root_x;
    gd::Int tz = fight()->fighters[target->index]->root_z;
    gd::Int a = b->s16(B_DIR);
    if (a < -0x800) {
        b->put16(B_DIR, a + 0x1000);
    }
    if (b->s16(B_DIR) > 0x800) {
        b->put16(B_DIR, b->u16(B_DIR) - 0x1000);
    }
    if (b->s16(B_HOMING) == 0) {
        return;
    }
    b->put16(B_DIR, _atan2(tx - b->s32(B_POS), tz - b->s32(B_POS + 8)));
    if (b->s32(B_SPEED) <= 0x1000) {
        return;
    }
    gd::Int dx = Fx::w32(b->s32(B_POS) - tx);
    gd::Int dz = Fx::w32(b->s32(B_POS + 8) - tz);
    gd::Int dist = CameraMath::isqrt(Fx::w32(Fx::w32(dx * dx) + Fx::w32(dz * dz)) & 0xFFFFFFFFLL);
    gd::Int n = _div(dist, b->s32(B_SPEED) >> 8);
    gd::Int vy = b->s32(B_VEL + 4);
    gd::Int head = target->body->joints[1]->t[1];
    gd::Int fall = Fx::w32(vy + Fx::w32(n * b->s32(B_GRAVITY))) >> 8;
    gd::Int miss = Fx::w32(Fx::w32(b->s32(B_POS + 4) + fall - head) << 8);
    gd::Int q = _div(Fx::w32(vy - miss), n);
    b->put32(B_VEL + 4, Fx::w32(Fx::w32(vy * 13 + q * 3) << 8) >> 12);
}

// The four hit points sweep from their last position to the ball's position + offset.
void TekkenBall::_hit_volume() {
    for (gd::Int j = 0; j < 4; ++j) {
        gd::Int s = B_POINTS + 0x18 * j;
        for (gd::Int i = 0; i < 3; ++i) {
            b->put32(s + 4 * i, b->u32(s + 0xC + 4 * i));
        }
        for (gd::Int i = 0; i < 3; ++i) {
            b->put32(s + 0xC + 4 * i, b->s32(B_POS + 4 * i) + _v16(HIT_VOLUME + 8 * j + 2 * i));
        }
    }
}

// ==== touches

// The attack and body tests of a flying ball; returns the hit mask.
gd::Int TekkenBall::_touches(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f_a,
                             const gd::Ref<FighterState>& f_b, const gd::Ref<SimEvents>& events) {
    (void)sim;
    for (gd::Int i = 0; i < 3; ++i) {
        b->put32(B_CENTRE + 4 * i, b->u32(B_POS + 4 * i));
    }
    gd::PI32 d{0, 0, 0};
    gd::Int mask = _attack_touch(f_a, f_b, d);
    if ((mask & 7) != 0) {
        gd::Ref<FighterState> hitter = (mask & 2) != 0 ? f_a : f_b;
        gd::Ref<FighterState> other = (mask & 2) != 0 ? f_b : f_a;
        if (hitter->hit_done[2] == 0) {
            _attack_hit(hitter, other, mask, d, events);
        }
        return mask;
    }
    mask = 0;
    if (f_a->in_throw == 0 && f_b->in_throw == 0) {
        mask = _body_touch(f_a, f_b);
    }
    if (fight()->round_state > RoundState::WIN_POSES) {
        if (f_b->active == 0) {
            mask &= 2;
        } else if (f_a->active == 0) {
            mask &= 1;
        }
    }
    if (mask == 0) {
        return 0;
    }
    gd::Ref<FighterState> hitter = (mask & 2) != 0 ? f_a : f_b;
    gd::Ref<FighterState> other = (mask & 2) != 0 ? f_b : f_a;
    gd::Int owner = b->u32(B_OWNER);
    gd::Int charge = b->s16(B_CHARGE);
    if (FighterState::address(hitter->index) == owner && charge != 0) {
        return mask;
    }
    b->put32(B_SPEED + 4, 0);
    b->put16(B_HOMING, 0);
    if (state() != 3) {
        b->put16(B_STATE, 2);
    }
    b->put16(B_DIR, _atan2(other->root_x - b->s32(B_POS), other->root_z - b->s32(B_POS + 8)));
    gd::Int lob = gd::Int(fight()->fighter_distance) & 0xFFFFFFFFLL;
    gd::Int g = _v16(GRAVITY);
    gd::Int k = Fx::w32((lob >> 7) + 12);
    b->put32(B_GRAVITY, g);
    b->put32(B_VEL + 4, Fx::w32((Fx::w32(-g * k) >> 1) - Fx::w32(b->s32(B_POS + 4) + 0x1000)));
    gd::Int dist = lob > 0x800 ? Fx::w32(lob - 0x800) : 0x400;
    b->put32(B_SPEED, _div(Fx::w32(dist << 8), k));
    if (charge == 0 || owner == FighterState::address(hitter->index)) {
        if (_v32(TOUCH_SOUND_WAIT) == 0) {
            _sound(SOUND_TOUCH, events);
            _vput32(TOUCH_SOUND_WAIT, 10);
        }
        b->put32(B_OWNER, FighterState::address(hitter->index));
        b->put32(B_TARGET, FighterState::address(other->index));
    } else {
        _charged_touch(hitter, other, events);
    }
    return mask;
}

// FUN_80048030: the fighters' attack segments against the ball's cylinder; bit 2 for `f_a`, bit 1
// for `f_b`, bit 8 when a move uses projectile segments. The first hitting segment's direction
// goes to `d`.
gd::Int TekkenBall::_attack_touch(const gd::Ref<FighterState>& f_a, const gd::Ref<FighterState>& f_b,
                                  const gd::PI32& d) {
    gd::PI32 cyl{b->s32(B_CENTRE), b->s32(B_CENTRE + 4), b->s32(B_CENTRE + 8),
                 b->s32(B_RADIUS) & 0xFFFF, b->s32(0xC8)};
    gd::Int mask = 0;
    gd::Int bit = 2;
    for (const gd::Ref<FighterState>& fighter : gd::Arr<gd::Ref<FighterState>>{f_a, f_b}) {
        gd::Arr<gd::PI32> segments = fighter->attack_segs;
        gd::Int count = gd::mini(fighter->active_segs, 4);
        if (gd::Int(AttackRecords::header(fighter->pose_move, t)[0]) >= Combat::PROJECTILE_JOINT) {
            mask |= 8;
            segments = fight()->effects->segments[gd::mini(fighter->index, 1)];
            count = segments.size();
        }
        for (gd::Int k = 0; k < gd::maxi(count, 0); ++k) {
            gd::PI32 s = segments[k];
            if (FightMath::segment_hits_cylinder(s, cyl, fight()->rules->fix_segment_hit_overflow)) {
                for (gd::Int i = 0; i < 3; ++i) {
                    d[i] = Fx::w32(s[3 + i] - s[i]);
                }
                mask |= bit;
                break;
            }
        }
        bit = 1;
    }
    return mask;
}

// FUN_800481AC: the ball's four swept hit points against each fighter's 14 hurt cylinders; the
// first contact goes to the fighter's first hit slot. Bit 2 for `f_a`, bit 1 for `f_b`.
gd::Int TekkenBall::_body_touch(const gd::Ref<FighterState>& f_a, const gd::Ref<FighterState>& f_b) {
    gd::Int mask = 0;
    gd::Int bit = 2;
    for (const gd::Ref<FighterState>& fighter : gd::Arr<gd::Ref<FighterState>>{f_a, f_b}) {
        bool done = false;
        for (gd::Int k = 0; k < 14; ++k) {
            gd::PI32 c = fighter->hurt_zones[k];
            if (Fx::s16(c[3]) == 0) {
                continue;
            }
            for (gd::Int j = 0; j < 4; ++j) {
                gd::Int at = B_POINTS + 0x18 * j;
                gd::PI32 s;
                for (gd::Int i = 0; i < 6; ++i) {
                    s.append(b->s32(at + 4 * i));
                }
                if (FightMath::segment_hits_cylinder(s, c, fight()->rules->fix_segment_hit_overflow)) {
                    gd::Ref<HitSlot> slot = fighter->hit_slots[0];
                    slot->zone = k;
                    for (gd::Int i = 0; i < 3; ++i) {
                        slot->point[i] = gd::Int(s[3 + i]);
                        slot->direction[i] = Fx::s16(s[3 + i] - s[i]);
                    }
                    mask |= bit;
                    done = true;
                    break;
                }
            }
            if (done) {
                break;
            }
        }
        bit = 1;
    }
    return mask;
}

// The shared part of a charging hit: re-hit bookkeeping, the hitter's power gain (damage · gain
// / 100), the charge and the damage popup; returns the speed steps clamped to 20..cap.
gd::Int TekkenBall::_charge_hit(const gd::Ref<FighterState>& hitter, gd::Int base_add, gd::Int cap) {
    if (b->s16(B_CHARGE) != 0) {
        b->put8(B_REHIT, 1);
        b->put8(B_REHITS, b->u8(B_REHITS) + 1);
    }
    gd::Int add = Fx::div_trunc(Fx::s16(hitter->damage) * _v16(GAIN), 100);
    gd::Int slot = B_POWER + 2 * hitter->player_index;
    gd::Int power = (b->u16(slot) + add) & 0xFFFF;
    b->put16(slot, power);
    b->put16(B_CHARGE, power);
    b->put16(B_HOMING, 1);
    gd::Int colour = FighterState::index_at(b->u32(B_OWNER)) == 0 ? 2 : 5;
    _popup_add(gd::str(add), colour, 1, 0x1E);
    gd::Int steps = Fx::s16((hitter->damage & 0xFFFF) + base_add);
    if (steps >= cap + 1) {
        steps = cap;
    } else if (steps < 0x14) {
        steps = 0x14;
    }
    return steps;
}

void TekkenBall::_speed_from_steps(gd::Int steps) {
    gd::Int speed = (steps + b->u8(B_REHITS)) * _v16(SPIKE_UNIT);
    if (b->u8(B_REHIT) != 0) {
        speed += b->s32(B_SPEED) >> 4;
    }
    b->put32(B_SPEED, speed);
}

// A fighter's attack hits the ball: the ball takes the hitter's record as its attacker and flies
// by the attack level.
void TekkenBall::_attack_hit(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, gd::Int mask,
                             const gd::PI32& d, const gd::Ref<SimEvents>& events) {
    _take_ball(hitter, other, d, events);
    gd::Int g = _v16(GRAVITY);
    gd::Int unit = _v16(SPEED_UNIT);
    gd::Int x = b->s32(B_POS);
    gd::Int level = hitter->attack & 0xFFFF;
    gd::Int damage = Fx::s16(hitter->damage);
    gd::Int lob = fight()->fighter_distance;
    if ((mask & 3) == 3) {
        // Both players at once: the ball pops up neutrally.
        b->put16(B_CHARGE, 0);
        b->put32(B_SPEED, 0);
        b->put16(B_STATE, 2);
        b->put32(B_VEL + 4, -0x25 * g);
        b->put32(B_GRAVITY, g);
    } else if (hitter->bank_type == BankType::YOSHIMITSU && UNBLOCKABLE.has(level) && (mask & 8) == 0
               && hitter->cur_slot != 0x181) {
        _yoshimitsu_unblockable(hitter, other);
    } else if (damage == 0) {
        _set_hit(damage, g, unit, x, events);
    } else if (UNBLOCKABLE.has(level)) {
        _homing_hit(hitter, other, lob);
    } else if (hitter->in_air == 0) {
        _ground_hit(hitter, other, level, damage, g, unit, x, lob, events);
    } else {
        // The hitter is airborne.
        _speed_from_steps(_charge_hit(hitter, 0xF, 0x28));
        b->put16(B_STATE, 2);
        b->put32(B_GRAVITY, _v16(GRAVITY) >> 1);
        b->put32(B_VEL + 4, Fx::w32(other->root_z - b->s32(B_POS + 4) - lob));
    }
}

// The hitter takes the ball: the third record carries its move, the ball turns towards the
// other player and squashes by the damage.
void TekkenBall::_take_ball(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, const gd::PI32& d,
                            const gd::Ref<SimEvents>& events) {
    Vibration::fighter(fight(), hitter->player_index, 5, events);
    b->put32(B_SPEED + 4, 0);
    b->put32(B_OWNER, FighterState::address(hitter->index));
    b->put32(B_TARGET, FighterState::address(other->index));
    b->put16(B_HOMING, 0);
    gd::Ref<FighterState> third = fight()->fighters[THIRD];
    third->copy_from(hitter);
    third->index = THIRD;
    third->power_timer = 1;
    third->attack = 0xFFFF;
    third->about_to_hit = 1;
    hitter->hit_done[2] = 1;
    hitter->contact_this_move = 1;
    hitter->contact = 1;
    hitter->last_hit_target = 2;
    b->put8(B_LOCK, 5);
    b->put16(B_DIR, _atan2(other->root_x - b->s32(B_POS), other->root_z - b->s32(B_POS + 8)));
    _squash_axis(d);
    b->put8(B_SQUASHING, 1);
    b->put16(B_SQUASH, (hitter->damage & 0xFFFF) * 8 + 0x140);
}

// Yoshimitsu's unblockable: his own health pays the opponent's stored power, and the point ends.
void TekkenBall::_yoshimitsu_unblockable(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other) {
    gd::Int hp = Fx::w32(hitter->health - (b->s16(B_POWER + 2 * other->index) << 16));
    hitter->health = gd::maxi(hp, 0);
    b->put16(B_POWER + 2, 0);
    b->put16(B_POWER, 0);
    b->put16(B_STATE, 7);
    b->put8(B_TIMER, 5);
    _vput32(POINT, hitter->index + 1);
}

// A move without damage sets a charged ball up, or hits it flat.
void TekkenBall::_set_hit(gd::Int damage, gd::Int g, gd::Int unit, gd::Int x, const gd::Ref<SimEvents>& events) {
    if (b->s16(B_CHARGE) > 0) {
        b->put16(B_STATE, 3);
        b->put32(B_VEL + 4, -0x25 * g);
        b->put32(B_GRAVITY, g);
        b->put16(B_SPIKE_SPEED, b->u16(B_SPEED) + _vu16(SPIKE_UNIT) * 5);
        gd::Int quarter = b->s32(B_SPEED) >> 2;
        gd::Int s = Fx::w32((damage + 8) * unit);
        s = Fx::w32((x >= 0 ? s + x : s - x) - quarter);
        b->put32(B_SPEED, s >= 0 ? s : 0);
        b->put16(B_CHARGE, 0);
        b->put8(B_LOCK, b->u8(B_LOCK) + 2);
        _popup_add(EXCLAIM, 1, 1, 0x1E);
        _sound(SOUND_SET, events);
    } else {
        _flat(10, damage, g, unit, x);
    }
}

// Unblockable: the ball homes in with the hitter's power as its charge.
void TekkenBall::_homing_hit(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, gd::Int lob) {
    b->put16(B_HOMING, 1);
    b->put32(B_SPEED, _v16(SPIKE_UNIT) * 60);
    b->put32(B_GRAVITY, _v16(GRAVITY) >> 2);
    b->put16(B_CHARGE, b->u16(B_POWER + 2 * hitter->player_index));
    b->put16(B_STATE, 2);
    b->put32(B_VEL + 4, Fx::w32(other->root_z - 2 * b->s32(B_POS + 4) - lob));
    _popup_add(EXCLAIM, 2, 1, 0x1E);
}

// A grounded hit by level: mids lob it charged, lows shoot it low, highs hit it flat or spike a
// set ball.
void TekkenBall::_ground_hit(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, gd::Int level,
                             gd::Int damage, gd::Int g, gd::Int unit, gd::Int x, gd::Int lob,
                             const gd::Ref<SimEvents>& events) {
    if (MIDS.has(level)) {
        // Mids: a charged lob.
        _speed_from_steps(_charge_hit(hitter, 0x12, 0x2E));
        b->put32(B_GRAVITY, _v16(GRAVITY) >> 2);
        b->put16(B_STATE, 2);
        b->put32(B_VEL + 4, Fx::w32(other->root_z - 2 * b->s32(B_POS + 4) - lob));
    } else if (LOWS.has(level)) {
        b->put32(B_VEL + 4, -0x32 * g);
        b->put32(B_GRAVITY, g);
        b->put32(B_SPEED, Fx::w32((Fx::s16(hitter->damage & 0xFFFF) >> 2) * unit + gd::absi(x)));
        b->put16(B_CHARGE, 0);
        b->put16(B_STATE, 2);
    } else if (level == HIGH) {
        if (state() != 3) {
            _flat(10, damage, g, unit, x);
        } else {
            // A spike.
            b->put16(B_STATE, 4);
            b->put32(B_SPIKE_TARGET, FighterState::address(other->index));
            b->put32(B_VEL + 4, -0x48 * g);
            b->put32(B_GRAVITY, g);
            b->put32(B_SPEED, unit * 10);
            _sound(SOUND_SPIKE, events);
            _charge_hit(hitter, 0, 0x7FFF);
        }
    }
}

// A flat, fast hit (highs, sets without a charge).
void TekkenBall::_flat(gd::Int extra, gd::Int damage, gd::Int g, gd::Int unit, gd::Int x) {
    b->put32(B_GRAVITY, g);
    b->put32(B_VEL + 4, -0x25 * g);
    gd::Int quarter = b->s32(B_SPEED) >> 2;
    gd::Int s = Fx::w32((damage + extra) * unit);
    s = x < 0 ? s - x : s + x;
    s = Fx::w32(s - quarter);
    b->put32(B_SPEED, s >= 0 ? s : 0);
    b->put16(B_CHARGE, 0);
    b->put16(B_STATE, 2);
}

// The charged ball reaches the opponent: the resident hit system with the third record as the
// attacker, whose damage is the charge.
void TekkenBall::_charged_touch(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other,
                                const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> third = fight()->fighters[THIRD];
    gd::Int charge = b->u16(B_CHARGE);
    third->damage = charge;
    b->put32(B_OWNER, FighterState::address(hitter->index));
    b->put32(B_TARGET, FighterState::address(other->index));
    b->put16(B_STATE, 2);
    v->put8(CHARGED_HIT - BASE, 1);
    b->put16(B_CHARGE, 0);
    third->hit_done[other->index] = 1;
    third->contact_this_move = 1;
    third->contact = 1;
    third->last_hit_target = other->index;
    hitter->was_hit_this_move = 1;
    hitter->got_hit = 1;
    hitter->hit_freeze_in = third->pose_move->hit_freeze;
    hitter->hit_slots[0]->used = 1;
    hitter->hit_slots[0]->attacker = THIRD;
    gd::Int c = Fx::s16(third->damage);
    if (c != 0 || hitter->in_air != 0) {
        hitter->hit_cooldown = 4;
    }
    bool lethal = c > 99 || b->u8(B_KIND) == 9;
    if ((hitter->guard & third->attack) == 0) {
        if (lethal) {
            EffectObjects::burn(fight(), hitter->index, events);
            _log_burn(hitter);
            if (fight()->region->ctx8(BALL_TYPE) == 2) {
                fight()->region->ctx_put32(RESULT, 6);
            }
        }
        gd::Int g = _vu16(GRAVITY);
        b->put32(B_SPEED, 0);
        b->put8(B_REHITS, 0);
        b->put16(B_POWER, 0);
        b->put16(B_POWER + 2, 0);
        b->put16(B_STATE, 5);
        b->put32(B_OWNER, FighterState::address(hitter->index));
        b->put32(B_TARGET, FighterState::address(hitter->index));
        b->put8(B_LOCK, 5);
        b->put32(B_VEL + 4, Fx::s16(g) * -0x25);
        b->put32(B_GRAVITY, Fx::s16(g) >> 1);
        _sound(c < 0x32 ? SOUND_CHARGED : SOUND_SPIKE, events);
    } else {
        _sound(SOUND_GUARDED, events);
        if (lethal && hitter->health <= Fx::w32(c << 13)) {
            EffectObjects::burn(fight(), hitter->index, events);
            _log_burn(hitter);
        }
    }
}

// The replay's record of a lethal charged hit (FUN_8004A840(player, 0x82, buffer, 1)): a dust
// ring when the replay plays it. Bug #45 (not reproduced): the game copies the point from a stack
// buffer it never fills; here it is the burned fighter's position, as a fall's dust records.
void TekkenBall::_log_burn(const gd::Ref<FighterState>& hitter) {
    gd::PI32 point{hitter->root_x, hitter->root_y, hitter->root_z};
    fight()->replay->log_effect(hitter->index, 0x82, point, gd::PI32{0, 0, 0}, 1);
}

// volley.ovl FUN_800B3118: the squash axis angles from the hit direction.
void TekkenBall::_squash_axis(const gd::PI32& d) {
    b->put16(B_SQUASH_AXIS, 0);
    b->put16(B_SQUASH_AXIS + 2, _atan2(d[0], d[2]));
    gd::Int a = Fx::s16(_atan2(d[0], d[1]));
    b->put16(B_SQUASH_AXIS + 4, d[0] > 0 ? a : -a);
}

// ==== render kind and glow (FUN_800B2F14)

void TekkenBall::after_update() {
    gd::Int s = b->s16(B_SQUASH);
    bool relax = true;
    if (b->s16(B_HOMING) != 0) {
        if (s < 0x80) {
            relax = false;
            b->put8(B_SQUASHING, 0);
            _squash_axis(gd::PI32{b->s32(B_VEL), b->s32(B_VEL + 4), b->s32(B_VEL + 8)});
            b->put16(B_SQUASH_AXIS + 4, b->u16(B_SQUASH_AXIS + 4) + 0x400);
            gd::Int sq = Fx::div_trunc(b->s16(B_SQUASH) * 2, 3) + Fx::div_trunc(b->s32(B_SPEED) >> 6, 3);
            b->put16(B_SQUASH, sq);
        } else if (b->u8(B_SQUASHING) == 0) {
            relax = false;
        }
    }
    if (relax) {
        s = b->s16(B_SQUASH);
        b->put16(B_SQUASH, b->u16(B_SQUASH) - ((s + 0x10) >> 4));
    }
    gd::Int st = state();
    gd::Int kind = 0;
    if (st == 6) {
        kind = 1;
    } else if (st == 1 || st == 8) {
        b->put8(B_KIND, 0);
        return;
    } else if (st == 7) {
        kind = 2;
    } else {
        gd::Int charge = b->s16(B_CHARGE);
        if (charge == 0) {
            if (b->u8(B_LOCK) != 0) {
                kind = 5;
            } else {
                b->put8(B_KIND, st == 3 ? 4 : 3);
                return;
            }
        } else {
            gd::Int who = FighterState::index_at(b->u32(B_OWNER));
            b->put8(B_TIMER, who);
            if (charge < 100) {
                if (b->u8(B_LOCK) == 5) {
                    glow(who, 0);
                }
                b->put8(B_KIND, b->u8(B_TIMER) != 0 ? 7 : 6);
            } else {
                if (b->u8(B_LOCK) == 5) {
                    glow(who, 1);
                }
                b->put8(B_KIND, 9);
            }
            if (b->u8(B_LOCK) == 0) {
                return;
            }
            kind = b->u8(B_REHIT) != 0 ? 8 : 5;
        }
    }
    b->put8(B_KIND, kind);
}

// FUN_800B4720: once per player and size, effect type 15 (the charged ball's glow).
void TekkenBall::glow(gd::Int player, gd::Int big) {
    gd::Int u = player + 2 * big;
    gd::Int bit = gd::shl(1, u & 0x1F);
    if ((v->u8(GLOWS - BASE) & bit) != 0) {
        return;
    }
    gd::Ref<EffectObjects::Obj> o = EffectObjects::alloc(fight(), 15);
    if (o == nullptr) {
        return;
    }
    o->a = u & 1;
    o->d = (u & 0xFF) >> 1;
    v->put8(GLOWS - BASE, v->u8(GLOWS - BASE) | bit);
}

// ==== what the drawing does to the state (FUN_800B3198)

// The ball's drawing (kind 2, a point) spends rand() on its streaks' jitter while the fight runs,
// and at the point's strength 5 spawns 16 sparks (FUN_800B47B4) around the ball. The drawing
// itself is the presentation's.
void TekkenBall::_draw_effects() {
    if (b->u8(B_KIND) != 2 || fight()->paused_player != 0) {
        return;
    }
    if (b->u8(B_TIMER) == 5) {
        _sparks(b->s32(B_POS + 4) < FLOOR_Y);
    }
    for (gd::Int i = 0; i < 4; ++i) {
        streaks[i] = (fight()->rng->next() >> 4) - 0x400;
    }
}

// FUN_800B47B4: 16 sparks (effect type 1) within ±255 of the ball; above the floor they fly out
// with a random side, on it they take the ball's velocity halfwords.
void TekkenBall::_sparks(bool high) {
    for (gd::Int n = 0; n < 16; ++n) {
        gd::Int r1 = fight()->rng->next();
        gd::Int u = ((r1 << 16) + fight()->rng->next()) & 0xFFFFFFFFLL;
        gd::Ref<EffectObjects::Obj> o = EffectObjects::alloc(fight(), 1);
        if (o == nullptr) {
            return;
        }
        o->x = Fx::w32(b->s32(B_POS) - 0xFF + (u >> 23));
        o->y = Fx::w32(b->s32(B_POS + 4) - 0xFF + ((Fx::w32(u) >> 14) & 0x1FF));
        o->z = Fx::w32(b->s32(B_POS + 8) - 0xFF + ((Fx::w32(u) >> 5) & 0x1FF));
        if (!high) {
            o->pitch = 0x400;
            o->yaw = Fx::s16((fight()->rng->next() - 0x800) & 0x100);
        } else {
            o->pitch = Fx::s16(b->u16(B_VEL));
            o->yaw = Fx::s16(b->u16(B_VEL + 4));
        }
    }
}

// ==== gauges and popups

// FUN_800B4E6C's state: the fill moves 3 pixels per frame towards power · 76 / maximum; a lighter
// trail follows a drop by a sixteenth of the gap per frame. (The drawing is the presentation's.)
void TekkenBall::_gauges(gd::Int p1, gd::Int maximum, gd::Int p2, gd::Int highlight) {
    for (gd::Int g = 0; g < 2; ++g) {
        gd::Int r = GAUGES + 0x10 * g;
        gd::Int power = (g == 0 ? p1 : p2) & 0xFFFFFFFFLL;
        gd::Int target = _v16(r + 4);
        gd::Int fill = _v16(r + 6);
        gd::Int trail = _v16(r + 8);
        if (v->u32(r - BASE) != power) {
            _vput32(r, power);
            target = Fx::div_trunc(Fx::w32(Fx::w32(power) * 76), maximum);
        }
        target = gd::mini(target, 0x4C);
        trail = fill < trail ? trail - ((trail - fill + 15) >> 4) : fill;
        fill = fill < target ? gd::mini(fill + 3, target) : target;
        if (trail < fill) {
            trail = fill;
        }
        _vput16(r + 4, target);
        _vput16(r + 6, fill);
        _vput16(r + 8, trail);
    }
    gauge_highlight = highlight;
}

// A gauge's state for the presentation: fill, trail (pixels of 76), grows rightwards, x, y.
gd::PI32 TekkenBall::gauge(gd::Int g) {
    gd::Int r = GAUGES + 0x10 * g;
    return gd::PI32{_v16(r + 6), _v16(r + 8), _vu16(r + 0xA), _vu16(r + 0xC), _vu16(r + 0xE)};
}

// FUN_800B445C: a popup text over the ball in the next of eight slots (skipped while that slot
// is still showing), at the head of the list.
void TekkenBall::_popup_add(const std::string& text, gd::Int colour, gd::Int font, gd::Int frames) {
    gd::Int i = _v32(POPUP_NEXT) + 1;
    i -= ((i < 0 ? i + 7 : i) >> 3) << 3;
    _vput32(POPUP_NEXT, i);
    gd::Int node = POPUP_SLOTS + 0x20 * i;
    if (v->u8(node + 0x1F - BASE) != 0) {
        return;
    }
    v->put8(node + 0x1C - BASE, colour);
    v->put8(node + 0x1D - BASE, font);
    v->put8(node + 0x1E - BASE, frames);
    v->put8(node + 0x1F - BASE, 1);
    gd::Ref<BallPopup> p = gd::make<BallPopup>();
    p->text = text;
    p->colour = colour;
    p->font = font;
    p->frames = frames;
    p->point = position();
    p->view = view;
    popups.push_front(p);
    _slots.insert(0, i);
}

// FUN_800B4350's state: each popup rises a pixel per frame and leaves when its frames run out;
// while the fight is frozen they only show.
void TekkenBall::_popups_step(bool frozen) {
    gd::Int i = 0;
    while (i < popups.size()) {
        gd::Ref<BallPopup> p = popups[i];
        gd::Int node = POPUP_SLOTS + 0x20 * _slots[i];
        if (!frozen && p->frames == 0) {
            v->put8(node + 0x1F - BASE, 0);
            popups.remove_at(i);
            _slots.remove_at(i);
            continue;
        }
        if (!frozen) {
            p->rise += 1;
            p->frames -= 1;
            v->put8(node + 0x1E - BASE, p->frames);
        }
        i += 1;
    }
}

// ==== helpers

void TekkenBall::_sound(gd::Int code, const gd::Ref<SimEvents>& events) {
    events->add(SimEvents::Kind::SOUND, 0, code, 0);
}

// C division with the R3000's quotient for a zero divisor.
gd::Int TekkenBall::_div(gd::Int a, gd::Int d) {
    a = Fx::w32(a);
    d = Fx::w32(d);
    if (d == 0) {
        return a >= 0 ? -1 : 1;
    }
    return Fx::div_trunc(a, d);
}

// FUN_8004B634: the angle of (x, z) in 4096 units.
gd::Int TekkenBall::_atan2(gd::Int x, gd::Int z) {
    return CameraMath::atan2_units4096(x, z, t->camera) & 0xFFFF;
}

}  // namespace t3
