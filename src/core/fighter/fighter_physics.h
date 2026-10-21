#pragma once
// remake/core/fighter/fighter_physics.gd: FighterMovePhysics (0x8003F330): slides, the move's step
// displacement, launches, push-back, timed turns and tracking, and the per-frame bookkeeping
// (moves.md#displacement-and-slides, moves.md#facing-and-tracking,
// moves.md#per-frame-bookkeeping-in-fightermovephysics).
// Written from the verified port `move_physics` in tools/research/fight_sim.py.
// Tekken Ball drifts the anchor of some moves (the tuning of FUN_800B0B54).
// Tekken Force's thrown bodies and power moves hit whoever they touch (forced hits).
#include "gd/gd.h"
#include "core/combat/attack_word.h"

namespace t3 {

class FightState;
class FightTables;
class MoveSystem;
class FighterState;
class SimEvents;

class FighterPhysics {
public:
    static constexpr gd::Int LANDING_SHAKE = 1;        // air kind whose landing shakes the camera (script 1)
    static constexpr gd::Int FORCED_ATTACK = AttackWord::UNBLOCKABLE;  // the attack of a forced hit (attackHi 6)
    static constexpr gd::Int FORCED_ATTACK_HI = 6;
    static constexpr gd::Int FORCED_THROWN_SPEED = 0x59;   // a thrown CPU body hits above this speed
    static constexpr gd::Int FORCED_THROWN_FRAME = 10;     // ... after this frame of the throw
    static constexpr gd::Int FORCED_DAMAGE = 10;
    static constexpr gd::Int POWER_MOVE_COST = 0xA0000;    // the health a power move costs

    gd::Ref<FightState> fight;
    gd::Ref<FightTables> t;
    gd::Ref<MoveSystem> moves;

    FighterPhysics(const gd::Ref<FightState>& fight_state, const gd::Ref<MoveSystem>& move_system);

    // (length · sin / 0x7FFF, length · cos / 0x7FFF) in 32-bit C arithmetic.
    gd::Vector2i polar(gd::Int length, gd::Int angle);
    static gd::Int clamp_symmetric(gd::Int v, gd::Int limit);
    static gd::Int track_budget(gd::Int step, gd::Int accum, gd::Int limit);
    static gd::Int _degrees(gd::Int d);
    static gd::Int _towards(gd::Int target, gd::Int current, gd::Int frames);

    // FighterMovePhysics for one fighter; effects outside the record go to `events`.
    void update(const gd::Ref<FighterState>& f, const gd::Ref<SimEvents>& events);
    // Slides (transitions SLIDE*): from the root move's air window on, the distance to the opponent
    // (less 500, at most 0xA0C) is spread over the window; at its end the anchor snaps to the root.
    void _slide(const gd::Ref<FighterState>& f);
    // The move's step displacement inside its air window: step / 256 units a frame against the
    // facing, through the per-player 24.8 accumulators.
    void _step_displacement(const gd::Ref<FighterState>& f);
    // Ballistic flight (combat.md#launches-and-juggles) until the body lands.
    void _flight(const gd::Ref<FighterState>& f, const gd::Ref<SimEvents>& events);
    // Slide to point: the anchor is set back along the direction from the target, and the push-back
    // carries it there over 16 frames.
    void _slide_to_point(const gd::Ref<FighterState>& f);
    // Push-back along pushDir: the push speed, the push table, and the repeated-hit bonus.
    void _push(const gd::Ref<FighterState>& f);
    // The per-frame bookkeeping after tracking (moves.md#per-frame-bookkeeping-in-fightermovephysics).
    void _bookkeeping(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events);
    // Knock-down recovery (combat.md#knock-down-recovery): the counters run down, faster with
    // button presses.
    void _recovery(const gd::Ref<FighterState>& f);
    // aboutToHit: a damage move up to 3 frames before its active window, flag 10, or Gon against
    // anyone else.
    void _about_to_hit(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // The hand and face shape: hit, or the attack shape before an attack's active window and at the
    // start of a non-damage move during the fight.
    void _face_shape(const gd::Ref<FighterState>& f);
    // Tekken Force: a CPU body thrown fast enough, or the player's power move (which costs health),
    // hits any enemy it touches (Combat.hit_test).
    void _forced_hit(const gd::Ref<FighterState>& f);
    void _set_guard_state(const gd::Ref<FighterState>& f, gd::Int value);
    // The guard stance from the stick while the move allows it (flag 0x4000 or a side step).
    void _guard_state(const gd::Ref<FighterState>& f);
    void _track_step(const gd::Ref<FighterState>& f, gd::Int step, gd::Int budget, bool set_facing = false);
    void _timed_turn(const gd::Ref<FighterState>& f);
    // Heading tracking by the transition's tracking mode (moves.md#facing-and-tracking).
    void _tracking(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // Tekken Ball: within the tuned frames of the move the anchor drifts at the tuned speed along the
    // tuned angle from the opponent's direction.
    void _ball_drift(const gd::Ref<FighterState>& f);
};

}  // namespace t3
