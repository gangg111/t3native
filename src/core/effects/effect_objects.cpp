// remake/core/effects/effect_objects.gd
// The effect object pool (effects.md#effect-objects): sparks, power glows, fire breath, smoke,
// floor fire, breath and gas clouds, burning bodies and the voice echoes. It is simulation
// state: the objects draw from the shared `rand()`, their number decides the chance of
// optional spawns, and fire, breath and gas publish projectile hit segments.
//
// `EffectsUpdate` (0x8006F8A8) updates the live objects in allocation order; an object spawned
// during the pass is updated in the same pass. A finished object turns into type 0 and leaves
// the pool two updates later (FUN_8006F894, FUN_8006F840). The presentation draws the objects
// from their type, phase, counters and position.
//
// Type 15 is Tekken Ball's charged ball: a glow of the hitter's character, or a flame for a
// charge of 100 or more (FUN_80073F78).
#include "core/effects/effect_objects.h"

#include "content/fight_tables.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_math.h"
#include "core/combat/attack_records.h"
#include "core/effects/effect_pool.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/replay_state.h"
#include "core/fight/vibration.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/game/byte_block.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"
#include "core/math/gte.h"
#include "core/sim_events.h"

namespace t3 {

// ==== the pool

// FUN_8006F750: every object back in the free list.
void EffectObjects::reset(const gd::Ref<FightState>& fight) {
    fight->effects->objects.clear();
}

// EffectAlloc (0x8006F7DC): a new object at the end of the live list, null when all 80 are used.
gd::Ref<EffectObjects::Obj> EffectObjects::alloc(const gd::Ref<FightState>& fight, gd::Int type) {
    gd::Ref<EffectPool> pool = fight->effects;
    if (pool->objects.size() >= CAPACITY) {
        return nullptr;
    }
    gd::Ref<Obj> o = gd::make<Obj>();
    o->type = type;
    o->flags = MARK;
    o->state = 0;
    pool->objects.append(o);
    return o;
}

// FUN_8006F894: the object ends; it stays in the pool as type 0 for two more updates.
void EffectObjects::finish(const gd::Ref<Obj>& o) {
    o->type = 0;
    o->state = 0;
    o->flags = MARK;
}

// Round start (FUN_8006E8FC): the pool emptied and the eight power glows created.
void EffectObjects::round_start(const gd::Ref<FightState>& fight) {
    gd::Ref<EffectPool> pool = fight->effects;
    reset(fight);
    pool->replay_glows = 0;
    pool->end_cleared = 0;
    for (const gd::Ref<FighterState>& f : fight->active()) {
        f->keep_light = 0;
    }
    pool->glow_mask = gd::PI32{0, 0};
    _spawn_glows(fight);
}

void EffectObjects::_spawn_glows(const gd::Ref<FightState>& fight) {
    for (gd::Int joint : GLOW_JOINTS) {
        gd::Ref<Obj> o = alloc(fight, 3);
        if (o != nullptr) {
            o->a = joint;
        }
    }
}

// EffectsUpdate (0x8006F8A8). `enabled` is FightFrame's 0x800958C0 (0 while the fight only
// draws): the objects update and are counted only when it is set.
void EffectObjects::update(const gd::Ref<FightState>& fight, bool enabled, const gd::Ref<SimEvents>& events) {
    gd::Ref<EffectPool> pool = fight->effects;
    pool->segments[0].clear();
    pool->segments[1].clear();
    if (enabled) {
        for (const gd::Ref<FighterState>& f : fight->active()) {
            f->keep_light = 0;
        }
        pool->counter += 1;
        _power_glow_joints(fight);
    }
    if (pool->replay_glows == 0 && fight->replay_playing != 0) {
        reset(fight);
        _spawn_glows(fight);
        pool->burn_flags = 0;
        pool->replay_glows += 1;
    }
    if (pool->end_cleared == 0 && fight->camera_phase >= 3 && fight->camera_phase <= 7) {
        reset(fight);
        pool->end_cleared += 1;
    }
    gd::Int count = 0;
    gd::Int i = 0;
    while (i < pool->objects.size()) {
        gd::Ref<Obj> o = pool->objects[i];
        if (enabled) {
            _update_object(fight, o, events);
            count += 1;
            if ((o->flags & SEGMENT) != 0) {
                pool->segments[(o->flags & OWNER_0) != 0 ? 0 : 1].append(
                    gd::PI32{o->px, o->py, o->pz, o->x, o->y, o->z});
            }
        }
        if (o->gone) {
            pool->objects.remove_at(i);
        } else {
            i += 1;
        }
    }
    pool->live = count;
}

void EffectObjects::_update_object(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events) {
    switch (o->type) {
        case 0:
            _placeholder(o);
            break;
        case 1:
            _spark(fight, o, false);
            break;
        case 2:
            _spark_spray(fight, o, false);
            break;
        case 3:
            _glow(fight, o);
            break;
        case 4:
            _glow_rise(fight, o);
            break;
        case 5:
            _ember(fight, o);
            break;
        case 6:
            _fireball(fight, o);
            break;
        case 7:
            _smoke(fight, o);
            break;
        case 8:
            _flame(fight, o);
            break;
        case 9:
            _floor_fire(fight, o);
            break;
        case 10:
            _cloud(fight, o);
            break;
        case 11:
            _burn(fight, o);
            break;
        case 12:
            _spark(fight, o, true);
            break;
        case 13:
            _spark_spray(fight, o, true);
            break;
        case 15:
            _ball_glow(fight, o);
            break;
        case 16:
            _announcer_wait(o, events);
            break;
        case 17:
            _voice_echo(fight, o, events);
            break;
        case 18:
            _replay_echo(fight, o, events);
            break;
        case 19:
            _gas_emitter(fight, o);
            break;
        case 20:
            _gas(fight, o);
            break;
        default:
            break;
    }
}

// ==== helpers

// The chance of an optional spawn: `rand() < ((80 - live)*k / 80000)*4`.
bool EffectObjects::_chance(const gd::Ref<FightState>& fight, gd::Int r, gd::Int k) {
    return r < Fx::div_trunc((CAPACITY - fight->effects->live) * k, 80000) * 4;
}

// FUN_8006E408: the velocity of the direction (pitch, yaw) and the speed (16.16).
void EffectObjects::_aim(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    gd::Ref<FightTables> t = fight->tables;
    gd::Int p = -o->pitch & 0xFFF;
    gd::Int w = -o->yaw & 0xFFF;
    o->vy = Fx::w32(Fx::s16(o->speed) * FightMath::sin12(p, t));
    gd::Int h = (Fx::s16(o->speed) * FightMath::cos12(p, t)) >> 12;
    o->vx = Fx::w32(h * FightMath::sin12(w, t));
    o->vz = Fx::w32(h * FightMath::cos12(w, t));
}

// FUN_8006E3B8: the horizontal velocity of the yaw and the speed.
void EffectObjects::_aim_flat(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    gd::Ref<FightTables> t = fight->tables;
    gd::Int w = -o->yaw & 0xFFF;
    o->vx = Fx::w32(Fx::s16(o->speed) * FightMath::sin12(w, t));
    o->vz = Fx::w32(Fx::s16(o->speed) * FightMath::cos12(w, t));
}

// One step along the velocity (the upper halves of the 16.16 values).
void EffectObjects::_move(const gd::Ref<Obj>& o) {
    o->x = Fx::w32(o->x + Fx::s16(o->vx >> 16));
    o->y = Fx::w32(o->y + Fx::s16(o->vy >> 16));
    o->z = Fx::w32(o->z + Fx::s16(o->vz >> 16));
}

void EffectObjects::_save_previous(const gd::Ref<Obj>& o) {
    o->px = o->x;
    o->py = o->y;
    o->pz = o->z;
}

// Two rand() draws as the game combines them: r1 | r2 << 16.
gd::Vector2i EffectObjects::_rand2(const gd::Ref<FightState>& fight) {
    gd::Int r1 = fight->rng->next();
    gd::Int r2 = fight->rng->next();
    return gd::Vector2i(r1, r1 | r2 << 16);
}

// The owner fighter of an object (owner bit 0x10 -> fighter 0, else fighter 1).
gd::Ref<FighterState> EffectObjects::_owner(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    return (o->flags & OWNER_0) != 0 ? fight->fighters[0] : fight->fighters[1];
}

// The owner's move still runs descriptor `code` inside its active window (the game's test
// reads "before the active window" as ended).
bool EffectObjects::_owner_active(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, gd::Int code) {
    gd::Ref<FighterState> f = _owner(fight, o);
    if (AttackRecords::first_joint(f->pose_move, fight->tables) != code) {
        return false;
    }
    return !(f->pose_frame < f->pose_move->active_first);
}

void EffectObjects::_spawn_smoke(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, gd::Int pitch, gd::Int yaw) {
    gd::Ref<Obj> s = alloc(fight, 7);
    if (s != nullptr) {
        s->x = o->x;
        s->y = o->y;
        s->z = o->z;
        s->pitch = pitch;
        s->yaw = yaw;
    }
}

// ==== types

void EffectObjects::_placeholder(const gd::Ref<Obj>& o) {
    if (o->state == 0) {
        o->a = 2;
        o->state += 1;
    } else if (o->state != 1) {
        return;
    }
    o->a -= 1;
    if (o->a < 1 && (o->flags & MARK) != 0) {
        o->gone = true;
    }
}

// Types 1 and 12: six decelerating sparks along the hit direction +- 60 deg (12: the power ramp).
void EffectObjects::_spark(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, bool power) {
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE;
        o->model = power ? Model::POWER_SPARK : Model::SPARK;
        o->clut = CLUT_DUST;
        gd::Vector2i r = _rand2(fight);
        gd::Int u = r.y;
        gd::Int i4 = ((r.x & 0x7FF) - 0x3FF) * 2;
        o->b = ((u >> 4) & 0x1F) + 10;
        gd::Int i3 = (((u >> 2) & 0x7FF) - 0x3FF) * 2;
        o->c = (u >> 6) & 0x1FF;
        o->a = 0;
        o->d = ((u >> 8) & 0x1FF) + 0x680;
        o->pitch = Fx::s16(o->pitch + Fx::div_trunc(i4, 3));
        o->yaw = Fx::s16(o->yaw + Fx::div_trunc(i3, 3));
    } else if (o->state != 1) {
        return;
    }
    o->a += 1;
    if (o->a < o->b) {
        gd::Int i = Fx::div_trunc(o->a * 0x10, o->b);
        o->clut = power ? ramp509(i) : ramp510(i);
        gd::Int w = FightMath::cos12(Fx::div_trunc(o->a * 0x3FF, o->b) & 0x3FF, fight->tables);
        o->speed = Fx::s16(o->c + (((o->d - o->c) * (w * 4 + 0x2000)) >> 15));
        _aim(fight, o);
        _move(o);
        if (o->y < 0) {
            return;
        }
    }
    finish(o);
}

// Types 2 and 13: twenty sparks sprayed from a jittered start at a constant speed.
void EffectObjects::_spark_spray(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, bool power) {
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE;
        o->model = power ? Model::POWER_SPARK : Model::SPARK;
        o->clut = CLUT_DUST;
        gd::Vector2i r = _rand2(fight);
        gd::Int u = r.y;
        o->b = ((u >> 10) & 0x1F) + 0x14;
        o->a = 0;
        o->x = Fx::w32(o->x - 0x7F + (r.x & 0xFF));
        o->y = Fx::w32(o->y - 0x7F + ((u >> 2) & 0xFF));
        o->z = Fx::w32(o->z - 0x7F + ((u >> 4) & 0xFF));
        o->pitch = Fx::s16(o->pitch - 0x3F + ((u >> 6) & 0x7F));
        o->yaw = Fx::s16(o->yaw - 0x3F + ((u >> 8) & 0x7F));
        o->speed = Fx::s16((((u >> 12) & 0x7FF) * 5) >> 2);
        _aim(fight, o);
        o->x = Fx::w32(o->x + (o->vx >> 13));
        o->y = Fx::w32(o->y + (o->vy >> 13));
        o->z = Fx::w32(o->z + (o->vz >> 13));
    } else if (o->state != 1) {
        return;
    }
    o->a += 1;
    if (o->a < o->b) {
        gd::Int i = Fx::div_trunc(o->a * 0x10, o->b);
        o->clut = power ? ramp509(i) : ramp510(i);
        _move(o);
        if (o->y < 0) {
            return;
        }
    }
    finish(o);
}

// Type 3: the power glow on one joint (6, 10, 14, 17; +0x8000 for fighter 1), shown while
// PowerGlowJoints sets the joint's bit, with rising particles.
void EffectObjects::_glow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE | 0x20;
        o->model = Model::GLOW;
        o->clut = CLUT_GLOW;
        o->b = -1;
    } else if (o->state != 1) {
        return;
    }
    gd::Int player = (o->a & 0x8000) >> 15;
    gd::Int mask = fight->effects->glow_mask[player];
    if ((gd::shr(mask, o->a & 0x1F) & 1) == 0) {
        o->flags &= ~VISIBLE;
        return;
    }
    o->flags |= VISIBLE;
    gd::Int joint = o->a & 0x7FFF;
    gd::Ref<FighterState> f = fight->fighters[player];
    gd::Ref<FightTables::EffectTables> e = fight->tables->effects;
    gd::Ref<FighterBody> body = f->body;
    gd::PI32 p;
    if (joint == 6) {
        gd::Int block = f->char_id == Character::TRUE_OGRE ? 19 : 6;
        gd::PI32 off = e->hand_offsets[f->char_id][0];
        gd::PI32 m = Gte::apply(body->joints[block]->rot, off[0], off[1], off[2]);
        p = gd::PI32{Fx::w32(m[0] + body->joints[block]->t[0]), Fx::w32(m[1] + body->joints[block]->t[1]),
                     Fx::w32(m[2] + body->joints[block]->t[2])};
    } else if (joint == 10) {
        gd::PI32 off = e->hand_offsets[f->char_id][1];
        gd::PI32 m = Gte::apply(body->joints[10]->rot, off[0], off[1], off[2]);
        p = gd::PI32{Fx::w32(m[0] + body->joints[10]->t[0]), Fx::w32(m[1] + body->joints[10]->t[1]),
                     Fx::w32(m[2] + body->joints[10]->t[2])};
    } else {
        p = body->joints[joint]->t.duplicate();
    }
    o->x = p[0];
    o->y = p[1];
    o->z = p[2];
    o->b += 1;
    o->clut = ramp509((o->b >> 1) & 7);
    if ((mask & GLOW_JOINT_BIT) != 0) {
        gd::Ref<Obj> s = alloc(fight, 4);
        if (s != nullptr) {
            s->x = o->x;
            s->y = o->y;
            s->z = o->z;
        }
    }
    gd::Int r = fight->rng->next();
    if (_chance(fight, r, 0xC8000)) {
        gd::Ref<Obj> s = alloc(fight, 5);
        if (s != nullptr) {
            s->x = o->x;
            s->y = o->y;
            s->z = o->z;
        }
    }
    if ((r & 0x80) != 0) {
        fight->effects->lights.append(gd::PI32{o->x, o->y, o->z, 1});
    }
}

// Type 4: a rising glow particle (16 frames).
void EffectObjects::_glow_rise(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE | 0x20;
        o->model = Model::GLOW_RISE;
        o->clut = CLUT_GLOW;
        o->b = -1;
        gd::Vector2i r = _rand2(fight);
        o->vy = 0;
        o->x = Fx::w32(o->x - 0x3F + (r.x & 0x7F));
        o->y = Fx::w32(o->y - 0x3F + ((r.y >> 2) & 0x7F));
        o->z = Fx::w32(o->z - 0x3F + ((r.y >> 4) & 0x7F));
    } else if (o->state != 1) {
        return;
    }
    o->b += 1;
    if (o->b < 0x10) {
        o->clut = ramp509(o->b);
        o->vy = Fx::w32(o->vy + 0x12000);
        o->y = Fx::w32(o->y - (o->vy >> 16));
    } else {
        finish(o);
    }
}

// Type 5: a rising ember (32 frames).
void EffectObjects::_ember(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE;
        o->model = Model::EMBER;
        o->clut = CLUT_GLOW;
        o->a = -1;
        o->vy = 0;
        gd::Vector2i r = _rand2(fight);
        o->x = Fx::w32(o->x - 0x7F + (r.x & 0xFF));
        o->z = Fx::w32(o->z - 0x7F + ((r.y >> 4) & 0xFF));
    } else if (o->state != 1) {
        return;
    }
    o->a += 1;
    if (o->a < 0x20) {
        o->clut = ramp509(o->a >> 1);
        o->vy = Fx::w32(o->vy + 0x12000);
        o->y = Fx::w32(o->y - (o->vy >> 16));
    } else {
        finish(o);
    }
}

// Type 6: the fireball of a fire breath (descriptor 0x18): grows, loops, bursts; slides along
// the floor; a projectile segment on the frames of its owner's display buffer; smoke and floor
// fire.
void EffectObjects::_fireball(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    gd::Ref<EffectPool> pool = fight->effects;
    bool own_buffer = ((o->flags & OWNER_0) == 0) == (fight->display_buffer == 0);
    if (o->d != 0) {
        if (!_owner_active(fight, o, FIRE_BREATH)) {
            o->d = 0;
        }
        if (own_buffer) {
            _save_previous(o);
        }
    }
    switch (o->state) {
        case 0:
        case 1:
            if (o->state == 0) {
                _fireball_start(fight, o);
            }
            _fireball_grow(fight, o);
            break;
        case 2:
            _fireball_loop(fight, o);
            break;
        case 3:
            _fireball_burst(fight, o);
            break;
        default:
            break;
    }
    o->x = Fx::w32(o->x + Fx::s16(o->vx >> 16));
    o->y = Fx::w32(o->y + Fx::s16(o->vy >> 16));
    o->z = Fx::w32(o->z + Fx::s16(o->vz >> 16));
    if (o->y > FLOOR) {
        o->y = FLOOR;
        o->pitch = 0;
        o->vy = 0;
        _aim_flat(fight, o);
    }
    o->flags &= ~SEGMENT;
    if (o->d != 0 && !own_buffer) {
        o->flags |= SEGMENT;
    }
    gd::Int r = fight->rng->next();
    if (_chance(fight, r, 0x3C000)) {
        _spawn_smoke(fight, o, o->pitch, o->yaw);
    } else if (o->y == FLOOR && o->d != 0) {
        r = fight->rng->next();
        if (_chance(fight, r, 0x24000)) {
            gd::Ref<Obj> s = alloc(fight, 9);
            if (s != nullptr) {
                s->x = o->x;
                s->z = o->z;
            }
        }
    }
    if (o->state >= 1 && o->state <= 2 && ((r >> 8) & 0xF) == 0) {
        pool->lights.append(gd::PI32{o->x, o->y, o->z, 0});
    }
}

void EffectObjects::_fireball_start(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    o->state += 1;
    o->flags |= VISIBLE;
    o->clut = CLUT_FIRE;
    _save_previous(o);
    gd::Int r = fight->rng->next();
    o->model = (r & 1) == 0 ? Model::FIREBALL_A : Model::FIREBALL_B;
    o->b = ((r >> 2) & 3) + 5;
    o->c = -1;
    o->d = 1;
    o->speed = 0xA00;
    o->a = 0;
    o->pitch = Fx::s16(o->pitch - 0x3F + ((r >> 4) & 0x7F));
    o->yaw = Fx::s16(o->yaw - 0x1F + ((r >> 6) & 0x3F));
    _aim(fight, o);
}

void EffectObjects::_fireball_grow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    o->c += 1;
    o->uv = o->c;
    o->uv_table = UvTable::FIREBALL_GROW;
    if (o->c > 6) {
        o->state += 1;
        o->c = fight->rng->next();
        o->speed = 0x1000;
        _aim(fight, o);
    }
}

void EffectObjects::_fireball_loop(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    o->c = Fx::w32(o->c + 1);
    o->uv = o->c & 7;
    o->uv_table = UvTable::FIREBALL_LOOP;
    o->a += 1;
    if (o->b <= o->a) {
        o->c = -1;
        o->state += 1;
        o->model = (fight->rng->next() & 1) == 0 ? Model::FIREBALL_A : Model::FIREBALL_B;
    }
}

void EffectObjects::_fireball_burst(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    o->clut = CLUT_BURST;
    o->c += 1;
    if (o->c < 8) {
        o->uv = o->c;
        o->uv_table = UvTable::FIREBALL_BURST;
        o->model = (o->c & 1) == 0 ? Model::FIREBALL_A : Model::FIREBALL_B;
        if (o->d != 0 && fight->tables->effects->segment_frames.at("fireball")[o->c] == 0) {
            o->d = 0;
        }
    } else {
        finish(o);
    }
}

// Type 7: a smoke or ember puff that rises faster and faster; ends at the floor.
void EffectObjects::_smoke(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    switch (o->state) {
        case 0:
        case 1:
            if (o->state == 0) {
                o->state += 1;
                o->flags |= VISIBLE;
                o->model = Model::SMOKE;
                o->clut = CLUT_GLOW;
                gd::Vector2i r = _rand2(fight);
                gd::Int u = r.y;
                o->b = (u >> 10) & 0x1F;
                o->a = 0;
                o->x = Fx::w32(o->x - 0x7F + (r.x & 0xFF));
                o->y = Fx::w32(o->y - 0x7F + ((u >> 2) & 0xFF));
                o->z = Fx::w32(o->z - 0x7F + ((u >> 4) & 0xFF));
                o->pitch = Fx::s16(o->pitch - 0x3F + ((u >> 6) & 0x7F));
                o->yaw = Fx::s16(o->yaw - 0x3F + ((u >> 8) & 0x7F));
                o->speed = Fx::s16(((((u >> 12) & 0x3FF) * 5) >> 2) + 0x200);
                _aim(fight, o);
            }
            o->a += 1;
            o->uv = (o->a & 0xE) >> 1;
            o->uv_table = UvTable::SMOKE_PUFF;
            if (o->b <= o->a) {
                o->a = -1;
                o->state += 1;
            }
            break;
        case 2:
            o->a += 1;
            if (o->a < 0xC) {
                o->uv = o->a;
                o->uv_table = UvTable::SMOKE_FADE;
            } else {
                finish(o);
            }
            break;
        default:
            break;
    }
    o->vy = Fx::w32(o->vy - 0x30000);
    _move(o);
    if (o->y >= 0) {
        finish(o);
    }
}

// Type 8: Gon's flame (descriptor 0x1A): a projectile until its owner is hit (then gone) or
// the move leaves the active window; during the replay it follows the recorded hits.
void EffectObjects::_flame(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if ((o->flags & SEGMENT) != 0) {
        if (fight->replay_playing == 0) {
            if (!_owner_active(fight, o, GON_FLAME)) {
                o->flags &= ~SEGMENT;
            }
        } else if ((o->flags & OWNER_0) == 0) {
            if ((fight->effects->burn_flags & 0x10) != 0) {
                finish(o);
                return;
            }
        } else if ((fight->effects->burn_flags & 8) != 0) {
            finish(o);
            return;
        }
        if ((o->flags & SEGMENT) != 0 && o->state >= 1 && o->state <= 2) {
            gd::Ref<FighterState> target = (o->flags & OWNER_0) == 0 ? fight->fighters[0] : fight->fighters[1];
            if (target->hit_clean != 0) {
                finish(o);
                return;
            }
            if (target->guarded != 0) {
                o->state = 3;
            }
        }
    }
    switch (o->state) {
        case 0:
        case 1:
            if (o->state == 0) {
                o->state += 1;
                o->flags |= VISIBLE | SEGMENT;
                o->clut = CLUT_FIRE;
                gd::Int r = fight->rng->next();
                o->model = (r & 1) == 0 ? Model::FLAME_B : Model::FLAME_A;
                o->b = ((r >> 2) & 3) + 5;
                o->c = -1;
                o->a = 0;
                o->speed = 0x800;
                _aim(fight, o);
            }
            o->c += 1;
            o->uv = o->c >> 1;
            o->uv_table = UvTable::FLAME_GROW;
            if (o->c > 6) {
                o->state += 1;
                o->c = fight->rng->next();
            }
            break;
        case 2:
            o->c = Fx::w32(o->c + 1);
            o->uv = (o->c & 6) >> 1;
            o->uv_table = UvTable::FLAME_LOOP;
            o->a += 1;
            if (o->b <= o->a) {
                o->state = 4;
                o->c = -1;
            }
            break;
        case 3:
        case 4:
            if (o->state == 3) {
                o->state = 4;
                o->vx = 0;
                o->vy = 0;
                o->vz = 0;
                o->c = -1;
            }
            o->clut = CLUT_BURST;
            o->c += 1;
            if (o->c < 9) {
                o->uv = o->c >> 1;
                o->uv_table = UvTable::FLAME_BURST;
                if ((o->flags & SEGMENT) != 0 && fight->tables->effects->segment_frames.at("flame")[o->c] == 0) {
                    o->flags &= ~SEGMENT;
                }
            } else {
                finish(o);
            }
            break;
        default:
            break;
    }
    _save_previous(o);
    _move(o);
    if (o->y > FLOOR) {
        o->y = FLOOR;
        o->pitch = 0;
        o->vy = 0;
        _aim_flat(fight, o);
    }
    gd::Int r = fight->rng->next();
    if (_chance(fight, r, 0x3C000)) {
        _spawn_smoke(fight, o, o->pitch, o->yaw);
    }
    if (((r >> 8) & 0xF) == 0) {
        fight->effects->lights.append(gd::PI32{o->x, o->y, o->z, 0});
    }
}

// Type 9: floor fire: flickers for 60-91 frames, then rises and fades; emits smoke.
void EffectObjects::_floor_fire(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    switch (o->state) {
        case 0:
        case 1:
            if (o->state == 0) {
                o->state += 1;
                o->flags |= VISIBLE;
                o->model = Model::FLOOR_FIRE;
                o->clut = CLUT_GLOW;
                gd::Vector2i r = _rand2(fight);
                o->y = -0x100;
                o->vy = 0;
                o->a = r.y >> 2;
                o->b = (r.x & 0x1F) + 0x3C;
            }
            o->a = Fx::w32(o->a + 1);
            o->uv = (o->a & 0xE) >> 1;
            o->uv_table = UvTable::SMOKE_PUFF;
            o->b -= 1;
            if (o->b < 0) {
                o->a = -1;
                o->state += 1;
            }
            break;
        case 2:
            o->a += 1;
            if (o->a < 0x10) {
                o->uv = o->a;
                o->uv_table = UvTable::FIRE_FADE;
                o->vy = Fx::w32(o->vy - 0x24000);
                o->y = Fx::w32(o->y + (o->vy >> 16));
            } else {
                finish(o);
            }
            break;
        default:
            break;
    }
    gd::Int r = fight->rng->next();
    if (_chance(fight, r, 0x28000)) {
        _spawn_smoke(fight, o, 0x400, 0);
    }
}

// Type 10: a breath cloud (descriptors 0x19 and 0x1C): 21 frames, a projectile for its first
// ones while the owner's move stays active.
void EffectObjects::_cloud(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if ((o->flags & SEGMENT) != 0) {
        gd::Int code = o->d != 0 ? DOCTOR_CLOUD : BREATH_CLOUD;
        if (!_owner_active(fight, o, code)) {
            o->flags &= ~SEGMENT;
        }
    }
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE | SEGMENT;
        o->model = Model::CLOUD;
        o->tpage = TPAGE_HALF;
        o->clut = o->d == 0 ? CLUT_CLOUD : CLUT_DUST;
        gd::Vector2i r = _rand2(fight);
        o->c = -1;
        o->speed = 0x300;
        o->pitch = Fx::s16(o->pitch - 0x7F + (r.x & 0xFF));
        o->yaw = Fx::s16(o->yaw - 0x7F + ((r.y >> 2) & 0xFF));
        _aim(fight, o);
    }
    if (o->state == 1) {
        o->c += 1;
        if (o->c < 0x15) {
            o->uv = o->c;
            o->uv_table = UvTable::CLOUD;
            if ((o->flags & SEGMENT) != 0 && fight->tables->effects->segment_frames.at("cloud")[o->c] == 0) {
                o->flags &= ~SEGMENT;
            }
        } else {
            finish(o);
        }
    }
    _save_previous(o);
    _move(o);
}

// Type 11: one flame of a burning body, following its joint; then it rises and bursts.
void EffectObjects::_burn(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    switch (o->state) {
        case 0:
        case 1:
            if (o->state == 0) {
                o->state += 1;
                o->flags |= VISIBLE;
                o->model = (o->flags & 0x20) != 0 ? Model::BURN_GON : Model::BURN;
                o->clut = CLUT_FIRE;
                gd::Int r = fight->rng->next();
                o->a = (r & 0xF) + 0x3C;
                o->b = r >> 4;
            }
            _burn_follow(fight, o);
            break;
        case 2:
            o->uv = o->a >> 1;
            o->uv_table = UvTable::FLAME_LOOP;
            o->vy = Fx::w32(o->vy - 0x18000);
            o->a += 1;
            o->y = Fx::w32(o->y + (o->vy >> 16));
            if (o->a > 6) {
                o->a = -1;
                o->state += 1;
            }
            break;
        case 3:
            o->a += 1;
            if (o->a < 5) {
                o->uv = o->a >> 1;
                o->uv_table = UvTable::FLAME_BURST;
                o->clut = CLUT_BURST;
                o->vy = Fx::w32(o->vy - 0x18000);
                o->y = Fx::w32(o->y + (o->vy >> 16));
            } else {
                finish(o);
            }
            break;
        default:
            break;
    }
}

void EffectObjects::_burn_follow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    o->b = Fx::w32(o->b + 1);
    gd::Ref<FighterState> f;
    if ((o->flags & 0x10) != 0) {
        f = fight->fighters[0];
    } else if ((o->flags & 8) != 0) {
        f = fight->fighters[1];
    } else {
        f = fight->fighters[0];
    }
    gd::Ref<JointFrame> joint = f->body->joints[o->d];
    o->x = joint->t[0];
    o->y = joint->t[1];
    o->z = joint->t[2];
    o->uv = (o->b & 6) >> 1;
    o->uv_table = UvTable::FLAME_LOOP;
    gd::Int r = fight->rng->next();
    if (_chance(fight, r, 0x3C000)) {
        _spawn_smoke(fight, o, 0x400, 0);
    }
    o->a -= 1;
    if (o->a < 0) {
        o->a = 0;
        o->vy = 0;
        o->state += 1;
    }
    f->keep_light = 1;
}

// Type 15 (FUN_80073F78): Tekken Ball's charged ball. `d` 0 glows with the character flipbook of
// player `a` (a 12-frame loop), `d` 1 is a flame that grows over 7 frames, then flickers. It
// follows the ball one frame ahead and ends (freeing its start mark, 0x800B645C) once the ball's
// render kind no longer shows that charge; a frame in 16 lights the scene.
void EffectObjects::_ball_glow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    gd::Ref<TekkenBall> ball = fight->ball;
    gd::Int r = fight->rng->next();
    fight->rng->next();
    if (o->d == 0) {
        if (o->state == 0) {
            _ball_glow_start(fight, o);
        }
        if (o->state == 1) {
            o->c += 1;
            if (o->c > 0xB) {
                o->c = 0;
            }
            o->uv = o->c;
            o->uv_table = UvTable::BALL_CHARACTER;
        }
    } else if (o->d == 1) {
        switch (o->state) {
            case 0:
            case 1:
                if (o->state == 0) {
                    _ball_glow_start(fight, o);
                }
                o->c += 1;
                o->uv = o->c;
                o->uv_table = UvTable::FIREBALL_GROW;
                if (o->c > 6) {
                    o->state += 1;
                    o->c = fight->rng->next();
                }
                break;
            case 2:
                o->c = (o->c + 1) & 0xFFFFFFFFLL;
                o->uv = o->c & 7;
                o->uv_table = UvTable::FIREBALL_LOOP;
                break;
            default:
                break;
        }
    }
    o->px = o->x;
    o->py = o->y;
    o->pz = o->z;
    gd::Ref<ByteBlock> b = ball->b;
    o->x = Fx::w32(b->s32(TekkenBall::B_POS) + (b->s32(TekkenBall::B_VEL) >> 8));
    o->y = Fx::w32(b->s32(TekkenBall::B_POS + 4) + (b->s32(TekkenBall::B_VEL + 4) >> 8));
    o->z = Fx::w32(b->s32(TekkenBall::B_POS + 8) + (b->s32(TekkenBall::B_VEL + 8) >> 8));
    gd::Int kind = b->u8(TekkenBall::B_KIND);
    if (kind == 5 || kind == 8) {
        return;
    }
    if (o->d == 0) {
        if ((r & 0xF) == 0) {
            fight->effects->lights.append(gd::PI32{o->x, o->y, o->z, 1});
        }
        if (kind == (o->a == 0 ? 6 : 7)) {
            return;
        }
    } else if (o->d == 1) {
        if ((r & 0xF) == 0) {
            fight->effects->lights.append(gd::PI32{o->x, o->y, o->z, 0});
        }
        if (kind == 9) {
            return;
        }
    }
    ball->v->put8(TekkenBall::GLOWS - TekkenBall::BASE, 0);
    finish(o);
}

// FUN_800741E0: the charged ball's effect appears with a random tilt.
void EffectObjects::_ball_glow_start(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    o->state += 1;
    o->flags |= VISIBLE;
    o->model = Model::BALL;
    o->speed = 0;
    o->c = -1;
    if (o->d == 1) {
        o->flags |= SEGMENT;
    }
    gd::Vector2i r = _rand2(fight);
    gd::Int u = r.y;
    o->speed = 0;
    o->pitch = Fx::s16(o->pitch - 0x3F + ((Fx::w32(u) >> 4) & 0x7F));
    o->yaw = Fx::s16(o->yaw - 0x1F + ((Fx::w32(u) >> 6) & 0x3F));
    _aim(fight, o);
}

// Type 16: waits for the announcer's voice (SPU voice 2) to stop, then plays 0x86DA. The
// simulation takes the voice as silent (as the harness does), so it plays at once.
void EffectObjects::_announcer_wait(const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events) {
    if (o->state == 0) {
        o->state = 1;
    } else if (o->state == 1) {
        events->add(SimEvents::Kind::SOUND, -1, 0x86DA, 0);
        finish(o);
    }
}

// Type 17: a throw voice echoed twice, 15 frames apart (twice as fast during the replay), with
// the SPU voice bits changed.
void EffectObjects::_voice_echo(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events) {
    o->c -= fight->replay_playing != 0 ? 2 : 1;
    switch (o->state) {
        case 0:
            o->c = 0xF;
            o->state += 1;
            break;
        case 1:
            if (o->c < 0) {
                o->b = (o->b & ~0x3C00) | (o->a * 0x400 + 0x3000);
                events->add(SimEvents::Kind::SOUND, o->a, o->b, o->a)->c = 1;
                o->c = 0xF;
                o->state += 1;
            }
            break;
        case 2:
            if (o->c < 0) {
                o->b = (o->b & ~0x3C00) | (o->a * 0x400 + 0x3800);
                events->add(SimEvents::Kind::SOUND, o->a, o->b, o->a)->c = 2;
                finish(o);
            }
            break;
        default:
            break;
    }
}

// Type 18: a voice repeated during the replay, on rotating SPU voices.
void EffectObjects::_replay_echo(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events) {
    switch (o->state) {
        case 0:
            o->b = mokujin_code(fight, o->b);
            if ((o->b & 0x3C00) == 0) {
                finish(o);
            } else {
                o->c = 0xF;
                o->state += 1;
            }
            break;
        case 1:
        case 2:
            o->c -= fight->replay_playing != 0 ? 2 : 1;
            if (o->c < 0) {
                fight->effects->echo_voice = (fight->effects->echo_voice + 1) & 7;
                events->add(SimEvents::Kind::SOUND, o->a, (o->b & ~0x3C00) | fight->effects->echo_voice << 10, o->a)->c = o->state | 0x80;
                o->c = 0xF;
                o->state += 1;
            }
            break;
        case 3:
            finish(o);
            break;
        default:
            break;
    }
}

// FUN_80075830: Mokujin's replacement of a sound code (the sound code of a listed sound id).
gd::Int EffectObjects::mokujin_code(const gd::Ref<FightState>& fight, gd::Int code) {
    for (const gd::PI32& pair : fight->tables->effects->mokujin_sounds) {
        if (code == fight->tables->sound_codes[pair[0]]) {
            return pair[1];
        }
    }
    return code;
}

// Type 19: Gon's gas emitter (descriptor 0x1B) at the hips: clouds on a timetable while the
// move stays active.
void EffectObjects::_gas_emitter(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if (!_owner_active(fight, o, GAS)) {
        finish(o);
        return;
    }
    if (o->state == 0) {
        o->a = -1;
        o->b = 0;
        o->state += 1;
    } else if (o->state != 1) {
        return;
    }
    o->a += 1;
    gd::Arr<gd::PI32> times = fight->tables->effects->gas_times;
    gd::PI32 entry = times[o->b];
    if (entry[0] <= o->a) {
        gd::Ref<Obj> s = alloc(fight, 20);
        if (s != nullptr) {
            s->flags |= o->flags & (0x10 | 8);
            s->x = o->x;
            s->y = -gd::Int(entry[2]);
            s->z = o->z;
            s->pitch = 0;
            s->speed = Fx::s16(entry[1] << 4);
            s->yaw = o->yaw;
        }
        o->b += 1;
        if (o->b >= times.size()) {
            finish(o);
        }
    }
}

// Type 20: a gas cloud: 27 frames rising slowly, a projectile for its first frames.
void EffectObjects::_gas(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o) {
    if ((o->flags & SEGMENT) != 0 && !_owner_active(fight, o, GAS)) {
        o->flags &= ~SEGMENT;
    }
    _save_previous(o);
    if (o->state == 0) {
        o->state += 1;
        o->flags |= VISIBLE | SEGMENT;
        o->model = (fight->rng->next() & 1) == 0 ? Model::GAS_A : Model::GAS_B;
        o->clut = CLUT_DUST;
        o->tpage = TPAGE_HALF;
        o->c = -1;
        _aim(fight, o);
        _move(o);
        o->vy = 0;
    }
    if (o->state == 1) {
        o->c += 1;
        if (o->c < 0x36) {
            o->uv = (o->c & 0x7FFE) >> 1;
            o->uv_table = UvTable::GAS;
            o->vy = Fx::w32(o->vy - 0x3999);
            o->y = Fx::w32(o->y + (o->vy >> 16));
            if ((o->flags & SEGMENT) != 0 && fight->tables->effects->segment_frames.at("gas")[o->c >> 1] == 0) {
                o->flags &= ~SEGMENT;
            }
        } else {
            finish(o);
        }
    }
}

// ==== spawns

// PowerGlowJoints (0x8006F5D8): the joints that glow this frame: while the power timer runs,
// the running attack's joints (by the descriptor), or both hands (Gon, or a move without
// attack joints). The game reads a move without descriptor from address 0 (bug #56); the
// remake takes it as having no attack joints.
void EffectObjects::_power_glow_joints(const gd::Ref<FightState>& fight) {
    gd::Ref<EffectPool> pool = fight->effects;
    for (gd::Int p = 0; p < 2; ++p) {
        pool->glow_mask[p] = 0;
        gd::Ref<FighterState> f = fight->fighters[p];
        if (fight->replay->power_timer(f) == 0) {
            continue;
        }
        gd::PBytes h = AttackRecords::header(f->pose_move, fight->tables);
        if (f->char_id != Character::GON && gd::Int(h[0]) + h[1] + h[2] + h[3] != 0) {
            if (f->pose_frame <= f->pose_move->active_last) {
                gd::PI32 glow_joints = fight->tables->effects->glow_joints;
                for (gd::Int k = 0; k < 4; ++k) {
                    gd::Int j = h[k] < glow_joints.size() ? gd::Int(glow_joints[h[k]]) : 0;
                    if (j != 0) {
                        pool->glow_mask[p] |= gd::shl(1, j & 0x1F) | GLOW_JOINT_BIT;
                    }
                }
            }
            continue;
        }
        pool->glow_mask[p] |= HANDS_GLOW;
    }
}

// EffectDescriptorSpawn (0x8006F3DC) for a fighter: fire breath, breath clouds, Gon's flame
// and gas start inside the active window of a move with descriptor 0x18-0x1C.
void EffectObjects::descriptor_spawn(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f, bool enabled,
                                     const gd::Ref<SimEvents>& events) {
    if (!enabled || fight->effects->end_cleared != 0) {
        return;
    }
    gd::Ref<MoveRow> move = f->pose_move;
    gd::Int code = AttackRecords::first_joint(f->pose_move, fight->tables);
    if (code < FIRE_BREATH || code > DOCTOR_CLOUD) {
        return;
    }
    if (f->pose_frame < move->active_first || move->active_last < f->pose_frame) {
        return;
    }
    gd::Ref<FightTables::EffectTables> e = fight->tables->effects;
    switch (code) {
        case FIRE_BREATH: {
            if (f->pose_frame == move->active_first) {
                Vibration::fighter(fight, f->index, 3, events);
            }
            gd::Arr<gd::PI32> offsets = e->breath_offsets[f->player_index];
            _emit(fight, f, 6, 2, offsets);
            gd::Ref<JointFrame> head = f->body->joints[2];
            gd::PI32 v0 = offsets[0];
            gd::PI32 m = Gte::apply(head->rot, v0[0], v0[1], v0[2]);
            fight->effects->lights.append(gd::PI32{m[0] + head->t[0], m[1] + head->t[1], m[2] + head->t[2], 0});
            break;
        }
        case BREATH_CLOUD:
        case DOCTOR_CLOUD: {
            gd::Ref<Obj> o = _emit(fight, f, 10, 2, e->cloud_offsets);
            if (o != nullptr) {
                o->d = code == DOCTOR_CLOUD ? 1 : 0;
            }
            break;
        }
        case GON_FLAME: {
            if (f->pose_frame == move->active_first) {
                Vibration::fighter(fight, f->index, 4, events);
            }
            gd::Ref<Obj> o = _emit(fight, f, 8, 2, e->flame_offsets);
            if (o != nullptr) {
                fight->effects->lights.append(gd::PI32{o->x, o->y, o->z, 0});
            }
            break;
        }
        case GAS:
            if (f->pose_frame == move->active_first) {
                _emit(fight, f, 19, 11, e->gas_offsets);
            }
            break;
        default:
            break;
    }
}

// An object at the first of two offsets from a joint, aimed at the second (FUN_8006E21C).
gd::Ref<EffectObjects::Obj> EffectObjects::_emit(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                                                 gd::Int type, gd::Int joint, const gd::Arr<gd::PI32>& offsets) {
    gd::Ref<JointFrame> j = f->body->joints[joint];
    gd::PI32 p;
    for (gd::Int k = 0; k < 2; ++k) {
        gd::PI32 v = offsets[k];
        gd::PI32 m = Gte::apply(j->rot, v[0], v[1], v[2]);
        p.append_array(gd::PI32{Fx::w32(m[0] + j->t[0]), Fx::w32(m[1] + j->t[1]), Fx::w32(m[2] + j->t[2])});
    }
    gd::Ref<Obj> o = alloc(fight, type);
    if (o == nullptr) {
        return nullptr;
    }
    o->flags |= f->player_index == 0 ? OWNER_0 : 8;
    o->x = p[0];
    o->y = p[1];
    o->z = p[2];
    gd::Vector2i d = direction(fight, p[3] - p[0], p[4] - p[1], p[5] - p[2]);
    o->pitch = d.x;
    o->yaw = d.y;
    return o;
}

// FUN_8006E21C: (pitch, yaw) of a vector, as the effects measure it.
gd::Vector2i EffectObjects::direction(const gd::Ref<FightState>& fight, gd::Int dx, gd::Int dy, gd::Int dz) {
    gd::Ref<CameraTables> ct = fight->tables->camera;
    gd::Int yaw = (Fx::s16(CameraMath::atan2_units4096(dx, dz, ct)) - 0x400) & 0xFFF;
    gd::Int flat = _flat_length(fight, dx, dz, yaw);
    gd::Int pitch = (Fx::s16(CameraMath::atan2_units4096(dy, flat, ct)) - 0x400) & 0xFFF;
    return gd::Vector2i(pitch, yaw);
}

// FUN_8006E2AC: the horizontal length from the larger component and the yaw.
gd::Int EffectObjects::_flat_length(const gd::Ref<FightState>& fight, gd::Int x, gd::Int z, gd::Int yaw) {
    gd::Ref<FightTables> t = fight->tables;
    gd::Int n = x;
    gd::Int s = 0;
    if (gd::absi(z) < gd::absi(x)) {
        gd::Int sv = FightMath::sin12(yaw & 0xFFF, t);
        s = Fx::s16(sv << 3);
        if ((sv & 0x1FFF) == 0) {
            s = 1;
        }
    } else {
        gd::Int cv = FightMath::cos12(yaw & 0xFFF, t);
        s = Fx::s16(cv << 3);
        n = z;
        if ((cv & 0x1FFF) == 0) {
            s = 1;
        }
    }
    return gd::absi(Fx::div_trunc(Fx::w32(n << 16), s) >> 1);
}

// FUN_80076F10's effect objects of a damaging hit: fire (descriptor 0x18 or 0x1A) sets the
// defender alight; otherwise a hit of 21 or more sprays sparks along the attacker's first
// attack segment (the power sparks while its power timer runs).
void EffectObjects::hit_objects(const gd::Ref<FightState>& fight, gd::Int damage, const gd::Ref<FighterState>& defender,
                                gd::Int attacker_index, const gd::PI32& point, const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> attacker = fight->fighters[attacker_index];
    gd::Int code = AttackRecords::first_joint(attacker->pose_move, fight->tables);
    if (code == BREATH_CLOUD || code == GAS || code == DOCTOR_CLOUD) {
        return;
    }
    if (code == FIRE_BREATH || code == GON_FLAME) {
        burn(fight, defender->index, events);
        // Logged with an unset point, which the replay's burn does not use.
        gd::PI32 root{defender->root_x, defender->root_y, defender->root_z};
        fight->replay->log_effect(defender->index, 0x82, root, gd::PI32{0, 0, 0}, 0);
        return;
    }
    if (damage < SPARK_DAMAGE) {
        return;
    }
    gd::PI32 seg = attacker->attack_segs[0];
    gd::Vector2i d = direction(fight, seg[3] - seg[0], seg[4] - seg[1], seg[5] - seg[2]);
    bool power = attacker->power_timer != 0;
    gd::PI32 dir{d.x, d.y, damage};
    _spray(fight, power ? 13 : 2, 20, point, d);
    fight->replay->log_effect(attacker_index, power ? 0x84 : 0x81, point, dir, 0);
    _spray(fight, power ? 12 : 1, 6, point, d);
    fight->replay->log_effect(attacker_index, power ? 0x83 : 0x80, point, dir, 0);
}

void EffectObjects::_spray(const gd::Ref<FightState>& fight, gd::Int type, gd::Int count, const gd::PI32& point,
                           gd::Vector2i d) {
    for (gd::Int i = 0; i < count; ++i) {
        gd::Ref<Obj> o = alloc(fight, type);
        if (o == nullptr) {
            return;
        }
        o->x = point[0];
        o->y = point[1];
        o->z = point[2];
        o->pitch = Fx::s16(d.x);
        o->yaw = Fx::s16(d.y);
    }
}

// FUN_8006F248: a burning body: ten flames on the body's joints (one for Gon), pad vibration.
void EffectObjects::burn(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<SimEvents>& events) {
    Vibration::fighter(fight, player, 0, events);
    gd::Ref<FighterState> f = fight->fighters[fight->fighters[player]->last_attacker];
    gd::Int count = 10;
    if (f->char_id == Character::GON) {
        count = 1;
        fight->effects->burn_flags |= gd::shr(0x10, player);
    }
    gd::PI32 joints = fight->tables->effects->burn_joints;
    for (gd::Int i = 0; i < count; ++i) {
        gd::Ref<Obj> o = alloc(fight, 11);
        if (o == nullptr) {
            return;
        }
        if (f->char_id == Character::GON) {
            o->flags |= 0x20;
        }
        o->c = i;
        o->d = joints[i];
        o->flags |= gd::shr(0x10, player);
    }
}

// FUN_8006F4F0 / FUN_8006F584: the echo of a throw voice (17, not while its fighter is in a
// throw) and of a voice replayed during the replay (18).
void EffectObjects::voice_echo(const gd::Ref<FightState>& fight, gd::Int player, gd::Int code, bool replayed) {
    if (!replayed && fight->fighters[player]->in_throw != 0) {
        return;
    }
    gd::Ref<Obj> o = alloc(fight, replayed ? 18 : 17);
    if (o != nullptr) {
        o->a = player;
        o->b = code & 0xFFFF;
    }
}

// FUN_8004A954: the replay plays the effect log's entries of the frame: flipbooks (sets
// 0x80+ re-create effect objects) and dust rings.
void EffectObjects::play_log(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    for (const gd::Ref<ReplayState::LogEntry>& e : fight->replay->log_take()) {
        if (e->mode == 1) {
            events->add_at(SimEvents::Kind::DUST, -1, e->point.duplicate());
            continue;
        }
        if (e->mode != 0) {
            continue;
        }
        if ((e->index & 0x80) == 0) {
            if (e->set == 3) {
                events->add_at(SimEvents::Kind::SPARK, -1, e->point.duplicate(), e->index);
            } else {
                events->add_at(SimEvents::Kind::EFFECT, -1, e->point.duplicate(), e->set);
            }
            continue;
        }
        gd::Vector2i d(e->direction[0], e->direction[1]);
        switch (e->index & 0x7F) {
            case 0:
                _spray(fight, 1, 6, e->point, d);
                break;
            case 1:
                _spray(fight, 2, 20, e->point, d);
                break;
            case 2:
                burn(fight, e->set, events);
                break;
            case 3:
                _spray(fight, 12, 6, e->point, d);
                break;
            case 4:
                _spray(fight, 13, 20, e->point, d);
                break;
            default:
                break;
        }
    }
}

// FUN_8006F4BC: the announcer's follow-up sound once its voice ends.
void EffectObjects::announcer_follow_up(const gd::Ref<FightState>& fight) {
    alloc(fight, 16);
}

}  // namespace t3
