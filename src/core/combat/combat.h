#pragma once
// remake/core/combat/combat.gd
// Everything between the two fighters' bodies (combat.md, fight-frame.md): PairwiseDistances,
// ArenaBounds with its ramp and the throw link, the saved segment ends, FighterVelocity,
// CollisionShapesUpdate with the baked attack points, BodySphereProfile, BodySeparate,
// HitTest, AttackVelocity and HitApply.
//
// Written from the verified ports in tools/research/fight_sim.py (arena_bounds, body_separate,
// collision_shapes_update, attack_segments_baked, hit_test, hit_apply) and the decompiled
// PairwiseDistances, BodySphereProfile and HitSpawnEffect.
#include "core/combat/attack_word.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/character.h"
#include "gd/gd.h"

namespace t3 {

class FightState;
class FightTables;
class MoveSystem;
class FighterPhysics;
class FighterState;
class JointFrame;
class SimEvents;
class HitSlot;

class Combat {
public:
    static constexpr gd::Int ARENA_HALF = 300000;
    static constexpr gd::Int FOLLOW_START = 0x1CFF;
    static constexpr gd::Int FOLLOW_MAX = 0x1E00;
    static constexpr gd::Int RAMP_MAX = 2000;
    static constexpr gd::Int SEPARATION_RANGE = 3000;
    static constexpr gd::Int FRONT_REACH = 200;
    static constexpr gd::Int FRONT_NEAR = 500;
    static constexpr gd::Int CHARGE_OVERLAP = 400;
    static constexpr gd::Int HEIGHT_GAP = 299;
    static constexpr gd::Int SCREEN_EDGE = 0x191;
    static constexpr gd::Int LOW_ATTACK = AttackWord::LOW;
    static constexpr gd::Int JUMP_CLASS = 12;
    static constexpr gd::Int PROJECTILE_JOINT = 0x18;
    static inline const gd::Dict<gd::Int, gd::Int> HURT_ZONE_Y = [] {
        gd::Dict<gd::Int, gd::Int> d;
        d.set(8, -0x78);
        d.set(11, -0x3C);
        return d;
    }();
    // Byte offset of frame t's points in a baked attack record, by layout kind: (stride, lead).
    static inline const gd::Dict<gd::Int, gd::Vector2i> LAYOUT_STRIDE = [] {
        gd::Dict<gd::Int, gd::Vector2i> d;
        d.set(0, gd::Vector2i(10, 0));
        d.set(1, gd::Vector2i(5, 5));
        d.set(2, gd::Vector2i(10, 10));
        d.set(3, gd::Vector2i(15, 5));
        d.set(4, gd::Vector2i(15, 5));
        d.set(5, gd::Vector2i(20, 0));
        d.set(6, gd::Vector2i(10, 5));
        d.set(7, gd::Vector2i(10, 5));
        d.set(9, gd::Vector2i(10, 5));
        d.set(10, gd::Vector2i(15, 0));
        d.set(12, gd::Vector2i(15, 0));
        d.set(13, gd::Vector2i(15, 0));
        d.set(14, gd::Vector2i(15, 0));
        return d;
    }();
    // Weapon points computed into spare joints outside the active window: bank type, character
    // (-1: any), joint, local point, target joint.
    // (An untyped GDScript record [bank_type, character, joint, [x, y, z], target]: a struct.)
    struct WeaponPoint {
        gd::Int bank_type = 0;
        gd::Int character = 0;
        gd::Int joint = 0;
        gd::PI32 point;
        gd::Int target = 0;
    };
    static inline const gd::Arr<WeaponPoint> WEAPON_POINTS{
        WeaponPoint{BankType::YOSHIMITSU, -1, 10, gd::PI32{100, -720, 0}, 18},
        WeaponPoint{BankType::YOSHIMITSU, -1, 10, gd::PI32{100, -360, 0}, 19},
        WeaponPoint{BankType::OGRE, Character::TRUE_OGRE, 11, gd::PI32{100, -500, 0}, 22},
        WeaponPoint{BankType::OGRE, Character::TRUE_OGRE, 2, gd::PI32{800, 0, 600}, 23},
        WeaponPoint{BankType::GON, -1, 11, gd::PI32{200, 900, 0}, 21},
        WeaponPoint{BankType::GON, -1, 2, gd::PI32{400, 0, 100}, 23},
    };

    gd::Ref<FightState> fight;
    gd::Ref<FightTables> t;
    gd::Ref<MoveSystem> moves;
    gd::Ref<FighterPhysics> physics;

    Combat(const gd::Ref<FightState>& fight_state, const gd::Ref<MoveSystem>& move_system,
           const gd::Ref<FighterPhysics>& fighter_physics);

    static gd::Int _pair(gd::Int a, gd::Int b);

    // ---- distances

    // PairwiseDistances (0x8004391C): distance, offset and direction of every pair, then per
    // fighter its distance to its opponent and the direction (or, with a fixed facing or an
    // opponent off screen, a screen-based stand-in).
    void pairwise_distances();

    // ---- arena bounds

    static constexpr gd::Int BALL_COURT_X = 0x1400;
    static constexpr gd::Int BALL_COURT_Z = 0x100;
    static constexpr gd::Int BALL_CROSS = 1000;
    static constexpr gd::Int BALL_CEILING = -0x1200;
    static constexpr gd::Int NO_POSTURE = 0x5000;
    static constexpr gd::Int THROW_DRIFT = 0x578;
    static constexpr gd::Int FORCED_HIT_SOUND = 0x4951;    // FUN_800421BC: SoundPlayFighter(0, 0x4951, 0)

    static gd::Int _excess(gd::Int v, gd::Int limit);

    static gd::Int _clamp_ramp(gd::Int v, gd::Int limit);

    // ArenaBounds (0x80043394) for the normal modes: the root back inside |x|, |z| <= 300,000, and
    // the pull towards the opponent (or back to where the fighter stood) beyond 0x1E00 apart;
    // Tekken Ball's court and Tekken Force's walls in their modes.
    void arena_bounds(const gd::Ref<FighterState>& f);

    // FUN_800432B0 (Tekken Ball and Tekken Force): a throw victim's anchor follows the thrower's
    // while the throw link holds; in Tekken Force the link drops when the pair drifts more than
    // 0x578 apart after the move's 18th frame.
    void throw_anchor(const gd::Ref<FighterState>& f);

    // FUN_80043260: the throw link follows the number of fighters in a throw.
    void update_throw_link();

    // ArenaRampStep (0x800431D8): the correction limit grows by an increment that grows by 2.
    void ramp_step();

    // FUN_80045B10: the number of fighters in a throw.
    gd::Int count_throws();

    // ---- per-frame records

    // FighterSavePrevSegments (0x8006AD84): the anchor and segment ends of the last frame; a
    // segment without a start joint then sweeps from its old end.
    void save_prev_segments(const gd::Ref<FighterState>& f);

    // SavePlayerPosition (0x80046F98).
    void save_position(const gd::Ref<FighterState>& f);

    // FighterVelocity (0x80040CA4).
    static void velocity(const gd::Ref<FighterState>& f);

    // AttackVelocity (0x80040CF8): the direction of the first attack segment.
    static void attack_velocity(const gd::Ref<FighterState>& f);

    // ---- collision shapes

    // CollisionShapesUpdate (0x80042204): attack segments (baked inside the active window, from the
    // joints outside it), the hurt cylinders and the body spheres.
    void collision_shapes(const gd::Ref<FighterState>& f, const gd::Arr<gd::Ref<JointFrame>>& joints);

    gd::Int _segment_joint(const gd::Ref<FighterState>& f, gd::Int joint);

    // FUN_8006A6A4: the attack segments inside the active window from the baked points.
    void _attack_segments_baked(const gd::Ref<FighterState>& f);

    static void _copy(const gd::PI32& dst, gd::Int at, const gd::PI32& src, gd::Int from);

    // sin and cos of -angle (16-bit angle units, the plain int), 4.12.
    gd::Vector2i _trig_neg16(gd::Int angle);

    // FUN_800699FC: a 40-bit point, x and y 13 bits (bias 0x1000), z 14 bits (bias 0x2000).
    static gd::PI32 _unpack(const gd::PBytes& data, gd::Int at);

    // FUN_8006A518: origin (swung about the root by heading - facing) + point rotated by -heading.
    static void _place(const gd::PI32& out, gd::Int at, const gd::PI32& p, const gd::PI32& origin,
                       gd::Vector2i trig, const gd::PI32& swing);

    // BodySphereProfile (0x8003EEFC): the body sphere radii by the move's profile (+0x30).
    void body_sphere_profile(const gd::Ref<FighterState>& f);

    // ---- body separation

    // FUN_80043F40: clears the contact flags; a fighter in a throw and its partner are not pushed.
    void clear_contacts();

    // BodySeparate (0x8004401C).
    void body_separate(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b);

    static gd::Int _axis_angle(gd::Int facing);

    gd::Int _deepest_overlap(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b);

    // BodyOverlapSolve (0x80046FCC): fills bodyPushX/Y/Z of both fighters.
    void _overlap_solve(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b);

    // ---- hits

    // HitClearSlots (0x800442A0).
    static void clear_hit_slots(const gd::Ref<FighterState>& f);

    // HitTest (0x80044304). A forced hit (Tekken Force) lands on a CPU defender it touches, at the
    // defender's root, without the segment test.
    void hit_test(const gd::Ref<FighterState>& attacker, const gd::Ref<FighterState>& defender,
                  const gd::Ref<SimEvents>& events);

    // HitTestSegments (0x80047E54): the attacker's live segments against the defender's cylinders.
    // Projectile descriptors (first joint >= 0x18) use the segments their owner's effect objects
    // published this frame (fire, breath and gas clouds) instead.
    bool _hit_segments(const gd::Ref<FighterState>& attacker, const gd::Ref<FighterState>& defender,
                       const gd::Ref<HitSlot>& slot);

    // HitExtraDamage (0x80044C4C).
    gd::Int _extra_damage(const gd::Ref<FighterState>& f);

    // HitClassify (0x80044D24).
    void _classify(const gd::Ref<FighterState>& defender, const gd::Ref<FighterState>& attacker,
                   const gd::Ref<HitSlot>& slot);

    // HitDamage (0x80044E70): the attacker's base damage into the slot and the hit's damage added.
    void _damage(const gd::Ref<FighterState>& defender, const gd::Ref<FighterState>& attacker,
                 const gd::Ref<HitSlot>& slot);

    // HitApply (0x80044634): classification, damage, the best slot, the reaction record and KO.
    void hit_apply(const gd::Ref<FighterState>& f, const gd::Ref<SimEvents>& events);

    // Tekken Force: the damage an enemy took from the player or a forced hit, and twice the extra
    // damage, scored.
    void _force_score(const gd::Ref<FighterState>& f, gd::Int extra);

    // The reaction of a move's +0x32 word: a record below 0x1000, else a throw victim entry
    // (only its first field, the victim's move slot, is meaningful).
    void _set_reaction(const gd::Ref<FighterState>& f, gd::Int value);

    void _spark_event(const gd::Ref<SimEvents>& events, const gd::Ref<FighterState>& f,
                      const gd::Ref<HitSlot>& slot, gd::Int kind, gd::Int damage, gd::Int side);

    // HitSpawnEffect (0x80044B78): the guard or hit flash and spark at the contact point.
    void _spawn_effect(const gd::Ref<FighterState>& f, const gd::Ref<HitSlot>& slot, gd::Int total,
                       const gd::Ref<SimEvents>& events);
};

}  // namespace t3
