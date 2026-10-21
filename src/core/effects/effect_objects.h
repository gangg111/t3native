#pragma once
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
#include "gd/gd.h"

namespace t3 {

class FightState;
class FighterState;
class SimEvents;

class EffectObjects {
public:
    static constexpr gd::Int CAPACITY = 80;
    static constexpr gd::Int VISIBLE = 0x40;
    static constexpr gd::Int SEGMENT = 0x02;
    static constexpr gd::Int OWNER_0 = 0x10;               // owner bits: 0x10 >> player
    static constexpr gd::Int MARK = 0x80;                  // allocated (FUN_8006F840 only frees marked objects)
    static constexpr gd::Int FLOOR = -0xD8;
    static inline const gd::Arr<gd::Int> GLOW_JOINTS{6, 10, 0xE, 0x11, 0x8006, 0x800A, 0x800E, 0x8011};
    static constexpr gd::Int GLOW_JOINT_BIT = 0x80000000LL;
    static constexpr gd::Int HANDS_GLOW = 0x440;
    static constexpr gd::Int FIRE_BREATH = 0x18;
    static constexpr gd::Int BREATH_CLOUD = 0x19;
    static constexpr gd::Int GON_FLAME = 0x1A;
    static constexpr gd::Int GAS = 0x1B;
    static constexpr gd::Int DOCTOR_CLOUD = 0x1C;
    static constexpr gd::Int SPARK_DAMAGE = 0x15;
    // Fixed CLUTs (0x800AE248...) and the fade ramps of rows 509 and 510 (FUN_8006E848).
    static constexpr gd::Int CLUT_DUST = 0x7DC6;
    static constexpr gd::Int CLUT_GLOW = 0x7DC7;
    static constexpr gd::Int CLUT_FIRE = 0x7DC8;
    static constexpr gd::Int CLUT_BURST = 0x7DC9;
    static constexpr gd::Int CLUT_CLOUD = 0x7DCA;
    static constexpr gd::Int TPAGE_ADD = 0x3F;
    static constexpr gd::Int TPAGE_HALF = 0x1F;

    // (The two enums are declared after Obj in the GDScript; moved before it because Obj's
    // `uv_table` default names UvTable.MODEL. Their values clash (CLOUD in both, GAS with the
    // constant), so each is an unscoped enum inside a struct of the enum's name: `Model::CLOUD`,
    // `UvTable::GAS` are gd::Int values as in GDScript; a variable of either type is gd::Int.)
    // Quad models of the objects (the executable's small models at 0x800254xx-0x800259xx).
    struct Model {
        enum : gd::Int { NONE, SPARK, GLOW, GLOW_RISE, EMBER, FIREBALL_A, FIREBALL_B, SMOKE, FLAME_A, FLAME_B,
            FLOOR_FIRE, CLOUD, BURN, BURN_GON, POWER_SPARK, GAS_A, GAS_B, BALL };
    };
    // The UV tables of the animated phases (effects.md, "UV animation"): MODEL is the model's own
    // sprite; BALL_CHARACTER the character flipbook uploaded for Tekken Ball's charged ball.
    struct UvTable {
        enum : gd::Int { MODEL, FIREBALL_GROW, FIREBALL_LOOP, FIREBALL_BURST, SMOKE_PUFF, SMOKE_FADE, FLAME_GROW,
            FLAME_LOOP, FLAME_BURST, FIRE_FADE, CLOUD, GAS, BALL_CHARACTER };
    };

    // One pool object (0xA0 bytes in the game; the draw primitives are the presentation's).
    class Obj {
    public:
        gd::Int type = 0;                   // +0x0C
        gd::Int flags = MARK;               // +0x0D
        gd::Int state = 0;                  // +0x0E
        gd::Int model = 0;                  // +0x08 the quad model (EffectObjects.Model)
        gd::Int pitch = 0;                  // +0x10 s16 direction
        gd::Int yaw = 0;                    // +0x12
        gd::Int speed = 0;                  // +0x18 s16
        gd::Int vx = 0;                     // +0x1C velocity, 16.16
        gd::Int vy = 0;                     // +0x20
        gd::Int vz = 0;                     // +0x24
        gd::Int x = 0;                      // +0x28
        gd::Int y = 0;
        gd::Int z = 0;
        gd::Int px = 0;                     // +0x34 the previous position (hit segment start)
        gd::Int py = 0;
        gd::Int pz = 0;
        gd::Int a = 0;                      // +0x40 per type counters and parameters
        gd::Int b = 0;                      // +0x44
        gd::Int c = 0;                      // +0x48
        gd::Int d = 0;                      // +0x4C
        gd::Int clut = 0;                   // the frame's CLUT id as the game computes it
        gd::Int tpage = TPAGE_ADD;          // additive or 50 % blending
        gd::Int uv = -1;                    // the frame of the phase's UV table (-1: the model's own)
        gd::Int uv_table = UvTable::MODEL;  // the phase's UV table (the presentation draws from it)
        bool gone = false;                  // unlinked (FUN_8006F840)

        gd::PI32 position() const { return gd::PI32{x, y, z}; }
    };

    // ==== the pool

    // FUN_8006F750: every object back in the free list.
    static void reset(const gd::Ref<FightState>& fight);

    // EffectAlloc (0x8006F7DC): a new object at the end of the live list, null when all 80 are used.
    static gd::Ref<Obj> alloc(const gd::Ref<FightState>& fight, gd::Int type);

    // FUN_8006F894: the object ends; it stays in the pool as type 0 for two more updates.
    static void finish(const gd::Ref<Obj>& o);

    // Round start (FUN_8006E8FC): the pool emptied and the eight power glows created.
    static void round_start(const gd::Ref<FightState>& fight);

    static void _spawn_glows(const gd::Ref<FightState>& fight);

    // EffectsUpdate (0x8006F8A8). `enabled` is FightFrame's 0x800958C0 (0 while the fight only
    // draws): the objects update and are counted only when it is set.
    static void update(const gd::Ref<FightState>& fight, bool enabled, const gd::Ref<SimEvents>& events);

    static void _update_object(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events);

    // ==== helpers

    // The chance of an optional spawn: `rand() < ((80 - live)*k / 80000)*4`.
    static bool _chance(const gd::Ref<FightState>& fight, gd::Int r, gd::Int k);

    // FUN_8006E408: the velocity of the direction (pitch, yaw) and the speed (16.16).
    static void _aim(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);

    // FUN_8006E3B8: the horizontal velocity of the yaw and the speed.
    static void _aim_flat(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);

    // One step along the velocity (the upper halves of the 16.16 values).
    static void _move(const gd::Ref<Obj>& o);

    static void _save_previous(const gd::Ref<Obj>& o);

    // Two rand() draws as the game combines them: r1 | r2 << 16.
    static gd::Vector2i _rand2(const gd::Ref<FightState>& fight);

    // The owner fighter of an object (owner bit 0x10 -> fighter 0, else fighter 1).
    static gd::Ref<FighterState> _owner(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);

    // The owner's move still runs descriptor `code` inside its active window (the game's test
    // reads "before the active window" as ended).
    static bool _owner_active(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, gd::Int code);

    static void _spawn_smoke(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, gd::Int pitch, gd::Int yaw);

    // ==== types

    // Type 0: a finished object, freed on its second update.
    // The fade ramp CLUTs: row 509 (glows, power sparks) and row 510 (sparks).
    static gd::Int ramp509(gd::Int i) { return ((0x10 + i) & 0x3F) | 0x7F40; }

    static gd::Int ramp510(gd::Int i) { return ((0x10 + i) & 0x3F) | 0x7F80; }

    static void _placeholder(const gd::Ref<Obj>& o);

    // Types 1 and 12: six decelerating sparks along the hit direction +- 60 deg (12: the power ramp).
    static void _spark(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, bool power);

    // Types 2 and 13: twenty sparks sprayed from a jittered start at a constant speed.
    static void _spark_spray(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, bool power);

    // Type 3: the power glow on one joint (6, 10, 14, 17; +0x8000 for fighter 1), shown while
    // PowerGlowJoints sets the joint's bit, with rising particles.
    static void _glow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);

    static void _glow_rise(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _ember(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _fireball(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _fireball_start(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _fireball_grow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _fireball_loop(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _fireball_burst(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _smoke(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _flame(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _floor_fire(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _cloud(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _burn(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _burn_follow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _ball_glow(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _ball_glow_start(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _announcer_wait(const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events);
    static void _voice_echo(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events);
    static void _replay_echo(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o, const gd::Ref<SimEvents>& events);
    static gd::Int mokujin_code(const gd::Ref<FightState>& fight, gd::Int code);
    static void _gas_emitter(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _gas(const gd::Ref<FightState>& fight, const gd::Ref<Obj>& o);
    static void _power_glow_joints(const gd::Ref<FightState>& fight);

    static void descriptor_spawn(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f, bool enabled,
                                 const gd::Ref<SimEvents>& events);

    // An object at the first of two offsets from a joint, aimed at the second (FUN_8006E21C).
    // (`offsets` is an untyped Array of PackedInt32Array in the GDScript.)
    static gd::Ref<Obj> _emit(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f, gd::Int type,
                              gd::Int joint, const gd::Arr<gd::PI32>& offsets);

    // FUN_8006E21C: (pitch, yaw) of a vector, as the effects measure it.
    static gd::Vector2i direction(const gd::Ref<FightState>& fight, gd::Int dx, gd::Int dy, gd::Int dz);

    // FUN_8006E2AC: the horizontal length from the larger component and the yaw.
    static gd::Int _flat_length(const gd::Ref<FightState>& fight, gd::Int x, gd::Int z, gd::Int yaw);

    // FUN_80076F10's effect objects of a damaging hit: fire (descriptor 0x18 or 0x1A) sets the
    // defender alight; otherwise a hit of 21 or more sprays sparks along the attacker's first
    // attack segment (the power sparks while its power timer runs).
    static void hit_objects(const gd::Ref<FightState>& fight, gd::Int damage, const gd::Ref<FighterState>& defender,
                            gd::Int attacker_index, const gd::PI32& point, const gd::Ref<SimEvents>& events);

    static void _spray(const gd::Ref<FightState>& fight, gd::Int type, gd::Int count, const gd::PI32& point,
                       gd::Vector2i d);

    // FUN_8006F248: a burning body: ten flames on the body's joints (one for Gon), pad vibration.
    static void burn(const gd::Ref<FightState>& fight, gd::Int player, const gd::Ref<SimEvents>& events);

    // FUN_8006F4F0 / FUN_8006F584: the echo of a throw voice (17, not while its fighter is in a
    // throw) and of a voice replayed during the replay (18).
    static void voice_echo(const gd::Ref<FightState>& fight, gd::Int player, gd::Int code, bool replayed);

    // FUN_8004A954: the replay plays the effect log's entries of the frame: flipbooks (sets
    // 0x80+ re-create effect objects) and dust rings.
    static void play_log(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // FUN_8006F4BC: the announcer's follow-up sound once its voice ends.
    static void announcer_follow_up(const gd::Ref<FightState>& fight);
};

}  // namespace t3
