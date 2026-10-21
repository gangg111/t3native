// remake/core/force/tekken_force.gd
// Tekken Force (force.ovl, mode 8; modes.md#tekken-force), written from the verified ports in
// tools/research/force_sim.py and the decompiled overlay: the level runner FUN_800B1778 (the level
// script, the spawn patterns, the two enemy slots, the items, the score, timer and countdown,
// the end of a level with its tally), who fights whom (FUN_800B5EC0, FUN_800B598C, the hidden
// manual switch), the side-scrolling camera (FUN_800B545C, FUN_800B5504) with the camera's
// view-space x the walls follow (FUN_800B4DF8, FUN_800B2F98, FUN_800B3020, FUN_800B309C), the
// boss of each level (FUN_800B2B3C) and the mode's hooks.
#include "core/force/tekken_force.h"

#include "content/fight_tables.h"
#include "content/game_ram.h"
#include "core/camera/camera_view.h"
#include "core/camera/view_matrix.h"
#include "core/combat/combat.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/move_system.h"
#include "core/game/byte_block.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/math/gte.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

TekkenForce::TekkenForce(const gd::Ref<FightState>& fight_state, const gd::Ref<GameRam>& overlay) {
    v = gd::make<ByteBlock>(SIZE);
    slot_popups = gd::Arr<gd::Ref<ForcePopup>>{gd::make<ForcePopup>(), gd::make<ForcePopup>()};
    item_popups = gd::Arr<gd::Ref<ForcePopup>>{gd::make<ForcePopup>(), gd::make<ForcePopup>()};
    _fight = fight_state;
    t = fight_state->tables;
    ram = overlay;
    v->bytes = overlay->bytes(BASE, SIZE);
}

// ==== byte access by game address

gd::Int TekkenForce::g32(gd::Int a) {
    return v->s32(a - BASE);
}

gd::Int TekkenForce::gu32(gd::Int a) {
    return v->u32(a - BASE);
}

gd::Int TekkenForce::g16(gd::Int a) {
    return v->s16(a - BASE);
}

gd::Int TekkenForce::gu16(gd::Int a) {
    return v->u16(a - BASE);
}

void TekkenForce::put32(gd::Int a, gd::Int x) {
    v->put32(a - BASE, x);
}

void TekkenForce::put16(gd::Int a, gd::Int x) {
    v->put16(a - BASE, x);
}

gd::Int TekkenForce::level() {
    return g32(LEVEL);
}

gd::Int TekkenForce::score() {
    return gu32(SCORE);
}

// 0x800B70D8: the pad the player uses (the human's side; the player's record is always the first).
gd::Int TekkenForce::human() {
    return gu32(HUMAN);
}

// 0x800B70E8: the level's enemies are over (the boss fight, or the level won).
bool TekkenForce::level_ended() {
    return gu32(LEVEL_ENDED) != 0;
}

// HitApply's mode-8 scoring: ten points per damage the player (or a forced hit) did to an enemy.
void TekkenForce::add_score(gd::Int points) {
    put32(SCORE, Fx::w32(g32(SCORE) + points * 10));
}

// FUN_800B2E60: the player's record.
gd::Ref<FighterState> TekkenForce::player() {
    return fight()->fighters[FighterState::index_at(gu32(PLAYER))];
}

// FUN_800B2E90: enemy slot `i` (0x18 bytes at SLOTS).
gd::Int TekkenForce::slot(gd::Int i) {
    return SLOTS + 0x18 * (i & 1);
}

gd::Ref<FighterState> TekkenForce::slot_fighter(gd::Int i) {
    return fight()->fighters[FighterState::index_at(gu32(slot(i)))];
}

gd::Int TekkenForce::slot_state(gd::Int i) {
    return g16(slot(i) + 8);
}

// FUN_800B2EEC: enemy slot `i` is free while the level runs (PairwiseDistances then counts the
// CPU record `i + 1` as off screen).
bool TekkenForce::slot_free(gd::Int i) {
    return slot_state(i) == 1 && g32(LEVEL_ENDED) == 0;
}

// ==== the mode's start and each level's set-up

// FUN_800B1508 (the mode's start, FUN_800B60A4): the keys from the save, the score and the
// saved hi-score.
void TekkenForce::mode_start() {
    put32(KEYS, fight()->progress->force_keys);
    put32(SCORE, 0);
    put32(HISCORE, fight()->progress->force_hi_score);
}

// FUN_800B2E10: the player (fighter 0, or 1 when the other side started) and the enemy records.
void TekkenForce::assign_records() {
    gd::Int p = fight()->fighters[0]->is_cpu != 0 ? 1 : 0;
    put32(PLAYER, FighterState::address(p));
    put32(ENEMIES, FighterState::address(1 - p));
    put32(THIRD, FighterState::address(2));
}

// FUN_800B15EC (the level's set-up at each round start): the slots empty, the script from the
// start, the camera and walls, the progress bar; the Doctor B. level has no script and starts
// with its boss (state 8) and 60 seconds.
void TekkenForce::level_setup(const gd::PBytes& level_script) {
    assign_records();
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Int s = slot(i);
        put16(s + 8, 0);
        put16(s + 0xA, 0);
        _set_timer(i, SLOT_TIMER, 0);
        put32(s, gu32(ENEMIES + 4 * i));
        put16(s + 0xE, 0);
        put16(s + 0x10, i);
        gd::Ref<FighterState> f = slot_fighter(i);
        f->active = 0;
        f->health = 0;
        f->invulnerable = 1;
    }
    put32(CLOCK, 0);
    put32(STATE, 0);
    put32(SCROLL_PARAM, 0);
    scroll_mode(0);
    put32(LEVEL_ENDED, 0);
    put32(LEVEL, fight()->region->fight_index);
    script_bytes = level_script;
    put32(SCRIPT_PTR, 0);
    for (gd::Int a : gd::Arr<gd::Int>{KEY_EARNED, FINAL_CHALLENGE, TALLY_DONE, WAIT_DEFEATED, WAIT_COUNT, PATTERN_ON,
                                      PATTERN_STOP_X, PATTERN_PTR, FIGHT_START_TIMER, CLEAR_TIME, BONUS_TIME, DRAWN}) {
        put32(a, 0);
    }
    _progress_setup();
    _items_setup();
    _walls_setup();
    bool doctor = tile_only_level(fight()->region->fight_index);
    if (doctor) {
        put32(STATE, 8);
        fight()->timer = 0xE10;
    }
    put32(TILE_ONLY, doctor ? 1 : 0);
}

// Whether the level of `fight_index` is the Doctor B. level: no script, its boss from the start,
// only the ground tile of the panorama (TILE_ONLY), and in the resident code (FUN_80039BB4,
// FUN_8003A030, mode 8 with TILE_ONLY) stage TILE_ONLY_LIGHT_STAGE's light record for the stage's
// lights and the fighters' base colour.
bool TekkenForce::tile_only_level(gd::Int fight_index) {
    return fight_index > LEVEL_4;
}

// FUN_800B396C's state: the bar's phase and done flag.
void TekkenForce::_progress_setup() {
    put32(BAR_PHASE, 0);
    put32(BAR_DONE, 0);
}

// FUN_800B4198's state: both items free, at y 0x420 (the point the picture is drawn above).
void TekkenForce::_items_setup() {
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Int it = ITEMS + 0xBC * i;
        for (gd::Int off : gd::Arr<gd::Int>{0, 4, 0xC, 0x18}) {
            put32(it + off, 0);
        }
        put32(it + 8, ITEM_Y);
    }
}

// FUN_800B2F98: the wall lines by stage.
void TekkenForce::_walls_setup() {
    gd::Arr<gd::Int> lines = fight()->stage == STAGE_18 ? WALL_LINES_18 : WALL_LINES;
    for (gd::Int k = 0; k < 4; ++k) {
        put32(WALLS + 4 * k, lines[k]);
    }
    put32(WALLS + 0x10, -0x103);
    put32(WALLS + 0x14, -0x8ED);
}

// FUN_800B53E0: 0 free scrolling, 1 locked, 2 locked within 5,000 units either side.
void TekkenForce::scroll_mode(gd::Int mode) {
    if (mode == 0) {
        put32(SCROLL_RIGHT, 1);
    } else if (mode == 2) {
        put32(SCROLL_LEFT, -100);
        put32(SCROLL_RIGHT, 60);
        put32(SCROLL_MIN, g32(CAM_X) - SPAWN_EDGE);
        put32(SCROLL_MAX, g32(CAM_X) + SPAWN_EDGE);
    }
    put32(SCROLL_MODE, mode);
}

// ==== who fights whom (FUN_800B5EC0, FUN_800B598C)

// FUN_800B5910 at the mode's fight hook: holding all four face buttons turns the hidden manual
// target switch on (L1 if held, else L2, R1 or R2; L2 without a shoulder button).
void TekkenForce::manual_target_setup(gd::Int held) {
    if ((held & PadState::FACE_BUTTONS) != PadState::FACE_BUTTONS) {
        return;
    }
    gd::Int button = (held & 4) != 0 ? 4 : (held & 1) != 0 ? 1 : (held & 8) != 0 ? 8 : (held & 2) != 0 ? 2 : 1;
    put32(MANUAL_BUTTON, button);
    put32(MANUAL, 1);
}

// FUN_800B58FC.
void TekkenForce::manual_target_off() {
    put32(MANUAL_BUTTON, 0);
    put32(MANUAL, 0);
}

// FUN_800B5EC0: the player, the enemy it faces and the other one (and every fighter's opponent).
gd::Arr<gd::Ref<FighterState>> TekkenForce::roles(const gd::Ref<MoveSystem>& moves,
                                                  const gd::Ref<FighterInput::PadWords>& pads) {
    gd::Ref<FighterState> f0 = fight()->fighters[0];
    gd::Ref<FighterState> f1 = fight()->fighters[1];
    gd::Ref<FighterState> f2 = fight()->fighters[2];
    gd::Ref<FighterState> p = f0->is_cpu == 0 ? f0 : f1;
    gd::Ref<FighterState> a = p == f0 ? f1 : f0;
    gd::Ref<FighterState> b = f2;
    if (p->index == 0) {
        if (p->opp_index == 1) {
            a = f1;
            b = f2;
        } else {
            a = f2;
            b = f1;
        }
    } else {
        if (p->opp_index == 0) {
            a = f0;
            b = f1;
        } else {
            a = f1;
            b = f0;
        }
    }
    put32(CLOSE_A, ((((a->screen_x & 0xFFFF) - 1) & 0xFFFFFFFFLL) < CLOSE_LIMIT) ? 1 : 0);
    put32(CLOSE_B, ((((b->screen_x & 0xFFFF) - 1) & 0xFFFFFFFFLL) < CLOSE_LIMIT) ? 1 : 0);
    gd::Ref<FighterState> target = _choose_target(moves, pads, p, a, b);
    gd::Ref<FighterState> other = a == target ? b : a;
    p->fixed_facing = a->invulnerable == 0 || b->invulnerable == 0 ? 0 : 1;
    p->fixed_facing_side = 0;
    p->opp_index = target->index;
    target->opp_index = p->index;
    other->opp_index = p->index;
    for (const gd::Ref<FighterState>& f : gd::Arr<gd::Ref<FighterState>>{p, target, other}) {
        f->cur_opp_index = fight()->opponent_index(f);
    }
    return gd::Arr<gd::Ref<FighterState>>{p, target, other};
}

// FUN_800B598C: which enemy the player faces. A hidden enemy (+0xC2) is skipped; while only
// one is close it is taken; with both close the nearer one wins by 400 units, else the one more
// in front of the player; a change waits 120 frames. The manual switch swaps the two. (Bug #47,
// not reproduced: the player's angle fields are restored from a full copy.)
gd::Ref<FighterState> TekkenForce::_choose_target(const gd::Ref<MoveSystem>& moves,
                                                  const gd::Ref<FighterInput::PadWords>& pads,
                                                  const gd::Ref<FighterState>& p, const gd::Ref<FighterState>& a,
                                                  const gd::Ref<FighterState>& b) {
    gd::Int wait = g32(TARGET_WAIT);
    if (wait > 0) {
        put32(TARGET_WAIT, wait - 1);
    }
    if (g32(TARGET_WAIT) < 0) {
        put32(TARGET_WAIT, 0);
    }
    gd::Ref<FighterState> pick = a;
    if (a->invulnerable != 0) {
        if (b->invulnerable == 0) {
            pick = b;
        }
    } else if (b->invulnerable != 0) {
        // pass
    } else if (gu32(CLOSE_A) == 0) {
        if (gu32(CLOSE_B) != 0) {
            pick = b;
        }
    } else if (gu32(CLOSE_B) == 0 || gu32(TARGET_WAIT) != 0) {
        // pass
    } else {
        gd::Int da = fight()->pair_distance[Combat::_pair(p->index, a->index)];
        gd::Int db = fight()->pair_distance[Combat::_pair(p->index, b->index)];
        gd::Int diff = Fx::w32(da - db);
        if (da < NEAR || db < NEAR) {
            if (gd::absi(diff) >= NEAR_GAP) {
                pick = diff > 0 ? b : a;
            } else {
                gd::PI32 saved = _angles(p);
                moves->relative_angles(p, a);
                gd::Int qa = Fx::s16(p->facing_quadrant);
                gd::Int ra = Fx::s16(p->rel_angle);
                moves->relative_angles(p, b);
                gd::Int qb = Fx::s16(p->facing_quadrant);
                gd::Int rb = Fx::s16(p->rel_angle);
                if (qa == qb) {
                    gd::Int threshold = qa == 0 ? 0x1000 : qa == 2 ? 0x2000 : 0x3000;
                    if (gd::absi(ra - rb) >= threshold) {
                        pick = ra < rb ? a : b;
                    }
                } else {
                    pick = ram->s32(QUADRANT_ORDER + 4 * qb) < ram->s32(QUADRANT_ORDER + 4 * qa) ? a : b;
                }
                _restore_angles(p, saved);
            }
        }
        if (gu32(MANUAL) != 0) {
            gd::Int side = gu32(MANUAL_SIDE);
            // The side of the human: the first player's CPU flag (0x800A95F0), i.e. HUMAN.
            gd::Int pressed = pads->physical_pressed[human()];
            if ((pressed & gu32(MANUAL_BUTTON)) != 0) {
                side = (side + 1) & 1;
                put32(MANUAL_SIDE, side);
            }
            pick = gu32(MANUAL_SIDE) != 0 ? b : a;
        }
        if (pick->index != a->index) {
            put32(TARGET_WAIT, TARGET_WAIT_FRAMES);
        }
    }
    if (gu32(MANUAL) != 0) {
        put32(MANUAL_SIDE, pick != a ? 1 : 0);
    }
    return pick;
}

// The player's twelve angle fields (+0x2A … +0x42) FUN_800B598C saves around its tests.
gd::PI32 TekkenForce::_angles(const gd::Ref<FighterState>& f) {
    return gd::PI32{f->target_dir, f->heading, f->heading_delta, f->rel_angle, f->facing_quadrant,
                    f->aim_dir, f->opp_to_self_dir, f->opp_heading, f->opp_heading_delta, f->opp_rel_angle,
                    f->opp_quadrant, f->opp_aim_dir};
}

void TekkenForce::_restore_angles(const gd::Ref<FighterState>& f, const gd::PI32& a) {
    f->target_dir = a[0];
    f->heading = a[1];
    f->heading_delta = a[2];
    f->rel_angle = a[3];
    f->facing_quadrant = a[4];
    f->aim_dir = a[5];
    f->opp_to_self_dir = a[6];
    f->opp_heading = a[7];
    f->opp_heading_delta = a[8];
    f->opp_rel_angle = a[9];
    f->opp_quadrant = a[10];
    f->opp_aim_dir = a[11];
}

// ==== the camera (FUN_800B545C, FUN_800B5504) and the walls

// FUN_800B545C: the side-scrolling camera starts on the player (level 4 rolls it slightly).
void TekkenForce::camera_setup(const gd::Ref<CameraView>& view) {
    view->roll = fight()->region->fight_index == LEVEL_4 ? CAMERA_ROLL_LEVEL_4 : 0;
    put32(SCROLL_RIGHT, 1);
    put32(CAM_Y, -0xDAC);
    put32(CAM_Z, -0x1B58);
    put32(SCROLL_MODE, 0);
    put32(CAM_X, 0);
    put32(CAM_PITCH, 0x2AAA);
    put32(CAM_YAW, 0);
    put32(CAM_H, 500);
    put32(CAM_PITCH_TARGET, 0x2AAA);
    put32(CAM_TARGET_X, 0);
    put32(CAM, gu32(PLAYER));
    put32(START_X, g32(CAM_X));
}

// FUN_800B5504: the camera scrolls with the player's projected screen x (in scroll mode 0 past
// the centre, in mode 2 within its bounds), and pitches back when the player nears the bottom
// of the screen. Returns the source record (x, y, z, pitch, yaw, h) for CameraUseSource(0).
gd::PI32 TekkenForce::camera_frame(const gd::Ref<CameraView>& view) {
    gd::Int pitch_q = (Fx::div_trunc(-g32(CAM_PITCH) + (-g32(CAM_PITCH) < 0 ? 0x3F : 0), 1) >> 5) & 0x1FFE;
    gd::Ref<FighterState> p = fight()->fighters[FighterState::index_at(gu32(CAM))];
    gd::Int dx = p->root_x - g32(CAM_X);
    gd::Int roll = -view->roll & 0xFFF;
    gd::Int cr = FightMath::cos12(roll, t);
    gd::Int sr = FightMath::sin12(roll, t);
    gd::Int cam_y = g32(CAM_Y);
    gd::Int u = cr * dx - sr * -cam_y;
    u = u < 0 ? u + 0xFFF : u;
    gd::Int cp = t->sin_table[(pitch_q >> 1) + 1024];
    gd::Int sp = t->sin_table[pitch_q >> 1];
    gd::Int dz = p->root_z - g32(CAM_Z);
    gd::Int w = sr * dx + cr * -cam_y;
    w = w < 0 ? w + 0xFFF : w;
    gd::Int depth = cp * dz - sp * (w >> 12);
    depth = depth < 0 ? depth + 0xFFF : depth;
    gd::Int up = sp * dz + cp * (w >> 12);
    up = up < 0 ? up + 0xFFF : up;
    gd::Int bottom = Fx::div_trunc(Fx::div_trunc((up >> 12) * 0x9C4, depth >> 12), 3);
    gd::Int screen = Fx::div_trunc((u >> 12) * 500, depth >> 12);
    put32(CAM_TARGET_X, g32(CAM_X));
    gd::Int mode = g32(SCROLL_MODE);
    if (mode == 0 || mode == 2) {
        if (mode == 2 && screen < g32(SCROLL_LEFT)) {
            put32(CAM_TARGET_X, g32(CAM_X) + (g32(SCROLL_LEFT) - screen) * -5);
        }
        if (g32(SCROLL_RIGHT) < screen) {
            put32(CAM_TARGET_X, g32(CAM_X) + (screen - g32(SCROLL_RIGHT)) * 5);
        }
    }
    if ((screen > 0 && g32(CAM_X) < g32(CAM_TARGET_X)) || (screen < 0 && g32(CAM_TARGET_X) < g32(CAM_X))) {
        put32(CAM_X, g32(CAM_TARGET_X));
    }
    if (mode == 2) {
        if (g32(CAM_X) < g32(SCROLL_MIN)) {
            put32(CAM_X, g32(SCROLL_MIN));
        }
        if (g32(SCROLL_MAX) < g32(CAM_X)) {
            put32(CAM_X, g32(SCROLL_MAX));
        }
    }
    if (bottom >= 0xD5) {
        put32(CAM_PITCH_TARGET, g32(CAM_PITCH) + 0x2D8);
    } else if (bottom < 0xBC) {
        put32(CAM_PITCH_TARGET, g32(CAM_PITCH) - 0x2D8);
    }
    gd::Int pitch = g32(CAM_PITCH);
    if (pitch != g32(CAM_PITCH_TARGET)) {
        gd::Int d = pitch - g32(CAM_PITCH_TARGET);
        put32(CAM_PITCH, pitch - ((d < 0 ? d + 7 : d) >> 3));
    }
    put32(CAM_PITCH, gd::clampi(g32(CAM_PITCH), 0x2AAA, 0x10000));
    return gd::PI32{g32(CAM_X), g32(CAM_Y), g32(CAM_Z), g32(CAM_PITCH), g32(CAM_YAW), g32(CAM_H)};
}

// FUN_800B4DF8's first part: the camera's view-space x (the walls follow it).
void TekkenForce::background(const gd::Ref<ViewMatrix>& view, const gd::Ref<CameraView>& camera) {
    gd::PI32 p = Gte::apply_lv(view->rot, gd::PI32{camera->x, camera->y, camera->z});
    put32(VIEW_X, p[0]);
}

// A wall line (FUN_800B3020 / FUN_800B309C): v · k / 1000 + c.
gd::Int TekkenForce::_line(gd::Int at, gd::Int x) {
    return Fx::w32(Fx::div_trunc(Fx::w32(x * g32(at)), 1000) + g32(at + 4));
}

// ArenaBounds in Tekken Force: z within [−0x898, 0x8FC] (on stage 18 the lower limit is a line
// of x), the player's x between two lines of z relative to the camera's view x, an enemy within
// 0x1068 of it. Returns Vector2i(dz, dx).
gd::Vector2i TekkenForce::walls(const gd::Ref<FighterState>& f) {
    gd::Int dz = 0;
    gd::Int dx = 0;
    gd::Int z = f->root_z;
    if (Fx::w32(z - 0x8FC) > 0) {
        dz = Fx::w32(-(z - 0x8FC));
    }
    gd::Int low = Fx::w32(z + 0x898);
    if (fight()->stage == STAGE_18) {
        low = Fx::w32(f->root_z - _line(WALLS + 0x10, Fx::w32(f->root_x - g32(VIEW_X))));
    }
    if (low < 0) {
        dz = Fx::w32(-low);
    }
    gd::Int view = g32(VIEW_X);
    gd::Int high = 0;
    if (f->is_cpu == 0) {
        gd::Int rel = Fx::w32(f->root_x - view);
        gd::Int lo = _line(WALLS, f->root_z);
        gd::Int hi = _line(WALLS + 8, f->root_z);
        if (Fx::w32(rel - lo) < 0) {
            dx = Fx::w32(-(rel - lo));
        }
        high = Fx::w32(rel - hi);
    } else {
        gd::Int left = Fx::w32(Fx::w32(f->root_x + 0x1068) - view);
        if (left < 0) {
            dx = Fx::w32(-left);
        }
        high = Fx::w32(Fx::w32(f->root_x - 0x1068) - view);
    }
    if (high > 0) {
        dx = Fx::w32(-high);
    }
    return gd::Vector2i(dz, dx);
}

// ==== the level runner (FUN_800B1778)

// One frame of the level: the script, the enemy slots, the progress bar's phase, the items, the
// score and timer caps and the announcer's countdown. Returns true on the frame NOW LOADING ends
// (FightFrame then changes the area: round state 7).
bool TekkenForce::level_frame(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> p = player();
    bool area_change = _script_frame(sim, events);
    for (gd::Int i = 0; i < 2; ++i) {
        _enemy_slot(sim, i, p, events);
    }
    if (fight()->paused_player == 0) {
        put32(BAR_PHASE, gu32(BAR_PHASE) + 0x40);
    }
    if (gu32(BAR_DONE) == 0 && gu32(LEVEL_ENDED) != 0) {
        put32(BAR_DONE, 1);
    }
    _items(sim, p, events);
    if (SCORE_CAP < gu32(SCORE)) {
        put32(SCORE, SCORE_CAP);
    }
    if (gu32(HISCORE) < gu32(SCORE)) {
        fight()->region->ctx_put8(NEW_RECORD, 1);
        put32(HISCORE, gu32(SCORE));
    }
    if (fight()->timer >= TIMER_CAP + 1) {
        fight()->timer = TIMER_CAP;
    }
    if (fight()->paused_player == 0 && fight()->round_state == RoundState::FIGHT) {
        gd::Int left = fight()->timer;
        if (gd::Arr<gd::Int>{60, 120, 180, 240, 300}.has(left)) {
            _sound(ram->u16(COUNTDOWN_SOUNDS + 2 * (left / 60 - 1)), events);
        }
    }
    return area_change;
}

void TekkenForce::_sound(gd::Int code, const gd::Ref<SimEvents>& events) {
    events->add(SimEvents::Kind::SOUND, 0, code, 0);
}

// The level script's part (FUN_800B1778 up to its enemy slots).
bool TekkenForce::_script_frame(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events) {
    bool area_change = false;
    gd::Int progress = Fx::w32(g32(CAM_X) - g32(START_X));
    if (fight()->paused_player == 0) {
        put32(CLOCK, gu32(CLOCK) + 1);
    }
    put32(DRAWN, gu32(DRAWN) + 1);
    gd::Int clock = g32(CLOCK);
    bool frozen = fight()->paused_player != 0;
    switch (gu32(STATE)) {
        case 0:
            _commands(progress, events);
            break;
        case 1: {
            gd::Int w = g32(WAIT_COUNT);
            if (w < 0) {
                if (slot_state(0) == SLOT_FREE && slot_state(1) == SLOT_FREE) {
                    put32(STATE, 0);
                }
            } else if (!(g32(WAIT_DEFEATED) < w)) {
                put32(STATE, 0);
            }
            break;
        }
        case 3:
            if (g32(TALLY) < clock && !frozen) {
                put32(TALLY, clock + 2);
                put32(STATE, 4);
            }
            break;
        case 4:
            if (g32(TALLY) < clock && !frozen) {
                area_change = true;
                put32(STATE, 5);
            }
            break;
        case 5:
            put32(LEVEL_ENDED, 1);
            fight()->region->ctx_put8(FEATURE_PAUSE, 1);
            put32(STATE, 6);
            put32(FIGHT_START_TIMER, fight()->timer);
            break;
        case 6:
            _bonus_frame();
            break;
        case 7:
            _tally(events);
            break;
        case 8:
            _boss(sim, events);
            break;
        case 9: {
            gd::Int d = g32(FIGHT_START_TIMER) - fight()->timer;
            put32(CLEAR_TIME, gd::maxi(d, 0));
            if (fight()->round_state == RoundState::STAGE_CLEAR) {
                put32(TALLY, 0);
                put32(STATE, 10);
                fight()->region->ctx_put8(RESULT_FLAGS, fight()->region->ctx8(RESULT_FLAGS) | 8);
            }
            break;
        }
        case 10: {
            gd::Int n = g32(TALLY);
            put32(TALLY, n + 1);
            if (n >= 0xB5) {
                put32(TALLY_DONE, 1);
            }
            break;
        }
        default:
            break;
    }
    _patterns(progress);
    return area_change;
}

// A script record (20 bytes): s32 trigger, s16 command, dx, _, z, _, param, u16 arg.
gd::Int TekkenForce::_rec32(gd::Int at) {
    return at + 4 <= script_bytes.size() ? script_bytes.decode_s32(at) : 0x7FFFFFFF;
}

gd::Int TekkenForce::_rec16(gd::Int at) {
    return at + 2 <= script_bytes.size() ? script_bytes.decode_s16(at) : 0;
}

gd::Int TekkenForce::_recu16(gd::Int at) {
    return at + 2 <= script_bytes.size() ? script_bytes.decode_u16(at) : 0;
}

// State 0: every record whose trigger the camera has passed runs.
void TekkenForce::_commands(gd::Int progress, const gd::Ref<SimEvents>& events) {
    if (fight()->round_state != RoundState::FIGHT || progress < _rec32(gu32(SCRIPT_PTR))) {
        return;
    }
    while (true) {
        gd::Int rec = gu32(SCRIPT_PTR);
        gd::Int cmd = _rec16(rec + 4);
        gd::Int cam = g32(CAM_X);
        bool stop = false;
        switch (cmd) {
            case 0:
                put32(SCROLL_PARAM, _rec16(rec + 0xE));
                scroll_mode(_rec16(rec + 0xE));
                break;
            case 1: {
                gd::Int s = free_slot();
                if (s >= 0) {
                    _spawn(s, _recu16(rec + 0xE), _recu16(rec + 0x10), cam + _rec16(rec + 6), _rec16(rec + 0xA));
                }
                break;
            }
            case 2:
                put32(PATTERN_ON, gu32(PATTERN_ON) + 1);
                put32(PATTERN_STOP_X, _recu16(rec + 0x10));
                if (_recu16(rec + 0xE) < 0x31) {
                    put32(PATTERN_PTR, PATTERNS + 6 * _rec16(rec + 0xE));
                }
                break;
            case 3:
                stop = true;
                put32(WAIT_DEFEATED, 0);
                put32(STATE, 1);
                put32(WAIT_COUNT, _rec16(rec + 0xE));
                break;
            case 4:
                _place_item(cam + _rec16(rec + 6), _rec16(rec + 0xA));
                break;
            case 6:
                for (gd::Int i = 0; i < 2; ++i) {
                    _release(i);
                }
                events->add(SimEvents::Kind::MUSIC_VOLUME, -1, 0, 30);
                stop = true;
                fight()->region->ctx_put8(FEATURE_PAUSE, 0);
                put32(PATTERN_ON, 0);
                put32(STATE, 3);
                put32(TALLY, gu32(CLOCK) + 0x3C);
                break;
            default:
                break;
        }
        put32(SCRIPT_PTR, rec + 0x14);
        if (stop || progress < _rec32(rec + 0x14)) {
            break;
        }
    }
}

// FUN_800B2EB0: the first free enemy slot, −1 for none.
gd::Int TekkenForce::free_slot() {
    for (gd::Int i = 0; i < 2; ++i) {
        if (slot_state(i) == SLOT_FREE) {
            return i;
        }
    }
    return -1;
}

// FUN_800B2F30.
void TekkenForce::_release(gd::Int i) {
    gd::Ref<FighterState> f = slot_fighter(i);
    f->health = 0;
    f->active = 0;
    f->invulnerable = 1;
    put16(slot(i) + 8, 1);
}

// A new enemy in slot `i`: state 4 (enter) when its model is loaded, else 2 (load it first).
void TekkenForce::_spawn(gd::Int i, gd::Int variant, gd::Int kind, gd::Int x, gd::Int z) {
    gd::Int s = slot(i);
    if (g16(s + 0x10) == Fx::s16(variant)) {
        put16(s + 8, 4);
    } else {
        put16(s + 8, 2);
        put16(s + 0x12, variant);
    }
    put16(s + 0xE, Fx::div_trunc(Fx::s16(kind), 5) * -5 + Fx::s16(kind));
    place(slot_fighter(i), x, z);
}

// FUN_800B2F50: anchor and root at (x, z).
void TekkenForce::place(const gd::Ref<FighterState>& f, gd::Int x, gd::Int z) {
    f->root_x = x;
    f->pos_x = x;
    f->root_z = z;
    f->pos_z = z;
}

// FUN_800B4328: the first free item slot gets the item.
void TekkenForce::_place_item(gd::Int x, gd::Int z) {
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Int it = ITEMS + 0xBC * i;
        if (gu32(it) == 0) {
            put32(it + 4, x);
            put32(it + 0xC, z);
            put32(it, 1);
            return;
        }
    }
}

// The spawn pattern (command 2): a new enemy enters from the right whenever a slot is free, until
// the pattern's 99 or the camera passing its stop x.
void TekkenForce::_patterns(gd::Int progress) {
    if (gu32(PATTERN_ON) != 1) {
        return;
    }
    if (!(progress < g32(PATTERN_STOP_X))) {
        put32(PATTERN_ON, 0);
        return;
    }
    gd::Int at = gu32(PATTERN_PTR);
    if (ram->u16(at) == 0x63) {
        return;
    }
    gd::Int s = free_slot();
    if (s < 0) {
        return;
    }
    _spawn(s, ram->u16(at), ram->u16(at + 2), g32(CAM_X) + SPAWN_EDGE, 0);
    put32(PATTERN_PTR, at + 6);
}

// State 6: the clear and bonus time, and on the stage clear (round state 8) the clear bonus and
// the key rule.
void TekkenForce::_bonus_frame() {
    gd::Ref<FighterState> p = player();
    gd::Int clear = g32(FIGHT_START_TIMER) - fight()->timer;
    gd::Int bonus = ram->s32(ALLOWANCE + 4 * level()) - clear;
    put32(CLEAR_TIME, clear >= 0 ? clear : 0);
    put32(BONUS_TIME, bonus >= 0 ? bonus : 0);
    if (fight()->round_state != RoundState::STAGE_CLEAR) {
        return;
    }
    gd::Int health = p->health;
    put32(CLEAR_BONUS, ((health < 0 ? health + 0xFFFF : health) >> 16) * 100);
    if (level() == LEVEL_4) {
        gd::Int doctor = (fight()->progress->unlocked >> 19) & 1;
        gd::Int keys = g32(KEYS);
        if (keys < 3 && doctor == 0) {
            put32(KEY_EARNED, 1);
            fight()->progress->force_keys = gd::mini(fight()->progress->force_keys + 1, 0xFF);
            fight()->save_request = true;
            // The literal `{0: 1, 1: 3, 2: 7}` Dictionary.
            gd::Dict<gd::Int, gd::Int> key_flags;
            key_flags.set(0, 1);
            key_flags.set(1, 3);
            key_flags.set(2, 7);
            gd::Int flag = key_flags.get(keys, 0);
            if (flag != 0) {
                fight()->region->ctx_put8(RESULT_FLAGS, fight()->region->ctx8(RESULT_FLAGS) | flag);
            }
        } else if (keys == 3 && doctor == 0) {
            put32(FINAL_CHALLENGE, 1);
        }
    }
    put32(TALLY, 0);
    put32(STATE, 7);
}

// State 7: STAGE BONUS, CLEAR BONUS, CLEAR TIME, then BONUS TIME (or the key or final stage
// message) 30 frames apart, each with its sound; the bonuses go to the score as they appear.
void TekkenForce::_tally(const gd::Ref<SimEvents>& events) {
    gd::Int n = g32(TALLY) + 1;
    put32(TALLY, n);
    if (n == 0x1E) {
        put32(SCORE, gu32(SCORE) + ram->u32(STAGE_BONUS + 4 * level()));
        _sound(SOUND_TALLY, events);
    }
    if (n == 0x3C) {
        put32(SCORE, gu32(SCORE) + gu32(CLEAR_BONUS));
        _sound(SOUND_TALLY, events);
    }
    if (n == 0x5A) {
        _sound(SOUND_TALLY, events);
    }
    if (n < 0x78) {
        return;
    }
    put32(TALLY_DONE, 1);
    gd::Int b = g32(BONUS_TIME);
    if (level() < LEVEL_4 && b != 0) {
        if (n == 0x78) {
            fight()->timer = Fx::w32(fight()->timer + b);
            _sound(SOUND_TALLY, events);
        }
    } else if (gu32(KEY_EARNED) != 0) {
        if (n == 0x78) {
            put32(KEYS, gu32(KEYS) + 1);
            _sound(SOUND_TALLY_KEY, events);
        }
    } else if (gu32(FINAL_CHALLENGE) != 0 && n == 0x78) {
        _sound(SOUND_TALLY_KEY, events);
    }
}

// The tally's lines for the presentation: (frames shown, stage bonus, clear bonus, clear time,
// bonus time) and which last line shows (0 bonus time, 1 the key, 2 the final stage, −1 none).
gd::PI32 TekkenForce::tally_lines() {
    gd::Int last = -1;
    if (level() < LEVEL_4 && g32(BONUS_TIME) != 0) {
        last = 0;
    } else if (gu32(KEY_EARNED) != 0) {
        last = 1;
    } else if (gu32(FINAL_CHALLENGE) != 0) {
        last = 2;
    }
    return gd::PI32{g32(TALLY), ram->u32(STAGE_BONUS + 4 * level()), gu32(CLEAR_BONUS),
                    g32(CLEAR_TIME), g32(BONUS_TIME), last};
}

// State 8 (the Doctor B. level's fight): slot 0 becomes a type-4 enemy with the level's boss
// health, its entrance starts, the camera locks (scroll mode 2) and state 9 times the fight.
void TekkenForce::_boss(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events) {
    gd::Int s = slot(0);
    gd::Ref<FighterState> f = slot_fighter(0);
    put16(s + 0xE, 4);
    put16(s + 8, 1);
    put16(s + 0xA, 0);
    sim->start_move(f, 3);
    f->active = 1;
    gd::Int hp = ram->s32(ENEMY_HEALTH + 20 * level() + 4 * g16(s + 0xE));
    f->invulnerable = 0;
    f->pos_y = 0;
    f->ballistic = 0;
    f->air_phase = 0;
    f->health_max = hp;
    f->health = hp;
    events->add(SimEvents::Kind::FORCE_NAMES, -1, 1, Character::DOCTOR_B);
    put32(SCROLL_PARAM, 2);
    scroll_mode(2);
    put32(LEVEL_ENDED, 1);
    put32(STATE, 9);
    put32(FIGHT_START_TIMER, fight()->timer);
}

// One enemy slot: 0 release, 1 free, 2–3 load the costume's model, 4 enter with full health,
// 5 fight, 6 defeated (+n SEC), 7 blink out.
void TekkenForce::_enemy_slot(const gd::Ref<FightSimulation>& sim, gd::Int i, const gd::Ref<FighterState>& p,
                              const gd::Ref<SimEvents>& events) {
    gd::Int s = slot(i);
    gd::Ref<FighterState> f = slot_fighter(i);
    gd::Int clock = g32(CLOCK);
    switch (slot_state(i)) {
        case 0:
            f->active = 0;
            f->health = 0;
            f->invulnerable = 1;
            put16(s + 8, 1);
            break;
        case 2:
            f->active = 0;
            put16(s + 8, 3);
            _set_timer(i, SLOT_TIMER, g32(DRAWN) + 2);
            break;
        case 3:
            if (_timer(i, SLOT_TIMER) < g32(DRAWN)) {
                gd::Int variant = gu16(s + 0x12);
                put16(s + 0x10, variant);
                gd::Int key = enemy_key(level(), Fx::s16(variant));
                f->costume_key = key + ENEMY_KEY_OFFSET;
                sim->reload_enemy(f);
                events->add(SimEvents::Kind::FORCE_NAMES, -1, f->index, ENEMY);
                put16(s + 8, 4);
            }
            break;
        case 4: {
            f->active = 1;
            gd::Int hp = ram->s32(ENEMY_HEALTH + 20 * level() + 4 * g16(s + 0xE));
            f->invulnerable = 0;
            f->health_max = hp;
            f->health = hp;
            put16(s + 8, 5);
            sim->start_move(f, 3);
            f->pos_y = 0;
            f->ballistic = 0;
            f->air_phase = 0;
            break;
        }
        case 5: {
            if (f->health > 0) {
                return;
            }
            put16(s + 8, 6);
            _set_timer(i, SLOT_TIMER, clock + 0x3C);
            if (f->last_attacker != p->index && f->forced_hit_in == 0) {
                return;
            }
            gd::Int tm = fight()->timer;
            gd::Int sec = ram->u16(KILL_SECONDS + 4 * g16(s + 0xE) + 2);
            fight()->timer = Fx::w32(tm + 59 - (tm - Fx::div_trunc(tm, 60) * 60) + sec * 60);
            gd::PI32 head = f->body->joints[2]->t;
            slot_popups[i]->point = gd::PI32{head[0], head[1] - 300, head[2]};
            slot_popups[i]->view = sim->view;
            _set_timer(i, POPUP_TIMER, clock + 0x3C);
            slot_popups[i]->until = _timer(i, POPUP_TIMER);
            fight()->region->ctx_put16(DEFEATED, fight()->region->ctx16(DEFEATED) + 1);
            break;
        }
        case 6:
            if (_timer(i, SLOT_TIMER) < clock && f->in_throw == 0) {
                put16(s + 8, 7);
                _set_timer(i, SLOT_TIMER, clock + 0x28);
                f->invulnerable = 1;
            } else if (f->got_hit != 0 && f->last_attacker == p->index) {
                _set_timer(i, SLOT_TIMER, clock + 0x3C);
            }
            break;
        case 7:
            f->active = gu32(CLOCK) & 1;
            if (_timer(i, SLOT_TIMER) < clock) {
                put32(WAIT_DEFEATED, gu32(WAIT_DEFEATED) + 1);
                f->active = 0;
                put16(s + 8, 1);
            }
            break;
        default:
            break;
    }
}

// The boss of the level for the player's costume key (FUN_800B2B3C's table): (character,
// costume); a costume of −1 is the player's other costume when the boss is the player's own
// character, else costume 0.
gd::Vector2i TekkenForce::boss_of(gd::Int key) {
    gd::Int entry = ram->u32(BOSS_TABLE + 4 * key) + 4 * (level() & 3);
    gd::Int character = ram->u16(entry);
    gd::Int costume = ram->s16(entry + 2);
    if (costume == -1) {
        if (key >> 2 == character) {
            costume = (key & 3) == 0 ? 1 : 0;
        } else {
            costume = (key & 3) != 0 ? 1 : 0;
        }
    }
    return gd::Vector2i(character, costume);
}

// FUN_800B2F64: the costume key of an enemy variant on a level.
gd::Int TekkenForce::enemy_key(gd::Int lv, gd::Int variant) {
    return ram->u8(ENEMY_KEYS + 2 * (lv & 3) + (variant & 1));
}

// Whether a slot's "+n SEC" shows (the presentation draws it rising over its point).
bool TekkenForce::slot_popup_shown(gd::Int i) {
    return slot_state(i) == 6 && g32(CLOCK) < _timer(i, POPUP_TIMER);
}

// A slot's timer (SLOT_TIMER, POPUP_TIMER): the game's 16-bit copy, sign-extended and compared with
// the 32-bit clocks (bug #41), or the whole value with the fix.
gd::Int TekkenForce::_timer(gd::Int i, gd::Int field) {
    if (fight()->rules->fix_force_slot_timers) {
        return _slot_timers[2 * i + (field == POPUP_TIMER ? 1 : 0)];
    }
    return g16(slot(i) + field);
}

void TekkenForce::_set_timer(gd::Int i, gd::Int field, gd::Int value) {
    put16(slot(i) + field, value);
    _slot_timers[2 * i + (field == POPUP_TIMER ? 1 : 0)] = value;
}

// FUN_800B4374: the two items. A placed item is removed 5,000 units behind the camera; the player
// picks it up within 500 units in x and z (health +50, sound 0x4E6A), which starts LIFE UP! for
// 60 frames (sound 0x87C0 15 frames before its end).
void TekkenForce::_items(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& p,
                         const gd::Ref<SimEvents>& events) {
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Int it = ITEMS + 0xBC * i;
        switch (gu32(it)) {
            case 1: {
                if (g32(it + 4) < Fx::w32(g32(CAM_X) - ITEM_BEHIND)) {
                    put32(it, 0);
                    continue;
                }
                gd::Int dx = gd::absi(Fx::w32(p->root_x - g32(it + 4)));
                gd::Int dz = gd::absi(Fx::w32(p->root_z - g32(it + 0xC)));
                if (dx < ITEM_REACH && dz < ITEM_REACH && p->health > 0 && p->in_air == 0) {
                    p->health = Fx::w32(p->health + ITEM_HEALTH);
                    _sound(SOUND_ITEM, events);
                    gd::PI32 head = p->body->joints[2]->t;
                    item_popups[i]->point = gd::PI32{head[0], head[1] - 300, head[2]};
                    item_popups[i]->view = sim->view;
                    put32(it, 2);
                    put32(it + 0x18, gu32(CLOCK) + 0x3C);
                    item_popups[i]->until = g32(it + 0x18);
                    fight()->health_flash[p->player_index] = 1;
                }
                break;
            }
            case 2:
                if (gu32(CLOCK) == ((gu32(it + 0x18) - 0xF) & 0xFFFFFFFFLL)) {
                    _sound(SOUND_ITEM_END, events);
                }
                if (g32(it + 0x18) < g32(CLOCK)) {
                    put32(it, 0);
                }
                break;
            default:
                break;
        }
    }
}

// An item's state (0 none, 1 placed, 2 picked up: LIFE UP! shows) and world point.
gd::PI32 TekkenForce::item(gd::Int i) {
    gd::Int it = ITEMS + 0xBC * i;
    return gd::PI32{gu32(it), g32(it + 4), g32(it + 8), g32(it + 0xC)};
}

// ==== the health bars (FUN_800B30DC, FUN_800B3118)

// FUN_800B30DC: the three bars empty (they fill up at the round start).
void TekkenForce::bars_reset() {
    for (gd::Int k = 0; k < 3; ++k) {
        gd::Int r = BARS + 0x10 * k;
        put32(r, 0);
        for (gd::Int off : gd::Arr<gd::Int>{4, 6, 8}) {
            put16(r + off, 0);
        }
    }
}

// FUN_800B3118's state: the player's bar (0x9C pixels) and each shown enemy's (as wide as its
// type's entry): a drop shows at once, a refill grows 3 pixels per frame, the recent-damage part
// closes a sixteenth per frame.
void TekkenForce::bars_step() {
    for (gd::Int k = 0; k < 3; ++k) {
        gd::Ref<FighterState> f = k == 0 ? player() : slot_fighter(k - 1);
        if (k > 0 && (f->invulnerable != 0 || fight()->round_state == RoundState::INTRO)) {
            continue;
        }
        gd::Int width = k == 0 ? 0x9C : ram->u16(KILL_SECONDS + 4 * g16(slot(k - 1) + 0xE));
        gd::Int r = BARS + 0x10 * k;
        gd::Int target = g16(r + 4);
        gd::Int fill = g16(r + 6);
        gd::Int trail = g16(r + 8);
        gd::Int room = width - 4;
        if (g32(r) != f->health) {
            gd::Int step = Fx::div_trunc(f->health_max, room);
            put32(r, f->health);
            target = step != 0 ? Fx::div_trunc(f->health - 1 + step, step) : 0;
        }
        target = gd::mini(target, room);
        gd::Int recent = fill < trail ? trail - ((trail - fill + 0xF) >> 4) : fill;
        gd::Int grown = target <= fill || target < fill + 3 ? target : fill + 3;
        if (recent < grown) {
            recent = grown;
        }
        put16(r + 4, target);
        put16(r + 6, grown);
        put16(r + 8, recent);
    }
}

// A bar for the presentation: target, fill, recent damage, grows rightwards, x, y, width.
gd::PI32 TekkenForce::bar(gd::Int k) {
    gd::Int r = BARS + 0x10 * k;
    gd::Int width = k == 0 ? 0x9C : ram->u16(KILL_SECONDS + 4 * g16(slot(k - 1) + 0xE));
    return gd::PI32{g16(r + 4), g16(r + 6), g16(r + 8), g16(r + 0xA), gu16(r + 0xC), gu16(r + 0xE), width};
}

}  // namespace t3
